/// @file shared_mutex_native.hpp
/// @brief A std-free interface to Windows' slim reader/writer locks for gfx.core.shared_mutex (shared_mutex_native.cpp,
///        a plain translation unit: <windows.h> cannot share a unit with modules). A lock is one pointer, null when
///        unlocked (SRWLOCK_INIT); the functions take its address.
#pragma once

#if defined(_WIN32)

namespace gfx::shared_mutex_native
{

/// AcquireSRWLockExclusive.
void lock_exclusive(void** lock) noexcept;

/// TryAcquireSRWLockExclusive: whether the lock was taken.
[[nodiscard]] bool try_lock_exclusive(void** lock) noexcept;

/// ReleaseSRWLockExclusive.
void unlock_exclusive(void** lock) noexcept;

/// AcquireSRWLockShared.
void lock_shared(void** lock) noexcept;

/// TryAcquireSRWLockShared: whether the lock was taken.
[[nodiscard]] bool try_lock_shared(void** lock) noexcept;

/// ReleaseSRWLockShared.
void unlock_shared(void** lock) noexcept;

} // namespace gfx::shared_mutex_native

#endif
