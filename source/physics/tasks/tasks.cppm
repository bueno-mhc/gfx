/// @file tasks.cppm
/// @brief The task scheduler the physics library splits work over (the broad phase's pair search, Box3D's tasks; islands
///        and soft bodies later): the application's job system behind TaskScheduler, or the library's InlineTaskScheduler
///        (on the caller) and ThreadTaskScheduler (a pool of std::jthread workers the caller joins). Parallel loops
///        (parallel_for) and single pieces of work started and finished later (start, finish: what Box3D's task hooks
///        take). Results never depend on the thread count: the library gives each range its own output and merges them in
///        order.
export module gfx.physics.tasks;

import std;

export namespace gfx::physics
{

/// @brief The work of a range [begin, end) of a parallel loop; called concurrently from several threads (a lambda that
///        captures a pointer or two stays in std::function's own storage: no allocation).
using TaskFunction = std::function<void(std::uint32_t begin, std::uint32_t end)>;

/// @brief A piece of work started on its own (TaskScheduler::start()); as TaskFunction, a small capture is stored inline.
using StartedTask = std::function<void()>;

/// @brief Names the work TaskScheduler::start() began, for finish() (generation 0: it is done already).
struct TaskTicket
{
    std::uint32_t slot = 0;       ///< The scheduler's.
    std::uint32_t generation = 0; ///< The scheduler's; 0: nothing to wait for.
};

/// @brief Runs the ranges of parallel loops (the application's job system, or one of the library's).
class TaskScheduler
{
public:
    virtual ~TaskScheduler() = default;

    /// @brief Calls @p task on ranges covering [0, @p count) once each, ranges of at least @p minimum_range items (but
    ///        the last), possibly at the same time on several threads, and returns when every range is done.
    /// @throws The first exception a range threw (the other ranges still run).
    virtual void parallel_for(std::uint32_t count,
                              std::uint32_t minimum_range,
                              const TaskFunction& task) = 0;

    /// @brief How many ranges may run at once.
    [[nodiscard]] virtual auto concurrency() const noexcept -> std::uint32_t = 0;

    /// @brief Starts @p task: on another thread when one is free, otherwise later or on the caller. By default it runs at
    ///        once on the caller (and what it throws propagates from start()).
    /// @return What finish() takes, once.
    [[nodiscard]] virtual auto start(StartedTask task) -> TaskTicket;

    /// @brief Waits until the work of @p ticket is done; the caller may run other started work meanwhile.
    /// @throws What the work threw.
    virtual void finish(TaskTicket ticket);
};

/// @brief Runs every range on the calling thread, in order.
class InlineTaskScheduler final : public TaskScheduler
{
public:
    void parallel_for(std::uint32_t count,
                      std::uint32_t minimum_range,
                      const TaskFunction& task) override;

    [[nodiscard]] auto concurrency() const noexcept -> std::uint32_t override
    {
        return 1;
    }
};

/// @brief A pool of worker threads; the calling thread runs ranges too while it waits. One parallel_for() at a time;
///        started work is queued for the workers (any thread may start and finish).
class ThreadTaskScheduler final : public TaskScheduler
{
public:
    /// @param threads Threads running ranges, the caller included (at least 1; 1 runs everything on the caller).
    explicit ThreadTaskScheduler(std::uint32_t threads = std::max(1U, std::thread::hardware_concurrency()));
    ~ThreadTaskScheduler() override;

    ThreadTaskScheduler(const ThreadTaskScheduler&) = delete;
    auto operator=(const ThreadTaskScheduler&) -> ThreadTaskScheduler& = delete;

    void parallel_for(std::uint32_t count,
                      std::uint32_t minimum_range,
                      const TaskFunction& task) override;

    [[nodiscard]] auto concurrency() const noexcept -> std::uint32_t override;

    /// @brief Queues @p task for the workers (on the caller at once with one thread).
    [[nodiscard]] auto start(StartedTask task) -> TaskTicket override;

    /// @brief Waits for @p ticket's work, running queued work while it waits.
    void finish(TaskTicket ticket) override;

private:
    struct State;

private:
    std::unique_ptr<State> m_state;
};

} // namespace gfx::physics
