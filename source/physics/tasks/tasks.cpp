// The library's task schedulers: inline, and a pool of std::jthread workers that take ranges with an atomic counter
// while the caller takes some too and then waits for the last range, and that run started work from a queue.
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

auto
TaskScheduler::start(StartedTask task) -> TaskTicket
{
    task();
    return {};
}

void
TaskScheduler::finish(TaskTicket /*ticket*/)
{
}

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

/// The pool: workers sleep on `work` until a loop is posted (its generation changes) or work is queued; they take
/// ranges until none is left and count the finished ones, or run the oldest queued work. The caller of a loop returns
/// when every range is done and no worker is still taking ranges, and marks the loop over (no task) before it does: a
/// worker woken late joins only a loop in progress, so that none reads the next loop's counter while thinking it is
/// this one's.
struct ThreadTaskScheduler::State
{
    /// A started piece of work in its slot.
    struct Started
    {
        StartedTask task{};
        std::uint32_t generation = 0;
        bool done = true;
        std::exception_ptr failure{};
    };

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

    // Started work: slots (reused), the queue of slots waiting for a thread (under mutex).
    std::vector<Started> started{};
    std::vector<std::uint32_t> free_slots{};
    std::deque<std::uint32_t> queue{};
    std::uint32_t next_generation = 1;

    std::vector<std::jthread> workers{};

    /// Runs the oldest queued work; `lock` (held on entry and exit) is released while it runs.
    void run_queued(std::unique_lock<std::mutex>& lock)
    {
        const std::uint32_t slot = queue.front();
        queue.pop_front();
        StartedTask queued = std::move(started[slot].task);
        lock.unlock();
        std::exception_ptr thrown;
        try
        {
            queued();
        }
        catch (...)
        {
            thrown = std::current_exception();
        }
        lock.lock();
        started[slot].failure = thrown;
        started[slot].done = true;
        finished.notify_all();
    }

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
                work.wait(lock, [&] { return stopping || stop.stop_requested() || generation != seen || !queue.empty(); });
                if (stopping || stop.stop_requested())
                {
                    return;
                }
                if (generation == seen)
                {
                    // Started work, then wait again.
                    run_queued(lock);
                    continue;
                }
                seen = generation;
                if (task == nullptr)
                {
                    // Woken late, for a loop that is over: joining it would take the next loop's ranges with this one's count.
                    continue;
                }
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

auto
ThreadTaskScheduler::start(StartedTask task) -> TaskTicket
{
    // One thread: at once on the caller.
    State& state = *m_state;
    if (state.threads == 1)
    {
        return TaskScheduler::start(std::move(task));
    }

    // A slot for it, queued for the workers.
    TaskTicket ticket{};
    {
        const std::scoped_lock lock{state.mutex};
        if (state.free_slots.empty())
        {
            state.free_slots.push_back(static_cast<std::uint32_t>(state.started.size()));
            state.started.emplace_back();
        }
        ticket.slot = state.free_slots.back();
        state.free_slots.pop_back();
        ticket.generation = state.next_generation;
        state.next_generation = state.next_generation == std::numeric_limits<std::uint32_t>::max() ? 1 : state.next_generation + 1;
        State::Started& slot = state.started[ticket.slot];
        slot.task = std::move(task);
        slot.generation = ticket.generation;
        slot.done = false;
        slot.failure = nullptr;
        state.queue.push_back(ticket.slot);
    }
    state.work.notify_one();
    return ticket;
}

void
ThreadTaskScheduler::finish(TaskTicket ticket)
{
    // Done at once (one thread), or wait: running queued work meanwhile, or sleeping until a worker finishes some.
    if (ticket.generation == 0)
    {
        return;
    }
    State& state = *m_state;
    std::unique_lock lock{state.mutex};
    if (state.started[ticket.slot].generation != ticket.generation)
    {
        return;
    }

    // The slot is indexed anew each time: queued work run here unlocks, and a start() meanwhile may grow the slots.
    while (!state.started[ticket.slot].done)
    {
        if (!state.queue.empty())
        {
            state.run_queued(lock);
        }
        else
        {
            state.finished.wait(lock);
        }
    }
    const std::exception_ptr failure = std::exchange(state.started[ticket.slot].failure, nullptr);
    state.started[ticket.slot].generation = 0;
    state.free_slots.push_back(ticket.slot);
    lock.unlock();
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

} // namespace gfx::physics
