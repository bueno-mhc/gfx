// The reference of ProfilerCompileOut.MacrosLeaveNoCodeAndNoData: with_macros.cpp without its PROFILE_* macros.
extern "C" int
profiler_compile_out_step(int value)
{
    const int result = value * 3 + 1;
    return result;
}

struct ProfilerCompileOutRecorder
{
    void dispatch(unsigned x, unsigned y, unsigned z); // never defined
};

extern "C" void
profiler_compile_out_dispatch(ProfilerCompileOutRecorder& recorder,
                              unsigned groups)
{
    recorder.dispatch(groups, 1, 1);
}
