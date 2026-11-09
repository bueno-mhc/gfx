# The physics benchmark: gfx_physics_benchmark [--scenes ...] [--backends box3d,gpu_xpbd,reference] [--warm-up 100]
# [--steps 1000] [--threads 16] [--scale 1] [--gpu-device <index>] [--trace-frames 100] [--output <directory>]
# [--check-baseline <directory>] [--record-baseline <directory>] [--sleep] [--note <text>] [--only-when-asked]
# (benchmark.cpp tells what each does). Baselines of the machines that recorded one are kept in baselines/.
add_executable(gfx_physics_benchmark)
target_sources(gfx_physics_benchmark
    PRIVATE
        FILE_SET CXX_MODULES
        FILES scenes.cppm
    PRIVATE
        benchmark.cpp
        environment.cpp)
target_link_libraries(gfx_physics_benchmark PRIVATE gfx::physics gfx::project_options)
if(GFX_PHYSICS_BACKEND_GPU_XPBD)
    target_link_libraries(gfx_physics_benchmark PRIVATE gfx::physics_gpu_xpbd)
    target_compile_definitions(gfx_physics_benchmark PRIVATE GFX_BENCHMARK_GPU_XPBD=1)
else()
    target_compile_definitions(gfx_physics_benchmark PRIVATE GFX_BENCHMARK_GPU_XPBD=0)
endif()
gfx_deploy_runtime_dependencies(gfx_physics_benchmark)

if(GFX_BUILD_TESTS)
    # Every scene at a hundredth of its size for a few steps on every backend built: the harness, the quality checks and
    # the report work (any build; no timing is judged).
    add_test(NAME PhysicsBenchmark.Smoke
             COMMAND gfx_physics_benchmark --scale 0.01 --warm-up 2 --steps 5 --threads 4 --trace-frames 5
                     --backends box3d,gpu_xpbd,reference --output ${CMAKE_CURRENT_BINARY_DIR}/smoke)
    set_tests_properties(PhysicsBenchmark.Smoke PROPERTIES LABELS example)

    # The benchmark itself (only with GFX_RUN_BENCHMARKS=1): every scene on Box3D and the GPU backend, 100 warm-up and
    # 1 000 measured steps, against this machine's baseline when one was recorded with the same settings (a median 5 %
    # above it fails the test). The report: <build>/benchmarks/physics/suite/benchmark_report.md.
    add_test(NAME PhysicsBenchmark.Suite
             COMMAND gfx_physics_benchmark --only-when-asked --check-baseline ${CMAKE_CURRENT_SOURCE_DIR}/baselines
                     --output ${CMAKE_CURRENT_BINARY_DIR}/suite)
    set_tests_properties(PhysicsBenchmark.Suite PROPERTIES LABELS benchmark SKIP_RETURN_CODE 77 TIMEOUT 7200 RUN_SERIAL TRUE)
    if(GFX_PHYSICS_BACKEND_GPU_XPBD)
        gfx_use_gpu_in_tests(PhysicsBenchmark.Smoke PhysicsBenchmark.Suite)
    endif()
endif()
