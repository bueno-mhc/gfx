// GpuNarrowPhase: the shape table (records, vertices, hull faces and corners, mesh triangles, the CPU's static
// hierarchies, height field samples and active edges, compound children) built on the CPU and uploaded through a staging
// buffer, its arrays' bindless indices in a table buffer the shaders read; collide()'s passes (mid phase counted and
// written, sub-pairs, merge) and the cache's sorts recorded with their push constants (the shaders' Push blocks).
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
static_assert(sizeof(GpuInstance) == 32 && sizeof(GpuContactPoint) == 32 && sizeof(GpuManifold) == 176, "narrow_phase.glsl: MANIFOLD_VECTORS 11");

/// A shape of the table (six vectors: narrow_phase.glsl's ShapeRecord).
struct GpuShapeRecord
{
    std::uint32_t kind = 0;         ///< ShapeKind's value.
    std::uint32_t flags = 0;        ///< Bit 0: a two-sided mesh.
    std::uint32_t depth = 0;        ///< A compound's nesting (1: of other shapes).
    std::uint32_t reserved = 0;
    std::uint32_t first = 0;        ///< Hull, mesh: first vertex; height field: first sample; compound: first child.
    std::uint32_t count = 0;        ///< Vertices; rows; children.
    std::uint32_t second_first = 0; ///< Hull: first face; mesh: first triangle; height field: first active-edge word.
    std::uint32_t second_count = 0; ///< Faces; triangles; columns.
    std::uint32_t first_node = 0;   ///< A mesh's or compound's static hierarchy: its first node,
    std::uint32_t first_item = 0;   ///< first item,
    std::uint32_t first_box = 0;    ///< and first item box.
    std::uint32_t reserved_hierarchy = 0;
    std::array<float, 3> size{};    ///< Sphere: radius; capsule, cylinder, cone: half height, radius; box: half extents; height field: scale.
    float margin = 0.0F;            ///< The radius around the core.
    std::array<float, 4> bounds_minimum{}; ///< Its box in its own frame.
    std::array<float, 4> bounds_maximum{};
};

/// A hull's face: its plane, then its first corner in the corner array and its corners.
struct GpuHullFace
{
    std::array<float, 4> plane{};
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::array<std::uint32_t, 2> reserved{};
};

/// A node of a static hierarchy (the CPU's StaticBvhNode): its box, its first child or item (absolute), its items.
struct GpuStaticNode
{
    std::array<float, 3> minimum{};
    std::uint32_t first = 0;
    std::array<float, 3> maximum{};
    std::uint32_t count = 0;
};

/// A compound's child: its pose in the compound's frame, its shape.
struct GpuChild
{
    std::array<float, 3> position{};
    std::uint32_t shape = 0;
    std::array<float, 4> orientation{};
};
static_assert(sizeof(GpuShapeRecord) == 96 && sizeof(GpuHullFace) == 32 && sizeof(GpuStaticNode) == 32 && sizeof(GpuChild) == 32);

/// A sub-pair (narrow_phase.glsl's SUB_PAIR_VECTORS).
constexpr std::uint64_t SUB_PAIR_BYTES = 7 * sizeof(GpuVector);

// The push constants of the passes (the shaders' Push blocks, word by word).
struct PairsPush
{
    std::uint32_t tables = 0;
    std::uint32_t instances_buffer = 0;
    std::uint32_t instances_first = 0;
    std::uint32_t pairs_buffer = 0;
    std::uint32_t pairs_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t counts_buffer = 0;
    std::uint32_t offsets_buffer = 0;
    std::uint32_t sub_pairs_buffer = 0;
    std::uint32_t capacity = 0;
    std::uint32_t writing = 0;
    float speculative_distance = 0.0F;
};

struct CollidePush
{
    std::uint32_t tables = 0;
    std::uint32_t sub_pairs_buffer = 0;
    std::uint32_t manifolds_buffer = 0;
    std::uint32_t manifolds_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    float speculative_distance = 0.0F;
};

struct MergePush
{
    std::uint32_t manifolds_buffer = 0;
    std::uint32_t manifolds_first = 0;
    std::uint32_t counts_buffer = 0;
    std::uint32_t offsets_buffer = 0;
    std::uint32_t ranges_buffer = 0;
    std::uint32_t ranges_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t capacity = 0;
    float merge_cosine = 0.0F;
    std::uint32_t maximum_manifolds = 0;
};

struct BoundsPush
{
    std::uint32_t tables = 0;
    std::uint32_t instances_buffer = 0;
    std::uint32_t instances_first = 0;
    std::uint32_t boxes_buffer = 0;
    std::uint32_t boxes_first = 0;
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
static_assert(sizeof(PairsPush) == 14 * sizeof(std::uint32_t) && sizeof(MergePush) == 12 * sizeof(std::uint32_t));

/// The uploads of the shape table, before the passes read it.
constexpr GlobalMemoryBarrier TRANSFER_TO_COMPUTE{.source_stages = PipelineStage::TRANSFER,
                                                  .source_access = MemoryAccess::TRANSFER_WRITE,
                                                  .destination_stages = PipelineStage::COMPUTE_SHADER,
                                                  .destination_access = MemoryAccess::SHADER_STORAGE_READ};

/// The table's arrays, in the order of the table buffer's words (narrow_phase.glsl's load_tables()).
enum TableArray : std::size_t
{
    RECORDS,
    VERTICES,
    HULL_FACES,
    HULL_INDICES,
    TRIANGLES,
    NODES,
    ITEMS,
    BOXES,
    HEIGHTS,
    EDGES,
    CHILDREN,
    TABLE_ARRAYS,
};

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

[[nodiscard]] auto
vector_of(Vector3 vector) noexcept -> std::array<float, 4>
{
    return {vector.x, vector.y, vector.z, 0.0F};
}

/// How deep @p shape's compounds go (0: not a compound).
[[nodiscard]] auto
compound_depth(const Shape& shape) -> std::uint32_t
{
    const auto* compound = shape.get_if<CompoundShape>();
    if (compound == nullptr)
    {
        return 0;
    }
    std::uint32_t deepest = 0;
    for (const CompoundChild& child : compound->children())
    {
        deepest = std::max(deepest, compound_depth(*child.shape));
    }
    return deepest + 1;
}

} // namespace

auto
to_contact_manifold(const GpuManifold& manifold) noexcept -> ContactManifold
{
    ContactManifold result{.normal = {.x = manifold.normal[0], .y = manifold.normal[1], .z = manifold.normal[2]},
                           .point_count = std::min(manifold.point_count, MAXIMUM_MANIFOLD_POINTS),
                           .sub_shape_first = manifold.sub_shape_first,
                           .sub_shape_second = manifold.sub_shape_second,
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
        , pairs{make_pipeline(target, shaders::narrow_phase_pairs_comp, modules)}
        , collide{make_pipeline(target, shaders::narrow_phase_collide_comp, modules)}
        , merge{make_pipeline(target, shaders::narrow_phase_merge_comp, modules)}
        , bounds{make_pipeline(target, shaders::shape_bounds_comp, modules)}
        , keys{make_pipeline(target, shaders::contact_keys_comp, modules)}
        , match{make_pipeline(target, shaders::contact_match_comp, modules)}
        , tables{buffer(3 * sizeof(GpuVector), "tables")}
    {
        for (std::size_t array = 0; array < TABLE_ARRAYS; ++array)
        {
            arrays[array] = buffer(16, "shape table");
        }
    }

    /// A device-local storage buffer of @p bytes (at least a vector).
    [[nodiscard]] auto buffer(std::uint64_t bytes,
                              std::string_view name) const -> std::unique_ptr<Buffer>
    {
        return device.create_buffer({.size = std::max<std::uint64_t>(bytes, 16),
                                     .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_DESTINATION | BufferUsage::TRANSFER_SOURCE,
                                     .debug_name = std::format("GPU narrow phase: {}", name)});
    }

    [[nodiscard]] auto table() const noexcept -> std::uint32_t
    {
        return tables->bindless_index().value();
    }

    /// Throws when shapes were added since the last upload.
    void require_uploaded(std::string_view operation) const
    {
        if (pending_upload)
        {
            refuse(operation, "shapes were added since the last upload_shapes()", "record GpuNarrowPhase::upload_shapes() before the passes that read them");
        }
    }

    /// Appends @p hierarchy (nodes with absolute children and items, items, item boxes) for @p record.
    void append_hierarchy(const StaticBvh& hierarchy,
                          GpuShapeRecord& record)
    {
        record.first_node = static_cast<std::uint32_t>(nodes.size());
        record.first_item = static_cast<std::uint32_t>(items.size());
        record.first_box = static_cast<std::uint32_t>(boxes.size() / 2);
        for (const StaticBvhNode& node : hierarchy.nodes())
        {
            nodes.push_back({.minimum = {node.bounds.minimum.x, node.bounds.minimum.y, node.bounds.minimum.z},
                             .first = (node.is_leaf() ? record.first_item : record.first_node) + node.first,
                             .maximum = {node.bounds.maximum.x, node.bounds.maximum.y, node.bounds.maximum.z},
                             .count = node.count});
        }
        items.insert(items.end(), hierarchy.items().begin(), hierarchy.items().end());
        for (const Aabb& box : hierarchy.boxes())
        {
            boxes.push_back(vector_of(box.minimum));
            boxes.push_back(vector_of(box.maximum));
        }
    }

    /// Adds @p shape (its children first; shapes met before in this addition stored once).
    auto add(const Shape& shape,
             std::unordered_map<const Shape*, std::uint32_t>& added) -> std::uint32_t
    {
        if (const auto known = added.find(&shape); known != added.end())
        {
            return known->second;
        }
        const Aabb local = shape.bounds();
        GpuShapeRecord record{.kind = std::to_underlying(shape.kind()), .bounds_minimum = vector_of(local.minimum), .bounds_maximum = vector_of(local.maximum)};
        std::visit(
            [&](const auto& geometry) {
                using Geometry = std::remove_cvref_t<decltype(geometry)>;
                if constexpr (std::same_as<Geometry, SphereShape>)
                {
                    record.size = {geometry.radius, 0.0F, 0.0F};
                    record.margin = geometry.radius;
                }
                else if constexpr (std::same_as<Geometry, CapsuleShape>)
                {
                    record.size = {geometry.half_height, geometry.radius, 0.0F};
                    record.margin = geometry.radius;
                }
                else if constexpr (std::same_as<Geometry, CylinderShape> || std::same_as<Geometry, ConeShape>)
                {
                    record.size = {geometry.half_height, geometry.radius, 0.0F};
                }
                else if constexpr (std::same_as<Geometry, BoxShape>)
                {
                    record.size = {geometry.half_extents.x, geometry.half_extents.y, geometry.half_extents.z};
                    record.margin = geometry.radius;
                }
                else if constexpr (std::same_as<Geometry, HullShape>)
                {
                    // Vertices, faces with their planes and first corner, corners (vertices of the hull).
                    const HullView hull = geometry.view();
                    record.margin = hull.radius;
                    record.first = static_cast<std::uint32_t>(vertices.size());
                    record.count = static_cast<std::uint32_t>(hull.vertices.size());
                    record.second_first = static_cast<std::uint32_t>(hull_faces.size());
                    record.second_count = static_cast<std::uint32_t>(hull.faces.size());
                    const auto first_index = static_cast<std::uint32_t>(hull_indices.size());
                    for (const Vector3 vertex : hull.vertices)
                    {
                        vertices.push_back(vector_of(vertex));
                    }
                    for (std::size_t face = 0; face < hull.faces.size(); ++face)
                    {
                        const Plane& plane = hull.planes[face];
                        hull_faces.push_back({.plane = {plane.normal.x, plane.normal.y, plane.normal.z, plane.distance},
                                              .first = first_index + hull.faces[face].first,
                                              .count = hull.faces[face].count});
                    }
                    hull_indices.insert(hull_indices.end(), hull.face_vertices.begin(), hull.face_vertices.end());
                }
                else if constexpr (std::same_as<Geometry, TriangleMeshShape>)
                {
                    // Vertices, triangles (absolute vertices, active edges), the static hierarchy.
                    record.flags = geometry.two_sided() ? 1U : 0U;
                    record.first = static_cast<std::uint32_t>(vertices.size());
                    record.count = static_cast<std::uint32_t>(geometry.vertices().size());
                    record.second_first = static_cast<std::uint32_t>(triangles.size());
                    record.second_count = geometry.triangle_count();
                    for (const Vector3 vertex : geometry.vertices())
                    {
                        vertices.push_back(vector_of(vertex));
                    }
                    for (std::uint32_t index = 0; index < geometry.triangle_count(); ++index)
                    {
                        const std::array<std::uint32_t, 3>& corners = geometry.triangles()[index];
                        triangles.push_back({record.first + corners[0], record.first + corners[1], record.first + corners[2], geometry.active_edges(index)});
                    }
                    append_hierarchy(geometry.bvh(), record);
                }
                else if constexpr (std::same_as<Geometry, HeightFieldShape>)
                {
                    // Samples as given (row by row), each triangle's active edges, the scale.
                    record.first = static_cast<std::uint32_t>(heights.size());
                    record.count = geometry.rows();
                    record.second_first = static_cast<std::uint32_t>(edges.size());
                    record.second_count = geometry.columns();
                    record.size = {geometry.scale().x, geometry.scale().y, geometry.scale().z};
                    const auto samples = geometry.heights();
                    for (std::size_t row = 0; row < samples.extent(0); ++row)
                    {
                        for (std::size_t column = 0; column < samples.extent(1); ++column)
                        {
                            heights.push_back(samples[row, column]);
                        }
                    }
                    for (std::uint32_t index = 0; index < geometry.triangle_count(); ++index)
                    {
                        edges.push_back(geometry.active_edges(index));
                    }
                }
                else
                {
                    // A compound: its children's shapes, then its children (their shapes and poses), its hierarchy.
                    std::vector<std::uint32_t> child_shapes;
                    std::uint32_t deepest = 0;
                    for (const CompoundChild& child : geometry.children())
                    {
                        child_shapes.push_back(add(*child.shape, added));
                        deepest = std::max(deepest, shape_records[child_shapes.back()].depth);
                    }
                    record.depth = deepest + 1;
                    record.first = static_cast<std::uint32_t>(children.size());
                    record.count = static_cast<std::uint32_t>(geometry.children().size());
                    for (std::size_t index = 0; index < geometry.children().size(); ++index)
                    {
                        const Transform& pose = geometry.children()[index].pose;
                        children.push_back({.position = {pose.position.x, pose.position.y, pose.position.z},
                                            .shape = child_shapes[index],
                                            .orientation = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}});
                    }
                    append_hierarchy(geometry.bvh(), record);
                }
            },
            shape.geometry());
        shape_records.push_back(record);
        const auto index = static_cast<std::uint32_t>(shape_records.size() - 1);
        added.emplace(&shape, index);
        return index;
    }

    Device& device;
    GpuPrimitives& primitives;
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::unique_ptr<ComputePipeline> pairs{};
    std::unique_ptr<ComputePipeline> collide{};
    std::unique_ptr<ComputePipeline> merge{};
    std::unique_ptr<ComputePipeline> bounds{};
    std::unique_ptr<ComputePipeline> keys{};
    std::unique_ptr<ComputePipeline> match{};

    // The shape table on the CPU, and on the GPU once uploaded (its arrays, their indices in the table buffer).
    std::vector<GpuShapeRecord> shape_records{};
    std::vector<GpuVector> vertices{};
    std::vector<GpuHullFace> hull_faces{};
    std::vector<std::uint32_t> hull_indices{};
    std::vector<std::array<std::uint32_t, 4>> triangles{};
    std::vector<GpuStaticNode> nodes{};
    std::vector<std::uint32_t> items{};
    std::vector<GpuVector> boxes{};
    std::vector<float> heights{};
    std::vector<std::uint32_t> edges{};
    std::vector<GpuChild> children{};
    bool pending_upload = false;
    std::array<std::unique_ptr<Buffer>, TABLE_ARRAYS> arrays{};
    std::unique_ptr<Buffer> tables{};
    std::unique_ptr<Buffer> staging{}; ///< The last upload's (its copies read it until they ran: the device's deleter).

    // Scratch: per pair (counts, offsets), per manifold slot (sub-pairs, the cache's sort).
    std::uint32_t maximum_pairs = 0;
    std::uint32_t maximum_manifolds = 0;
    std::unique_ptr<Buffer> pair_counts{};
    std::unique_ptr<Buffer> pair_offsets{};
    std::unique_ptr<Buffer> sub_pairs{};
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
    reserve(GPU_XPBD_WORKGROUP_SIZE, GPU_XPBD_WORKGROUP_SIZE);
}

GpuNarrowPhase::~GpuNarrowPhase() = default;

auto
GpuNarrowPhase::add_shape(const Shape& shape) -> std::uint32_t
{
    check_shape(shape);
    std::unordered_map<const Shape*, std::uint32_t> added;
    const std::uint32_t index = m_state->add(shape, added);
    m_state->pending_upload = true;
    return index;
}

void
GpuNarrowPhase::check_shape(const Shape& shape)
{
    if (const std::uint32_t depth = compound_depth(shape); depth > GPU_XPBD_COMPOUND_DEPTH)
    {
        throw_unsupported(PhysicsBackendKind::GPU_XPBD, "GpuNarrowPhase::add_shape", PhysicsCapability::WORLD_OBJECTS,
                          std::format("the GPU narrow phase descends compounds {} deep at most (GPU_XPBD_COMPOUND_DEPTH); this one is {} deep",
                                      GPU_XPBD_COMPOUND_DEPTH, depth),
                          "flatten the compound (children holding the shapes of its inner compounds at their composed poses), or use another backend");
    }
}

auto
GpuNarrowPhase::shape_count() const noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(m_state->shape_records.size());
}

void
GpuNarrowPhase::upload_shapes(ComputeRecorder& recorder)
{
    // The whole table again (shapes are few): new arrays, the table buffer naming them, one staging buffer, the copies.
    State& state = *m_state;
    if (!state.pending_upload)
    {
        return;
    }
    const std::array<std::span<const std::byte>, TABLE_ARRAYS> parts{
        std::as_bytes(std::span{state.shape_records}), std::as_bytes(std::span{state.vertices}), std::as_bytes(std::span{state.hull_faces}),
        std::as_bytes(std::span{state.hull_indices}),  std::as_bytes(std::span{state.triangles}), std::as_bytes(std::span{state.nodes}),
        std::as_bytes(std::span{state.items}),         std::as_bytes(std::span{state.boxes}),     std::as_bytes(std::span{state.heights}),
        std::as_bytes(std::span{state.edges}),         std::as_bytes(std::span{state.children})};
    std::array<std::uint32_t, 12> indices{};
    for (std::size_t array = 0; array < TABLE_ARRAYS; ++array)
    {
        state.arrays[array] = state.buffer(parts[array].size(), "shape table");
        indices[array] = state.arrays[array]->bindless_index().value();
    }
    std::array<std::uint64_t, TABLE_ARRAYS + 1> offsets{};
    std::uint64_t total = 0;
    for (std::size_t array = 0; array <= TABLE_ARRAYS; ++array)
    {
        offsets[array] = total;
        total += ((array < TABLE_ARRAYS ? parts[array].size() : sizeof(indices)) + 15) / 16 * 16;
    }
    state.staging = state.device.create_buffer({.size = total,
                                                .usage = BufferUsage::TRANSFER_SOURCE,
                                                .location = MemoryLocation::HOST_UPLOAD,
                                                .debug_name = "GPU narrow phase: shape staging"});
    const std::span<std::byte> mapped = state.staging->mapped_memory();
    for (std::size_t array = 0; array < TABLE_ARRAYS; ++array)
    {
        std::ranges::copy(parts[array], mapped.begin() + static_cast<std::ptrdiff_t>(offsets[array]));
    }
    std::ranges::copy(std::as_bytes(std::span{indices}), mapped.begin() + static_cast<std::ptrdiff_t>(offsets[TABLE_ARRAYS]));
    state.staging->flush_mapped_range();
    for (std::size_t array = 0; array < TABLE_ARRAYS; ++array)
    {
        if (!parts[array].empty())
        {
            const std::array regions{BufferCopyRegion{.source_offset = offsets[array], .size = parts[array].size()}};
            recorder.copy_buffer(state.staging->handle(), state.arrays[array]->handle(), regions);
        }
    }
    const std::array regions{BufferCopyRegion{.source_offset = offsets[TABLE_ARRAYS], .size = sizeof(indices)}};
    recorder.copy_buffer(state.staging->handle(), state.tables->handle(), regions);
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_COMPUTE, 1}});
    state.pending_upload = false;
}

void
GpuNarrowPhase::reserve(std::uint32_t maximum_pairs,
                        std::uint32_t maximum_manifolds)
{
    State& state = *m_state;
    for (const std::uint32_t maximum : {maximum_pairs, maximum_manifolds})
    {
        if (maximum > GPU_XPBD_MAXIMUM_ELEMENTS)
        {
            refuse("reserve", std::format("{} is above GPU_XPBD_MAXIMUM_ELEMENTS ({})", maximum, GPU_XPBD_MAXIMUM_ELEMENTS),
                   "split the pairs between several narrow phases");
        }
    }
    state.primitives.reserve(std::max(maximum_pairs, maximum_manifolds));
    constexpr std::uint64_t WORD = sizeof(std::uint32_t);
    if (maximum_pairs > state.maximum_pairs)
    {
        state.pair_counts = state.buffer(maximum_pairs * WORD, "pair counts");
        state.pair_offsets = state.buffer(maximum_pairs * WORD, "pair offsets");
        state.maximum_pairs = maximum_pairs;
    }
    if (maximum_manifolds > state.maximum_manifolds)
    {
        state.sub_pairs = state.buffer(maximum_manifolds * SUB_PAIR_BYTES, "sub-pairs");
        state.cache_keys = state.buffer(maximum_manifolds * WORD, "cache keys");
        state.cache_values = state.buffer(maximum_manifolds * WORD, "cache values");
        state.cache_sorted_keys = state.buffer(maximum_manifolds * WORD, "cache sorted keys");
        state.cache_order = state.buffer(maximum_manifolds * WORD, "cache order");
        state.maximum_manifolds = maximum_manifolds;
    }
}

auto
GpuNarrowPhase::reserved_pairs() const noexcept -> std::uint32_t
{
    return m_state->maximum_pairs;
}

auto
GpuNarrowPhase::reserved_manifolds() const noexcept -> std::uint32_t
{
    return m_state->maximum_manifolds;
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
                BoundsPush{.tables = state.table(),
                           .instances_buffer = instances.buffer,
                           .instances_first = instances.first,
                           .boxes_buffer = boxes.buffer,
                           .boxes_first = boxes.first,
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
                        const GpuCount& count,
                        const GpuManifoldSpans& output,
                        const CollisionSettings& settings)
{
    // Valid input, as collide() checks it; room for it.
    const State& state = *m_state;
    state.require_uploaded("collide");
    if (!std::isfinite(settings.speculative_distance) || !(settings.speculative_distance >= 0.0F) || settings.maximum_manifolds == 0 ||
        !(settings.merge_cosine >= -1.0F && settings.merge_cosine <= 1.0F))
    {
        refuse("collide",
               std::format("invalid settings: speculative distance {}, merge cosine {}, maximum manifolds {}", settings.speculative_distance,
                           settings.merge_cosine, settings.maximum_manifolds),
               "give a finite speculative distance of at least 0, a cosine in [-1, 1] and at least one manifold");
    }
    if (count.maximum > state.maximum_pairs || output.capacity > state.maximum_manifolds)
    {
        refuse("collide",
               std::format("{} pairs into {} slots, above the {} pairs and {} slots reserved", count.maximum, output.capacity, state.maximum_pairs,
                           state.maximum_manifolds),
               "call GpuNarrowPhase::reserve() with the largest counts before recording");
    }

    // The mid phase counted, the counts scanned into offsets (their sum: the slots needed), the mid phase written.
    const std::uint32_t pair_workgroups = workgroups(std::max(count.maximum, 1U));
    PairsPush mid_phase{.tables = state.table(),
                        .instances_buffer = instances.buffer,
                        .instances_first = instances.first,
                        .pairs_buffer = pairs.buffer,
                        .pairs_first = pairs.first,
                        .maximum = count.maximum,
                        .count_buffer = count.word.buffer,
                        .count_word = count.word.first,
                        .counts_buffer = state.pair_counts->bindless_index().value(),
                        .offsets_buffer = state.pair_offsets->bindless_index().value(),
                        .sub_pairs_buffer = state.sub_pairs->bindless_index().value(),
                        .capacity = output.capacity,
                        .writing = 0,
                        .speculative_distance = settings.speculative_distance};
    record_pass(recorder, *state.pairs, mid_phase, pair_workgroups);
    state.primitives.exclusive_scan(recorder, span_of(*state.pair_counts), span_of(*state.pair_offsets), count, output.total);
    mid_phase.writing = 1;
    record_pass(recorder, *state.pairs, mid_phase, pair_workgroups);

    // Each sub-pair's manifold at its slot, then each pair's merged.
    record_pass(recorder, *state.collide,
                CollidePush{.tables = state.table(),
                            .sub_pairs_buffer = state.sub_pairs->bindless_index().value(),
                            .manifolds_buffer = output.manifolds.buffer,
                            .manifolds_first = output.manifolds.first,
                            .maximum = output.capacity,
                            .count_buffer = output.total.buffer,
                            .count_word = output.total.first,
                            .speculative_distance = settings.speculative_distance},
                workgroups(std::max(output.capacity, 1U)));
    record_pass(recorder, *state.merge,
                MergePush{.manifolds_buffer = output.manifolds.buffer,
                          .manifolds_first = output.manifolds.first,
                          .counts_buffer = state.pair_counts->bindless_index().value(),
                          .offsets_buffer = state.pair_offsets->bindless_index().value(),
                          .ranges_buffer = output.ranges.buffer,
                          .ranges_first = output.ranges.first,
                          .maximum = count.maximum,
                          .count_buffer = count.word.buffer,
                          .count_word = count.word.first,
                          .capacity = output.capacity,
                          .merge_cosine = settings.merge_cosine,
                          .maximum_manifolds = settings.maximum_manifolds},
                pair_workgroups);
}

void
GpuNarrowPhase::pieces(ComputeRecorder& recorder,
                       GpuSpan instances,
                       GpuSpan pairs,
                       const GpuCount& count,
                       GpuSpan total,
                       std::uint32_t capacity,
                       float expansion)
{
    // Valid input, as collide() checks it; the mid phase counted, scanned, written.
    const State& state = *m_state;
    state.require_uploaded("pieces");
    if (!std::isfinite(expansion) || !(expansion >= 0.0F))
    {
        refuse("pieces", std::format("invalid expansion {}", expansion), "give a finite expansion of at least 0");
    }
    if (count.maximum > state.maximum_pairs || capacity > state.maximum_manifolds)
    {
        refuse("pieces",
               std::format("{} pairs into {} pieces, above the {} pairs and {} slots reserved", count.maximum, capacity, state.maximum_pairs,
                           state.maximum_manifolds),
               "call GpuNarrowPhase::reserve() with the largest counts before recording");
    }
    const std::uint32_t pair_workgroups = workgroups(std::max(count.maximum, 1U));
    PairsPush mid_phase{.tables = state.table(),
                        .instances_buffer = instances.buffer,
                        .instances_first = instances.first,
                        .pairs_buffer = pairs.buffer,
                        .pairs_first = pairs.first,
                        .maximum = count.maximum,
                        .count_buffer = count.word.buffer,
                        .count_word = count.word.first,
                        .counts_buffer = state.pair_counts->bindless_index().value(),
                        .offsets_buffer = state.pair_offsets->bindless_index().value(),
                        .sub_pairs_buffer = state.sub_pairs->bindless_index().value(),
                        .capacity = capacity,
                        .writing = 0,
                        .speculative_distance = expansion};
    record_pass(recorder, *state.pairs, mid_phase, pair_workgroups);
    state.primitives.exclusive_scan(recorder, span_of(*state.pair_counts), span_of(*state.pair_offsets), count, total);
    mid_phase.writing = 1;
    record_pass(recorder, *state.pairs, mid_phase, pair_workgroups);
}

auto
GpuNarrowPhase::piece_spans() const noexcept -> GpuPieceSpans
{
    const State& state = *m_state;
    return {.sub_pairs = span_of(*state.sub_pairs), .counts = span_of(*state.pair_counts), .offsets = span_of(*state.pair_offsets)};
}

auto
GpuNarrowPhase::write_primitive(ComputeRecorder& recorder,
                                std::uint32_t index,
                                const Shape& shape,
                                const Buffer& staging,
                                std::span<std::byte> mapped,
                                std::uint64_t offset) -> bool
{
    // The record as add() makes it for these shapes (sizes, margin, box), copied over the one at index.
    const State& state = *m_state;
    state.require_uploaded("write_primitive");
    const Aabb local = shape.bounds();
    GpuShapeRecord record{.kind = std::to_underlying(shape.kind()), .bounds_minimum = vector_of(local.minimum), .bounds_maximum = vector_of(local.maximum)};
    if (const SphereShape* sphere = shape.get_if<SphereShape>())
    {
        record.size = {sphere->radius, 0.0F, 0.0F};
        record.margin = sphere->radius;
    }
    else if (const CapsuleShape* capsule = shape.get_if<CapsuleShape>())
    {
        record.size = {capsule->half_height, capsule->radius, 0.0F};
        record.margin = capsule->radius;
    }
    else if (const CylinderShape* cylinder = shape.get_if<CylinderShape>())
    {
        record.size = {cylinder->half_height, cylinder->radius, 0.0F};
    }
    else if (const ConeShape* cone = shape.get_if<ConeShape>())
    {
        record.size = {cone->half_height, cone->radius, 0.0F};
    }
    else if (const BoxShape* box = shape.get_if<BoxShape>())
    {
        record.size = {box->half_extents.x, box->half_extents.y, box->half_extents.z};
        record.margin = box->radius;
    }
    else
    {
        return false;
    }
    std::memcpy(mapped.data() + offset, &record, sizeof(record));
    const std::array regions{BufferCopyRegion{.source_offset = offset, .destination_offset = std::uint64_t{index} * sizeof(GpuShapeRecord), .size = sizeof(GpuShapeRecord)}};
    recorder.copy_buffer(staging.handle(), state.arrays[0]->handle(), regions);
    return true;
}

auto
GpuNarrowPhase::record_bytes() noexcept -> std::uint32_t
{
    return sizeof(GpuShapeRecord);
}

auto
GpuNarrowPhase::table_index() const noexcept -> std::uint32_t
{
    return m_state->table();
}

auto
GpuNarrowPhase::uploaded() const noexcept -> bool
{
    return !m_state->pending_upload;
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
    if (previous_count.maximum > state.maximum_manifolds)
    {
        refuse("match", std::format("the previous count's maximum is {}, above the {} reserved", previous_count.maximum, state.maximum_manifolds),
               "call GpuNarrowPhase::reserve() with the largest counts before recording");
    }
    const std::uint32_t keys = state.cache_keys->bindless_index().value();
    const std::uint32_t values = state.cache_values->bindless_index().value();
    const std::uint32_t sorted_keys = state.cache_sorted_keys->bindless_index().value();
    const std::uint32_t order = state.cache_order->bindless_index().value();
    const std::uint32_t previous_workgroups = workgroups(std::max(previous_count.maximum, 1U));
    KeysPush key_push{.manifolds_buffer = previous.buffer,
                      .manifolds_first = previous.first,
                      .keys_buffer = keys,
                      .values_buffer = values,
                      .order_buffer = order,
                      .maximum = previous_count.maximum,
                      .count_buffer = previous_count.word.buffer,
                      .count_word = previous_count.word.first,
                      .gather = 0};
    record_pass(recorder, *state.keys, key_push, previous_workgroups);
    state.primitives.sort(recorder, {.buffer = keys}, {.buffer = values}, {.buffer = sorted_keys}, {.buffer = order}, previous_count);
    key_push.gather = 1;
    record_pass(recorder, *state.keys, key_push, previous_workgroups);
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
