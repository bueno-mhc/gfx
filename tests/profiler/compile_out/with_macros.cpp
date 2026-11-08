// Compiled with GFX_PROFILER_ENABLED=0 by ProfilerCompileOut.MacrosLeaveNoCodeAndNoData: the object must hold the same
// instructions as without_macros.cpp, no string of the macros' names, and no reference to the function their arguments
// call (arguments are not evaluated when the profiler is compiled out).
#include <gfx/profiler.hpp>

extern "C" int profiler_compile_out_argument(int value); // never defined: referring to it would not link

extern "C" int
profiler_compile_out_step(int value)
{
    PROFILE_FUNCTION();
    PROFILE_SCOPE("profiler_compile_out_scope");
    PROFILE_SCOPE_CATEGORY("profiler_compile_out_category", ::gfx::profiler::ProfilerCategory::PHYSICS);
    PROFILE_NAMED_SCOPE(early, "profiler_compile_out_named");
    const int result = value * 3 + 1;
    early.end();
    PROFILE_COUNTER("profiler_compile_out_counter", profiler_compile_out_argument(result));
    PROFILE_COUNTER_FLOAT("profiler_compile_out_float", profiler_compile_out_argument(result) * 0.5);
    PROFILE_MEMORY("profiler_compile_out_memory", profiler_compile_out_argument(result));
    PROFILE_MARKER("profiler_compile_out_marker");
    PROFILE_ASYNC_BEGIN("profiler_compile_out_async", profiler_compile_out_argument(result));
    PROFILE_ASYNC_END("profiler_compile_out_async", profiler_compile_out_argument(result));
    return result;
}
