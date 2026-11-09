# ==============================================================================
# Benchmarks: programs measuring the libraries' speed (docs/profiler-performance-design.md, PF3). With the tests, each
# registers a CTest test with the label "benchmark" that runs only when GFX_RUN_BENCHMARKS=1 is in the environment
# (skipped otherwise: they take minutes), and a short smoke run (label "example") in the default runs.
# Measure with the profile build: python build.py --profile, then
#   GFX_RUN_BENCHMARKS=1 ctest --test-dir build/profile -L benchmark --output-on-failure
# ==============================================================================
include(GfxRuntimeDeployment)

# The physics benchmark: every scene on Box3D and the GPU backend (and the reference backend when asked).
if(TARGET gfx_physics)
    add_subdirectory(physics)
endif()
