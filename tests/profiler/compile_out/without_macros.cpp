// The reference of ProfilerCompileOut.MacrosLeaveNoCodeAndNoData: with_macros.cpp without its PROFILE_* macros.
extern "C" int
profiler_compile_out_step(int value)
{
    const int result = value * 3 + 1;
    return result;
}
