# Tests of the Vulkan backend on the real GPUs of the machine. Every test skips when no Vulkan device is
# available; with one, the validation layers are enabled and any validation error fails the test.

# Where the validation layers' manifest lives (MSYS2 installs it in bin/): the tests point
# VK_ADD_LAYER_PATH at it unless the environment already does.
cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _gfx_toolchain_binary_directory)
find_path(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY VkLayer_khronos_validation.json
    HINTS "${_gfx_toolchain_binary_directory}" "${_gfx_toolchain_binary_directory}/../share/vulkan/explicit_layer.d"
          ENV VULKAN_SDK
    PATH_SUFFIXES Bin bin)
if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
    message(STATUS "gfx: Vulkan validation layers for the tests: ${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
else()
    message(STATUS "gfx: Vulkan validation layers not found: the Vulkan tests run without them")
    set(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY "")
endif()

gfx_add_test(gfx_vulkan_tests
    SOURCES
        instance_test.cpp
        device_test.cpp
        synchronization_test.cpp
        resource_test.cpp
        pipeline_test.cpp
        hot_reload_test.cpp
        commands_test.cpp
        ray_tracing_test.cpp
        diagnostics_test.cpp
        frame_resources_test.cpp
        mesh_test.cpp
    LIBRARIES gfx::gfx)
target_compile_definitions(gfx_vulkan_tests PRIVATE
    GFX_TEST_VULKAN_LAYER_DIRECTORY="${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
target_include_directories(gfx_vulkan_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The tests' shaders, compiled at build time (and read from the source tree by the hot reload tests).
gfx_add_shaders(gfx_vulkan_tests
    MODULE gfx.tests.vulkan_shaders
    NAMESPACE gfx::tests::vulkan_shaders
    SHADERS
        shaders/fill.comp
        shaders/classic.comp
        shaders/triangle.vert
        shaders/triangle.frag
        shaders/vertices.vert
        shaders/textured.frag
        shaders/trace.rgen
        shaders/trace.rmiss
        shaders/trace.rchit
        shaders/query.comp
        shaders/accumulate.comp
        shaders/halves.task
        shaders/halves.mesh
        shaders/wide_atomics.comp
    INCLUDE_DIRECTORIES shaders/include)
target_compile_definitions(gfx_vulkan_tests PRIVATE
    GFX_TEST_VULKAN_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    GFX_TEST_WORK_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/work")

# No allocation when commands are recorded and submitted again (operator new counted in this executable, which
# therefore holds no other test).
gfx_add_test(gfx_vulkan_allocation_tests
    SOURCES command_allocation_test.cpp
    LIBRARIES gfx::gfx)
target_compile_definitions(gfx_vulkan_allocation_tests PRIVATE
    GFX_TEST_VULKAN_LAYER_DIRECTORY="${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
target_include_directories(gfx_vulkan_allocation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The same tests on the first physical device too, which may not be the one the tests pick (an integrated GPU, whose
# single hardware queue every queue type then shares: submissions go through the checks of shared hardware queues).
add_test(NAME VulkanAllocation.FirstPhysicalDevice COMMAND gfx_vulkan_allocation_tests)
set_tests_properties(VulkanAllocation.FirstPhysicalDevice PROPERTIES ENVIRONMENT "GFX_TEST_PHYSICAL_DEVICE=0")

# A capture through RenderDoc's in-application API, when RenderDoc is installed: renderdoccmd injects
# RenderDoc into the test process, which captures its own work and checks that the capture was written.
find_program(GFX_RENDERDOC_COMMAND renderdoccmd PATHS "$ENV{ProgramFiles}/RenderDoc" "C:/Program Files/RenderDoc")
if(GFX_RENDERDOC_COMMAND)
    set(_gfx_capture_directory "${CMAKE_CURRENT_BINARY_DIR}/captures")
    add_test(NAME VulkanCapture.CleanCaptures COMMAND ${CMAKE_COMMAND} -E rm -rf "${_gfx_capture_directory}")
    add_test(NAME VulkanCapture.RenderDoc
             COMMAND "${GFX_RENDERDOC_COMMAND}" capture -w -c "${_gfx_capture_directory}/gfx" $<TARGET_FILE:gfx_vulkan_tests>
                     --gtest_filter=VulkanDeviceTest.RenderDocCapturesTheWorkBetweenBeginAndEnd)
    set_tests_properties(VulkanCapture.CleanCaptures PROPERTIES FIXTURES_SETUP gfx_renderdoc_captures)
    set_tests_properties(VulkanCapture.RenderDoc PROPERTIES
        FIXTURES_REQUIRED gfx_renderdoc_captures
        ENVIRONMENT "GFX_TEST_EXPECT_RENDERDOC=1;GFX_TEST_CAPTURE_DIRECTORY=${_gfx_capture_directory}"
        PASS_REGULAR_EXPRESSION "[[]  PASSED  []] 1 test"
        FAIL_REGULAR_EXPRESSION "[[]  FAILED  []]|[[]  SKIPPED []]")
endif()
