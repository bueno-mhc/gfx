// Plain translation unit: the host clock of calibrated timestamps (vulkan_host_clock.hpp).
#include "vulkan_host_clock.hpp"

#ifdef _WIN32
#include <windows.h>

unsigned long long
gfx_vulkan_host_clock() noexcept
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<unsigned long long>(counter.QuadPart);
}

unsigned long long
gfx_vulkan_host_clock_frequency() noexcept
{
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return static_cast<unsigned long long>(frequency.QuadPart);
}

#else
#include <time.h>

unsigned long long
gfx_vulkan_host_clock() noexcept
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<unsigned long long>(now.tv_sec) * 1'000'000'000ULL + static_cast<unsigned long long>(now.tv_nsec);
}

unsigned long long
gfx_vulkan_host_clock_frequency() noexcept
{
    return 1'000'000'000ULL;
}

#endif
