/// @file gpu_primitives.cppm
/// @brief The GPU backend's data-parallel building blocks (exported by gfx.physics.gpu_xpbd: the backend's passes are built
///        on them; tests and tools may use them too): exclusive scans of words, reductions of boxes, stable sorts of
///        (key, value) pairs, compactions, indirect dispatch arguments — recorded into the caller's command buffer, on
///        storage buffers named by their bindless indices.
///
/// **Deterministic.** Integer sums and floating-point minimums and maximums are exact, and every element goes to a place
/// computed by a scan (never to an atomic append), so the same inputs give the same bits whatever the GPU's scheduling, and
/// the subgroup and portable variants give the same bits too.
///
/// **Ordering.** The caller orders what wrote a primitive's inputs before it (a barrier); each primitive orders its own
/// passes and ends with a barrier making its outputs visible to later compute passes (and to indirect dispatches, for
/// dispatch_arguments()).
export module gfx.physics.gpu_xpbd:primitives;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import :settings;

export namespace gfx::physics::gpu_xpbd
{

/// @brief Elements of a bindless storage buffer: its bindless index, and the first element (in the primitive's elements:
///        words, or boxes of two vectors).
struct GpuSpan
{
    std::uint32_t buffer = INVALID_BINDLESS_INDEX; ///< The buffer's bindless index (INVALID_BINDLESS_INDEX: no span).
    std::uint32_t first = 0;                       ///< The first element.

    /// @brief Whether it names a buffer.
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return buffer != INVALID_BINDLESS_INDEX;
    }
};

/// @brief The span of @p buffer's elements from @p first (the buffer has STORAGE usage and a bindless index).
[[nodiscard]] auto span_of(const Buffer& buffer,
                           std::uint32_t first = 0) noexcept -> GpuSpan;

/// @brief How many elements a primitive processes: `maximum`, or the word a pass wrote when `word` is valid (clamped to
///        `maximum`, which sizes the dispatches).
struct GpuCount
{
    std::uint32_t maximum = 0; ///< The count, or its upper bound; at most the primitives' reserved maximum.
    GpuSpan word{};            ///< The word holding the count (invalid: the count is `maximum`).
};

/// @brief The building blocks, with their compute pipelines (the subgroup variant when the capabilities chose it) and
///        their scratch buffers.
class GpuPrimitives
{
public:
    /// @param device       The device (it outlives the primitives).
    /// @param capabilities What the backend chose on it (GpuXpbdCapabilities::subgroups selects the variant).
    GpuPrimitives(Device& device,
                  const GpuXpbdCapabilities& capabilities);
    ~GpuPrimitives();

    GpuPrimitives(const GpuPrimitives&) = delete;
    auto operator=(const GpuPrimitives&) -> GpuPrimitives& = delete;

    /// @brief Makes scratch room for primitives of up to @p maximum elements. Call it before recording the commands that
    ///        use the primitives, never between recording and submitting them: larger buffers replace the old ones, which
    ///        commands recorded earlier name (submitted commands keep theirs alive through the device's deleter).
    /// @throws InvalidUsageError if @p maximum is above GPU_XPBD_MAXIMUM_ELEMENTS.
    void reserve(std::uint32_t maximum);

    /// @brief The largest count reserved.
    [[nodiscard]] auto reserved() const noexcept -> std::uint32_t;

    /// @brief Whether the subgroup variant runs.
    [[nodiscard]] auto uses_subgroups() const noexcept -> bool;

    /// @brief Records the exclusive prefix sum of @p count words of @p input into @p output (which may be @p input), and
    ///        their sum into the word @p total (when valid). Sums wrap around modulo 2³².
    /// @throws InvalidUsageError if the count's maximum is above reserved().
    void exclusive_scan(ComputeRecorder& recorder,
                        GpuSpan input,
                        GpuSpan output,
                        const GpuCount& count,
                        GpuSpan total = {});

    /// @brief Records the box around @p count boxes of @p boxes (two vectors each: minimum, maximum; w ignored) into the
    ///        box @p result: the empty box (+∞, −∞) when there is none.
    /// @throws InvalidUsageError if the count's maximum is above reserved().
    void reduce_bounds(ComputeRecorder& recorder,
                       GpuSpan boxes,
                       GpuSpan result,
                       const GpuCount& count);

    /// @brief Records a stable sort of @p count (key, value) word pairs by key, from @p keys and @p values into
    ///        @p sorted_keys and @p sorted_values (other buffers than the inputs): by rank up to GPU_XPBD_RANK_SORT_LIMIT
    ///        elements (one pass), else by radix, GPU_XPBD_RADIX_BITS per pass. The keys are below 2^@p key_bits (fewer
    ///        bits, fewer radix passes: ⌈key_bits / GPU_XPBD_RADIX_BITS⌉).
    /// @throws InvalidUsageError if the count's maximum is above reserved(), @p key_bits is not in [1, 32], or an output
    ///         is in an input's buffer.
    void sort(ComputeRecorder& recorder,
              GpuSpan keys,
              GpuSpan values,
              GpuSpan sorted_keys,
              GpuSpan sorted_values,
              const GpuCount& count,
              std::uint32_t key_bits = 32);

    /// @brief Records the compaction of @p count elements: the values of @p values (or the elements' indices when it is
    ///        invalid) whose flags (words of @p flags, 0 or 1) are 1, in order, into @p output, and how many into the word
    ///        @p output_count.
    /// @throws InvalidUsageError if the count's maximum is above reserved().
    void compact(ComputeRecorder& recorder,
                 GpuSpan flags,
                 GpuSpan values,
                 GpuSpan output,
                 GpuSpan output_count,
                 const GpuCount& count);

    /// @brief Records the arguments of an indirect dispatch of ⌈count / @p per_workgroup⌉ workgroups (three words of
    ///        @p arguments, in a buffer with STORAGE and INDIRECT usage) for the count in @p count's word.
    /// @throws InvalidUsageError if @p count has no word or @p per_workgroup is 0.
    void dispatch_arguments(ComputeRecorder& recorder,
                            const GpuCount& count,
                            GpuSpan arguments,
                            std::uint32_t per_workgroup = GPU_XPBD_WORKGROUP_SIZE);

    /// @brief What only the implementation touches (pipelines, scratch).
    struct State;

private:
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::physics::gpu_xpbd
