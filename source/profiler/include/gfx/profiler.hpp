// The gfx profiler's macros (docs/profiler-performance-design.md §A2): instrument any code of any library.
//
//   #include <gfx/profiler.hpp>   // in a module unit: in its global module fragment
//   import gfx.profiler;           // when GFX_PROFILER_ENABLED is 1 (the macros name its types)
//
//   void step()
//   {
//       PROFILE_FUNCTION();                                    // a scope named after the function
//       PROFILE_SCOPE("broad phase");                          // a scope until the end of the block
//       PROFILE_SCOPE_CATEGORY("solve", gfx::profiler::ProfilerCategory::PHYSICS);
//       PROFILE_NAMED_SCOPE(upload, "upload");  upload.end();  // a scope ended early
//       PROFILE_COUNTER("contacts", contact_count);            // an integer sample
//       PROFILE_COUNTER_FLOAT("error", error);                 // a floating-point sample
//       PROFILE_MEMORY("staging", bytes);                      // a size in bytes
//       PROFILE_MARKER("rebuilt");                             // an instant
//       PROFILE_ASYNC_BEGIN("submission", token);              // a span ending on any thread, in any frame
//       PROFILE_ASYNC_END("submission", token);
//   }
//
// Each macro declares a `static constinit` gfx::profiler::ProfileSite (its name, category, file and line, made at
// compile time) and records through it. With GFX_PROFILER_ENABLED 0 (the option GFX_PROFILER off), every macro expands
// to nothing: no code, no data, and its arguments are not evaluated. Names must be string literals.
//
// The header includes nothing and defines only macros (and, when disabled, the empty type PROFILE_NAMED_SCOPE needs).
#pragma once

#ifndef GFX_PROFILER_ENABLED
#define GFX_PROFILER_ENABLED 0
#endif

#define GFX_PROFILE_JOIN_IMPLEMENTATION(first, second) first##second
#define GFX_PROFILE_JOIN(first, second) GFX_PROFILE_JOIN_IMPLEMENTATION(first, second)
#define GFX_PROFILE_SITE_NAME(unique) GFX_PROFILE_JOIN(gfx_profile_site_, unique)

#if GFX_PROFILER_ENABLED

// A site and a scope sharing one unique number.
#define GFX_PROFILE_SCOPE_AT(unique, name, category)                                                                        \
    static constinit const ::gfx::profiler::ProfileSite GFX_PROFILE_SITE_NAME(unique){name, category};                      \
    const ::gfx::profiler::ProfileScope GFX_PROFILE_JOIN(gfx_profile_scope_, unique){GFX_PROFILE_SITE_NAME(unique)}

#define GFX_PROFILE_FUNCTION_AT(unique)                                                                                     \
    static constinit const ::gfx::profiler::ProfileSite GFX_PROFILE_SITE_NAME(unique) = ::gfx::profiler::ProfileSite::function(); \
    const ::gfx::profiler::ProfileScope GFX_PROFILE_JOIN(gfx_profile_scope_, unique){GFX_PROFILE_SITE_NAME(unique)}

#define GFX_PROFILE_NAMED_SCOPE_AT(unique, variable, name)                                                                  \
    static constinit const ::gfx::profiler::ProfileSite GFX_PROFILE_SITE_NAME(unique){name};                                \
    ::gfx::profiler::ProfileScope variable{GFX_PROFILE_SITE_NAME(unique)}

#define GFX_PROFILE_RECORD_AT(unique, name, call)                                                                           \
    do                                                                                                                      \
    {                                                                                                                       \
        static constinit const ::gfx::profiler::ProfileSite GFX_PROFILE_SITE_NAME(unique){name};                            \
        call;                                                                                                               \
    } while (false)

#define PROFILE_SCOPE_CATEGORY(name, category) GFX_PROFILE_SCOPE_AT(__COUNTER__, name, category)
#define PROFILE_SCOPE(name) PROFILE_SCOPE_CATEGORY(name, ::gfx::profiler::ProfilerCategory::GENERAL)
#define PROFILE_FUNCTION() GFX_PROFILE_FUNCTION_AT(__COUNTER__)
#define PROFILE_NAMED_SCOPE(variable, name) GFX_PROFILE_NAMED_SCOPE_AT(__COUNTER__, variable, name)

#define GFX_PROFILE_COUNTER_AT(unique, name, value)                                                                         \
    GFX_PROFILE_RECORD_AT(unique, name, ::gfx::profiler::record_counter(GFX_PROFILE_SITE_NAME(unique), static_cast<long long>(value)))
#define GFX_PROFILE_COUNTER_FLOAT_AT(unique, name, value)                                                                   \
    GFX_PROFILE_RECORD_AT(unique, name, ::gfx::profiler::record_counter_float(GFX_PROFILE_SITE_NAME(unique), static_cast<double>(value)))
#define GFX_PROFILE_MEMORY_AT(unique, name, bytes)                                                                          \
    GFX_PROFILE_RECORD_AT(unique, name, ::gfx::profiler::record_memory(GFX_PROFILE_SITE_NAME(unique), static_cast<unsigned long long>(bytes)))
#define GFX_PROFILE_MARKER_AT(unique, name)                                                                                 \
    GFX_PROFILE_RECORD_AT(unique, name, ::gfx::profiler::record_marker(GFX_PROFILE_SITE_NAME(unique)))
#define GFX_PROFILE_ASYNC_BEGIN_AT(unique, name, token)                                                                     \
    GFX_PROFILE_RECORD_AT(unique, name, ::gfx::profiler::record_async_begin(GFX_PROFILE_SITE_NAME(unique), static_cast<unsigned long long>(token)))
#define GFX_PROFILE_ASYNC_END_AT(unique, name, token)                                                                       \
    GFX_PROFILE_RECORD_AT(unique, name, ::gfx::profiler::record_async_end(GFX_PROFILE_SITE_NAME(unique), static_cast<unsigned long long>(token)))

#define PROFILE_COUNTER(name, value) GFX_PROFILE_COUNTER_AT(__COUNTER__, name, value)
#define PROFILE_COUNTER_FLOAT(name, value) GFX_PROFILE_COUNTER_FLOAT_AT(__COUNTER__, name, value)
#define PROFILE_MEMORY(name, bytes) GFX_PROFILE_MEMORY_AT(__COUNTER__, name, bytes)
#define PROFILE_MARKER(name) GFX_PROFILE_MARKER_AT(__COUNTER__, name)
#define PROFILE_ASYNC_BEGIN(name, token) GFX_PROFILE_ASYNC_BEGIN_AT(__COUNTER__, name, token)
#define PROFILE_ASYNC_END(name, token) GFX_PROFILE_ASYNC_END_AT(__COUNTER__, name, token)

#else

namespace gfx::profiler
{
// What PROFILE_NAMED_SCOPE declares when the profiler is compiled out: end() does nothing.
struct DisabledProfileScope
{
    constexpr void end() const noexcept
    {
    }
};
} // namespace gfx::profiler

#define PROFILE_SCOPE_CATEGORY(name, category) ((void)0)
#define PROFILE_SCOPE(name) ((void)0)
#define PROFILE_FUNCTION() ((void)0)
#define PROFILE_NAMED_SCOPE(variable, name) [[maybe_unused]] constexpr ::gfx::profiler::DisabledProfileScope variable{}
#define PROFILE_COUNTER(name, value) ((void)0)
#define PROFILE_COUNTER_FLOAT(name, value) ((void)0)
#define PROFILE_MEMORY(name, bytes) ((void)0)
#define PROFILE_MARKER(name) ((void)0)
#define PROFILE_ASYNC_BEGIN(name, token) ((void)0)
#define PROFILE_ASYNC_END(name, token) ((void)0)

#endif
