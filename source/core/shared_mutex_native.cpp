// Windows' slim reader/writer locks behind the std-free interface of shared_mutex_native.hpp: a plain translation unit,
// since <windows.h> cannot share one with modules (docs: CLAUDE.md §6, defect 2).
#include "shared_mutex_native.hpp"

#if defined(_WIN32)

#include <windows.h>

static_assert(sizeof(SRWLOCK) == sizeof(void*), "an SRW lock is one pointer (SharedMutex stores a void*)");
static_assert(alignof(SRWLOCK) == alignof(void*));

namespace gfx::shared_mutex_native
{

namespace
{

/// The lock a SharedMutex stores (a pointer-sized SRWLOCK, null when unlocked: SRWLOCK_INIT).
auto
native(void** lock) noexcept -> PSRWLOCK
{
    return reinterpret_cast<PSRWLOCK>(lock);
}

} // namespace

void
lock_exclusive(void** lock) noexcept
{
    AcquireSRWLockExclusive(native(lock));
}

bool
try_lock_exclusive(void** lock) noexcept
{
    return TryAcquireSRWLockExclusive(native(lock)) != 0;
}

void
unlock_exclusive(void** lock) noexcept
{
    ReleaseSRWLockExclusive(native(lock));
}

void
lock_shared(void** lock) noexcept
{
    AcquireSRWLockShared(native(lock));
}

bool
try_lock_shared(void** lock) noexcept
{
    return TryAcquireSRWLockShared(native(lock)) != 0;
}

void
unlock_shared(void** lock) noexcept
{
    ReleaseSRWLockShared(native(lock));
}

} // namespace gfx::shared_mutex_native

#endif
