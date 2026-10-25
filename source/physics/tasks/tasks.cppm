/// @file tasks.cppm
/// @brief The task scheduler the physics library splits work over (the broad phase's pair search now; islands and
///        soft bodies later): the application's job system behind TaskScheduler, or the library's InlineTaskScheduler
///        (on the caller) and ThreadTaskScheduler (a pool of std::jthread workers the caller joins). Results never depend
///        on the thread count: the library gives each range its own output and merges them in order.
export module gfx.physics.tasks;

import std;

export namespace gfx::physics
{

/// @brief The work of a range [begin, end) of a parallel loop; called concurrently from several threads (a lambda that
///        captures a pointer or two stays in std::function's own storage: no allocation).
using TaskFunction = std::function<void(std::uint32_t begin, std::uint32_t end)>;

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

/// @brief A pool of worker threads; the calling thread runs ranges too while it waits. One parallel_for() at a time.
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

private:
    struct State;

private:
    std::unique_ptr<State> m_state;
};

} // namespace gfx::physics
