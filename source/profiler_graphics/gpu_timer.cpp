// The GpuTimer: a QueryPool split into one range of queries per frame in flight, the scopes each frame recorded, their
// timestamps read back and placed on the CPU's timeline (calibrated, or estimated at construction).
module gfx.profiler.graphics;

import std;

namespace gfx::profiler
{

namespace
{

/// Submissions the estimate of the CPU-GPU offset takes the best of.
constexpr std::uint32_t ESTIMATE_SUBMISSIONS = 5;

/// steady_clock nanoseconds (the profiler's timeline).
auto
steady_nanoseconds(std::chrono::steady_clock::time_point time) -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}

/// A GPU tick and the steady_clock nanoseconds it happened at.
struct Calibration
{
    std::uint64_t ticks{0};
    std::int64_t cpu{0};
};

/// A scope a frame recorded.
struct PendingScope
{
    const ProfileSite* site{nullptr};
    std::uint32_t query{GpuTimer::NO_INDEX}; // its begin timestamp's index within the frame's range
    std::uint32_t depth{0};
    GpuScopeStatus status{GpuScopeStatus::TIMED};
    std::int64_t recorded{0};                // steady_clock nanoseconds when it was recorded
};

/// One frame in flight's range of queries and what it recorded.
struct Slot
{
    bool recorded{false};                    // a frame began in it
    bool read{true};                         // its timestamps were read (or given up on)
    std::uint32_t used{0};                   // queries taken
    std::vector<PendingScope> scopes{};
    Calibration calibration{};               // the calibration current when it began
};

} // namespace

struct GpuTimer::Implementation
{
    Implementation(Device& timed_device,
                   const GpuTimerDescription& timer_description);

    /// The device's calibrated timestamps, as a Calibration.
    [[nodiscard]] auto calibrate() -> Calibration;

    /// Estimates the offset from submissions that write a timestamp (the best of a few).
    void estimate();

    /// A command buffer of @p Type's queue resetting every query (@p reset_all) or writing a timestamp into @p query
    /// (not NO_INDEX), submitted; the CPU's steady_clock nanoseconds before the submission and when the timestamp was
    /// seen written (polled), or when the submission completed (no timestamp).
    template<QueueType Type>
    auto round_trip(bool reset_all,
                    std::uint32_t query) -> std::pair<std::int64_t, std::int64_t>;

    /// round_trip() on the timer's queue type.
    auto round_trip(bool reset_all,
                    std::uint32_t query) -> std::pair<std::int64_t, std::int64_t>;

    /// Reads @p slot (index @p index) into latest, and into resolved when a profiler collects; with @p wait, waits up to
    /// the limit (then its timed scopes are INCOMPLETE). False when not waiting and its timestamps are not all written.
    auto read_slot(Slot& slot,
                   std::uint32_t index,
                   bool wait,
                   bool collecting) -> bool;

    Device& device;
    GpuTimerDescription description;
    bool timestamps{false};
    std::uint32_t queries_per_frame{0};
    std::uint64_t valid_mask{0};
    double nanoseconds_per_tick{1.0};
    std::unique_ptr<QueryPool> pool{};
    QueryPoolHandle pool_handle{};
    GpuClockAlignment alignment{GpuClockAlignment::UNAVAILABLE};
    std::atomic<std::int64_t> deviation{0}; // nanoseconds (read without the lock)
    Calibration calibration{};
    std::chrono::steady_clock::time_point calibrated_at{};
    std::string name{};

    mutable std::mutex mutex{};
    std::vector<Slot> slots{};
    std::uint64_t frames{0};
    std::uint32_t current{0};
    bool frame_started{false};
    std::uint32_t depth{0};
    std::vector<GpuScopeRecord> resolved{};
    std::vector<GpuScopeRecord> latest{};
    std::vector<std::uint64_t> ticks{};
};

GpuTimer::Implementation::Implementation(Device& timed_device,
                                         const GpuTimerDescription& timer_description)
    : device{timed_device}
    , description{timer_description}
{
    if (description.frames_in_flight == 0U || description.scopes_per_frame == 0U)
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::RECORDING,
                             .operation = "GpuTimer::GpuTimer",
                             .problem = std::format("{} frames in flight of {} scopes each time nothing", description.frames_in_flight,
                                                    description.scopes_per_frame),
                             .suggestion = "give at least one frame in flight and one scope per frame"});
    }
    if (description.queue == QueueType::SPARSE_BINDING)
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::RECORDING,
                             .operation = "GpuTimer::GpuTimer",
                             .problem = "a SPARSE_BINDING queue records no commands to time",
                             .suggestion = "time the GRAPHICS, COMPUTE or TRANSFER queue"});
    }
    name = description.name.empty() ? std::format("{} {}", device.physical_device().properties().name, description.queue) : description.name;
    slots.resize(description.frames_in_flight);

    // Without timestamps every scope is UNAVAILABLE (and the queue is not needed); with them, the queue must exist.
    if (!device.enabled(DeviceFeature::TIMESTAMP_QUERY))
    {
        return;
    }
    if (device.queue_count(description.queue) == 0U)
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::RECORDING,
                             .operation = "GpuTimer::GpuTimer",
                             .problem = std::format("the device has no {} queue", description.queue),
                             .suggestion = "time a queue type the device was created with"});
    }

    // Timestamps on this queue: their valid bits, their period, the queries of every frame in flight.
    const std::uint32_t family = device.queue(description.queue).family_index();
    const std::span<const QueueFamilyProperties> families = device.physical_device().queue_families();
    const std::uint32_t valid_bits = family < families.size() ? families[family].timestamp_valid_bits : 0U;
    timestamps = valid_bits != 0U;
    if (!timestamps)
    {
        return;
    }
    valid_mask = valid_bits >= 64U ? ~std::uint64_t{0} : (std::uint64_t{1} << valid_bits) - 1U;
    nanoseconds_per_tick = device.physical_device().properties().limits.timestamp_period_nanoseconds;
    queries_per_frame = description.scopes_per_frame * 2U;
    pool = device.create_query_pool({.type = QueryType::TIMESTAMP, .count = queries_per_frame * description.frames_in_flight, .debug_name = "gpu timer"});
    pool_handle = pool->handle();

    // Aligned by the device's calibrated timestamps (the queries reset on the GPU), else by an estimate (which resets them).
    if (description.calibrated && device.enabled(DeviceFeature::CALIBRATED_TIMESTAMPS))
    {
        (void)round_trip(true, GpuTimer::NO_INDEX);
        calibration = calibrate();
        alignment = GpuClockAlignment::CALIBRATED;
    }
    else
    {
        estimate();
        alignment = GpuClockAlignment::ESTIMATED;
    }
}

auto
GpuTimer::Implementation::calibrate() -> Calibration
{
    const TimestampCalibration calibrated = device.calibrate_timestamps();
    calibrated_at = std::chrono::steady_clock::now();
    deviation.store(calibrated.deviation.count(), std::memory_order_relaxed);
    return {.ticks = calibrated.gpu_ticks, .cpu = steady_nanoseconds(calibrated.cpu_time)};
}

template<QueueType Type>
auto
GpuTimer::Implementation::round_trip(bool reset_all,
                                     std::uint32_t query) -> std::pair<std::int64_t, std::int64_t>
{
    const std::unique_ptr<CommandPool> commands = device.create_command_pool({.queue_type = Type, .debug_name = "gpu timer"});
    const std::unique_ptr<CommandBuffer> buffer = commands->allocate(CommandBufferLevel::PRIMARY, "gpu timer");
    {
        Recorder<Type> recorder{*buffer};
        if (reset_all)
        {
            recorder.reset_query_pool(pool_handle, 0, pool->count());
        }
        if (query != GpuTimer::NO_INDEX)
        {
            recorder.write_timestamp(pool_handle, PipelineStage::NONE, query);
        }
    }
    const std::unique_ptr<TimelineSemaphore> timeline = device.create_timeline_semaphore({.initial_value = 0, .debug_name = "gpu timer"});
    const std::array command_buffers{buffer->handle()};
    const std::array signals{TimelineSemaphoreSignal{.semaphore = timeline->handle(), .value = 1}};
    const std::array batches{SubmitDescription{.command_buffers = command_buffers, .timeline_signals = signals}};
    const std::int64_t before = steady_nanoseconds(std::chrono::steady_clock::now());
    device.queue(Type).submit(batches);

    // The timestamp polled (a wait for the submission's end can return milliseconds after the GPU wrote it).
    std::int64_t after = 0;
    if (query != GpuTimer::NO_INDEX)
    {
        std::array<std::uint64_t, 1> tick{};
        const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + description.wait_limit;
        while (!pool->results(query, tick, false) && std::chrono::steady_clock::now() < limit)
        {
        }
        after = steady_nanoseconds(std::chrono::steady_clock::now());
    }
    (void)timeline->wait(1);
    if (query == GpuTimer::NO_INDEX)
    {
        after = steady_nanoseconds(std::chrono::steady_clock::now());
    }
    return {before, after};
}

auto
GpuTimer::Implementation::round_trip(bool reset_all,
                                     std::uint32_t query) -> std::pair<std::int64_t, std::int64_t>
{
    switch (description.queue)
    {
        case QueueType::GRAPHICS:
            return round_trip<QueueType::GRAPHICS>(reset_all, query);
        case QueueType::COMPUTE:
            return round_trip<QueueType::COMPUTE>(reset_all, query);
        case QueueType::TRANSFER:
            return round_trip<QueueType::TRANSFER>(reset_all, query);
        case QueueType::SPARSE_BINDING:
            break; // refused by the constructor: no command buffers
    }
    return {};
}

void
GpuTimer::Implementation::estimate()
{
    // Every query reset; then a few submissions each writing one timestamp into its own query, polled: the GPU wrote it
    // between the submission and when it was seen written — its midpoint, off by half that window at most; the
    // narrowest window of a few. The queries are reset again after (frames read their ranges all or nothing).
    (void)round_trip(true, GpuTimer::NO_INDEX);
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    const std::uint32_t attempts = std::min(ESTIMATE_SUBMISSIONS, pool->count());
    for (std::uint32_t attempt = 0; attempt < attempts; ++attempt)
    {
        const auto [before, after] = round_trip(false, attempt);
        std::array<std::uint64_t, 1> tick{};
        if (!pool->results(attempt, tick, false))
        {
            continue;
        }
        if (after - before < best)
        {
            best = after - before;
            calibration = {.ticks = tick[0] & valid_mask, .cpu = before + (after - before) / 2};
            deviation.store((after - before + 1) / 2, std::memory_order_relaxed);
        }
    }
    (void)round_trip(true, GpuTimer::NO_INDEX);
}

auto
GpuTimer::Implementation::read_slot(Slot& slot,
                                    std::uint32_t index,
                                    bool wait,
                                    bool collecting) -> bool
{
    // Every timestamp the frame took, all written (or, waiting, until the limit).
    bool written = true;
    if (timestamps && slot.used != 0U)
    {
        ticks.resize(slot.used);
        const std::uint32_t first = index * queries_per_frame;
        written = pool->results(first, ticks, false);
        const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + description.wait_limit;
        while (!written && wait && std::chrono::steady_clock::now() < limit)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            written = pool->results(first, ticks, false);
        }
        if (!written && !wait)
        {
            return false;
        }
    }

    // Each scope on the CPU's timeline (untimed ones where they were recorded).
    const auto to_cpu = [&slot, this](std::uint64_t tick)
    {
        const auto elapsed = static_cast<std::int64_t>((tick & valid_mask) - slot.calibration.ticks);
        return slot.calibration.cpu + std::llround(static_cast<double>(elapsed) * nanoseconds_per_tick);
    };
    latest.clear();
    for (const PendingScope& scope : slot.scopes)
    {
        GpuScopeRecord record{.site = scope.site, .begin = scope.recorded, .end = scope.recorded, .depth = scope.depth, .query = scope.query, .status = scope.status};
        if (scope.status == GpuScopeStatus::TIMED)
        {
            if (written)
            {
                record.begin = to_cpu(ticks[scope.query]);
                record.end = to_cpu(ticks[scope.query + 1]);
            }
            else
            {
                record.status = GpuScopeStatus::INCOMPLETE;
            }
        }
        latest.push_back(record);
    }
    if (collecting)
    {
        resolved.insert(resolved.end(), latest.begin(), latest.end());
    }
    slot.read = true;
    return true;
}

GpuTimer::GpuTimer(Device& device,
                   const GpuTimerDescription& description)
    : m_implementation{std::make_unique<Implementation>(device, description)}
{
}

GpuTimer::~GpuTimer()
{
    // No profiler collects from it once it starts going.
    detach();
}

namespace
{

/// Whether resolved scopes are kept for a profiler: one collects from @p timer and records.
[[nodiscard]] auto
collecting(const GpuTimer& timer) noexcept -> bool
{
    return timer.attached() && is_recording();
}

} // namespace

auto
GpuTimer::start_frame() -> std::uint32_t
{
    Implementation& implementation = *m_implementation;
    const std::scoped_lock lock{implementation.mutex};

    // Calibrated again when due; the slot's last frame read if nobody read it.
    if (implementation.alignment == GpuClockAlignment::CALIBRATED
        && std::chrono::steady_clock::now() - implementation.calibrated_at >= implementation.description.recalibration)
    {
        implementation.calibration = implementation.calibrate();
    }
    implementation.current = static_cast<std::uint32_t>(implementation.frames % implementation.slots.size());
    Slot& slot = implementation.slots[implementation.current];
    if (slot.recorded && !slot.read)
    {
        (void)implementation.read_slot(slot, implementation.current, true, collecting(*this));
    }
    slot.recorded = true;
    slot.read = false;
    slot.used = 0;
    slot.scopes.clear();
    slot.calibration = implementation.calibration;
    ++implementation.frames;
    implementation.frame_started = true;
    implementation.depth = 0;
    return implementation.timestamps ? implementation.current * implementation.queries_per_frame : NO_INDEX;
}

auto
GpuTimer::open_scope(const ProfileSite& site,
                     GpuScopeTiming timing) -> std::pair<std::uint32_t, std::uint32_t>
{
    // Nothing records it: no bookkeeping, no timestamp.
    Implementation& implementation = *m_implementation;
    if (timing != GpuScopeTiming::ALWAYS && !implementation.description.always && !(attached() && is_recording(site.category())))
    {
        return {NO_INDEX, NO_INDEX};
    }
    const std::scoped_lock lock{implementation.mutex};
    if (!implementation.frame_started)
    {
        return {NO_INDEX, NO_INDEX};
    }

    // Two queries of the frame's range, while there are; else recorded untimed.
    Slot& slot = implementation.slots[implementation.current];
    PendingScope pending{.site = &site,
                         .query = NO_INDEX,
                         .depth = implementation.depth,
                         .status = GpuScopeStatus::TIMED,
                         .recorded = steady_nanoseconds(std::chrono::steady_clock::now())};
    std::uint32_t query = NO_INDEX;
    if (!implementation.timestamps)
    {
        pending.status = GpuScopeStatus::UNAVAILABLE;
    }
    else if (slot.used + 2U > implementation.queries_per_frame)
    {
        pending.status = GpuScopeStatus::DROPPED;
    }
    else
    {
        pending.query = slot.used;
        query = implementation.current * implementation.queries_per_frame + slot.used;
        slot.used += 2U;
    }
    slot.scopes.push_back(pending);
    ++implementation.depth;
    return {static_cast<std::uint32_t>(slot.scopes.size() - 1U), query};
}

void
GpuTimer::close_scope(std::uint32_t /*scope*/) noexcept
{
    Implementation& implementation = *m_implementation;
    const std::scoped_lock lock{implementation.mutex};
    implementation.depth = implementation.depth == 0U ? 0U : implementation.depth - 1U;
}

auto
GpuTimer::query_pool() const noexcept -> QueryPoolHandle
{
    return m_implementation->pool_handle;
}

auto
GpuTimer::queries_per_frame() const noexcept -> std::uint32_t
{
    return m_implementation->queries_per_frame;
}

void
GpuTimer::finish_frame()
{
    Implementation& implementation = *m_implementation;
    const std::scoped_lock lock{implementation.mutex};
    if (!implementation.frame_started)
    {
        return;
    }
    Slot& slot = implementation.slots[implementation.current];
    if (slot.recorded && !slot.read)
    {
        (void)implementation.read_slot(slot, implementation.current, true, collecting(*this));
    }
    implementation.frame_started = false;
}

auto
GpuTimer::latest() const -> std::vector<GpuScopeRecord>
{
    const std::scoped_lock lock{m_implementation->mutex};
    return m_implementation->latest;
}

void
GpuTimer::latest(std::vector<GpuScopeRecord>& scopes) const
{
    const std::scoped_lock lock{m_implementation->mutex};
    scopes.insert(scopes.end(), m_implementation->latest.begin(), m_implementation->latest.end());
}

auto
GpuTimer::description() const noexcept -> const GpuTimerDescription&
{
    return m_implementation->description;
}

auto
GpuTimer::track_name() const -> std::string
{
    return m_implementation->name;
}

auto
GpuTimer::alignment() const noexcept -> GpuClockAlignment
{
    return m_implementation->alignment;
}

auto
GpuTimer::alignment_deviation() const noexcept -> std::chrono::nanoseconds
{
    return std::chrono::nanoseconds{m_implementation->deviation.load(std::memory_order_relaxed)};
}

void
GpuTimer::collect(std::vector<GpuScopeRecord>& scopes,
                  bool wait)
{
    // The frames before the current one, oldest first (the current one only when waiting: it may still record).
    Implementation& implementation = *m_implementation;
    const std::scoped_lock lock{implementation.mutex};
    const auto slot_count = static_cast<std::uint32_t>(implementation.slots.size());
    for (std::uint32_t age = 1; age <= slot_count; ++age)
    {
        const std::uint32_t index = (implementation.current + age) % slot_count;
        Slot& slot = implementation.slots[index];
        if (!slot.recorded || slot.read || (index == implementation.current && !wait))
        {
            continue;
        }
        if (!implementation.read_slot(slot, index, wait, true))
        {
            break; // later frames are later still
        }
    }
    scopes.insert(scopes.end(), implementation.resolved.begin(), implementation.resolved.end());
    implementation.resolved.clear();
}

} // namespace gfx::profiler
