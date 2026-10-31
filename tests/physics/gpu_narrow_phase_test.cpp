// The GPU backend's narrow phase (phase G3) against the CPU's (physics::collide()), on each GPU of the machine with the
// Vulkan validation layers: every ordered pair of eight convex shapes (sphere, capsule, cylinder, cone, box, rounded box,
// hull, rounded hull) at random poses — apart, touching, overlapping — gives the CPU's manifold (algorithm, normal, points
// with their feature ids, separations), or, where float rounding tips a decision, the manifold the CPU gives at a pose
// 0.2 mm or 0.2 mrad away; instance boxes; the same bits twice; the contact cache's predecessors; boxes, a BVH, its pairs
// and their manifolds against brute force on the CPU; misuses; and the throughput measured.
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

/// A shape the pairs are drawn from: its name, the shape, how far its surface reaches from its center at most.
struct TestShape
{
    std::string_view name;
    std::shared_ptr<const Shape> shape;
    float reach = 0;
};

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

/// The eight shapes.
auto
test_shapes() -> std::vector<TestShape>
{
    const std::vector<Vector3> corners = ellipsoid_points({.x = 0.6F, .y = 0.45F, .z = 0.5F}, 7, 30);
    std::vector<Vector3> smaller;
    for (const Vector3 corner : corners)
    {
        smaller.push_back(corner * 0.85F);
    }
    std::vector<TestShape> shapes{
        {.name = "sphere", .shape = make_shape(SphereShape{.radius = 0.5F})},
        {.name = "capsule", .shape = make_shape(CapsuleShape{.half_height = 0.6F, .radius = 0.3F})},
        {.name = "cylinder", .shape = make_shape(CylinderShape{.half_height = 0.5F, .radius = 0.4F})},
        {.name = "cone", .shape = make_shape(ConeShape{.half_height = 0.6F, .radius = 0.5F})},
        {.name = "box", .shape = make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.3F, .z = 0.7F}})},
        {.name = "rounded box", .shape = make_shape(BoxShape{.half_extents = {.x = 0.4F, .y = 0.25F, .z = 0.6F}, .radius = 0.08F})},
        {.name = "hull", .shape = make_shape(HullShape::from_points(corners))},
        {.name = "rounded hull", .shape = make_shape(HullShape::from_points(smaller, 0.06F))},
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

/// The CPU's manifold of two shapes at their poses (convex shapes: one at most).
auto
cpu_manifold(const Shape& first,
             const Transform& first_pose,
             const Shape& second,
             const Transform& second_pose,
             const CollisionSettings& settings) -> std::optional<ContactManifold>
{
    thread_local ContactCollector collector;
    collide({.shape = &first, .pose = first_pose}, {.shape = &second, .pose = second_pose}, collector, settings);
    if (collector.manifolds().empty())
    {
        return std::nullopt;
    }
    return collector.manifolds().front();
}

/// How close a GPU manifold must be to the CPU's: float rounding for the closed forms and GJK; for EPA, its convergence
/// (it stops when the depth is within 10⁻⁴ of its reach, which leaves a curved surface's normal free by about a degree,
/// and a contact point on it free by its radius times that).
struct Tolerance
{
    float normal_cosine = 0.0F;
    float position = 0.0F;
    float separation = 0.0F;
};

constexpr Tolerance ROUNDING{.normal_cosine = 0.9995F, .position = 5e-3F, .separation = 5e-3F};
constexpr Tolerance EPA_CONVERGENCE{.normal_cosine = 0.999F, .position = 2.5e-2F, .separation = 2e-3F};

/// Whether the GPU's manifold is the CPU's: no contact on both, or the same algorithm, point count and features (each CPU
/// point matched to the nearest unmatched GPU point of its feature: a manifold may hold a feature twice), normals,
/// positions and separations within the algorithm's tolerance.
auto
agree(const std::optional<ContactManifold>& cpu,
      const gpu::GpuManifold& gpu) -> bool
{
    if (!cpu)
    {
        return gpu.point_count == 0;
    }
    if (gpu.point_count > MAXIMUM_MANIFOLD_POINTS || gpu.point_count != cpu->point_count || gpu.algorithm != std::to_underlying(cpu->algorithm))
    {
        return false;
    }
    const Tolerance tolerance = cpu->algorithm == ContactAlgorithm::EPA ? EPA_CONVERGENCE : ROUNDING;
    const Vector3 gpu_normal{.x = gpu.normal[0], .y = gpu.normal[1], .z = gpu.normal[2]};
    if (gfx::dot(gpu_normal, cpu->normal) < tolerance.normal_cosine)
    {
        return false;
    }
    std::array<bool, MAXIMUM_MANIFOLD_POINTS> used{};
    for (const ContactPoint& point : cpu->contacts())
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

/// Whether the GPU's manifold is the CPU's at the poses, or at a nearby pose of either shape (rounding tipped a decision:
/// a tie of support points, a clipped segment along a side plane, EPA's polytope in a deep overlap of curved shapes).
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
          const gpu::GpuManifold& gpu) -> Agreement
{
    if (agree(cpu_manifold(first, first_pose, second, second_pose, settings), gpu))
    {
        return Agreement::EXACT_POSE;
    }
    for (std::size_t variation = 0; variation < PERTURBATIONS; ++variation)
    {
        if (agree(cpu_manifold(first, perturbed(first_pose, variation), second, second_pose, settings), gpu))
        {
            return Agreement::NEARBY_POSE;
        }
        if (agree(cpu_manifold(first, first_pose, second, perturbed(second_pose, variation), settings), gpu))
        {
            return Agreement::NEARBY_POSE;
        }
    }
    return Agreement::NONE;
}

/// A manifold in a line.
auto
describe(const std::optional<ContactManifold>& manifold) -> std::string
{
    if (!manifold)
    {
        return "none";
    }
    std::string text = std::format("{} normal ({:.4f}, {:.4f}, {:.4f})", manifold->algorithm, manifold->normal.x, manifold->normal.y, manifold->normal.z);
    for (const ContactPoint& point : manifold->contacts())
    {
        text += std::format(" [{:08x} ({:.4f}, {:.4f}, {:.4f}) {:.4f}]", point.feature.local, point.position.x, point.position.y, point.position.z, point.separation);
    }
    return text;
}

auto
instance_of(const Transform& pose,
            std::uint32_t shape) -> gpu::GpuInstance
{
    return {.position = {pose.position.x, pose.position.y, pose.position.z},
            .shape = shape,
            .orientation = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}};
}

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
        for (const TestShape& shape : m_shapes)
        {
            m_narrow_phase->add_shape(*shape.shape);
        }
        record([&](gfx::ComputeRecorder& recorder) { m_narrow_phase->upload_shapes(recorder); });
    }

    void release_gpu_objects() override
    {
        m_narrow_phase.reset();
    }

    /// The instances of @p samples: each sample's first then second shape.
    auto instances_buffer(std::span<const Sample> samples) -> gfx::Buffer&
    {
        std::vector<gpu::GpuInstance> instances;
        for (const Sample& sample : samples)
        {
            instances.push_back(instance_of(sample.first_pose, static_cast<std::uint32_t>(sample.first)));
            instances.push_back(instance_of(sample.second_pose, static_cast<std::uint32_t>(sample.second)));
        }
        return elements_buffer(std::span<const gpu::GpuInstance>{instances});
    }

    /// The GPU's manifolds of @p samples (pair i: instances 2i and 2i + 1).
    auto gpu_manifolds(std::span<const Sample> samples,
                       const CollisionSettings& settings = {}) -> std::vector<gpu::GpuManifold>
    {
        const auto count = static_cast<std::uint32_t>(samples.size());
        std::vector<gpu::GpuPair> pairs;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            pairs.push_back({.first = 2 * index, .second = 2 * index + 1});
        }
        gfx::Buffer& instances = instances_buffer(samples);
        gfx::Buffer& pair_buffer = elements_buffer(std::span<const gpu::GpuPair>{pairs});
        gfx::Buffer& manifolds = words_buffer(40 * std::size_t{count});
        record([&](gfx::ComputeRecorder& recorder) {
            m_narrow_phase->collide(recorder, gpu::span_of(instances), gpu::span_of(pair_buffer), gpu::span_of(manifolds), {.maximum = count}, settings);
        });
        return read<gpu::GpuManifold>(manifolds, count);
    }

    std::unique_ptr<gpu::GpuNarrowPhase> m_narrow_phase;
    std::vector<TestShape> m_shapes;
};

// ===================================================================================================== tests

TEST_P(GpuNarrowPhaseTest, ManifoldsAreTheCpusOnEveryPairOfShapes)
{
    // 300 poses of each of the 64 ordered pairs, with the default speculative distance and with none.
    for (const float speculative : {CollisionSettings{}.speculative_distance, 0.0F})
    {
        const CollisionSettings settings{.speculative_distance = speculative};
        const std::vector<Sample> samples = random_samples(m_shapes, 300, speculative > 0 ? 11U : 12U);
        const std::vector<gpu::GpuManifold> manifolds = gpu_manifolds(samples, settings);
        std::map<std::pair<std::size_t, std::size_t>, std::array<std::size_t, 3>> outcomes;
        std::array<std::size_t, gfx::ENUMERATOR_COUNT<ContactAlgorithm>> algorithms{};
        std::size_t contacts = 0;
        std::vector<std::string> failures;
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            const Sample& sample = samples[index];
            const Shape& first = *m_shapes[sample.first].shape;
            const Shape& second = *m_shapes[sample.second].shape;
            const Agreement result = agreement(first, sample.first_pose, second, sample.second_pose, settings, manifolds[index]);
            ++outcomes[{sample.first, sample.second}][std::to_underlying(result)];
            if (manifolds[index].point_count > 0)
            {
                ++contacts;
                ++algorithms[std::min<std::size_t>(manifolds[index].algorithm, algorithms.size() - 1)];
            }
            if (result == Agreement::NONE && failures.size() < 6)
            {
                failures.push_back(std::format("{} / {} sample {}: CPU {} — GPU {}", m_shapes[sample.first].name, m_shapes[sample.second].name, index,
                                               describe(cpu_manifold(first, sample.first_pose, second, sample.second_pose, settings)),
                                               describe(manifolds[index].point_count > 0 ? std::optional{gpu::to_contact_manifold(manifolds[index])} : std::nullopt)));
            }
        }

        // Every pair agrees; rounding tips few; every algorithm ran.
        std::size_t exact = 0;
        std::size_t nearby = 0;
        std::size_t none = 0;
        for (const auto& [kinds, counts] : outcomes)
        {
            exact += counts[0];
            nearby += counts[1];
            none += counts[2];
            EXPECT_EQ(counts[2], 0U) << m_shapes[kinds.first].name << " / " << m_shapes[kinds.second].name << ": " << counts[2] << " disagree";
        }
        for (const std::string& failure : failures)
        {
            ADD_FAILURE() << failure;
        }
        std::cout << std::format("speculative {}: {} pairs, {} contacts; the CPU's at the pose {}, at a nearby pose {}, neither {}; algorithms {}\n", speculative,
                                 samples.size(), contacts, exact, nearby, none, algorithms);
        EXPECT_LT(nearby, samples.size() / 50) << "rounding tips at most 2 % of the pairs";
        for (const ContactAlgorithm algorithm : {ContactAlgorithm::SPHERE_SPHERE, ContactAlgorithm::SPHERE_CAPSULE, ContactAlgorithm::CAPSULE_CAPSULE,
                                                 ContactAlgorithm::SPHERE_BOX, ContactAlgorithm::BOX_BOX, ContactAlgorithm::GJK, ContactAlgorithm::EPA})
        {
            EXPECT_GT(algorithms[std::to_underlying(algorithm)], 0U) << gfx::to_string(algorithm) << " ran";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, BoundsAreTheCpusBoxes)
{
    // Each shape at 200 poses, the boxes grown by 2 cm: the CPU's Shape::bounds(pose) grown the same.
    std::vector<Sample> samples = random_samples(m_shapes, 25, 21);
    std::vector<gpu::GpuInstance> instances;
    for (const Sample& sample : samples)
    {
        instances.push_back(instance_of(sample.first_pose, static_cast<std::uint32_t>(sample.first)));
    }
    const auto count = static_cast<std::uint32_t>(instances.size());
    gfx::Buffer& instance_buffer = elements_buffer(std::span<const gpu::GpuInstance>{instances});
    gfx::Buffer& boxes = words_buffer(8 * std::size_t{count});
    record([&](gfx::ComputeRecorder& recorder) { m_narrow_phase->bounds(recorder, gpu::span_of(instance_buffer), gpu::span_of(boxes), {.maximum = count}, 0.02F); });
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
    const std::vector<Sample> samples = random_samples(m_shapes, 100, 31);
    const std::vector<gpu::GpuManifold> first = gpu_manifolds(samples);
    const std::vector<gpu::GpuManifold> second = gpu_manifolds(samples);
    const auto bytes = [](const std::vector<gpu::GpuManifold>& manifolds) {
        const std::span<const std::byte> view = std::as_bytes(std::span{manifolds});
        return std::vector<std::byte>{view.begin(), view.end()};
    };
    EXPECT_EQ(bytes(first), bytes(second));
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, TheCacheFindsEachPointsPredecessor)
{
    // Step one: pairs (2i, 2i + 1). Step two: the instances moved by up to a millimeter, every seventh pair gone, the others
    // shuffled, and pairs (2i + 1, 2i + 2) added; the previous count in a word.
    const std::vector<Sample> samples = random_samples(m_shapes, 40, 41);
    const auto count = static_cast<std::uint32_t>(samples.size());
    std::vector<gpu::GpuPair> first_pairs;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        first_pairs.push_back({.first = 2 * index, .second = 2 * index + 1});
    }
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
    const auto second_count = static_cast<std::uint32_t>(second_pairs.size());

    gfx::Buffer& first_instances = instances_buffer(samples);
    gfx::Buffer& second_instances = instances_buffer(moved);
    gfx::Buffer& first_pair_buffer = elements_buffer(std::span<const gpu::GpuPair>{first_pairs});
    gfx::Buffer& second_pair_buffer = elements_buffer(std::span<const gpu::GpuPair>{second_pairs});
    gfx::Buffer& first_manifolds = words_buffer(40 * std::size_t{count});
    gfx::Buffer& second_manifolds = words_buffer(40 * std::size_t{second_count});
    gfx::Buffer& predecessors = words_buffer(4 * std::size_t{second_count});
    gfx::Buffer& previous_count = words_buffer(1, std::array{count});
    m_narrow_phase->reserve(count + 100);
    record([&](gfx::ComputeRecorder& recorder) {
        m_narrow_phase->collide(recorder, gpu::span_of(first_instances), gpu::span_of(first_pair_buffer), gpu::span_of(first_manifolds), {.maximum = count});
        m_narrow_phase->collide(recorder, gpu::span_of(second_instances), gpu::span_of(second_pair_buffer), gpu::span_of(second_manifolds),
                                {.maximum = second_count});
        m_narrow_phase->match(recorder, gpu::span_of(first_manifolds), {.maximum = count + 100, .word = gpu::span_of(previous_count)},
                              gpu::span_of(second_manifolds), {.maximum = second_count}, gpu::span_of(predecessors));
    });
    const std::vector<gpu::GpuManifold> before = read<gpu::GpuManifold>(first_manifolds, count);
    const std::vector<gpu::GpuManifold> after = read<gpu::GpuManifold>(second_manifolds, second_count);
    const std::vector<std::uint32_t> found = words(predecessors, 4 * std::size_t{second_count});

    // The expected predecessors from the manifolds read back.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> previous;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (before[index].point_count > 0)
        {
            previous[{before[index].first, before[index].second}] = index;
        }
    }
    std::size_t wrong = 0;
    std::size_t matched = 0;
    std::size_t could_match = 0;
    for (std::uint32_t index = 0; index < second_count; ++index)
    {
        const gpu::GpuManifold& manifold = after[index];
        for (std::uint32_t point = 0; point < 4; ++point)
        {
            std::uint32_t expected = NO_PREDECESSOR;
            const auto earlier = previous.find({manifold.first, manifold.second});
            could_match += point < manifold.point_count && earlier != previous.end() ? 1U : 0U;
            if (point < manifold.point_count && earlier != previous.end())
            {
                const gpu::GpuManifold& old = before[earlier->second];
                for (std::uint32_t other = 0; other < old.point_count; ++other)
                {
                    if (old.points[other].feature == manifold.points[point].feature)
                    {
                        expected = earlier->second * 4 + other;
                        break;
                    }
                }
            }
            wrong += found[4 * std::size_t{index} + point] == expected ? 0U : 1U;
            matched += expected != NO_PREDECESSOR ? 1U : 0U;
        }
    }
    EXPECT_EQ(wrong, 0U);
    EXPECT_GT(matched, could_match * 4 / 5) << "most points keep their features when the shapes barely move (" << matched << " of " << could_match << ")";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, BoxesTreesPairsAndManifoldsAreTheCpusBruteForce)
{
    // 1200 shapes in a 12 m cube: their boxes (grown by the speculative distance), a tree, its pairs, their manifolds,
    // the pair count from the tree's word; against every pair on the CPU.
    constexpr std::uint32_t COUNT = 1'200;
    constexpr std::uint32_t CAPACITY = 40'000;
    const CollisionSettings settings{};
    std::mt19937 generator{51};
    std::uniform_real_distribution<float> coordinate{-6.0F, 6.0F};
    std::uniform_int_distribution<std::size_t> kind{0, m_shapes.size() - 1};
    std::vector<std::size_t> kinds;
    std::vector<Transform> poses;
    std::vector<gpu::GpuInstance> instances;
    for (std::uint32_t index = 0; index < COUNT; ++index)
    {
        kinds.push_back(kind(generator));
        poses.push_back({.position = {.x = coordinate(generator), .y = coordinate(generator), .z = coordinate(generator)}, .orientation = random_rotation(generator)});
        instances.push_back(instance_of(poses.back(), static_cast<std::uint32_t>(kinds.back())));
    }
    gfx::Buffer& instance_buffer = elements_buffer(std::span<const gpu::GpuInstance>{instances});
    gfx::Buffer& boxes = words_buffer(8 * std::size_t{COUNT});
    gfx::Buffer& pairs = words_buffer(2 * std::size_t{CAPACITY});
    gfx::Buffer& pair_count = words_buffer(1);
    gfx::Buffer& manifolds = words_buffer(40 * std::size_t{CAPACITY});
    gpu::GpuBvh tree{*m_device, *m_primitives, {.layout = GpuBvhLayout::FOUR_WIDE}};
    tree.reserve(COUNT);
    record([&](gfx::ComputeRecorder& recorder) {
        m_narrow_phase->bounds(recorder, gpu::span_of(instance_buffer), gpu::span_of(boxes), {.maximum = COUNT}, settings.speculative_distance);
        tree.build(recorder, gpu::span_of(boxes), {.maximum = COUNT});
        tree.find_pairs(recorder, gpu::span_of(pairs), gpu::span_of(pair_count), CAPACITY);
        m_narrow_phase->collide(recorder, gpu::span_of(instance_buffer), gpu::span_of(pairs), gpu::span_of(manifolds),
                                {.maximum = CAPACITY, .word = gpu::span_of(pair_count)}, settings);
    });
    const std::uint32_t found = words(pair_count, 1)[0];
    ASSERT_LE(found, CAPACITY);
    const std::vector<gpu::GpuManifold> results = read<gpu::GpuManifold>(manifolds, found);
    std::map<std::pair<std::uint32_t, std::uint32_t>, gpu::GpuManifold> gpu_contacts;
    for (const gpu::GpuManifold& manifold : results)
    {
        if (manifold.point_count > 0)
        {
            gpu_contacts[{manifold.first, manifold.second}] = manifold;
        }
    }

    // Every pair on the CPU.
    std::size_t contacts = 0;
    std::size_t missing = 0;
    std::size_t disagreeing = 0;
    std::size_t gpu_contacts_seen = 0;
    const gpu::GpuManifold nothing{};
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
            const gpu::GpuManifold& gpu_manifold = gpu_contact != gpu_contacts.end() ? gpu_contact->second : nothing;
            gpu_contacts_seen += gpu_contact != gpu_contacts.end() ? 1U : 0U;
            const bool cpu_contact = cpu_manifold(a, poses[first], b, poses[second], settings).has_value();
            contacts += cpu_contact ? 1U : 0U;
            missing += cpu_contact && gpu_contact == gpu_contacts.end() ? 1U : 0U;
            if (agreement(a, poses[first], b, poses[second], settings, gpu_manifold) == Agreement::NONE)
            {
                ++disagreeing;
                ADD_FAILURE() << std::format("{} {} / {} {}: CPU {} — GPU {}", m_shapes[kinds[first]].name, first, m_shapes[kinds[second]].name, second,
                                             describe(cpu_manifold(a, poses[first], b, poses[second], settings)),
                                             describe(gpu_manifold.point_count > 0 ? std::optional{gpu::to_contact_manifold(gpu_manifold)} : std::nullopt));
            }
        }
    }
    std::cout << std::format("{} shapes: {} pairs from the tree, {} contacts on the GPU, {} on the CPU\n", COUNT, found, gpu_contacts.size(), contacts);
    EXPECT_GT(contacts, 100U);
    EXPECT_EQ(missing, 0U) << "every CPU contact's pair came from the tree";
    EXPECT_EQ(gpu_contacts_seen, gpu_contacts.size()) << "no GPU contact between shapes the CPU finds far apart";
    EXPECT_EQ(disagreeing, 0U);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuNarrowPhaseTest, MisusesAreRefused)
{
    // Meshes, height fields and compounds wait for G4; passes refuse a table not uploaded, invalid distances, caches
    // above the reserve.
    const std::array<Vector3, 3> vertices{Vector3{}, Vector3{.x = 1}, Vector3{.z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 1> triangles{{{0, 1, 2}}};
    const std::shared_ptr<const Shape> mesh = make_shape(TriangleMeshShape{{.vertices = vertices, .triangles = triangles}});
    EXPECT_THROW(m_narrow_phase->add_shape(*mesh), gfx::BackendCapabilityError);
    EXPECT_THROW(m_narrow_phase->reserve(GPU_XPBD_MAXIMUM_ELEMENTS + 1), gfx::InvalidUsageError);
    gfx::Buffer& scratch = words_buffer(64);
    run<gfx::QueueType::COMPUTE>([&](gfx::ComputeRecorder& recorder) {
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1},
                                             {.speculative_distance = -1.0F}),
                     gfx::InvalidUsageError);
        EXPECT_THROW(m_narrow_phase->bounds(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}, std::numeric_limits<float>::infinity()),
                     gfx::InvalidUsageError);
        EXPECT_THROW(m_narrow_phase->match(recorder, gpu::span_of(scratch), {.maximum = m_narrow_phase->reserved() + 1}, gpu::span_of(scratch), {.maximum = 1},
                                           gpu::span_of(scratch)),
                     gfx::InvalidUsageError);
        m_narrow_phase->add_shape(*m_shapes.front().shape);
        EXPECT_THROW(m_narrow_phase->collide(recorder, gpu::span_of(scratch), gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}),
                     gfx::InvalidUsageError)
            << "a shape added since the upload";
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
};

TEST_P(GpuNarrowPhaseMeasurementTest, ThroughputIsMeasured)
{
    // 3000 poses of each ordered pair (192 000 pairs, every algorithm), the fastest of five runs.
    const std::vector<Sample> samples = random_samples(m_shapes, 3'000, 61);
    const auto count = static_cast<std::uint32_t>(samples.size());
    std::vector<gpu::GpuPair> pairs;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        pairs.push_back({.first = 2 * index, .second = 2 * index + 1});
    }
    gfx::Buffer& instances = instances_buffer(samples);
    gfx::Buffer& pair_buffer = elements_buffer(std::span<const gpu::GpuPair>{pairs});
    gfx::Buffer& manifolds = words_buffer(40 * std::size_t{count});
    double fastest = std::numeric_limits<double>::infinity();
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const auto start = std::chrono::steady_clock::now();
        record([&](gfx::ComputeRecorder& recorder) {
            m_narrow_phase->collide(recorder, gpu::span_of(instances), gpu::span_of(pair_buffer), gpu::span_of(manifolds), {.maximum = count});
        });
        fastest = std::min(fastest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::cout << std::format("{} | {} pairs of convex shapes in {:.2f} ms ({:.1f} pairs per microsecond)\n", m_physical_device->properties().name, count, fastest,
                             count / fastest / 1000.0);
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuNarrowPhaseMeasurementTest,
                         testing::Combine(testing::ValuesIn(gfx::tests::gpu_physical_devices()), testing::Values(GpuVariantChoice::AUTOMATIC)),
                         gfx::tests::gpu_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuNarrowPhaseMeasurementTest);

} // namespace
