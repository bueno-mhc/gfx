/// @file shared_mutex.cppm
/// @brief Module gfx.core.shared_mutex: SharedMutex, a reader-writer lock with std::shared_mutex's interface that is
///        fast on every platform. Not part of the gfx.core umbrella: import it where it is used.
module;

#include "shared_mutex_native.hpp" // std-free: Windows' slim reader/writer locks (shared_mutex_native.cpp)

export module gfx.core.shared_mutex;

import std;

export namespace gfx
{

/// @brief A reader-writer lock: std::shared_mutex's interface (it works with std::shared_lock, std::unique_lock,
///        std::lock_guard and std::scoped_lock), at the cost of an uncontended atomic operation on every platform.
///
/// MinGW's std::shared_mutex is winpthreads' read-write lock, which takes about 900 ns to lock and unlock even when no
/// other thread holds it (std::mutex: 8.5 ns), and a world's objects lock theirs once per call: reading the pose of
/// each of 8 000 bodies took 8.4 ms a frame. On Windows SharedMutex is a slim reader/writer lock (SRWLOCK: 10 ns);
/// elsewhere it is std::shared_mutex. Like both, it is not recursive (a thread holding it, shared or exclusive, must not
/// lock it again) and not upgradable; a waiting writer is not guaranteed to overtake later readers.
class SharedMutex
{
public:
    /// @brief An unlocked mutex.
    SharedMutex() noexcept = default;

    SharedMutex(const SharedMutex&) = delete;
    auto operator=(const SharedMutex&) -> SharedMutex& = delete;

    /// @brief Destroys it (unlocked: destroying a locked mutex is undefined, as for std::shared_mutex).
    ~SharedMutex() = default;

    /// @brief Locks it exclusively, waiting while any thread holds it.
    void lock()
    {
#if defined(_WIN32)
        shared_mutex_native::lock_exclusive(&m_lock);
#else
        m_mutex.lock();
#endif
    }

    /// @brief Locks it exclusively if no thread holds it.
    /// @return Whether it was locked.
    [[nodiscard]] auto try_lock() noexcept -> bool
    {
#if defined(_WIN32)
        return shared_mutex_native::try_lock_exclusive(&m_lock);
#else
        return m_mutex.try_lock();
#endif
    }

    /// @brief Releases the exclusive lock the calling thread holds.
    void unlock() noexcept
    {
#if defined(_WIN32)
        shared_mutex_native::unlock_exclusive(&m_lock);
#else
        m_mutex.unlock();
#endif
    }

    /// @brief Locks it shared, waiting while a thread holds it exclusively.
    void lock_shared()
    {
#if defined(_WIN32)
        shared_mutex_native::lock_shared(&m_lock);
#else
        m_mutex.lock_shared();
#endif
    }

    /// @brief Locks it shared if no thread holds it exclusively.
    /// @return Whether it was locked.
    [[nodiscard]] auto try_lock_shared() noexcept -> bool
    {
#if defined(_WIN32)
        return shared_mutex_native::try_lock_shared(&m_lock);
#else
        return m_mutex.try_lock_shared();
#endif
    }

    /// @brief Releases a shared lock the calling thread holds.
    void unlock_shared() noexcept
    {
#if defined(_WIN32)
        shared_mutex_native::unlock_shared(&m_lock);
#else
        m_mutex.unlock_shared();
#endif
    }

private:
#if defined(_WIN32)
    void* m_lock = nullptr; ///< An SRWLOCK: one pointer, null when unlocked (SRWLOCK_INIT).
#else
    std::shared_mutex m_mutex{}; ///< The standard library's.
#endif
};

} // namespace gfx
