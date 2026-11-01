// The GPU backend's narrow phase (phases G3 and G4) against the CPU's (physics::collide()), on each GPU of the machine with
// the Vulkan validation layers. Every ordered pair of eight convex shapes (sphere, capsule, cylinder, cone, box, rounded box,
// hull, rounded hull) at random poses — apart, touching, overlapping — and every ordered pair with a triangle mesh (rough,
// two-sided, closed), a height field or a compound (of convex shapes, nested, holding a mesh) at overlapping poses gives
// the CPU's manifolds (their order, algorithms, sub-shapes, normals, points with their feature ids, separations), or,
// where float rounding tips a decision, the manifolds the CPU gives at a pose 0.2 mm or 0.2 mrad away; the CPU's own
// collide cases (a box sliding over a floor's seams, one-sided floors, a height field's ridge, a table's legs, meshes
// lowered onto each other); instance boxes; the same bits twice; pairs past the capacity cut short; the contact cache's
// predecessors; boxes, a BVH, its pairs and their manifolds against brute force on the CPU; misuses; and the throughput
// measured.
#include "gpu_test.hpp"

import gfx.physics.narrow_phase;
import gfx.physics.shapes;

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;
using gfx::Quaternion;
using gfx::Transform;
using gfx::Vector3;

constexpr std::uint32_t NO_PREDECESSOR = 0xFFFFFFFFU;

/// Words of a GpuManifold.
constexpr std::size_t MANIFOLD_WORDS = sizeof(gpu::GpuManifold) / sizeof(std::uint32_t);

/// A shape the pairs are drawn from: its name, the shape, its index in the narrow phase's table, how far its surface
/// reaches from its frame's origin at most.
struct TestShape
{
    std::string_view name;
    std::shared_ptr<const Shape> shape;
    std::uint32_t index = 0;
    float reach = 0;
};

/// The test shapes' convex ones come first.
constexpr std::size_t CONVEX_SHAPES = 8;

/// Points on an ellipsoid of half axes @p half (radii scaled by 0.8 to 1): a hull's corners.
auto
ellipsoid_points(Vector3 half,
                 std::uint32_t seed,
                 std::size_t count) -> std::vector<Vector3>
{
    std::mt19937 generator{seed};
    std::normal_distribution<float> normal{0.0F, 1.0F};
    std::uniform_real_distribution<float> scale{0.8F, 1.0F};
    std::vector<Vector3> points;
    for (std::size_t index = 0; index < count; ++index)
    {
        const Vector3 direction = gfx::normalized(Vector3{.x = normal(generator), .y = normal(generator), .z = normal(generator)});
        const float radius = scale(generator);
        points.push_back({.x = direction.x * half.x * radius, .y = direction.y * half.y * radius, .z = direction.z * half.z * radius});
    }
    return points;
}

/// A grid of @p cells × @p cells cells of @p size, two triangles each, facing up (+Y) and centered on the origin; heights
/// within ±@p roughness (0: flat at y = 0).
auto
grid_mesh(std::uint32_t cells,
          float size,
          float roughness,
          std::uint32_t seed,
          bool two_sided) -> std::shared_ptr<const Shape>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> height{-roughness, roughness};
    std::vector<Vector3> vertices;
    for (std::uint32_t row = 0; row <= cells; ++row)
    {
        for (std::uint32_t column = 0; column <= cells; ++column)
        {
            vertices.push_back({.x = (static_cast<float>(column) - static_cast<float>(cells) * 0.5F) * size,
                                .y = roughness > 0.0F ? height(generator) : 0.0F,
                                .z = (static_cast<float>(row) - static_cast<float>(cells) * 0.5F) * size});
        }
    }
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::uint32_t row = 0; row < cells; ++row)
    {
        for (std::uint32_t column = 0; column < cells; ++column)
        {
            const std::uint32_t corner = row * (cells + 1) + column;
            triangles.push_back({corner, corner + cells + 1, corner + cells + 2});
            triangles.push_back({corner, corner + cells + 2, corner + 1});
        }
    }
    return make_shape(TriangleMeshShape{{.vertices = vertices, .triangles = triangles, .two_sided = two_sided}});
}

/// The CPU tests' floor: @p cells × @p cells unit cells at y = 0, facing up.
auto
floor_mesh(std::uint32_t cells,
           bool two_sided = false) -> std::shared_ptr<const Shape>
{
    return grid_mesh(cells, 1.0F, 0.0F, 0, two_sided);
}

/// A box of half extents @p half as a closed mesh, triangles facing out (each face two triangles: an inactive diagonal).
auto
box_mesh(Vector3 half) -> std::shared_ptr<const Shape>
{
    std::vector<Vector3> vertices;
    for (std::uint32_t corner = 0; corner < 8; ++corner)
    {
        vertices.push_back({.x = (corner & 1U) != 0 ? half.x : -half.x, .y = (corner & 2U) != 0 ? half.y : -half.y, .z = (corner & 4U) != 0 ? half.z : -half.z});
    }
    const std::vector<std::array<std::uint32_t, 3>> triangles{{0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}, {0, 1, 5}, {0, 5, 4},
                                                              {2, 6, 7}, {2, 7, 3}, {0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}};
    return make_shape(TriangleMeshShape{{.vertices = vertices, .triangles = triangles}});
}

/// A height field of @p rows × @p columns samples within ±@p roughness at the spacing @p spacing.
auto
height_field(std::size_t rows,
             std::size_t columns,
             float spacing,
             float roughness,
             std::uint32_t seed) -> std::shared_ptr<const Shape>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> height{-roughness, roughness};
    std::vector<float> samples(rows * columns);
    for (float& sample : samples)
    {
        sample = height(generator);
    }
    return make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{samples.data(), rows, columns},
                                        .scale = {.x = spacing, .y = 1.0F, .z = spacing}}});
}

/// The compound of @p children.
auto
compound(std::vector<CompoundChild> children) -> std::shared_ptr<const Shape>
{
    return make_shape(CompoundShape{std::move(children)});
}

/// The fifteen shapes: eight convex, then two meshes facing up (rough one-sided, flat two-sided), a closed box mesh, a
/// height field, a dumbbell (two spheres and a bar), a compound holding the dumbbell (nested), a hull and a cylinder, and a
/// platform (a mesh over a box).
auto
test_shapes() -> std::vector<TestShape>
{
    const std::vector<Vector3> corners = ellipsoid_points({.x = 0.6F, .y = 0.45F, .z = 0.5F}, 7, 30);
    std::vector<Vector3> smaller;
    for (const Vector3 corner : corners)
    {
        smaller.push_back(corner * 0.85F);
    }
    const std::shared_ptr<const Shape> cylinder = make_shape(CylinderShape{.half_height = 0.5F, .radius = 0.4F});
    const std::shared_ptr<const Shape> hull = make_shape(HullShape::from_points(corners));
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.3F});
    const std::shared_ptr<const Shape> dumbbell = compound({{.shape = ball, .pose = {.position = {.x = -0.6F}}},
                                                            {.shape = ball, .pose = {.position = {.x = 0.6F}}},
                                                            {.shape = make_shape(BoxShape{.half_extents = {.x = 0.45F, .y = 0.08F, .z = 0.08F}})}});
    const std::shared_ptr<const Shape> nested =
        compound({{.shape = dumbbell, .pose = {.position = {.y = 0.45F}, .orientation = Quaternion::from_axis_angle({.y = 1}, 1.5707964F)}},
                  {.shape = hull, .pose = {.position = {.y = -0.4F}}},
                  {.shape = cylinder, .pose = {.position = {.x = 0.7F}, .orientation = Quaternion::from_axis_angle({.z = 1}, 1.5707964F)}}});
    const std::shared_ptr<const Shape> platform =
        compound({{.shape = grid_mesh(3, 0.4F, 0.05F, 5, false)},
                  {.shape = make_shape(BoxShape{.half_extents = {.x = 0.6F, .y = 0.1F, .z = 0.6F}}), .pose = {.position = {.y = -0.3F}}}});
    std::vector<TestShape> shapes{
        {.name = "sphere", .shape = make_shape(SphereShape{.radius = 0.5F})},
        {.name = "capsule", .shape = make_shape(CapsuleShape{.half_height = 0.6F, .radius = 0.3F})},
        {.name = "cylinder", .shape = cylinder},
        {.name = "cone", .shape = make_shape(ConeShape{.half_height = 0.6F, .radius = 0.5F})},
        {.name = "box", .shape = make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.3F, .z = 0.7F}})},
        {.name = "rounded box", .shape = make_shape(BoxShape{.half_extents = {.x = 0.4F, .y = 0.25F, .z = 0.6F}, .radius = 0.08F})},
        {.name = "hull", .shape = hull},
        {.name = "rounded hull", .shape = make_shape(HullShape::from_points(smaller, 0.06F))},
        {.name = "rough mesh", .shape = grid_mesh(6, 0.5F, 0.15F, 3, false)},
        {.name = "two-sided mesh", .shape = grid_mesh(4, 0.6F, 0.0F, 0, true)},
        {.name = "box mesh", .shape = box_mesh({.x = 0.5F, .y = 0.4F, .z = 0.6F})},
        {.name = "height field", .shape = height_field(7, 7, 0.5F, 0.3F, 4)},
        {.name = "dumbbell", .shape = dumbbell},
        {.name = "nested compound", .shape = nested},
        {.name = "platform", .shape = platform},
    };
    for (TestShape& shape : shapes)
    {
        const Aabb bounds = shape.shape->bounds();
        shape.reach = gfx::length(gfx::component_maximum(gfx::component_absolute(bounds.minimum), gfx::component_absolute(bounds.maximum)));
    }
    return shapes;
}

auto
random_rotation(std::mt19937& generator) -> Quaternion
{
    std::normal_distribution<float> normal{0.0F, 1.0F};
    return gfx::normalized(Quaternion{.x = normal(generator), .y = normal(generator), .z = normal(generator), .w = normal(generator)});
}

/// Two shapes (indices into the test shapes) at their poses.
struct Sample
{
    std::size_t first = 0;
    std::size_t second = 0;
    Transform first_pose{};
    Transform second_pose{};
};

/// @p count samples of each ordered pair of @p shapes: random orientations, the second shape's center at 0.15 to 1.05
/// times the sum of their reaches from the first's in a random direction (apart, touching, overlapping).
auto
random_samples(std::span<const TestShape> shapes,
               std::size_t count,
               std::uint32_t seed) -> std::vector<Sample>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> offset{-0.5F, 0.5F};
    std::uniform_real_distribution<float> fraction{0.15F, 1.05F};
    std::normal_distribution<float> normal{0.0F, 1.0F};
    std::vector<Sample> samples;
    for (std::size_t first = 0; first < shapes.size(); ++first)
    {
        for (std::size_t second = 0; second < shapes.size(); ++second)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                Sample sample{.first = first, .second = second};
                sample.first_pose = {.position = {.x = offset(generator), .y = offset(generator), .z = offset(generator)}, .orientation = random_rotation(generator)};
                const Vector3 direction = gfx::normalized(Vector3{.x = normal(generator), .y = normal(generator), .z = normal(generator)});
                const float distance = fraction(generator) * (shapes[first].reach + shapes[second].reach);
                sample.second_pose = {.position = sample.first_pose.position + direction * distance, .orientation = random_rotation(generator)};
                samples.push_back(sample);
            }
        }
    }
    return samples;
}

/// @p count samples of each ordered pair of @p shapes with a mesh, height field or compound: random orientations, the
/// second shape's box centered on a random point of the first's box, moved by up to 0.6 times its half diagonal (mostly
/// overlapping: their triangles and children meet).
auto
overlapping_samples(std::span<const TestShape> shapes,
                    std::size_t count,
                    std::uint32_t seed) -> std::vector<Sample>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> offset{-0.5F, 0.5F};
    std::uniform_real_distribution<float> unit{0.0F, 1.0F};
    std::normal_distribution<float> normal{0.0F, 1.0F};
    std::vector<Sample> samples;
    for (std::size_t first = 0; first < shapes.size(); ++first)
    {
        for (std::size_t second = 0; second < shapes.size(); ++second)
        {
            if (first < CONVEX_SHAPES && second < CONVEX_SHAPES)
            {
                continue;
            }
            const Aabb first_box = shapes[first].shape->bounds();
            const Aabb second_box = shapes[second].shape->bounds();
            const Vector3 second_center = (second_box.minimum + second_box.maximum) * 0.5F;
            const float second_radius = gfx::length(second_box.maximum - second_box.minimum) * 0.5F;
            for (std::size_t index = 0; index < count; ++index)
            {
                Sample sample{.first = first, .second = second};
                sample.first_pose = {.position = {.x = offset(generator), .y = offset(generator), .z = offset(generator)}, .orientation = random_rotation(generator)};
                const Vector3 target{.x = std::lerp(first_box.minimum.x, first_box.maximum.x, unit(generator)),
                                     .y = std::lerp(first_box.minimum.y, first_box.maximum.y, unit(generator)),
                                     .z = std::lerp(first_box.minimum.z, first_box.maximum.z, unit(generator))};
                const Vector3 direction = gfx::normalized(Vector3{.x = normal(generator), .y = normal(generator), .z = normal(generator)});
                const Quaternion orientation = random_rotation(generator);
                sample.second_pose = {.position = gfx::transform_point(sample.first_pose, target) - gfx::rotate(orientation, second_center) +
                                                  direction * (0.6F * second_radius * unit(generator)),
                                      .orientation = orientation};
                samples.push_back(sample);
            }
        }
    }
    return samples;
}

/// The CPU's manifolds of two shapes at their poses.
auto
cpu_manifolds(const Shape& first,
              const Transform& first_pose,
              const Shape& second,
              const Transform& second_pose,
              const CollisionSettings& settings) -> std::vector<ContactManifold>
{
    thread_local ContactCollector collector;
    collide({.shape = &first, .pose = first_pose}, {.shape = &second, .pose = second_pose}, collector, settings);
    return {collector.manifolds().begin(), collector.manifolds().end()};
}

/// How close a GPU manifold must be to the CPU's: float rounding for the closed forms, GJK and separating axes; for EPA,
/// its convergence (it stops when the depth is within 10⁻⁴ of its reach, which leaves a curved surface's normal free by
/// about a degree, and a contact point on it free by its radius times that).
struct Tolerance
{
    float normal_cosine = 0.0F;
    float position = 0.0F;
    float separation = 0.0F;
};

constexpr Tolerance ROUNDING{.normal_cosine = 0.9995F, .position = 5e-3F, .separation = 5e-3F};
constexpr Tolerance EPA_CONVERGENCE{.normal_cosine = 0.999F, .position = 2.5e-2F, .separation = 2e-3F};

/// Whether the GPU's manifold is the CPU's: the same algorithm, sub-shapes, point count and features (each CPU point
/// matched to the nearest unmatched GPU point of its feature: a manifold may hold a feature twice), normals, positions
/// and separations within the algorithm's tolerance.
auto
agree(const ContactManifold& cpu,
      const gpu::GpuManifold& gpu) -> bool
{
    if (gpu.point_count != cpu.point_count || gpu.algorithm != std::to_underlying(cpu.algorithm) || gpu.sub_shape_first != cpu.sub_shape_first ||
        gpu.sub_shape_second != cpu.sub_shape_second)
    {
        return false;
    }
    const Tolerance tolerance = cpu.algorithm == ContactAlgorithm::EPA ? EPA_CONVERGENCE : ROUNDING;
    const Vector3 gpu_normal{.x = gpu.normal[0], .y = gpu.normal[1], .z = gpu.normal[2]};
    if (gfx::dot(gpu_normal, cpu.normal) < tolerance.normal_cosine)
    {
        return false;
    }
    std::array<bool, MAXIMUM_MANIFOLD_POINTS> used{};
    for (const ContactPoint& point : cpu.contacts())
    {
        std::size_t best = MAXIMUM_MANIFOLD_POINTS;
        float best_distance = std::numeric_limits<float>::infinity();
        for (std::size_t index = 0; index < gpu.point_count; ++index)
        {
            const gpu::GpuContactPoint& candidate = gpu.points[index];
            const float distance = gfx::distance(Vector3{.x = candidate.position[0], .y = candidate.position[1], .z = candidate.position[2]}, point.position);
            if (!used[index] && candidate.feature == point.feature && distance < best_distance)
            {
                best = index;
                best_distance = distance;
            }
        }
        if (best == MAXIMUM_MANIFOLD_POINTS || best_distance > tolerance.position ||
            std::abs(gpu.points[best].separation - point.separation) > tolerance.separation)
        {
            return false;
        }
        used[best] = true;
    }
    return true;
}

/// Whether the GPU's manifolds of a pair are the CPU's, in the same order.
auto
agree(std::span<const ContactManifold> cpu,
      std::span<const gpu::GpuManifold> gpu) -> bool
{
    if (cpu.size() != gpu.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < cpu.size(); ++index)
    {
        if (!agree(cpu[index], gpu[index]))
        {
            return false;
        }
    }
    return true;
}

/// Nearby poses: 12 per step (moved along an axis either way, turned about one either way), steps of 1 µm, 10 µm and
/// 0.2 mm (and µrad, mrad).
constexpr std::array<float, 3> PERTURBATION_STEPS{1e-6F, 1e-5F, 2e-4F};
constexpr std::size_t PERTURBATIONS = 12 * PERTURBATION_STEPS.size();

/// @p pose moved or turned: variation % 12 picks the axis, the sign and the kind, variation / 12 the step.
auto
perturbed(const Transform& pose,
          std::size_t variation) -> Transform
{
    const float step = PERTURBATION_STEPS[variation / 12];
    const std::size_t kind = variation % 12;
    const std::array<Vector3, 3> axes{Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}};
    const Vector3 axis = axes[kind % 3];
    const float sign = (kind / 3) % 2 == 0 ? 1.0F : -1.0F;
    Transform result = pose;
    if (kind < 6)
    {
        result.position = pose.position + axis * (sign * step);
    }
    else
    {
        result.orientation = gfx::normalized(Quaternion::from_axis_angle(axis, sign * step) * pose.orientation);
    }
    return result;
}

/// How often rounding may tip a comparison: for convex pairs, to a nearby pose (2 %) and never further; with triangles or
/// children, to a nearby pose (10 %) or, at ties no nearby pose separates (they move together), to neither (0.25 %) —
/// coincident points of neighboring triangles (which triangle names a shared point, which of them the reduction keeps),
/// manifolds sharing their deepest point (their order), EPA's support point on a flat face.
struct Leeway
{
    double nearby = 0.0;
    double ties = 0.0;
};

constexpr Leeway CONVEX_LEEWAY{.nearby = 0.02, .ties = 0.0};
constexpr Leeway TRIANGLE_LEEWAY{.nearby = 0.10, .ties = 0.0025};

/// Whether the GPU's manifolds are the CPU's at the poses, or at a nearby pose of either shape (rounding tipped a
/// decision: a tie of support points, a clipped segment along a side plane, EPA's polytope in a deep overlap of curved
/// shapes, a triangle's box touching the query's, two normals at the merge cosine).
enum class Agreement : std::uint8_t
{
    EXACT_POSE,
    NEARBY_POSE,
    NONE,
};

auto
agreement(const Shape& first,
          const Transform& first_pose,
          const Shape& second,
          const Transform& second_pose,
          const CollisionSettings& settings,
          std::span<const gpu::GpuManifold> gpu) -> Agreement
{
    if (agree(cpu_manifolds(first, first_pose, second, second_pose, settings), gpu))
    {
        return Agreement::EXACT_POSE;
    }
    for (std::size_t variation = 0; variation < PERTURBATIONS; ++variation)
    {
        if (agree(cpu_manifolds(first, perturbed(first_pose, variation), second, second_pose, settings), gpu))
        {
            return Agreement::NEARBY_POSE;
        }
        if (agree(cpu_manifolds(first, first_pose, second, perturbed(second_pose, variation), settings), gpu))
        {
            return Agreement::NEARBY_POSE;
        }
    }
    return Agreement::NONE;
}

/// A manifold in a line.
auto
describe(const ContactManifold& manifold) -> std::string
{
    const auto sub_shape = [](std::uint32_t sub) { return sub == NO_SUB_SHAPE ? std::string{"-"} : std::to_string(sub); };
    std::string text = std::format("{} ({}, {}) normal ({:.4f}, {:.4f}, {:.4f})", manifold.algorithm, sub_shape(manifold.sub_shape_first),
                                   sub_shape(manifold.sub_shape_second), manifold.normal.x, manifold.normal.y, manifold.normal.z);
    for (const ContactPoint& point : manifold.contacts())
    {
        text += std::format(" [{}/{}/{:08x} ({:.4f}, {:.4f}, {:.4f}) {:.4f}]", sub_shape(point.feature.sub_shape_first), sub_shape(point.feature.sub_shape_second),
                            point.feature.local, point.position.x, point.position.y, point.position.z, point.separation);
    }
    return text;
}

/// Manifolds in a line.
auto
describe(std::span<const ContactManifold> manifolds) -> std::string
{
    if (manifolds.empty())
    {
        return "none";
    }
    std::string text;
    for (const ContactManifold& manifold : manifolds)
    {
        text += (text.empty() ? "" : " | ") + describe(manifold);
    }
    return text;
}

/// GPU manifolds as the CPU holds them.
auto
converted(std::span<const gpu::GpuManifold> manifolds) -> std::vector<ContactManifold>
{
    std::vector<ContactManifold> result;
    for (const gpu::GpuManifold& manifold : manifolds)
    {
        result.push_back(gpu::to_contact_manifold(manifold));
    }
    return result;
}

auto
instance_of(const Transform& pose,
            std::uint32_t shape) -> gpu::GpuInstance
{
    return {.position = {pose.position.x, pose.position.y, pose.position.z},
            .shape = shape,
            .orientation = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}};
}

/// Pairs (2i, 2i + 1) for @p count samples.
auto
consecutive_pairs(std::size_t count) -> std::vector<gpu::GpuPair>
{
    std::vector<gpu::GpuPair> pairs;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        pairs.push_back({.first = 2 * index, .second = 2 * index + 1});
    }
    return pairs;
}

/// What a collide() wrote: its buffers, the slots in use (at most the capacity), each pair's range, the slots needed.
struct Collided
{
    gfx::Buffer* manifolds = nullptr;
    gfx::Buffer* total_word = nullptr;
    std::uint32_t capacity = 0;
    std::vector<gpu::GpuManifold> slots{};
    std::vector<std::array<std::uint32_t, 2>> ranges{};
    std::uint32_t total = 0;

    /// Pair @p pair's manifolds.
    [[nodiscard]] auto of(std::size_t pair) const -> std::span<const gpu::GpuManifold>
    {
        return ranges[pair][1] == 0 ? std::span<const gpu::GpuManifold>{} : std::span{slots}.subspan(ranges[pair][0], ranges[pair][1]);
    }
};

// ===================================================================================================== fixture

/// The narrow phase on one GPU with the test shapes in its table.
class GpuNarrowPhaseTest : public gfx::tests::GpuTest
{
protected:
    void SetUp() override
    {
        GpuTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        m_narrow_phase = std::make_unique<gpu::GpuNarrowPhase>(*m_device, *m_primitives);
        m_shapes = test_shapes();
        for (TestShape& shape : m_shapes)
        {
            shape.index = m_narrow_phase->add_shape(*shape.shape);
        }
    }

    void release_gpu_objects() override
    {
        m_narrow_phase.reset();
    }

    /// The convex test shapes.
    [[nodiscard]] auto convex_shapes() const -> std::span<const TestShape>
    {
        return std::span{m_shapes}.first(CONVEX_SHAPES);
    }

    /// The instances of @p samples: each sample's first then second shape.
    auto instances_buffer(std::span<const Sample> samples) -> gfx::Buffer&
    {
        std::vector<gpu::GpuInstance> instances;
        for (const Sample& sample : samples)
        {
            instances.push_back(instance_of(sample.first_pose, m_shapes[sample.first].index));
            instances.push_back(instance_of(sample.second_pose, m_shapes[sample.second].index));
        }
        return elements_buffer(std::span<const gpu::GpuInstance>{instances});
    }

    /// The GPU's manifolds of @p pairs of @p instances (the shapes uploaded first) into @p capacity slots (0: as many as
    /// the pairs need — a first run tells — and a slot more, left untouched).
    auto collide_pairs(gfx::Buffer& instances,
                       std::span<const gpu::GpuPair> pairs,
                       const CollisionSettings& settings = {},
                       std::uint32_t capacity = 0) -> Collided
    {
        const auto count = static_cast<std::uint32_t>(pairs.size());
        gfx::Buffer& pair_buffer = elements_buffer(pairs);
        const bool sized = capacity == 0;
        std::uint32_t slots = sized ? std::max(4 * count, 64U) : capacity;
        for (;;)
        {
            m_narrow_phase->reserve(std::max(count, 1U), slots);
            gfx::Buffer& manifolds = words_buffer(MANIFOLD_WORDS * (std::size_t{slots} + 1));
            gfx::Buffer& ranges = words_buffer(2 * std::size_t{count});
            gfx::Buffer& total = words_buffer(1);
            record([&](gfx::ComputeRecorder& recorder) {
                m_narrow_phase->upload_shapes(recorder);
                m_narrow_phase->collide(recorder, gpu::span_of(instances), gpu::span_of(pair_buffer), {.maximum = count},
                                        {.manifolds = gpu::span_of(manifolds), .ranges = gpu::span_of(ranges), .total = gpu::span_of(total), .capacity = slots},
                                        settings);
            });
            Collided result{.manifolds = &manifolds, .total_word = &total, .capacity = slots, .slots = {}, .ranges = {}, .total = words(total, 1)[0]};
            if (sized && result.total > slots)
            {
                slots = result.total;
                continue;
            }
            result.slots = read<gpu::GpuManifold>(manifolds, std::min(result.total, slots));
            const std::vector<std::uint32_t> range_words = words(ranges, 2 * std::size_t{count});
            for (std::size_t pair = 0; pair < count; ++pair)
            {
                result.ranges.push_back({range_words[2 * pair], range_words[2 * pair + 1]});
            }
            return result;
        }
    }

    /// The GPU's manifolds of @p samples (pair i: instances 2i and 2i + 1).
    auto gpu_manifolds(std::span<const Sample> samples,
                       const CollisionSettings& settings = {}) -> Collided
    {
        return collide_pairs(instances_buffer(samples), consecutive_pairs(samples.size()), settings);
    }

    /// Checks the GPU's manifolds of @p samples against the CPU's within @p leeway, and prints what ran.
    void expect_the_cpus_manifolds(std::span<const Sample> samples,
                                   const CollisionSettings& settings,
                                   std::string_view label,
                                   const Leeway& leeway)
    {
        const Collided collided = gpu_manifolds(samples, settings);
        std::map<std::pair<std::size_t, std::size_t>, std::array<std::size_t, 3>> outcomes;
        std::array<std::size_t, gfx::ENUMERATOR_COUNT<ContactAlgorithm>> algorithms{};
        std::size_t contacts = 0;
        std::size_t several = 0;
        std::size_t named = 0;
        std::size_t most = 0;
        std::vector<std::string> failures;
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            const Sample& sample = samples[index];
            const Shape& first = *m_shapes[sample.first].shape;
            const Shape& second = *m_shapes[sample.second].shape;
            const std::span<const gpu::GpuManifold> manifolds = collided.of(index);
            const Agreement result = agreement(first, sample.first_pose, second, sample.second_pose, settings, manifolds);
            ++outcomes[{sample.first, sample.second}][std::to_underlying(result)];
            contacts += manifolds.empty() ? 0U : 1U;
            several += manifolds.size() > 1 ? 1U : 0U;
            most = std::max(most, manifolds.size());
            for (const gpu::GpuManifold& manifold : manifolds)
            {
                ++algorithms[std::min<std::size_t>(manifold.algorithm, algorithms.size() - 1)];
                named += manifold.sub_shape_first != NO_SUB_SHAPE || manifold.sub_shape_second != NO_SUB_SHAPE ? 1U : 0U;
            }
            if (result == Agreement::NONE && failures.size() < 6)
            {
                failures.push_back(std::format("{} / {} sample {}: CPU {} — GPU {}", m_shapes[sample.first].name, m_shapes[sample.second].name, index,
                                               describe(cpu_manifolds(first, sample.first_pose, second, sample.second_pose, settings)),
                                               describe(converted(manifolds))));
            }
        }

        // Every pair agrees, but for the ties the leeway allows; rounding tips few.
        std::size_t exact = 0;
        std::size_t nearby = 0;
        std::size_t none = 0;
        for (const auto& [kinds, counts] : outcomes)
        {
            exact += counts[0];
            nearby += counts[1];
            none += counts[2];
            if (leeway.ties == 0.0)
            {
                EXPECT_EQ(counts[2], 0U) << label << ": " << m_shapes[kinds.first].name << " / " << m_shapes[kinds.second].name << ": " << counts[2]
                                         << " disagree";
            }
        }
        for (const std::string& failure : failures)
        {
            if (leeway.ties == 0.0)
            {
                ADD_FAILURE() << label << ": " << failure;
            }
            else
            {
                std::cout << label << ": at a tie: " << failure << '\n';
            }
        }
        std::cout << std::format("{}: {} pairs ({} slots), {} in contact, {} with several manifolds (at most {}), {} manifolds naming sub-shapes; the CPU's "
                                 "at the pose {}, at a nearby pose {}, neither {}; algorithms {}\n",
                                 label, samples.size(), collided.total, contacts, several, most, named, exact, nearby, none, algorithms);
        EXPECT_LE(static_cast<double>(nearby), leeway.nearby * static_cast<double>(samples.size())) << label << ": rounding tips few pairs to a nearby pose";
        EXPECT_LE(static_cast<double>(none), leeway.ties * static_cast<double>(samples.size())) << label << ": ties tip few pairs further";
        m_algorithms_seen = algorithms;
        m_several_seen = several;
        m_named_seen = named;
    }

    std::unique_ptr<gpu::GpuNarrowPhase> m_narrow_phase;
    std::vector<TestShape> m_shapes;
    std::array<std::size_t, gfx::ENUMERATOR_COUNT<ContactAlgorithm>> m_algorithms_seen{};
    std::size_t m_several_seen = 0;
    std::size_t m_named_seen = 0;
};

// ===================================================================================================== tests

TEST_P(GpuNarrowPhaseTest, ManifoldsAreTheCpusOnEveryPairOfConvexShapes)
{
    // 300 poses of each of the 64 ordered pairs, with the default speculative distance and with none; every algorithm ran.
    for (const float speculative : {CollisionSettings{}.speculative_distance, 0.0F})
    {
        expect_the_cpus_manifolds(random_samples(convex_shapes(), 300, speculative > 0 ? 11U : 12U), {.speculative_distance = speculative},
                                  std::format("convex, speculative {}", speculative), CONVEX_LEEWAY);
        for (const ContactAlgorithm algorithm : {ContactAlgorithm::SPHERE_SPHERE, ContactAlgorithm::SPHERE_CAPSULE, ContactAlgorithm::CAPSULE_CAPSULE,
                                                 ContactAlgorithm::SPHERE_BOX, ContactAlgorithm::BOX_BOX, ContactAlgorithm::GJK, ContactAlgorithm::EPA})
        {
            EXPECT_GT(m_algorithms_seen[std::to_underlying(algorithm)], 0U) << gfx::to_string(algorithm) << " ran";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, ManifoldsAreTheCpusWithMeshesHeightFieldsAndCompounds)
{
    // 30 overlapping poses of each of the 161 ordered pairs with a mesh, a height field or a compound: with the default
    // settings, without speculative contacts, and keeping one manifold of those merged only within 0.8° (the deepest).
    const std::array<std::pair<CollisionSettings, std::string_view>, 3> runs{{{{}, "with triangles and children"},
                                                                             {{.speculative_distance = 0.0F}, "without speculative contacts"},
                                                                             {{.merge_cosine = 0.9999F, .maximum_manifolds = 1}, "one manifold"}}};
    std::uint32_t seed = 13;
    for (const auto& [settings, label] : runs)
    {
        expect_the_cpus_manifolds(overlapping_samples(m_shapes, 30, seed++), settings, label, TRIANGLE_LEEWAY);
        EXPECT_GT(m_named_seen, 0U) << "manifolds name triangles and children";
        if (settings.maximum_manifolds > 1)
        {
            EXPECT_GT(m_several_seen, 0U) << "pairs with several manifolds";
            EXPECT_GT(m_algorithms_seen[std::to_underlying(ContactAlgorithm::TRIANGLE_TRIANGLE)], 0U) << "triangle pairs";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, TheCpusCollideCasesHold)
{
    // collide_test.cpp's cases on the GPU: each the CPU's manifolds, and what the CPU tests check.
    const std::shared_ptr<const Shape> floor = floor_mesh(8);
    const std::shared_ptr<const Shape> small_floor = floor_mesh(4);
    const std::shared_ptr<const Shape> two_sided_floor = floor_mesh(4, true);
    const std::shared_ptr<const Shape> big_floor = two_sided_floor;
    const std::shared_ptr<const Shape> little_floor = floor_mesh(2, true);
    const std::shared_ptr<const Shape> crate = make_shape(BoxShape{.half_extents = {.x = 0.7F, .y = 0.5F, .z = 0.7F}});
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.5F});
    const std::array<float, 9> ridge_heights{0, 1, 0, 0, 1, 0, 0, 1, 0};
    const std::shared_ptr<const Shape> ridge = make_shape(HeightFieldShape{
        {.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{ridge_heights.data(), 3, 3}, .scale = {.x = 2, .y = 1, .z = 2}}});
    const std::shared_ptr<const Shape> slab = make_shape(BoxShape{.half_extents = {.x = 1, .y = 0.2F, .z = 0.5F}});
    const std::shared_ptr<const Shape> leg = make_shape(BoxShape{.half_extents = {.x = 0.1F, .y = 0.5F, .z = 0.1F}});
    const std::shared_ptr<const Shape> table = compound({{.shape = make_shape(BoxShape{.half_extents = {.x = 1, .y = 0.1F, .z = 1}}), .pose = {.position = {.y = 1.1F}}},
                                                         {.shape = leg, .pose = {.position = {.x = -0.9F, .y = 0.5F, .z = -0.9F}}},
                                                         {.shape = leg, .pose = {.position = {.x = 0.9F, .y = 0.5F, .z = -0.9F}}},
                                                         {.shape = leg, .pose = {.position = {.x = -0.9F, .y = 0.5F, .z = 0.9F}}},
                                                         {.shape = leg, .pose = {.position = {.x = 0.9F, .y = 0.5F, .z = 0.9F}}}});
    const std::shared_ptr<const Shape> ground = make_shape(BoxShape{.half_extents = {.x = 5, .y = 0.5F, .z = 5}});

    // The cases: two shapes at their poses, with their settings.
    struct Case
    {
        std::string label;
        std::shared_ptr<const Shape> first;
        Transform first_pose;
        std::shared_ptr<const Shape> second;
        Transform second_pose;
        CollisionSettings settings{};
    };
    std::vector<Case> cases;
    for (int step = 0; step <= 60; ++step)
    {
        const float x = -2.0F + static_cast<float>(step) * 0.07F;
        cases.push_back({std::format("seam step {}", step), crate, {.position = {.x = x, .y = 0.49F, .z = 0.3F * x}}, floor, {}});
    }
    constexpr std::size_t SEAM_STEPS = 61;
    cases.push_back({"floor first", floor, {}, crate, {.position = {.y = 0.49F}}});
    cases.push_back({"under a one-sided floor", ball, {.position = {.y = -0.49F}}, small_floor, {}});
    cases.push_back({"under a two-sided floor", ball, {.position = {.y = -0.49F}}, two_sided_floor, {}});
    cases.push_back({"across a ridge", slab, {.position = {.x = 2, .y = 1.15F, .z = 2}}, ridge, {}});
    cases.push_back({"across a ridge, one manifold", slab, {.position = {.x = 2, .y = 1.15F, .z = 2}}, ridge, {}, {.maximum_manifolds = 1}});
    cases.push_back({"a table on the ground", table, {.position = {.y = 0.01F - 0.02F}}, ground, {.position = {.y = -0.5F}}});
    cases.push_back({"meshes lowered onto each other", big_floor, {}, little_floor,
                     {.position = {.y = 0.01F}, .orientation = Quaternion::from_axis_angle({.x = 1}, 0.004F)}});
    cases.push_back({"meshes apart", big_floor, {}, little_floor, {.position = {.y = 0.5F}}});

    // One pair per case (each run alone: the cases' settings differ).
    std::map<const Shape*, std::uint32_t> indices;
    const auto index_of = [&](const std::shared_ptr<const Shape>& shape) {
        const auto [place, added] = indices.try_emplace(shape.get(), 0U);
        if (added)
        {
            place->second = m_narrow_phase->add_shape(*shape);
        }
        return place->second;
    };
    std::vector<std::vector<gpu::GpuManifold>> results;
    for (const Case& test : cases)
    {
        const std::array instances{instance_of(test.first_pose, index_of(test.first)), instance_of(test.second_pose, index_of(test.second))};
        const Collided collided = collide_pairs(elements_buffer(std::span<const gpu::GpuInstance>{instances}), consecutive_pairs(1), test.settings);
        results.emplace_back(collided.of(0).begin(), collided.of(0).end());
        EXPECT_NE(agreement(*test.first, test.first_pose, *test.second, test.second_pose, test.settings, results.back()), Agreement::NONE)
            << test.label << ": CPU " << describe(cpu_manifolds(*test.first, test.first_pose, *test.second, test.second_pose, test.settings)) << " — GPU "
            << describe(converted(results.back()));
    }

    // The box over the seams: one manifold of four points facing up from the floor, 1 cm deep, naming a triangle.
    for (std::size_t step = 0; step < SEAM_STEPS; ++step)
    {
        ASSERT_EQ(results[step].size(), 1U) << cases[step].label;
        const ContactManifold manifold = gpu::to_contact_manifold(results[step][0]);
        EXPECT_TRUE(approximately_equal(manifold.normal, {.y = -1}, 1e-4F)) << cases[step].label << ": " << describe(manifold);
        EXPECT_EQ(manifold.point_count, 4U) << cases[step].label;
        EXPECT_NEAR(manifold.deepest(), -0.01F, 1e-4F) << cases[step].label;
        EXPECT_NE(manifold.sub_shape_second, NO_SUB_SHAPE) << cases[step].label;
        EXPECT_EQ(manifold.sub_shape_first, NO_SUB_SHAPE) << cases[step].label;
    }
    std::size_t next = SEAM_STEPS;
    const std::vector<gpu::GpuManifold>& floor_first = results[next++];
    ASSERT_EQ(floor_first.size(), 1U);
    EXPECT_GT(floor_first[0].normal[1], 0.9999F) << "the triangles first: the normal up";
    for (std::uint32_t point = 0; point < floor_first[0].point_count; ++point)
    {
        EXPECT_NE(floor_first[0].points[point].feature.sub_shape_first, NO_SUB_SHAPE) << "each point names its triangle";
    }
    EXPECT_TRUE(results[next++].empty()) << "nothing from below a one-sided floor";
    const std::vector<gpu::GpuManifold>& below = results[next++];
    ASSERT_EQ(below.size(), 1U);
    EXPECT_GT(below[0].normal[1], 0.9999F) << "from the ball up to the floor above it";
    const std::vector<gpu::GpuManifold>& slopes = results[next++];
    ASSERT_EQ(slopes.size(), 2U) << "the ridge's two slopes";
    EXPECT_LT(slopes[0].normal[0] * slopes[1].normal[0], 0.0F) << "the two slopes lean opposite ways";
    const std::vector<gpu::GpuManifold>& deepest = results[next++];
    ASSERT_EQ(deepest.size(), 1U);
    const float deeper = std::min(gpu::to_contact_manifold(slopes[0]).deepest(), gpu::to_contact_manifold(slopes[1]).deepest());
    EXPECT_EQ(gpu::to_contact_manifold(deepest[0]).deepest(), deeper) << "the deepest stays";
    const std::vector<gpu::GpuManifold>& legs = results[next++];
    ASSERT_EQ(legs.size(), 1U) << "the four legs' manifolds merged";
    EXPECT_EQ(legs[0].point_count, 4U);
    EXPECT_GE(legs[0].sub_shape_first, 1U) << "a leg";
    EXPECT_LE(legs[0].sub_shape_first, 4U) << "a leg";
    EXPECT_EQ(legs[0].sub_shape_second, NO_SUB_SHAPE);
    const std::vector<gpu::GpuManifold>& lowered = results[next++];
    ASSERT_FALSE(lowered.empty());
    for (const gpu::GpuManifold& manifold : lowered)
    {
        EXPECT_EQ(manifold.algorithm, std::to_underlying(ContactAlgorithm::TRIANGLE_TRIANGLE));
        EXPECT_GT(std::abs(manifold.normal[1]), 0.99F);
    }
    EXPECT_TRUE(results[next++].empty()) << "half a unit apart";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, BoundsAreTheCpusBoxes)
{
    // Each shape at 225 poses, the boxes grown by 2 cm: the CPU's Shape::bounds(pose) grown the same.
    const std::vector<Sample> samples = random_samples(m_shapes, 15, 21);
    std::vector<gpu::GpuInstance> instances;
    for (const Sample& sample : samples)
    {
        instances.push_back(instance_of(sample.first_pose, m_shapes[sample.first].index));
    }
    const auto count = static_cast<std::uint32_t>(instances.size());
    gfx::Buffer& instance_buffer = elements_buffer(std::span<const gpu::GpuInstance>{instances});
    gfx::Buffer& boxes = words_buffer(8 * std::size_t{count});
    record([&](gfx::ComputeRecorder& recorder) {
        m_narrow_phase->upload_shapes(recorder);
        m_narrow_phase->bounds(recorder, gpu::span_of(instance_buffer), gpu::span_of(boxes), {.maximum = count}, 0.02F);
    });
    const std::vector<std::array<float, 8>> gpu_boxes = read<std::array<float, 8>>(boxes, count);
    std::size_t different = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const Aabb expected = m_shapes[samples[index].first].shape->bounds(samples[index].first_pose).expanded(0.02F);
        const std::array<float, 8>& box = gpu_boxes[index];
        const bool close = std::abs(box[0] - expected.minimum.x) < 1e-4F && std::abs(box[1] - expected.minimum.y) < 1e-4F &&
                           std::abs(box[2] - expected.minimum.z) < 1e-4F && std::abs(box[4] - expected.maximum.x) < 1e-4F &&
                           std::abs(box[5] - expected.maximum.y) < 1e-4F && std::abs(box[6] - expected.maximum.z) < 1e-4F;
        different += close ? 0U : 1U;
    }
    EXPECT_EQ(different, 0U);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, TheSamePairsGiveTheSameBits)
{
    std::vector<Sample> samples = random_samples(convex_shapes(), 100, 31);
    std::ranges::copy(overlapping_samples(m_shapes, 10, 32), std::back_inserter(samples));
    const Collided first = gpu_manifolds(samples);
    const Collided second = gpu_manifolds(samples);
    const auto bytes = [](const Collided& collided) {
        const std::span<const std::byte> view = std::as_bytes(std::span{collided.slots});
        return std::vector<std::byte>{view.begin(), view.end()};
    };
    EXPECT_EQ(bytes(first), bytes(second));
    EXPECT_EQ(first.ranges, second.ranges);
    EXPECT_EQ(first.total, second.total);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, PairsPastTheCapacityAreCutShort)
{
    // The pairs' slots counted in full; the pairs whose slots fit are whole, those past the capacity have none, nothing is
    // written past it.
    const std::vector<Sample> samples = overlapping_samples(m_shapes, 3, 71);
    gfx::Buffer& instances = instances_buffer(samples);
    const std::vector<gpu::GpuPair> pairs = consecutive_pairs(samples.size());
    const Collided full = collide_pairs(instances, pairs);
    ASSERT_GT(full.total, 100U);
    const std::uint32_t capacity = full.total / 2;
    const Collided cut = collide_pairs(instances, pairs, {}, capacity);
    EXPECT_EQ(cut.total, full.total) << "the total counts every pair's slots";
    const std::vector<std::uint32_t> written = words(*cut.manifolds, MANIFOLD_WORDS * (std::size_t{capacity} + 1));
    EXPECT_TRUE(std::ranges::all_of(std::span{written}.last(MANIFOLD_WORDS), [](std::uint32_t word) { return word == gfx::tests::UNTOUCHED; }))
        << "nothing past the capacity";
    std::size_t whole = 0;
    std::size_t emptied = 0;
    for (std::size_t pair = 0; pair < pairs.size(); ++pair)
    {
        EXPECT_EQ(cut.ranges[pair][0], full.ranges[pair][0]) << "pair " << pair;
        const std::uint32_t end = pair + 1 < pairs.size() ? full.ranges[pair + 1][0] : full.total;
        if (end <= capacity)
        {
            const std::span<const std::byte> expected = std::as_bytes(full.of(pair));
            const std::span<const std::byte> found = std::as_bytes(cut.of(pair));
            EXPECT_TRUE(std::ranges::equal(expected, found)) << "pair " << pair << " fits";
            ++whole;
        }
        else if (full.ranges[pair][0] >= capacity)
        {
            EXPECT_EQ(cut.ranges[pair][1], 0U) << "pair " << pair << " is past the capacity";
            ++emptied;
        }
    }
    EXPECT_GT(whole, 0U);
    EXPECT_GT(emptied, 0U);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, TheCacheFindsEachPointsPredecessor)
{
    // Step one: pairs (2i, 2i + 1). Step two: the instances moved by up to a millimeter, every seventh pair gone, the others
    // shuffled, and pairs (2i + 1, 2i + 2) added; the previous count from the first step's total.
    std::vector<Sample> samples = random_samples(convex_shapes(), 5, 41);
    std::ranges::copy(overlapping_samples(m_shapes, 2, 43), std::back_inserter(samples));
    const auto count = static_cast<std::uint32_t>(samples.size());
    const std::vector<gpu::GpuPair> first_pairs = consecutive_pairs(count);
    std::vector<Sample> moved = samples;
    std::mt19937 generator{42};
    std::uniform_real_distribution<float> nudge{-1e-3F, 1e-3F};
    for (Sample& sample : moved)
    {
        sample.second_pose.position = sample.second_pose.position + Vector3{.x = nudge(generator), .y = nudge(generator), .z = nudge(generator)};
    }
    std::vector<gpu::GpuPair> second_pairs;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (index % 7 != 3)
        {
            second_pairs.push_back(first_pairs[index]);
        }
        if (index + 1 < count && index % 5 == 0)
        {
            second_pairs.push_back({.first = 2 * index + 1, .second = 2 * index + 2});
        }
    }
    std::ranges::shuffle(second_pairs, generator);
    const Collided before = collide_pairs(instances_buffer(samples), first_pairs);
    const Collided after = collide_pairs(instances_buffer(moved), second_pairs);
    m_narrow_phase->reserve(1, before.capacity);
    gfx::Buffer& predecessors = words_buffer(4 * std::size_t{after.capacity});
    record([&](gfx::ComputeRecorder& recorder) {
        m_narrow_phase->match(recorder, gpu::span_of(*before.manifolds), {.maximum = before.capacity, .word = gpu::span_of(*before.total_word)},
                              gpu::span_of(*after.manifolds), {.maximum = after.capacity, .word = gpu::span_of(*after.total_word)}, gpu::span_of(predecessors));
    });
    const std::vector<std::uint32_t> found = words(predecessors, 4 * after.slots.size());

    // The expected predecessors from the manifolds read back: the first point of the pair's earlier manifolds (in slot
    // order) with the same feature.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>> previous;
    for (std::uint32_t slot = 0; slot < before.slots.size(); ++slot)
    {
        if (before.slots[slot].point_count > 0)
        {
            previous[{before.slots[slot].first, before.slots[slot].second}].push_back(slot);
        }
    }
    std::size_t wrong = 0;
    std::size_t matched = 0;
    std::size_t could_match = 0;
    std::size_t several = 0;
    for (std::size_t slot = 0; slot < after.slots.size(); ++slot)
    {
        const gpu::GpuManifold& manifold = after.slots[slot];
        const auto earlier = previous.find({manifold.first, manifold.second});
        several += earlier != previous.end() && earlier->second.size() > 1 && manifold.point_count > 0 ? 1U : 0U;
        for (std::uint32_t point = 0; point < 4; ++point)
        {
            std::uint32_t expected = NO_PREDECESSOR;
            const bool candidate = point < manifold.point_count && earlier != previous.end();
            could_match += candidate ? 1U : 0U;
            for (std::size_t position = 0; candidate && position < earlier->second.size() && expected == NO_PREDECESSOR; ++position)
            {
                const gpu::GpuManifold& old = before.slots[earlier->second[position]];
                for (std::uint32_t other = 0; other < old.point_count; ++other)
                {
                    if (old.points[other].feature == manifold.points[point].feature)
                    {
                        expected = earlier->second[position] * 4 + other;
                        break;
                    }
                }
            }
            wrong += found[4 * slot + point] == expected ? 0U : 1U;
            matched += expected != NO_PREDECESSOR ? 1U : 0U;
        }
    }
    EXPECT_EQ(wrong, 0U);
    EXPECT_GT(several, 0U) << "manifolds of pairs that had several";
    EXPECT_GT(matched, could_match * 3 / 4) << "most points keep their features when the shapes barely move (" << matched << " of " << could_match << ")";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, BoxesTreesPairsAndManifoldsAreTheCpusBruteForce)
{
    // 1200 shapes of every kind (most of them convex) in a 12 m cube: their boxes (grown by the speculative distance), a
    // tree, its pairs, their manifolds, the pair count from the tree's word; against every pair on the CPU.
    constexpr std::uint32_t COUNT = 1'200;
    constexpr std::uint32_t CAPACITY = 40'000;
    const CollisionSettings settings{};
    std::mt19937 generator{51};
    std::uniform_real_distribution<float> coordinate{-6.0F, 6.0F};
    std::uniform_int_distribution<std::size_t> convex_kind{0, CONVEX_SHAPES - 1};
    std::uniform_int_distribution<std::size_t> any_kind{0, m_shapes.size() - 1};
    std::vector<std::size_t> kinds;
    std::vector<Transform> poses;
    std::vector<gpu::GpuInstance> instances;
    for (std::uint32_t index = 0; index < COUNT; ++index)
    {
        kinds.push_back(index % 4 == 0 ? any_kind(generator) : convex_kind(generator));
        poses.push_back({.position = {.x = coordinate(generator), .y = coordinate(generator), .z = coordinate(generator)}, .orientation = random_rotation(generator)});
        instances.push_back(instance_of(poses.back(), m_shapes[kinds.back()].index));
    }
    gfx::Buffer& instance_buffer = elements_buffer(std::span<const gpu::GpuInstance>{instances});
    gfx::Buffer& boxes = words_buffer(8 * std::size_t{COUNT});
    gfx::Buffer& pairs = words_buffer(2 * std::size_t{CAPACITY});
    gfx::Buffer& pair_count = words_buffer(1);
    gpu::GpuBvh tree{*m_device, *m_primitives, {.layout = GpuBvhLayout::FOUR_WIDE}};
    tree.reserve(COUNT);
    std::uint32_t slots = 4 * CAPACITY;
    std::uint32_t total = 0;
    gfx::Buffer* manifolds = nullptr;
    gfx::Buffer* ranges = nullptr;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        m_narrow_phase->reserve(CAPACITY, slots);
        manifolds = &words_buffer(MANIFOLD_WORDS * std::size_t{slots});
        ranges = &words_buffer(2 * std::size_t{CAPACITY});
        gfx::Buffer& total_word = words_buffer(1);
        record([&](gfx::ComputeRecorder& recorder) {
            m_narrow_phase->upload_shapes(recorder);
            m_narrow_phase->bounds(recorder, gpu::span_of(instance_buffer), gpu::span_of(boxes), {.maximum = COUNT}, settings.speculative_distance);
            tree.build(recorder, gpu::span_of(boxes), {.maximum = COUNT});
            tree.find_pairs(recorder, gpu::span_of(pairs), gpu::span_of(pair_count), CAPACITY);
            m_narrow_phase->collide(recorder, gpu::span_of(instance_buffer), gpu::span_of(pairs), {.maximum = CAPACITY, .word = gpu::span_of(pair_count)},
                                    {.manifolds = gpu::span_of(*manifolds), .ranges = gpu::span_of(*ranges), .total = gpu::span_of(total_word), .capacity = slots},
                                    settings);
        });
        total = words(total_word, 1)[0];
        if (total <= slots)
        {
            break;
        }
        slots = total;
    }
    const std::uint32_t found = words(pair_count, 1)[0];
    ASSERT_LE(found, CAPACITY);
    ASSERT_LE(total, slots);
    const std::vector<gpu::GpuPair> tree_pairs = read<gpu::GpuPair>(pairs, found);
    const std::vector<gpu::GpuManifold> results = read<gpu::GpuManifold>(*manifolds, total);
    const std::vector<std::uint32_t> range_words = words(*ranges, 2 * std::size_t{found});
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::span<const gpu::GpuManifold>> gpu_contacts;
    for (std::size_t pair = 0; pair < found; ++pair)
    {
        if (range_words[2 * pair + 1] > 0)
        {
            gpu_contacts[{tree_pairs[pair].first, tree_pairs[pair].second}] = std::span{results}.subspan(range_words[2 * pair], range_words[2 * pair + 1]);
        }
    }

    // Every pair on the CPU (the pairs with triangles or children within their leeway).
    std::size_t examined = 0;
    std::size_t contacts = 0;
    std::size_t missing = 0;
    std::size_t disagreeing = 0;
    std::size_t gpu_contacts_seen = 0;
    for (std::uint32_t first = 0; first < COUNT; ++first)
    {
        for (std::uint32_t second = first + 1; second < COUNT; ++second)
        {
            const Shape& a = *m_shapes[kinds[first]].shape;
            const Shape& b = *m_shapes[kinds[second]].shape;
            if (!intersects(a.bounds(poses[first]).expanded(0.1F), b.bounds(poses[second]).expanded(0.1F)))
            {
                continue;
            }
            const auto gpu_contact = gpu_contacts.find({first, second});
            const std::span<const gpu::GpuManifold> gpu_manifolds = gpu_contact != gpu_contacts.end() ? gpu_contact->second : std::span<const gpu::GpuManifold>{};
            gpu_contacts_seen += gpu_contact != gpu_contacts.end() ? 1U : 0U;
            const bool cpu_contact = !cpu_manifolds(a, poses[first], b, poses[second], settings).empty();
            const bool convex = kinds[first] < CONVEX_SHAPES && kinds[second] < CONVEX_SHAPES;
            ++examined;
            contacts += cpu_contact ? 1U : 0U;
            missing += cpu_contact && gpu_contact == gpu_contacts.end() ? 1U : 0U;
            if (agreement(a, poses[first], b, poses[second], settings, gpu_manifolds) == Agreement::NONE)
            {
                const std::string failure = std::format("{} {} / {} {}: CPU {} — GPU {}", m_shapes[kinds[first]].name, first, m_shapes[kinds[second]].name, second,
                                                        describe(cpu_manifolds(a, poses[first], b, poses[second], settings)), describe(converted(gpu_manifolds)));
                if (convex)
                {
                    ADD_FAILURE() << failure;
                }
                else
                {
                    ++disagreeing;
                    std::cout << "at a tie: " << failure << '\n';
                }
            }
        }
    }
    std::cout << std::format("{} shapes: {} pairs from the tree ({} slots), {} pairs examined, {} in contact on the GPU, {} on the CPU, {} at ties\n", COUNT,
                             found, total, examined, gpu_contacts.size(), contacts, disagreeing);
    EXPECT_GT(contacts, 100U);
    EXPECT_EQ(missing, 0U) << "every CPU contact's pair came from the tree";
    EXPECT_EQ(gpu_contacts_seen, gpu_contacts.size()) << "no GPU contact between shapes the CPU finds far apart";
    EXPECT_LE(static_cast<double>(disagreeing), TRIANGLE_LEEWAY.ties * static_cast<double>(examined));
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, MisusesAreRefused)
{
    // Compounds nested deeper than GPU_XPBD_COMPOUND_DEPTH, reserves above the maximum; passes refuse a table not
    // uploaded, invalid settings, counts and capacities above the reserve.
    std::shared_ptr<const Shape> nested = make_shape(SphereShape{.radius = 0.1F});
    for (std::uint32_t depth = 1; depth <= GPU_XPBD_COMPOUND_DEPTH + 1; ++depth)
    {
        nested = compound({{.shape = nested, .pose = {.position = {.x = 0.1F}}}});
        if (depth <= GPU_XPBD_COMPOUND_DEPTH)
        {
            EXPECT_NO_THROW(m_narrow_phase->add_shape(*nested)) << "depth " << depth;
        }
        else
        {
            EXPECT_THROW(m_narrow_phase->add_shape(*nested), gfx::BackendCapabilityError) << "depth " << depth;
        }
    }
    EXPECT_THROW(m_narrow_phase->reserve(GPU_XPBD_MAXIMUM_ELEMENTS + 1, 1), gfx::InvalidUsageError);
    EXPECT_THROW(m_narrow_phase->reserve(1, GPU_XPBD_MAXIMUM_ELEMENTS + 1), gfx::InvalidUsageError);
    gfx::Buffer& scratch = words_buffer(64);
    const gpu::GpuManifoldSpans output{.manifolds = gpu::span_of(scratch), .ranges = gpu::span_of(scratch), .total = gpu::span_of(scratch), .capacity = 1};
    run<gfx::QueueType::COMPUTE>([&](gfx::ComputeRecorder& recorder) {
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}, output), gfx::InvalidUsageError)
            << "shapes added since the upload";
        m_narrow_phase->upload_shapes(recorder);
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}, output, {.speculative_distance = -1.0F}),
                     gfx::InvalidUsageError);
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}, output, {.maximum_manifolds = 0}),
                     gfx::InvalidUsageError);
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = m_narrow_phase->reserved_pairs() + 1}, output),
                     gfx::InvalidUsageError);
        gpu::GpuManifoldSpans too_large = output;
        too_large.capacity = m_narrow_phase->reserved_manifolds() + 1;
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}, too_large), gfx::InvalidUsageError);
        EXPECT_THROW(m_narrow_phase->bounds(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}, std::numeric_limits<float>::infinity()),
                     gfx::InvalidUsageError);
        EXPECT_THROW(m_narrow_phase->match(recorder, gpu::span_of(scratch), {.maximum = m_narrow_phase->reserved_manifolds() + 1}, gpu::span_of(scratch),
                                           {.maximum = 1}, gpu::span_of(scratch)),
                     gfx::InvalidUsageError);
    });
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuNarrowPhaseTest,
                         testing::Combine(testing::ValuesIn(gfx::tests::gpu_physical_devices()), testing::Values(GpuVariantChoice::AUTOMATIC)),
                         gfx::tests::gpu_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuNarrowPhaseTest);

// ===================================================================================================== measurements

/// The narrow phase's throughput on each GPU (printed).
class GpuNarrowPhaseMeasurementTest : public GpuNarrowPhaseTest
{
protected:
    /// The fastest of five runs of collide() over @p samples (the slots sized by a first run).
    void measure(std::span<const Sample> samples,
                 std::string_view label)
    {
        const auto count = static_cast<std::uint32_t>(samples.size());
        gfx::Buffer& instances = instances_buffer(samples);
        const std::vector<gpu::GpuPair> pairs = consecutive_pairs(count);
        const Collided sizing = collide_pairs(instances, pairs);
        gfx::Buffer& pair_buffer = elements_buffer(std::span<const gpu::GpuPair>{pairs});
        gfx::Buffer& ranges = words_buffer(2 * std::size_t{count});
        double fastest = std::numeric_limits<double>::infinity();
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            const auto start = std::chrono::steady_clock::now();
            record([&](gfx::ComputeRecorder& recorder) {
                m_narrow_phase->collide(recorder, gpu::span_of(instances), gpu::span_of(pair_buffer), {.maximum = count},
                                        {.manifolds = gpu::span_of(*sizing.manifolds),
                                         .ranges = gpu::span_of(ranges),
                                         .total = gpu::span_of(*sizing.total_word),
                                         .capacity = sizing.capacity});
            });
            fastest = std::min(fastest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::cout << std::format("{} | {} pairs of {} ({} slots) in {:.2f} ms ({:.1f} pairs per microsecond)\n", m_physical_device->properties().name, count,
                                 label, sizing.total, fastest, count / fastest / 1000.0);
    }
};

TEST_P(GpuNarrowPhaseMeasurementTest, ThroughputIsMeasured)
{
    // 3000 poses of each ordered pair of convex shapes (192 000 pairs, every algorithm); 200 overlapping poses of each pair
    // with a mesh, a height field or a compound (32 200 pairs).
    measure(random_samples(convex_shapes(), 3'000, 61), "convex shapes");
    measure(overlapping_samples(m_shapes, 200, 62), "shapes with triangles or children");
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuNarrowPhaseMeasurementTest,
                         testing::Combine(testing::ValuesIn(gfx::tests::gpu_physical_devices()), testing::Values(GpuVariantChoice::AUTOMATIC)),
                         gfx::tests::gpu_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuNarrowPhaseMeasurementTest);

} // namespace
