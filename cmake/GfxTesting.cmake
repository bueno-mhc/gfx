# ==============================================================================
# Test infrastructure: GoogleTest executables registered with CTest.
# Every discovered test case is its own CTest test, so `ctest --parallel` runs them concurrently.
# ==============================================================================
include_guard(GLOBAL)

enable_testing()
include(GoogleTest)
include(GfxDependencies)
include(GfxRuntimeDeployment)
gfx_require_googletest()

# gfx_add_test(<name> SOURCES <file>... [MODULES <file>...] [LIBRARIES <target>...])
#   SOURCES   : regular translation units (may `#include <gtest/gtest.h>` and `import` modules)
#   MODULES   : test-only C++ module interface units
#   LIBRARIES : additional targets to link
function(gfx_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 argument "" "" "SOURCES;MODULES;LIBRARIES")
    add_executable(${name})
    target_sources(${name} PRIVATE ${argument_SOURCES})
    if(argument_MODULES)
        target_sources(${name} PRIVATE FILE_SET CXX_MODULES FILES ${argument_MODULES})
    endif()
    target_link_libraries(${name} PRIVATE gfx::project_options GTest::gtest_main ${argument_LIBRARIES})
    gfx_deploy_runtime_dependencies(${name})
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()
