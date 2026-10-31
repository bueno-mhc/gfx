// GpuPrimitives: the passes of each primitive recorded with their push constants (the layouts of the shaders' Push
// blocks), the levels of scans and reductions planned from the maximum count, the scratch buffers sized by reserve().
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.gpu_xpbd.shaders;
import :settings;
import :resources;
import :primitives;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// Digits of a radix pass.
constexpr std::uint32_t RADIX_SIZE = std::uint32_t{1} << GPU_XPBD_RADIX_BITS;

/// Levels of a scan or a reduction at most: GPU_XPBD_MAXIMUM_ELEMENTS elements, then 65535 tiles, then 128, then 1.
constexpr std::size_t MAXIMUM_LEVELS = 4;

/// Workgroups covering @p count elements a tile at a time (one at least: an empty primitive still writes its result).
[[nodiscard]] constexpr auto
tiles(std::uint32_t count) noexcept -> std::uint32_t
{
    return std::max((count + GPU_XPBD_TILE_SIZE - 1) / GPU_XPBD_TILE_SIZE, 1U);
}

/// The element counts of the levels of a scan or a reduction of @p count elements: the elements, then each level's tiles,
/// down to one tile (@p last_level_size: TILE_SIZE for a scan, whose top level is scanned by one workgroup; 1 for a
/// reduction, whose top level is its result).
struct Levels
{
    std::array<std::uint32_t, MAXIMUM_LEVELS> sizes{};   ///< Elements of each level.
    std::array<std::uint32_t, MAXIMUM_LEVELS> offsets{}; ///< Where each level above the first starts in the scratch.
    std::size_t count = 0;                               ///< Levels.
    std::uint32_t scratch = 0;                           ///< Scratch elements of the levels above the first.
};

[[nodiscard]] constexpr auto
plan_levels(std::uint32_t count,
            std::uint32_t last_level_size) noexcept -> Levels
{
    Levels levels{};
    levels.sizes[0] = count;
    levels.count = 1;
    while (levels.sizes[levels.count - 1] > last_level_size)
    {
        levels.offsets[levels.count] = levels.scratch;
        levels.sizes[levels.count] = tiles(levels.sizes[levels.count - 1]);
        levels.scratch += levels.sizes[levels.count];
        ++levels.count;
    }
    return levels;
}

static_assert(plan_levels(GPU_XPBD_MAXIMUM_ELEMENTS, GPU_XPBD_TILE_SIZE).count <= MAXIMUM_LEVELS);
static_assert(plan_levels(GPU_XPBD_MAXIMUM_ELEMENTS, 1).count <= MAXIMUM_LEVELS);
static_assert(plan_levels(GPU_XPBD_TILE_SIZE, GPU_XPBD_TILE_SIZE).count == 1 && plan_levels(GPU_XPBD_TILE_SIZE + 1, GPU_XPBD_TILE_SIZE).count == 2);

// The push constants of the passes (the Push blocks of the shaders, word by word).
struct ScanReducePush
{
    std::uint32_t input_buffer = 0;
    std::uint32_t input_first = 0;
    std::uint32_t partials_buffer = 0;
    std::uint32_t partials_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};

struct ScanDownsweepPush
{
    std::uint32_t input_buffer = 0;
    std::uint32_t input_first = 0;
    std::uint32_t output_buffer = 0;
    std::uint32_t output_first = 0;
    std::uint32_t offsets_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t offsets_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t total_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t total_word = 0;
};

struct ReduceBoundsPush
{
    std::uint32_t boxes_buffer = 0;
    std::uint32_t boxes_first = 0;
    std::uint32_t output_buffer = 0;
    std::uint32_t output_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};

struct RadixHistogramPush
{
    std::uint32_t keys_buffer = 0;
    std::uint32_t keys_first = 0;
    std::uint32_t histogram_buffer = 0;
    std::uint32_t histogram_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t shift = 0;
    std::uint32_t tile_count = 0;
};

struct SortPush
{
    std::uint32_t keys_buffer = 0;
    std::uint32_t keys_first = 0;
    std::uint32_t values_buffer = 0;
    std::uint32_t values_first = 0;
    std::uint32_t sorted_keys_buffer = 0;
    std::uint32_t sorted_keys_first = 0;
    std::uint32_t sorted_values_buffer = 0;
    std::uint32_t sorted_values_first = 0;
};

struct RadixScatterPush
{
    SortPush arrays{};
    std::uint32_t histogram_buffer = 0;
    std::uint32_t histogram_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t shift = 0;
    std::uint32_t tile_count = 0;
};

struct RankSortPush
{
    SortPush arrays{};
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};

struct CompactScatterPush
{
    std::uint32_t flags_buffer = 0;
    std::uint32_t flags_first = 0;
    std::uint32_t values_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t values_first = 0;
    std::uint32_t places_buffer = 0;
    std::uint32_t places_first = 0;
    std::uint32_t output_buffer = 0;
    std::uint32_t output_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};

struct DispatchArgumentsPush
{
    std::uint32_t count_buffer = 0;
    std::uint32_t count_word = 0;
    std::uint32_t arguments_buffer = 0;
    std::uint32_t arguments_first = 0;
    std::uint32_t per_workgroup = 0;
    std::uint32_t maximum = 0;
};
static_assert(sizeof(RadixScatterPush) == 15 * sizeof(std::uint32_t) && sizeof(RankSortPush) == 11 * sizeof(std::uint32_t));

/// The barrier ending a dispatch of arguments: the arguments are read by indirect dispatches (and compute passes).
constexpr GlobalMemoryBarrier COMPUTE_TO_INDIRECT{.source_stages = PipelineStage::COMPUTE_SHADER,
                                                  .source_access = MemoryAccess::SHADER_STORAGE_WRITE,
                                                  .destination_stages = PipelineStage::DRAW_INDIRECT | PipelineStage::COMPUTE_SHADER,
                                                  .destination_access = MemoryAccess::INDIRECT_COMMAND_READ | MemoryAccess::SHADER_STORAGE_READ |
                                                                        MemoryAccess::SHADER_STORAGE_WRITE};

/// Records a pass: its pipeline, its push constants, @p workgroups workgroups, then the barrier to the next pass.
template<class Push>
void
record_pass(ComputeRecorder& recorder,
            const ComputePipeline& pipeline,
            const Push& push,
            std::uint32_t workgroups,
            const GlobalMemoryBarrier& barrier = COMPUTE_TO_COMPUTE)
{
    recorder.bind_pipeline(pipeline.handle());
    recorder.push_constants(push);
    recorder.dispatch(workgroups);
    recorder.barrier({.memory_barriers = std::span{&barrier, 1}});
}

/// Throws the InvalidUsageError of @p operation.
[[noreturn]] void
refuse(std::string_view operation,
       std::string problem,
       std::string_view suggestion)
{
    throw InvalidUsageError({.backend = PhysicsBackendKind::GPU_XPBD,
                             .operation = std::format("GpuPrimitives::{}", operation),
                             .problem = std::move(problem),
                             .suggestion = std::string{suggestion}});
}

} // namespace

auto
span_of(const Buffer& buffer,
        std::uint32_t first) noexcept -> GpuSpan
{
    return {.buffer = buffer.bindless_index().value(), .first = first};
}

// ===================================================================================================== State

struct GpuPrimitives::State
{
    State(Device& target,
          bool subgroup_variant)
        : device{target}
        , subgroups{subgroup_variant}
        , scan_reduce{make_pipeline(target, subgroup_variant ? shaders::scan_reduce_subgroups_comp : shaders::scan_reduce_comp, modules)}
        , scan_downsweep{make_pipeline(target, subgroup_variant ? shaders::scan_downsweep_subgroups_comp : shaders::scan_downsweep_comp, modules)}
        , reduce_bounds{make_pipeline(target, subgroup_variant ? shaders::reduce_bounds_subgroups_comp : shaders::reduce_bounds_comp, modules)}
        , radix_histogram{make_pipeline(target, shaders::radix_histogram_comp, modules)}
        , radix_scatter{make_pipeline(target, shaders::radix_scatter_comp, modules)}
        , rank_sort{make_pipeline(target, shaders::rank_sort_comp, modules)}
        , compact_scatter{make_pipeline(target, shaders::compact_scatter_comp, modules)}
        , dispatch_arguments{make_pipeline(target, shaders::dispatch_arguments_comp, modules)}
    {
    }

    /// A device-local storage buffer of @p bytes (at least a vector).
    [[nodiscard]] auto scratch(std::uint64_t bytes,
                               std::string_view name) const -> std::unique_ptr<Buffer>
    {
        return device.create_buffer({.size = std::max<std::uint64_t>(bytes, 16), .usage = BufferUsage::STORAGE, .debug_name = name});
    }

    /// Throws when @p count is above the reserved maximum.
    void check_count(std::string_view operation,
                     const GpuCount& count) const
    {
        if (count.maximum > maximum)
        {
            refuse(operation, std::format("the count's maximum is {}, above the {} reserved", count.maximum, maximum),
                   "call GpuPrimitives::reserve() with the largest count before recording");
        }
    }

    /// Records the exclusive scan of @p count words of @p input into @p output, the sum into @p total.
    void scan(ComputeRecorder& recorder,
              GpuSpan input,
              GpuSpan output,
              const GpuCount& count,
              GpuSpan total) const
    {
        // Up: each level's tiles summed into the level above.
        const Levels levels = plan_levels(count.maximum, GPU_XPBD_TILE_SIZE);
        const std::uint32_t partials = scan_partials->bindless_index().value();
        const auto level_span = [&](std::size_t level) {
            return level == 0 ? input : GpuSpan{.buffer = partials, .first = levels.offsets[level]};
        };
        for (std::size_t level = 0; level + 1 < levels.count; ++level)
        {
            const GpuSpan source = level_span(level);
            const bool first_level = level == 0;
            record_pass(recorder, *scan_reduce,
                        ScanReducePush{.input_buffer = source.buffer,
                                       .input_first = source.first,
                                       .partials_buffer = partials,
                                       .partials_first = levels.offsets[level + 1],
                                       .maximum = levels.sizes[level],
                                       .count_buffer = first_level ? count.word.buffer : INVALID_BINDLESS_INDEX,
                                       .count_word = first_level ? count.word.first : 0},
                        tiles(levels.sizes[level]));
        }

        // Down: the top level scanned by one workgroup (writing the total), then each level from the offsets above it.
        for (std::size_t level = levels.count; level-- > 0;)
        {
            const bool top = level + 1 == levels.count;
            const bool first_level = level == 0;
            const GpuSpan source = level_span(level);
            const GpuSpan destination = first_level ? output : source;
            record_pass(recorder, *scan_downsweep,
                        ScanDownsweepPush{.input_buffer = source.buffer,
                                          .input_first = source.first,
                                          .output_buffer = destination.buffer,
                                          .output_first = destination.first,
                                          .offsets_buffer = top ? INVALID_BINDLESS_INDEX : partials,
                                          .offsets_first = top ? 0 : levels.offsets[level + 1],
                                          .maximum = levels.sizes[level],
                                          .count_buffer = first_level ? count.word.buffer : INVALID_BINDLESS_INDEX,
                                          .count_word = first_level ? count.word.first : 0,
                                          .total_buffer = top ? total.buffer : INVALID_BINDLESS_INDEX,
                                          .total_word = total.first},
                        tiles(levels.sizes[level]));
        }
    }

    Device& device;
    bool subgroups = false;
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::unique_ptr<ComputePipeline> scan_reduce{};
    std::unique_ptr<ComputePipeline> scan_downsweep{};
    std::unique_ptr<ComputePipeline> reduce_bounds{};
    std::unique_ptr<ComputePipeline> radix_histogram{};
    std::unique_ptr<ComputePipeline> radix_scatter{};
    std::unique_ptr<ComputePipeline> rank_sort{};
    std::unique_ptr<ComputePipeline> compact_scatter{};
    std::unique_ptr<ComputePipeline> dispatch_arguments{};

    // Scratch for primitives of up to `maximum` elements.
    std::uint32_t maximum = 0;
    std::unique_ptr<Buffer> scan_partials{}; ///< Words: the levels above the first of a scan.
    std::unique_ptr<Buffer> box_partials{};  ///< Boxes (two vectors): the levels above the first of a reduction.
    std::unique_ptr<Buffer> sort_keys{};     ///< The keys between radix passes.
    std::unique_ptr<Buffer> sort_values{};   ///< The values between radix passes.
    std::unique_ptr<Buffer> histogram{};     ///< RADIX_SIZE words per tile.
    std::unique_ptr<Buffer> places{};        ///< The scanned flags of a compaction.
};

// ===================================================================================================== GpuPrimitives

GpuPrimitives::GpuPrimitives(Device& device,
                             const GpuXpbdCapabilities& capabilities)
    : m_state{std::make_unique<State>(device, capabilities.subgroups)}
{
    reserve(GPU_XPBD_TILE_SIZE);
}

GpuPrimitives::~GpuPrimitives() = default;

void
GpuPrimitives::reserve(std::uint32_t maximum)
{
    State& state = *m_state;
    if (maximum <= state.maximum)
    {
        return;
    }
    if (maximum > GPU_XPBD_MAXIMUM_ELEMENTS)
    {
        refuse("reserve", std::format("{} elements is above GPU_XPBD_MAXIMUM_ELEMENTS ({})", maximum, GPU_XPBD_MAXIMUM_ELEMENTS),
               "split the work into several primitives");
    }

    // Room for the largest count: a scan of it, or of a sort's histogram (RADIX_SIZE words per tile).
    const std::uint32_t histogram_words = RADIX_SIZE * tiles(maximum);
    const std::uint32_t scan_words = std::max(plan_levels(maximum, GPU_XPBD_TILE_SIZE).scratch, plan_levels(histogram_words, GPU_XPBD_TILE_SIZE).scratch);
    const std::uint64_t word = sizeof(std::uint32_t);
    state.scan_partials = state.scratch(scan_words * word, "GPU primitives: scan partials");
    state.box_partials = state.scratch(std::uint64_t{plan_levels(maximum, 1).scratch} * 2 * sizeof(GpuVector), "GPU primitives: box partials");
    state.sort_keys = state.scratch(maximum * word, "GPU primitives: sort keys");
    state.sort_values = state.scratch(maximum * word, "GPU primitives: sort values");
    state.histogram = state.scratch(histogram_words * word, "GPU primitives: radix histogram");
    state.places = state.scratch(maximum * word, "GPU primitives: compaction places");
    state.maximum = maximum;
}

auto
GpuPrimitives::reserved() const noexcept -> std::uint32_t
{
    return m_state->maximum;
}

auto
GpuPrimitives::uses_subgroups() const noexcept -> bool
{
    return m_state->subgroups;
}

void
GpuPrimitives::exclusive_scan(ComputeRecorder& recorder,
                              GpuSpan input,
                              GpuSpan output,
                              const GpuCount& count,
                              GpuSpan total)
{
    m_state->check_count("exclusive_scan", count);
    m_state->scan(recorder, input, output, count, total);
}

void
GpuPrimitives::reduce_bounds(ComputeRecorder& recorder,
                             GpuSpan boxes,
                             GpuSpan result,
                             const GpuCount& count)
{
    // Each level's tiles reduced into the level above, the last pass into the result (one pass when one tile holds
    // every box).
    const State& state = *m_state;
    state.check_count("reduce_bounds", count);
    const Levels levels = plan_levels(count.maximum, 1);
    const std::uint32_t partials = state.box_partials->bindless_index().value();
    const std::size_t passes = std::max<std::size_t>(levels.count - 1, 1);
    for (std::size_t level = 0; level < passes; ++level)
    {
        const bool first_level = level == 0;
        const bool last_level = level + 1 == passes;
        const GpuSpan source = first_level ? boxes : GpuSpan{.buffer = partials, .first = levels.offsets[level]};
        const GpuSpan destination = last_level ? result : GpuSpan{.buffer = partials, .first = levels.offsets[level + 1]};
        record_pass(recorder, *state.reduce_bounds,
                    ReduceBoundsPush{.boxes_buffer = source.buffer,
                                     .boxes_first = source.first,
                                     .output_buffer = destination.buffer,
                                     .output_first = destination.first,
                                     .maximum = levels.sizes[level],
                                     .count_buffer = first_level ? count.word.buffer : INVALID_BINDLESS_INDEX,
                                     .count_word = first_level ? count.word.first : 0},
                    tiles(levels.sizes[level]));
    }
}

void
GpuPrimitives::sort(ComputeRecorder& recorder,
                    GpuSpan keys,
                    GpuSpan values,
                    GpuSpan sorted_keys,
                    GpuSpan sorted_values,
                    const GpuCount& count,
                    std::uint32_t key_bits)
{
    const State& state = *m_state;
    state.check_count("sort", count);
    if (key_bits == 0 || key_bits > 32)
    {
        refuse("sort", std::format("{} key bits", key_bits), "sort by 1 to 32 bits of the keys");
    }
    for (const GpuSpan output : {sorted_keys, sorted_values})
    {
        if (output.buffer == keys.buffer || output.buffer == values.buffer)
        {
            refuse("sort", "an output is in an input's buffer", "sort into other buffers than the inputs'");
        }
    }

    // Few keys: by rank, in one pass.
    if (count.maximum <= GPU_XPBD_RANK_SORT_LIMIT)
    {
        record_pass(recorder, *state.rank_sort,
                    RankSortPush{.arrays = {.keys_buffer = keys.buffer,
                                            .keys_first = keys.first,
                                            .values_buffer = values.buffer,
                                            .values_first = values.first,
                                            .sorted_keys_buffer = sorted_keys.buffer,
                                            .sorted_keys_first = sorted_keys.first,
                                            .sorted_values_buffer = sorted_values.buffer,
                                            .sorted_values_first = sorted_values.first},
                                 .maximum = count.maximum,
                                 .count_buffer = count.word.buffer,
                                 .count_word = count.word.first},
                    workgroups(std::max(count.maximum, 1U)));
        return;
    }

    // By radix: each pass from the last one's output, alternating between the scratch and the outputs so that the last
    // pass writes the outputs.
    const std::uint32_t passes = (key_bits + GPU_XPBD_RADIX_BITS - 1) / GPU_XPBD_RADIX_BITS;
    const std::uint32_t tile_count = tiles(count.maximum);
    const GpuSpan histogram = span_of(*state.histogram);
    const GpuSpan scratch_keys = span_of(*state.sort_keys);
    const GpuSpan scratch_values = span_of(*state.sort_values);
    GpuSpan source_keys = keys;
    GpuSpan source_values = values;
    for (std::uint32_t pass = 0; pass < passes; ++pass)
    {
        const bool into_outputs = (passes - 1 - pass) % 2 == 0;
        const GpuSpan destination_keys = into_outputs ? sorted_keys : scratch_keys;
        const GpuSpan destination_values = into_outputs ? sorted_values : scratch_values;
        const std::uint32_t shift = pass * GPU_XPBD_RADIX_BITS;
        record_pass(recorder, *state.radix_histogram,
                    RadixHistogramPush{.keys_buffer = source_keys.buffer,
                                       .keys_first = source_keys.first,
                                       .histogram_buffer = histogram.buffer,
                                       .histogram_first = histogram.first,
                                       .maximum = count.maximum,
                                       .count_buffer = count.word.buffer,
                                       .count_word = count.word.first,
                                       .shift = shift,
                                       .tile_count = tile_count},
                    tile_count);
        state.scan(recorder, histogram, histogram, {.maximum = RADIX_SIZE * tile_count}, {});
        record_pass(recorder, *state.radix_scatter,
                    RadixScatterPush{.arrays = {.keys_buffer = source_keys.buffer,
                                                .keys_first = source_keys.first,
                                                .values_buffer = source_values.buffer,
                                                .values_first = source_values.first,
                                                .sorted_keys_buffer = destination_keys.buffer,
                                                .sorted_keys_first = destination_keys.first,
                                                .sorted_values_buffer = destination_values.buffer,
                                                .sorted_values_first = destination_values.first},
                                     .histogram_buffer = histogram.buffer,
                                     .histogram_first = histogram.first,
                                     .maximum = count.maximum,
                                     .count_buffer = count.word.buffer,
                                     .count_word = count.word.first,
                                     .shift = shift,
                                     .tile_count = tile_count},
                    tile_count);
        source_keys = destination_keys;
        source_values = destination_values;
    }
}

void
GpuPrimitives::compact(ComputeRecorder& recorder,
                       GpuSpan flags,
                       GpuSpan values,
                       GpuSpan output,
                       GpuSpan output_count,
                       const GpuCount& count)
{
    // The places are the scanned flags; their sum is the count kept.
    const State& state = *m_state;
    state.check_count("compact", count);
    const GpuSpan places = span_of(*state.places);
    state.scan(recorder, flags, places, count, output_count);
    record_pass(recorder, *state.compact_scatter,
                CompactScatterPush{.flags_buffer = flags.buffer,
                                   .flags_first = flags.first,
                                   .values_buffer = values.buffer,
                                   .values_first = values.first,
                                   .places_buffer = places.buffer,
                                   .places_first = places.first,
                                   .output_buffer = output.buffer,
                                   .output_first = output.first,
                                   .maximum = count.maximum,
                                   .count_buffer = count.word.buffer,
                                   .count_word = count.word.first},
                tiles(count.maximum));
}

void
GpuPrimitives::dispatch_arguments(ComputeRecorder& recorder,
                                  const GpuCount& count,
                                  GpuSpan arguments,
                                  std::uint32_t per_workgroup)
{
    if (!count.word.is_valid() || per_workgroup == 0)
    {
        refuse("dispatch_arguments", !count.word.is_valid() ? "the count has no word" : "0 elements per workgroup",
               "give the word a pass writes the count into, and a positive number of elements per workgroup");
    }
    record_pass(recorder, *m_state->dispatch_arguments,
                DispatchArgumentsPush{.count_buffer = count.word.buffer,
                                      .count_word = count.word.first,
                                      .arguments_buffer = arguments.buffer,
                                      .arguments_first = arguments.first,
                                      .per_workgroup = per_workgroup,
                                      .maximum = count.maximum},
                1, COMPUTE_TO_INDIRECT);
}

} // namespace gfx::physics::gpu_xpbd
