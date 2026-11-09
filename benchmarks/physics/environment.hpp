// The machine a benchmark runs on, read in a plain translation unit (environment.cpp): the processor's brand string
// (cpuid), the operating system (`ver` / `uname`) and the power scheme (`powercfg` on Windows) come through headers that
// cannot share a translation unit with `import std`. This header includes nothing.
#pragma once

namespace gfx::benchmark::environment
{

/// @brief Writes the processor's brand string into @p text (at most @p size - 1 characters and a terminating zero).
/// @return The characters written ("unknown processor" when it cannot be read).
auto processor_name(char* text,
                    unsigned long size) noexcept -> unsigned long;

/// @brief Writes the operating system's name and version into @p text, as processor_name() does.
auto operating_system(char* text,
                      unsigned long size) noexcept -> unsigned long;

/// @brief Writes the active power scheme's name into @p text (Windows; "unknown" elsewhere), as processor_name() does.
auto power_scheme(char* text,
                  unsigned long size) noexcept -> unsigned long;

} // namespace gfx::benchmark::environment
