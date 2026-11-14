// gfx.core.shared_mutex: SharedMutex locks like std::shared_mutex (shared locks together, exclusive alone, the try_
// functions), keeps writers apart from readers across threads, and costs nanoseconds where MinGW's std::shared_mutex
// costs most of a microsecond.
#include <gtest/gtest.h>

import std;
import gfx.core.enums;
import gfx.core.shared_mutex;

namespace
{

using namespace gfx;

TEST(SharedMutexTest, SharedLocksCoexistAndExclusiveLocksStandAlone)
{
    SharedMutex mutex;

    // Two shared locks at once; no exclusive lock beside them.
    {
        std::shared_lock first{mutex};
        ASSERT_TRUE(mutex.try_lock_shared());
        EXPECT_FALSE(mutex.try_lock());
        mutex.unlock_shared();
    }

    // An exclusive lock: neither a shared nor another exclusive lock beside it.
    {
        std::unique_lock exclusive{mutex};
        EXPECT_FALSE(mutex.try_lock_shared());
        EXPECT_FALSE(mutex.try_lock());
    }

    // Unlocked again: both kinds can be taken.
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
    ASSERT_TRUE(mutex.try_lock_shared());
    mutex.unlock_shared();
}

TEST(SharedMutexTest, WritersAreAloneAndReadersSeeWholeWrites)
{
    // Writers change two values together under the exclusive lock; readers, under shared locks, must never see them
    // differ, and every increment must be counted.
    constexpr int WRITERS = 4;
    constexpr int READERS = 4;
    constexpr int WRITES = 20000;
    SharedMutex mutex;
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::atomic<int> torn{0};
    std::atomic<bool> writing{true};
    {
        std::vector<std::jthread> threads;
        for (int reader = 0; reader < READERS; ++reader)
        {
            threads.emplace_back([&] {
                while (writing.load(std::memory_order_relaxed))
                {
                    const std::shared_lock lock{mutex};
                    if (first != second)
                    {
                        torn.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        {
            std::vector<std::jthread> writers;
            for (int writer = 0; writer < WRITERS; ++writer)
            {
                writers.emplace_back([&] {
                    for (int write = 0; write < WRITES; ++write)
                    {
                        const std::lock_guard lock{mutex};
                        ++first;
                        ++second;
                    }
                });
            }
        }
        writing.store(false, std::memory_order_relaxed);
    }
    EXPECT_EQ(torn.load(), 0);
    EXPECT_EQ(first, std::uint64_t{WRITERS} * WRITES);
    EXPECT_EQ(second, first);
}

/// The mean time of an uncontended shared lock and unlock of @p mutex, in nanoseconds.
template<class Mutex>
auto
shared_lock_nanoseconds(Mutex& mutex) -> double
{
    constexpr int COUNT = 1'000'000;
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < COUNT; ++index)
    {
        mutex.lock_shared();
        mutex.unlock_shared();
    }
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / COUNT;
}

TEST(SharedMutexTest, UncontendedLocksTakeNanoseconds)
{
    if constexpr (DEBUG_BUILD)
    {
        GTEST_SKIP() << "timed in optimized builds only";
    }
    SharedMutex mutex;
    std::shared_mutex standard;
    const double ours = shared_lock_nanoseconds(mutex);
    const double theirs = shared_lock_nanoseconds(standard);
    std::println("shared lock and unlock: gfx::SharedMutex {:.1f} ns, std::shared_mutex {:.1f} ns", ours, theirs);

    // An SRW lock takes about 10 ns here; MinGW's std::shared_mutex about 900 ns. The bound leaves room for a loaded
    // machine.
    EXPECT_LT(ours, 100.0);

#if defined(__MINGW32__)
    // Detector: passes while MinGW's std::shared_mutex is slow. When it fails, the standard library's has become fast
    // and SharedMutex could be std::shared_mutex on MinGW too.
    EXPECT_GT(theirs, 200.0) << "std::shared_mutex is fast now: SharedMutex's SRW lock may no longer be needed";
#endif
}

} // namespace
