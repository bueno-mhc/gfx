// The machine a benchmark runs on (environment.hpp): cpuid's brand string, the output of `ver` (or `uname -sr`) and of
// `powercfg /getactivescheme`, trimmed.
#include "environment.hpp"

#include <cstdio>
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace gfx::benchmark::environment
{

namespace
{

/// Copies @p source (@p length characters) into @p text without leading and trailing white space; the characters written.
auto
copy_trimmed(const char* source,
             unsigned long length,
             char* text,
             unsigned long size) noexcept -> unsigned long
{
    if (size == 0)
    {
        return 0;
    }
    unsigned long first = 0;
    while (first < length && (source[first] == ' ' || source[first] == '\r' || source[first] == '\n' || source[first] == '\t'))
    {
        ++first;
    }
    unsigned long last = length;
    while (last > first && (source[last - 1] == ' ' || source[last - 1] == '\r' || source[last - 1] == '\n' || source[last - 1] == '\t' || source[last - 1] == '\0'))
    {
        --last;
    }
    unsigned long count = last - first < size - 1 ? last - first : size - 1;
    std::memcpy(text, source + first, count);
    text[count] = '\0';
    return count;
}

/// The output of @p command (at most @p size - 1 characters) in @p output; the characters read.
auto
run(const char* command,
    char* output,
    unsigned long size) noexcept -> unsigned long
{
#ifdef _WIN32
    std::FILE* pipe = _popen(command, "r");
#else
    std::FILE* pipe = popen(command, "r");
#endif
    if (pipe == nullptr)
    {
        return 0;
    }
    unsigned long length = 0;
    while (length + 1 < size)
    {
        const std::size_t read = std::fread(output + length, 1, size - 1 - length, pipe);
        if (read == 0)
        {
            break;
        }
        length += static_cast<unsigned long>(read);
    }
    output[length] = '\0';
#ifdef _WIN32
    (void)_pclose(pipe);
#else
    (void)pclose(pipe);
#endif
    return length;
}

auto
unknown(const char* what,
        char* text,
        unsigned long size) noexcept -> unsigned long
{
    return copy_trimmed(what, static_cast<unsigned long>(std::strlen(what)), text, size);
}

} // namespace

auto
processor_name(char* text,
               unsigned long size) noexcept -> unsigned long
{
#if defined(__x86_64__) || defined(__i386__)
    unsigned int registers[12]{};
    if (__get_cpuid(0x80000000U, &registers[0], &registers[1], &registers[2], &registers[3]) != 0 && registers[0] >= 0x80000004U)
    {
        for (unsigned int leaf = 0; leaf < 3; ++leaf)
        {
            unsigned int* part = registers + 4 * leaf;
            (void)__get_cpuid(0x80000002U + leaf, &part[0], &part[1], &part[2], &part[3]);
        }
        char brand[49]{};
        std::memcpy(brand, registers, 48);
        return copy_trimmed(brand, static_cast<unsigned long>(std::strlen(brand)), text, size);
    }
#endif
    return unknown("unknown processor", text, size);
}

auto
operating_system(char* text,
                 unsigned long size) noexcept -> unsigned long
{
    char output[512]{};
#ifdef _WIN32
    const unsigned long length = run("ver", output, sizeof(output));
#else
    const unsigned long length = run("uname -sr", output, sizeof(output));
#endif
    return length != 0 ? copy_trimmed(output, length, text, size) : unknown("unknown operating system", text, size);
}

auto
power_scheme(char* text,
             unsigned long size) noexcept -> unsigned long
{
#ifdef _WIN32
    // "Power Scheme GUID: <guid>  (<name>)", in the system's language: the name between the last parentheses.
    char output[512]{};
    const unsigned long length = run("powercfg /getactivescheme", output, sizeof(output));
    const char* open = std::strrchr(output, '(');
    const char* close = open != nullptr ? std::strchr(open, ')') : nullptr;
    if (length != 0 && open != nullptr && close != nullptr && close > open + 1)
    {
        return copy_trimmed(open + 1, static_cast<unsigned long>(close - open - 1), text, size);
    }
#endif
    return unknown("unknown", text, size);
}

} // namespace gfx::benchmark::environment
