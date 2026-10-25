// The library's task schedulers: inline, and a pool of std::jthread workers that take ranges with an atomic counter
// while the caller takes some too and then waits for the last range.
module gfx.physics.tasks;

import std;

namespace gfx::physics
{

namespace
{

/// The ranges of a loop of @p count items in pieces of @p size: [index · size, min(count, (index + 1) · size)).
struct Ranges
{
    std::uint32_t count = 0;
    std::uint32_t size = 1;

    [[nodiscard]] auto number() const noexcept -> std::uint32_t
    {
        return count == 0 ? 0 : (count + size - 1) / size;
    }
};

} // namespace

void
InlineTaskScheduler::parallel_for(std::uint32_t count,
                                  std::uint32_t minimum_range,
                                  const TaskFunction& task)
{
    const std::uint32_t size = std::max(1U, minimum_range);
    for (std::uint32_t begin = 0; begin < count; begin += size)
    {
        task(begin, std::min(count, begin + size));
    }
}

/// The pool: workers sleep on `work` until a loop is posted (its generation changes), take ranges until none is left,
/// and count the finished ones. The caller returns when every range is done and no worker is still taking ranges, so
/// that no worker reads the next loop's counter while thinking it is this one's.
struct ThreadTaskScheduler::State
{
    std::uint32_t threads = 1;
    std::mutex mutex{};
    std::condition_variable work{};
    std::condition_variable finished{};
    std::uint64_t generation = 0;
    std::uint32_t active = 0;
    bool stopping = false;

    // The loop being run.
    const TaskFunction* task = nullptr;
    Ranges ranges{};
    std::atomic<std::uint32_t> next{0};
    std::atomic<std::uint32_t> done{0};
    std::exception_ptr failure{};
    std::vector<std::jthread> workers{};

    /// Runs ranges until none is left.
    void run_ranges()
    {
        const std::uint32_t total = ranges.number();
        for (std::uint32_t index = next.fetch_add(1, std::memory_order_relaxed); index < total; index = next.fetch_add(1, std::memory_order_relaxed))
        {
            const std::uint32_t begin = index * ranges.size;
            try
            {
                (*task)(begin, std::min(ranges.count, begin + ranges.size));
            }
            catch (...)
            {
                const std::scoped_lock lock{mutex};
                failure = failure ? failure : std::current_exception();
            }
            done.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void worker(const std::stop_token& stop)
    {
        std::uint64_t seen = 0;
        while (true)
        {
            {
                std::unique_lock lock{mutex};
                work.wait(lock, [&] { return stopping || stop.stop_requested() || generation != seen; });
                if (stopping || stop.stop_requested())
                {
                    return;
                }
                seen = generation;
                ++active;
            }
            run_ranges();
            {
                const std::scoped_lock lock{mutex};
                --active;
            }
            finished.notify_all();
        }
    }
};

ThreadTaskScheduler::ThreadTaskScheduler(std::uint32_t threads)
    : m_state{std::make_unique<State>()}
{
    m_state->threads = std::max(1U, threads);
    for (std::uint32_t index = 1; index < m_state->threads; ++index)
    {
        m_state->workers.emplace_back([state = m_state.get()](const std::stop_token& stop) { state->worker(stop); });
    }
}

ThreadTaskScheduler::~ThreadTaskScheduler()
{
    {
        const std::scoped_lock lock{m_state->mutex};
        m_state->stopping = true;
    }
    m_state->work.notify_all();
    m_state->workers.clear();
}

void
ThreadTaskScheduler::parallel_for(std::uint32_t count,
                                  std::uint32_t minimum_range,
                                  const TaskFunction& task)
{
    // Ranges of at least the minimum, about four per thread; one thread or one range: on the caller.
    State& state = *m_state;
    const std::uint32_t wanted = std::max(1U, (count + state.threads * 4 - 1) / (state.threads * 4));
    const Ranges ranges{.count = count, .size = std::max(std::max(1U, minimum_range), wanted)};
    if (ranges.number() == 0)
    {
        return;
    }
    if (state.threads == 1 || ranges.number() == 1)
    {
        InlineTaskScheduler{}.parallel_for(count, ranges.size, task);
        return;
    }
    {
        const std::scoped_lock lock{state.mutex};
        state.task = &task;
        state.ranges = ranges;
        state.next.store(0, std::memory_order_relaxed);
        state.done.store(0, std::memory_order_relaxed);
        state.failure = nullptr;
        ++state.generation;
    }
    state.work.notify_all();
    state.run_ranges();
    std::exception_ptr failure;
    {
        std::unique_lock lock{state.mutex};
        state.finished.wait(lock, [&] { return state.done.load(std::memory_order_acquire) == ranges.number() && state.active == 0; });
        state.task = nullptr;
        failure = state.failure;
    }
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

auto
ThreadTaskScheduler::concurrency() const noexcept -> std::uint32_t
{
    return m_state->threads;
}

} // namespace gfx::physics
