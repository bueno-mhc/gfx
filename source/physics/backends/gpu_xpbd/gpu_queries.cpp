// GpuXpbdBackend's queries (QueryEngine::BACKEND) on the GPU: one at a time, in a command buffer of their own between
// steps — the moving tree rebuilt from the published poses when the step did not (query.comp's mode 0 walks it and the
// static tree), the narrow phase's mid phase alone (the pieces: convex shapes, compound children, triangles under the
// query's region), each piece tested (mode 1), each collider's best piece (mode 2) — then the CPU picks the nearest or
// sorts, as the abstraction's queries do (ties to the smaller handle). When the GPU cannot answer exactly the abstraction
// answers: before the first step, a hull the shape table lacks, more colliders or pieces than a query holds; when the GPU
// fails (a lost device), the query fails (UNSUPPORTED) and the next step reports the error.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.interface;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.world;
import gfx.physics.gpu_xpbd.shaders;
import :resources;
import :primitives;
import :narrow_phase;
import :solver;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The query kinds (QUERY_* in query.comp).
enum class QueryKind : std::uint32_t
{
    RAY,
    RAY_ALL,
    CAST,
    OVERLAP,
    BOX,
    CLOSEST,
};

/// The push constants of query.comp.
struct QueryPush
{
    std::uint32_t query = 0;
    std::uint32_t mode = 0;
};

/// The results' header: pairs found, pieces, box results, overflow flags (one vector), a closest point's reach (a second
/// vector), then the results.
constexpr std::uint32_t RESULT_HEADER_WORDS = 4;
constexpr std::uint64_t RESULT_HEADER_BYTES = 32;

/// Where the query's input puts the shape's record (after the query).
constexpr std::uint64_t SHAPE_RECORD_OFFSET = 256;

[[nodiscard]] auto
vector4(Vector3 value,
        float w = 0.0F) noexcept -> std::array<float, 4>
{
    return {value.x, value.y, value.z, w};
}

/// A query's start: its kind, its filter.
[[nodiscard]] auto
make_query(QueryKind kind,
           const QueryOptions& options) noexcept -> GpuQuery
{
    const auto category = std::to_underlying(options.filter.category);
    const auto mask = std::to_underlying(options.filter.mask);
    return {.kind = std::to_underlying(kind),
            .filter = {static_cast<std::uint32_t>(category), static_cast<std::uint32_t>(category >> 32U), static_cast<std::uint32_t>(mask),
                       static_cast<std::uint32_t>(mask >> 32U)},
            .group = options.filter.group};
}

/// The failure of a query of @p kind.
[[nodiscard]] auto
failure(QueryFailureKind kind,
        std::string_view operation,
        std::string_view detail) noexcept -> QueryFailure
{
    return {.kind = kind, .operation = operation, .detail = detail, .backend = PhysicsBackendKind::GPU_XPBD, .capability = std::nullopt, .context = {}};
}

/// Keeps the @p output.size() smallest values seen (by @p less) in order, @p count of them held (as the framework does).
template<class Value, class Less>
void
keep_smallest(std::span<Value> output,
              std::size_t& count,
              const Value& value,
              Less&& less) noexcept
{
    if (count == output.size() && (output.empty() || !less(value, output.back())))
    {
        return;
    }
    std::size_t index = count < output.size() ? count++ : output.size() - 1;
    for (; index > 0 && less(value, output[index - 1]); --index)
    {
        output[index] = output[index - 1];
    }
    output[index] = value;
}

[[nodiscard]] auto
vector3(const std::array<float, 4>& value) noexcept -> Vector3
{
    return {.x = value[0], .y = value[1], .z = value[2]};
}

/// What a query returns when the GPU did not answer: nothing (the abstraction answers) or its failure.
template<class Value>
[[nodiscard]] auto
unanswered(const std::expected<bool, QueryFailure>& answered) noexcept -> std::optional<QueryResult<Value>>
{
    return answered.has_value() ? std::nullopt : std::optional{QueryResult<Value>{std::unexpected(answered.error())}};
}

/// The entry of a result's collider slot (an empty one past the entries).
[[nodiscard]] auto
entry_of(std::span<const ColliderEntry> colliders,
         std::uint32_t slot) noexcept -> ColliderEntry
{
    return slot < colliders.size() ? colliders[slot] : ColliderEntry{};
}

} // namespace

// ===================================================================================================== running a query

void
GpuXpbdBackend::run_commands(CommandBuffer* commands)
{
    // One submission signaling the next timeline value, waited for on the calling thread.
    const std::array command_buffers{(commands != nullptr ? commands : m_command_buffer.get())->handle()};
    const std::array signals{TimelineSemaphoreSignal{.semaphore = m_timeline->handle(), .value = m_submitted + 1}};
    const std::array batches{SubmitDescription{.command_buffers = command_buffers, .timeline_signals = signals}};
    m_queue->submit(batches);
    ++m_submitted;
    if (m_timeline->wait(m_submitted) != WaitStatus::SIGNALED)
    {
        throw PhysicsError({.backend = identity(), .operation = "PhysicsWorld::step", .problem = "the GPU did not complete", .suggestion = {}});
    }
}

auto
GpuXpbdBackend::run_query(GpuQuery query,
                          const Shape* shape,
                          std::string_view operation) noexcept -> std::expected<bool, QueryFailure>
{
    const std::scoped_lock lock{m_query_mutex};
    try
    {
        // Nothing on the GPU yet, or shapes it has not uploaded: the abstraction answers.
        GpuNarrowPhase& narrow_phase = m_solver.narrow_phase();
        if (m_query_pipeline == nullptr || m_submitted == 0 || !narrow_phase.uploaded())
        {
            return false;
        }

        // The buffers it reads; the cast or overlapping shape: a primitive's record written, a shape of the table named.
        const GpuQueryLayout layout = m_solver.query_layout();
        query.pair_capacity = GPU_XPBD_QUERY_PAIRS;
        query.region_instance = layout.region_instance;
        query.trees = layout.trees;
        query.tables = layout.tables;
        query.colliders = layout.colliders;
        query.collider_list = layout.collider_list;
        query.instances = layout.instances;
        query.boxes = layout.boxes;
        query.moving_header = layout.moving_header;
        query.moving_nodes = layout.moving_nodes;
        query.static_header = layout.static_header;
        query.static_nodes = layout.static_nodes;
        query.pairs = m_query_pairs->bindless_index().value();
        query.results = m_query_answers->bindless_index().value();
        const GpuPieceSpans pieces = narrow_phase.piece_spans();
        query.sub_pairs = pieces.sub_pairs.buffer;
        query.piece_counts = pieces.counts.buffer;
        query.piece_offsets = pieces.offsets.buffer;
        query.piece_results = m_query_pieces->bindless_index().value();
        query.piece_capacity = GPU_XPBD_QUERY_PIECES;
        query.moving_count = layout.moving_count;
        query.static_count = layout.static_count;
        query.region = {m_query_records[0], 0, 0, 0};
        bool primitive = false;
        if (shape != nullptr)
        {
            if (const auto known = m_shape_indices.find(shape); known != m_shape_indices.end())
            {
                query.shape = known->second;
            }
            else if (shape->is_convex() && !shape->get_if<HullShape>())
            {
                query.shape = m_query_records[1];
                primitive = true;
            }
            else
            {
                return false;
            }
        }

        // The tree of the published poses (unless the step left it), the input, the passes.
        m_query_pool->reset();
        {
            ComputeRecorder recorder{*m_query_commands};
            recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});
            if (!m_query_tree_current)
            {
                m_solver.record_query_tree(recorder);
            }
            const std::span<std::byte> input = m_query_input.bytes();
            std::memcpy(input.data(), &query, sizeof(query));
            if (primitive)
            {
                (void)narrow_phase.write_primitive(recorder, query.shape, *shape, m_query_input.buffer(), input, SHAPE_RECORD_OFFSET);
            }
            m_query_input.buffer().flush_mapped_range();
            recorder.fill_buffer(m_query_answers->handle(), 0, RESULT_HEADER_BYTES, 0);
            recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});
            record_pass(recorder, *m_query_pipeline, QueryPush{.query = m_query_input.index(), .mode = 0}, 1);
            if (query.kind != std::to_underlying(QueryKind::BOX))
            {
                narrow_phase.pieces(recorder, GpuSpan{.buffer = layout.instances, .first = 0}, span_of(*m_query_pairs),
                                    {.maximum = GPU_XPBD_QUERY_PAIRS, .word = span_of(*m_query_answers, 0)}, span_of(*m_query_answers, 1), GPU_XPBD_QUERY_PIECES,
                                    0.0F);
                record_pass(recorder, *m_query_pipeline, QueryPush{.query = m_query_input.index(), .mode = 1}, workgroups(GPU_XPBD_QUERY_PIECES));
                record_pass(recorder, *m_query_pipeline, QueryPush{.query = m_query_input.index(), .mode = 2}, workgroups(GPU_XPBD_QUERY_PAIRS));
            }
            recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_TRANSFER, 1}});
            const std::array regions{BufferCopyRegion{.size = m_query_answers->size()}};
            recorder.copy_buffer(m_query_answers->handle(), m_query_results.buffer().handle(), regions);
            recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_HOST, 1}});
        }
        run_commands(m_query_commands.get());
        m_query_tree_current = true;

        // More than a query holds, or a walk that overflowed its stack: the abstraction answers.
        m_query_results.buffer().invalidate_mapped_range();
        std::array<std::uint32_t, RESULT_HEADER_WORDS> header{};
        std::memcpy(header.data(), m_query_results.bytes().data(), sizeof(header));
        return header[0] <= GPU_XPBD_QUERY_PAIRS && header[1] <= GPU_XPBD_QUERY_PIECES && header[2] <= GPU_XPBD_QUERY_PAIRS && header[3] == 0;
    }
    catch (...)
    {
        return std::unexpected(failure(QueryFailureKind::UNSUPPORTED, operation, "the GPU failed (its device may be lost; the next step reports the error)"));
    }
}

auto
GpuXpbdBackend::query_results() noexcept -> std::span<const GpuQueryResult>
{
    // The header's pairs (or box results), then the results.
    const std::span<std::byte> bytes = m_query_results.bytes();
    std::array<std::uint32_t, RESULT_HEADER_WORDS> header{};
    std::memcpy(header.data(), bytes.data(), sizeof(header));
    const auto* results = reinterpret_cast<const GpuQueryResult*>(bytes.data() + RESULT_HEADER_BYTES);
    return {results, std::min(std::max(header[0], header[2]), GPU_XPBD_QUERY_PAIRS)};
}

// ===================================================================================================== the queries

auto
GpuXpbdBackend::ray_cast(const Ray& ray,
                         float maximum,
                         const QueryOptions& options) const noexcept -> std::optional<QueryResult<RayHit>>
{
    // The nearest hit (ties to the smaller handle).
    auto& self = const_cast<GpuXpbdBackend&>(*this);
    GpuQuery query = make_query(QueryKind::RAY, options);
    query.origin = vector4(ray.origin, maximum);
    query.direction = vector4(ray.direction);
    const std::expected<bool, QueryFailure> answered = self.run_query(query, nullptr, "PhysicsWorld::ray_cast");
    if (!answered.has_value() || !*answered)
    {
        return unanswered<RayHit>(answered);
    }
    std::optional<RayHit> best;
    for (const GpuQueryResult& result : self.query_results())
    {
        const ColliderEntry entry = entry_of(m_colliders, result.collider);
        if (result.hit == 0 || !entry.handle || (best && (result.value > best->distance || (result.value == best->distance && !(entry.handle < best->collider)))))
        {
            continue;
        }
        best = RayHit{.collider = entry.handle,
                      .body = entry.body,
                      .distance = result.value,
                      .point = ray.at(result.value),
                      .normal = vector3(result.normal),
                      .sub_shape = result.sub_shape};
    }
    if (!best)
    {
        return QueryResult<RayHit>{std::unexpected(failure(QueryFailureKind::NO_HIT, "PhysicsWorld::ray_cast", {}))};
    }
    return QueryResult<RayHit>{*best};
}

auto
GpuXpbdBackend::ray_cast_all(const Ray& ray,
                             float maximum,
                             std::span<RayHit> output,
                             const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<RayHit>>>
{
    // Every collider's nearest hit, the nearest kept in order.
    auto& self = const_cast<GpuXpbdBackend&>(*this);
    GpuQuery query = make_query(QueryKind::RAY_ALL, options);
    query.origin = vector4(ray.origin, maximum);
    query.direction = vector4(ray.direction);
    const std::expected<bool, QueryFailure> answered = self.run_query(query, nullptr, "PhysicsWorld::ray_cast_all");
    if (!answered.has_value() || !*answered)
    {
        return unanswered<std::span<RayHit>>(answered);
    }
    const auto nearer = [](const RayHit& first, const RayHit& second) {
        return first.distance < second.distance || (first.distance == second.distance && first.collider < second.collider);
    };
    std::size_t count = 0;
    bool exceeded = false;
    for (const GpuQueryResult& result : self.query_results())
    {
        const ColliderEntry entry = entry_of(m_colliders, result.collider);
        if (result.hit == 0 || !entry.handle)
        {
            continue;
        }
        exceeded = count == output.size() || exceeded;
        keep_smallest(output, count,
                      RayHit{.collider = entry.handle,
                             .body = entry.body,
                             .distance = result.value,
                             .point = ray.at(result.value),
                             .normal = vector3(result.normal),
                             .sub_shape = result.sub_shape},
                      nearer);
    }
    if (exceeded)
    {
        return QueryResult<std::span<RayHit>>{
            std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, "PhysicsWorld::ray_cast_all", "more hits than the output holds (it holds the nearest)"))};
    }
    return QueryResult<std::span<RayHit>>{output.first(count)};
}

auto
GpuXpbdBackend::shape_cast(const Shape& shape,
                           const Transform& pose,
                           Vector3 translation,
                           const QueryOptions& options) const noexcept -> std::optional<QueryResult<ShapeCastHit>>
{
    // The first touch along the translation (ties to the smaller handle), among the colliders the swept box reaches.
    auto& self = const_cast<GpuXpbdBackend&>(*this);
    GpuQuery query = make_query(QueryKind::CAST, options);
    const Aabb start = shape.bounds(pose);
    const Aabb swept = start.merged(Aabb{.minimum = start.minimum + translation, .maximum = start.maximum + translation});
    query.origin = vector4(pose.position);
    query.direction = vector4(translation);
    query.orientation = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
    query.region_minimum = vector4(swept.minimum);
    query.region_maximum = vector4(swept.maximum);
    const std::expected<bool, QueryFailure> answered = self.run_query(query, &shape, "PhysicsWorld::shape_cast");
    if (!answered.has_value() || !*answered)
    {
        return unanswered<ShapeCastHit>(answered);
    }
    std::optional<ShapeCastHit> best;
    for (const GpuQueryResult& result : self.query_results())
    {
        const ColliderEntry entry = entry_of(m_colliders, result.collider);
        if (result.hit == 0 || !entry.handle || (best && (result.value > best->fraction || (result.value == best->fraction && !(entry.handle < best->collider)))))
        {
            continue;
        }
        best = ShapeCastHit{.collider = entry.handle,
                            .body = entry.body,
                            .fraction = result.value,
                            .point = vector3(result.point),
                            .normal = vector3(result.normal),
                            .sub_shape = result.sub_shape};
    }
    if (!best)
    {
        return QueryResult<ShapeCastHit>{std::unexpected(failure(QueryFailureKind::NO_HIT, "PhysicsWorld::shape_cast", {}))};
    }
    return QueryResult<ShapeCastHit>{*best};
}

auto
GpuXpbdBackend::overlap(const Shape& shape,
                        const Transform& pose,
                        std::span<ColliderHandle> output,
                        const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>
{
    // The colliders the shape touches or overlaps, the smallest handles kept in order.
    auto& self = const_cast<GpuXpbdBackend&>(*this);
    GpuQuery query = make_query(QueryKind::OVERLAP, options);
    const Aabb box = shape.bounds(pose);
    query.origin = vector4(pose.position);
    query.orientation = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
    query.region_minimum = vector4(box.minimum);
    query.region_maximum = vector4(box.maximum);
    const std::expected<bool, QueryFailure> answered = self.run_query(query, &shape, "PhysicsWorld::overlap");
    if (!answered.has_value() || !*answered)
    {
        return unanswered<std::span<ColliderHandle>>(answered);
    }
    std::size_t count = 0;
    bool exceeded = false;
    for (const GpuQueryResult& result : self.query_results())
    {
        const ColliderEntry entry = entry_of(m_colliders, result.collider);
        if (result.hit != 0 && entry.handle)
        {
            exceeded = count == output.size() || exceeded;
            keep_smallest(output, count, entry.handle, std::less{});
        }
    }
    if (exceeded)
    {
        return QueryResult<std::span<ColliderHandle>>{
            std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, "PhysicsWorld::overlap", "more colliders than the output holds"))};
    }
    return QueryResult<std::span<ColliderHandle>>{output.first(count)};
}

auto
GpuXpbdBackend::overlap(const Aabb& box,
                        std::span<ColliderHandle> output,
                        const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>
{
    // The colliders whose boxes overlap the box, the smallest handles kept in order.
    auto& self = const_cast<GpuXpbdBackend&>(*this);
    GpuQuery query = make_query(QueryKind::BOX, options);
    query.region_minimum = vector4(box.minimum);
    query.region_maximum = vector4(box.maximum);
    const std::expected<bool, QueryFailure> answered = self.run_query(query, nullptr, "PhysicsWorld::overlap");
    if (!answered.has_value() || !*answered)
    {
        return unanswered<std::span<ColliderHandle>>(answered);
    }
    std::size_t count = 0;
    bool exceeded = false;
    for (const GpuQueryResult& result : self.query_results())
    {
        const ColliderEntry entry = entry_of(m_colliders, result.collider);
        if (entry.handle)
        {
            exceeded = count == output.size() || exceeded;
            keep_smallest(output, count, entry.handle, std::less{});
        }
    }
    if (exceeded)
    {
        return QueryResult<std::span<ColliderHandle>>{
            std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, "PhysicsWorld::overlap", "more colliders than the output holds"))};
    }
    return QueryResult<std::span<ColliderHandle>>{output.first(count)};
}

auto
GpuXpbdBackend::closest_point(Vector3 point,
                              float maximum,
                              const QueryOptions& options) const noexcept -> std::optional<QueryResult<ClosestPointHit>>
{
    // The nearest collider within the maximum (ties to the smaller handle), its nearest point.
    auto& self = const_cast<GpuXpbdBackend&>(*this);
    GpuQuery query = make_query(QueryKind::CLOSEST, options);
    query.origin = vector4(point, maximum);
    const std::expected<bool, QueryFailure> answered = self.run_query(query, nullptr, "PhysicsWorld::closest_point");
    if (!answered.has_value() || !*answered)
    {
        return unanswered<ClosestPointHit>(answered);
    }
    std::optional<ClosestPointHit> best;
    for (const GpuQueryResult& result : self.query_results())
    {
        const ColliderEntry entry = entry_of(m_colliders, result.collider);
        if (result.hit == 0 || !entry.handle || result.value > maximum ||
            (best && (result.value > best->distance || (result.value == best->distance && !(entry.handle < best->collider)))))
        {
            continue;
        }
        best = ClosestPointHit{.collider = entry.handle, .body = entry.body, .point = vector3(result.point), .distance = result.value, .sub_shape = result.sub_shape};
    }
    if (!best)
    {
        return QueryResult<ClosestPointHit>{std::unexpected(failure(QueryFailureKind::NO_HIT, "PhysicsWorld::closest_point", {}))};
    }
    return QueryResult<ClosestPointHit>{*best};
}

} // namespace gfx::physics::gpu_xpbd
