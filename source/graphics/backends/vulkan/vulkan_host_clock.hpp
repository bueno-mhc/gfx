// The host clock Vulkan's calibrated timestamps read beside the GPU's (VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER on
// Windows, VK_TIME_DOMAIN_CLOCK_MONOTONIC elsewhere), read now, to tie a calibration to steady_clock. A plain translation
// unit implements it (<windows.h> cannot share one with modules); this header is std-free.
#pragma once

/// The host clock's current value: QueryPerformanceCounter's ticks on Windows, CLOCK_MONOTONIC's nanoseconds elsewhere.
unsigned long long gfx_vulkan_host_clock() noexcept;

/// The host clock's ticks per second.
unsigned long long gfx_vulkan_host_clock_frequency() noexcept;
