// GpuNarrowPhase: the shape table (records, hull vertices, faces and polygons) built on the CPU and uploaded through a
// staging buffer, the passes recorded with their push constants (the shaders' Push blocks), the cache's sorts.
module gfx.physics.gpu_xpbd;

import std;
import gfx.core.math;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.narrow_phase;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.gpu_xpbd.shaders;
import :settings;
import :resources;
import :primitives;
import :narrow_phase;

namespace gfx::physics::gpu_xpbd
{

namespace
{

static_assert(MAXIMUM_MANIFOLD_POINTS == 4, "GpuManifold holds four points: narrow_phase.glsl's MANIFOLD_POINTS");
static_assert(sizeof(GpuInstance) == 32 && sizeof(GpuContactPoint) == 32 && sizeof(GpuManifold) == 160);

/// A shape of the table (three vectors in narrow_phase.glsl: load_shape()).
struct GpuShapeRecord
{
    std::uint32_t kind = 0;          ///< ShapeKind's value.
    std::uint32_t first_vertex = 0;  ///< A hull's first vertex in the vertex array.
    std::uint32_t vertex_count = 0;  ///< Its vertices.
    std::uint32_t first_face = 0;    ///< Its first face in the face array.
    std::uint32_t face_count = 0;    ///< Its faces.
    std::array<std::uint32_t, 3> reserved{};
    std::array<float, 3> size{};     ///< Sphere: radius; capsule, cylinder, cone: half height, radius; box: half extents.
    float margin = 0.0F;             ///< The radius around the core.
};

/// A hull's face (two vectors): its plane, then its first corner in the index array and its corners.
struct GpuHullFace
{
    std::array<float, 4> plane{};    ///< Outward normal, distance.
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::array<std::uint32_t, 2> reserved{};
};
static_assert(sizeof(GpuShapeRecord) == 48 && sizeof(GpuHullFace) == 32);

// The push constants of the passes (the shaders' Push blocks, word by word).
struct ShapeTablePush
{
    std::uint32_t records = 0;
    std::uint32_t vertices = 0;
    std::uint32_t faces = 0;
    std::uint32_t indices = 0;
};

struct CollidePush
{
    std::uint32_t instances_buffer = 0;
    std::uint32_t instances_first = 0;
    std::uint32_t pairs_buffer = 0;
    std::uint32_t pairs_first = 0;
    std::uint32_t manifolds_buffer = 0;
    std::uint32_t manifolds_first = 0;
    ShapeTablePush shapes{};
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    float speculative_distance = 0.0F;
};

struct BoundsPush
{
    std::uint32_t instances_buffer = 0;
    std::uint32_t instances_first = 0;
    std::uint32_t boxes_buffer = 0;
    std::uint32_t boxes_first = 0;
    ShapeTablePush shapes{};
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    float expansion = 0.0F;
};

struct KeysPush
{
    std::uint32_t manifolds_buffer = 0;
    std::uint32_t manifolds_first = 0;
    std::uint32_t keys_buffer = 0;
    std::uint32_t values_buffer = 0;
    std::uint32_t order_buffer = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t gather = 0;
};

struct MatchPush
{
    std::uint32_t previous_buffer = 0;
    std::uint32_t previous_first = 0;
    std::uint32_t sorted_firsts_buffer = 0;
    std::uint32_t order_buffer = 0;
    std::uint32_t previous_maximum = 0;
    std::uint32_t previous_count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t previous_count_word = 0;
    std::uint32_t current_buffer = 0;
    std::uint32_t current_first = 0;
    std::uint32_t predecessors_buffer = 0;
    std::uint32_t predecessors_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};
static_assert(sizeof(CollidePush) == 14 * sizeof(std::uint32_t) && sizeof(MatchPush) == 14 * sizeof(std::uint32_t));

/// The uploads of the shape table, before the passes read it.
constexpr GlobalMemoryBarrier TRANSFER_TO_COMPUTE{.source_stages = PipelineStage::TRANSFER,
                                                  .source_access = MemoryAccess::TRANSFER_WRITE,
                                                  .destination_stages = PipelineStage::COMPUTE_SHADER,
                                                  .destination_access = MemoryAccess::SHADER_STORAGE_READ};

/// Throws the InvalidUsageError of @p operation.
[[noreturn]] void
refuse(std::string_view operation,
       std::string problem,
       std::string_view suggestion)
{
    throw InvalidUsageError({.backend = PhysicsBackendKind::GPU_XPBD,
                             .operation = std::format("GpuNarrowPhase::{}", operation),
                             .problem = std::move(problem),
                             .suggestion = std::string{suggestion}});
}

/// The record of a convex shape of a closed form.
[[nodiscard]] auto
record_of(ShapeKind kind,
          std::array<float, 3> size,
          float margin) -> GpuShapeRecord
{
    return {.kind = std::to_underlying(kind), .size = size, .margin = margin};
}

} // namespace

auto
to_contact_manifold(const GpuManifold& manifold) noexcept -> ContactManifold
{
    ContactManifold result{.normal = {.x = manifold.normal[0], .y = manifold.normal[1], .z = manifold.normal[2]},
                           .point_count = std::min(manifold.point_count, MAXIMUM_MANIFOLD_POINTS),
                           .algorithm = static_cast<ContactAlgorithm>(manifold.algorithm)};
    for (std::uint32_t point = 0; point < result.point_count; ++point)
    {
        const GpuContactPoint& source = manifold.points[point];
        result.points[point] = {.position = {.x = source.position[0], .y = source.position[1], .z = source.position[2]},
                                .separation = source.separation,
                                .feature = source.feature};
    }
    return result;
}

// ===================================================================================================== State

struct GpuNarrowPhase::State
{
    State(Device& target,
          GpuPrimitives& shared_primitives)
        : device{target}
        , primitives{shared_primitives}
        , collide{make_pipeline(target, shaders::narrow_phase_collide_comp, modules)}
        , bounds{make_pipeline(target, shaders::shape_bounds_comp, modules)}
        , keys{make_pipeline(target, shaders::contact_keys_comp, modules)}
        , match{make_pipeline(target, shaders::contact_match_comp, modules)}
        , records{buffer(16, "shape records")}
        , vertices{buffer(16, "hull vertices")}
        , faces{buffer(16, "hull faces")}
        , indices{buffer(16, "hull indices")}
    {
    }

    /// A device-local storage buffer of @p bytes (at least a vector).
    [[nodiscard]] auto buffer(std::uint64_t bytes,
                              std::string_view name) const -> std::unique_ptr<Buffer>
    {
        return device.create_buffer({.size = std::max<std::uint64_t>(bytes, 16),
                                     .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_DESTINATION | BufferUsage::TRANSFER_SOURCE,
                                     .debug_name = std::format("GPU narrow phase: {}", name)});
    }

    /// The shape table, as the passes name it.
    [[nodiscard]] auto table() const noexcept -> ShapeTablePush
    {
        return {.records = records->bindless_index().value(),
                .vertices = vertices->bindless_index().value(),
                .faces = faces->bindless_index().value(),
                .indices = indices->bindless_index().value()};
    }

    /// Throws when shapes were added since the last upload.
    void require_uploaded(std::string_view operation) const
    {
        if (pending_upload)
        {
            refuse(operation, "shapes were added since the last upload_shapes()", "record GpuNarrowPhase::upload_shapes() before the passes that read them");
        }
    }

    Device& device;
    GpuPrimitives& primitives;
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::unique_ptr<ComputePipeline> collide{};
    std::unique_ptr<ComputePipeline> bounds{};
    std::unique_ptr<ComputePipeline> keys{};
    std::unique_ptr<ComputePipeline> match{};

    // The shape table on the CPU, and on the GPU once uploaded.
    std::vector<GpuShapeRecord> shape_records{};
    std::vector<GpuVector> hull_vertices{};
    std::vector<GpuHullFace> hull_faces{};
    std::vector<std::uint32_t> hull_indices{};
    bool pending_upload = false;
    std::unique_ptr<Buffer> records{};
    std::unique_ptr<Buffer> vertices{};
    std::unique_ptr<Buffer> faces{};
    std::unique_ptr<Buffer> indices{};
    std::unique_ptr<Buffer> staging{}; ///< The last upload's (its copies read it until they ran: the device's deleter).

    // The cache's scratch, for up to `maximum` manifolds.
    std::uint32_t maximum = 0;
    std::unique_ptr<Buffer> cache_keys{};
    std::unique_ptr<Buffer> cache_values{};
    std::unique_ptr<Buffer> cache_sorted_keys{};
    std::unique_ptr<Buffer> cache_order{};
};

// ===================================================================================================== GpuNarrowPhase

GpuNarrowPhase::GpuNarrowPhase(Device& device,
                               GpuPrimitives& primitives)
    : m_state{std::make_unique<State>(device, primitives)}
{
    reserve(GPU_XPBD_WORKGROUP_SIZE);
}

GpuNarrowPhase::~GpuNarrowPhase() = default;

auto
GpuNarrowPhase::add_shape(const Shape& shape) -> std::uint32_t
{
    // The convex kinds as records; a hull's arrays appended to the table's.
    State& state = *m_state;
    GpuShapeRecord record{};
    std::visit(
        [&](const auto& geometry) {
            using Geometry = std::remove_cvref_t<decltype(geometry)>;
            if constexpr (std::same_as<Geometry, SphereShape>)
            {
                record = record_of(ShapeKind::SPHERE, {geometry.radius, 0.0F, 0.0F}, geometry.radius);
            }
            else if constexpr (std::same_as<Geometry, CapsuleShape>)
            {
                record = record_of(ShapeKind::CAPSULE, {geometry.half_height, geometry.radius, 0.0F}, geometry.radius);
            }
            else if constexpr (std::same_as<Geometry, CylinderShape>)
            {
                record = record_of(ShapeKind::CYLINDER, {geometry.half_height, geometry.radius, 0.0F}, 0.0F);
            }
            else if constexpr (std::same_as<Geometry, ConeShape>)
            {
                record = record_of(ShapeKind::CONE, {geometry.half_height, geometry.radius, 0.0F}, 0.0F);
            }
            else if constexpr (std::same_as<Geometry, BoxShape>)
            {
                record = record_of(ShapeKind::BOX, {geometry.half_extents.x, geometry.half_extents.y, geometry.half_extents.z}, geometry.radius);
            }
            else if constexpr (std::same_as<Geometry, HullShape>)
            {
                const HullView hull = geometry.view();
                record = record_of(ShapeKind::CONVEX_HULL, {}, hull.radius);
                record.first_vertex = static_cast<std::uint32_t>(state.hull_vertices.size());
                record.vertex_count = static_cast<std::uint32_t>(hull.vertices.size());
                record.first_face = static_cast<std::uint32_t>(state.hull_faces.size());
                record.face_count = static_cast<std::uint32_t>(hull.faces.size());
                const auto first_index = static_cast<std::uint32_t>(state.hull_indices.size());
                for (const Vector3 vertex : hull.vertices)
                {
                    state.hull_vertices.push_back({vertex.x, vertex.y, vertex.z, 0.0F});
                }
                for (std::size_t face = 0; face < hull.faces.size(); ++face)
                {
                    const Plane& plane = hull.planes[face];
                    state.hull_faces.push_back({.plane = {plane.normal.x, plane.normal.y, plane.normal.z, plane.distance},
                                                .first = first_index + hull.faces[face].first,
                                                .count = hull.faces[face].count});
                }
                state.hull_indices.insert(state.hull_indices.end(), hull.face_vertices.begin(), hull.face_vertices.end());
            }
            else
            {
                throw_unsupported(PhysicsBackendKind::GPU_XPBD, "GpuNarrowPhase::add_shape", PhysicsCapability::WORLD_OBJECTS,
                                  std::format("the GPU narrow phase collides convex shapes only so far (a {}: triangle meshes, height fields and "
                                              "compounds arrive with phase G4 of docs/gpu-xpbd-design.md)",
                                              shape.kind()),
                                  "use convex shapes (sphere, capsule, cylinder, cone, box, hull), or another backend");
            }
        },
        shape.geometry());
    state.shape_records.push_back(record);
    state.pending_upload = true;
    return static_cast<std::uint32_t>(state.shape_records.size() - 1);
}

auto
GpuNarrowPhase::shape_count() const noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(m_state->shape_records.size());
}

void
GpuNarrowPhase::upload_shapes(ComputeRecorder& recorder)
{
    // The whole table again (shapes are few and small): new buffers, one staging buffer, four copies, a barrier.
    State& state = *m_state;
    if (!state.pending_upload)
    {
        return;
    }
    const std::array<std::span<const std::byte>, 4> parts{std::as_bytes(std::span{state.shape_records}), std::as_bytes(std::span{state.hull_vertices}),
                                                           std::as_bytes(std::span{state.hull_faces}), std::as_bytes(std::span{state.hull_indices})};
    std::array<std::uint64_t, 4> offsets{};
    std::uint64_t total = 0;
    for (std::size_t part = 0; part < parts.size(); ++part)
    {
        offsets[part] = total;
        total += (parts[part].size() + 15) / 16 * 16;
    }
    state.staging = state.device.create_buffer({.size = std::max<std::uint64_t>(total, 16),
                                                .usage = BufferUsage::TRANSFER_SOURCE,
                                                .location = MemoryLocation::HOST_UPLOAD,
                                                .debug_name = "GPU narrow phase: shape staging"});
    const std::span<std::byte> mapped = state.staging->mapped_memory();
    for (std::size_t part = 0; part < parts.size(); ++part)
    {
        std::ranges::copy(parts[part], mapped.begin() + static_cast<std::ptrdiff_t>(offsets[part]));
    }
    state.staging->flush_mapped_range();

    std::array<std::unique_ptr<Buffer>*, 4> targets{&state.records, &state.vertices, &state.faces, &state.indices};
    constexpr std::array<std::string_view, 4> NAMES{"shape records", "hull vertices", "hull faces", "hull indices"};
    for (std::size_t part = 0; part < parts.size(); ++part)
    {
        *targets[part] = state.buffer(parts[part].size(), NAMES[part]);
        if (!parts[part].empty())
        {
            const std::array regions{BufferCopyRegion{.source_offset = offsets[part], .size = parts[part].size()}};
            recorder.copy_buffer(state.staging->handle(), (*targets[part])->handle(), regions);
        }
    }
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_COMPUTE, 1}});
    state.pending_upload = false;
}

void
GpuNarrowPhase::reserve(std::uint32_t maximum)
{
    State& state = *m_state;
    if (maximum <= state.maximum)
    {
        return;
    }
    if (maximum > GPU_XPBD_MAXIMUM_ELEMENTS)
    {
        refuse("reserve", std::format("{} manifolds is above GPU_XPBD_MAXIMUM_ELEMENTS ({})", maximum, GPU_XPBD_MAXIMUM_ELEMENTS),
               "split the pairs between several caches");
    }
    state.primitives.reserve(maximum);
    const std::uint64_t bytes = std::uint64_t{maximum} * sizeof(std::uint32_t);
    state.cache_keys = state.buffer(bytes, "cache keys");
    state.cache_values = state.buffer(bytes, "cache values");
    state.cache_sorted_keys = state.buffer(bytes, "cache sorted keys");
    state.cache_order = state.buffer(bytes, "cache order");
    state.maximum = maximum;
}

auto
GpuNarrowPhase::reserved() const noexcept -> std::uint32_t
{
    return m_state->maximum;
}

void
GpuNarrowPhase::bounds(ComputeRecorder& recorder,
                       GpuSpan instances,
                       GpuSpan boxes,
                       const GpuCount& count,
                       float expansion)
{
    const State& state = *m_state;
    state.require_uploaded("bounds");
    if (!std::isfinite(expansion) || expansion < 0.0F)
    {
        refuse("bounds", std::format("the expansion is {}", expansion), "grow the boxes by a finite distance of at least 0");
    }
    record_pass(recorder, *state.bounds,
                BoundsPush{.instances_buffer = instances.buffer,
                           .instances_first = instances.first,
                           .boxes_buffer = boxes.buffer,
                           .boxes_first = boxes.first,
                           .shapes = state.table(),
                           .maximum = count.maximum,
                           .count_buffer = count.word.buffer,
                           .count_word = count.word.first,
                           .expansion = expansion},
                workgroups(std::max(count.maximum, 1U)));
}

void
GpuNarrowPhase::collide(ComputeRecorder& recorder,
                        GpuSpan instances,
                        GpuSpan pairs,
                        GpuSpan manifolds,
                        const GpuCount& count,
                        const CollisionSettings& settings)
{
    const State& state = *m_state;
    state.require_uploaded("collide");
    if (!std::isfinite(settings.speculative_distance) || settings.speculative_distance < 0.0F)
    {
        refuse("collide", std::format("the speculative distance is {}", settings.speculative_distance), "give a finite speculative distance of at least 0");
    }
    record_pass(recorder, *state.collide,
                CollidePush{.instances_buffer = instances.buffer,
                            .instances_first = instances.first,
                            .pairs_buffer = pairs.buffer,
                            .pairs_first = pairs.first,
                            .manifolds_buffer = manifolds.buffer,
                            .manifolds_first = manifolds.first,
                            .shapes = state.table(),
                            .maximum = count.maximum,
                            .count_buffer = count.word.buffer,
                            .count_word = count.word.first,
                            .speculative_distance = settings.speculative_distance},
                workgroups(std::max(count.maximum, 1U)));
}

void
GpuNarrowPhase::match(ComputeRecorder& recorder,
                      GpuSpan previous,
                      const GpuCount& previous_count,
                      GpuSpan current,
                      const GpuCount& count,
                      GpuSpan predecessors)
{
    // The previous manifolds sorted by pair: by second instance, then (stable) by first instance gathered in that order.
    const State& state = *m_state;
    if (previous_count.maximum > state.maximum)
    {
        refuse("match", std::format("the previous count's maximum is {}, above the {} reserved", previous_count.maximum, state.maximum),
               "call GpuNarrowPhase::reserve() with the largest count before recording");
    }
    const std::uint32_t keys = state.cache_keys->bindless_index().value();
    const std::uint32_t values = state.cache_values->bindless_index().value();
    const std::uint32_t sorted_keys = state.cache_sorted_keys->bindless_index().value();
    const std::uint32_t order = state.cache_order->bindless_index().value();
    const std::uint32_t previous_workgroups = workgroups(std::max(previous_count.maximum, 1U));
    record_pass(recorder, *state.keys,
                KeysPush{.manifolds_buffer = previous.buffer,
                         .manifolds_first = previous.first,
                         .keys_buffer = keys,
                         .values_buffer = values,
                         .order_buffer = order,
                         .maximum = previous_count.maximum,
                         .count_buffer = previous_count.word.buffer,
                         .count_word = previous_count.word.first,
                         .gather = 0},
                previous_workgroups);
    state.primitives.sort(recorder, {.buffer = keys}, {.buffer = values}, {.buffer = sorted_keys}, {.buffer = order}, previous_count);
    record_pass(recorder, *state.keys,
                KeysPush{.manifolds_buffer = previous.buffer,
                         .manifolds_first = previous.first,
                         .keys_buffer = keys,
                         .values_buffer = values,
                         .order_buffer = order,
                         .maximum = previous_count.maximum,
                         .count_buffer = previous_count.word.buffer,
                         .count_word = previous_count.word.first,
                         .gather = 1},
                previous_workgroups);
    state.primitives.sort(recorder, {.buffer = keys}, {.buffer = order}, {.buffer = sorted_keys}, {.buffer = values}, previous_count);

    // Each current point's predecessor.
    record_pass(recorder, *state.match,
                MatchPush{.previous_buffer = previous.buffer,
                          .previous_first = previous.first,
                          .sorted_firsts_buffer = sorted_keys,
                          .order_buffer = values,
                          .previous_maximum = previous_count.maximum,
                          .previous_count_buffer = previous_count.word.buffer,
                          .previous_count_word = previous_count.word.first,
                          .current_buffer = current.buffer,
                          .current_first = current.first,
                          .predecessors_buffer = predecessors.buffer,
                          .predecessors_first = predecessors.first,
                          .maximum = count.maximum,
                          .count_buffer = count.word.buffer,
                          .count_word = count.word.first},
                workgroups(std::max(count.maximum, 1U)));
}

} // namespace gfx::physics::gpu_xpbd
