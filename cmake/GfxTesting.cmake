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

# gfx_add_test(<name> SOURCES <file>... [MODULES <file>...] [MODULE_BASE_DIRECTORY <directory>]
#              [LIBRARIES <target>...])
#   SOURCES               : regular translation units (may `#include <gtest/gtest.h>` and `import` modules)
#   MODULES               : test-only C++ module interface units, compiled into this executable
#   MODULE_BASE_DIRECTORY : a directory above the current one that MODULES may come from (test modules
#                           shared by sibling test folders; each executable builds its own copy, as
#                           module libraries shared between test targets mix `import std` builds)
#   LIBRARIES             : additional targets to link
function(gfx_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 argument "" "MODULE_BASE_DIRECTORY" "SOURCES;MODULES;LIBRARIES")
    add_executable(${name})
    target_sources(${name} PRIVATE ${argument_SOURCES})
    if(argument_MODULES)
        if(argument_MODULE_BASE_DIRECTORY)
            target_sources(${name} PRIVATE FILE_SET CXX_MODULES
                BASE_DIRS ${argument_MODULE_BASE_DIRECTORY}
                FILES ${argument_MODULES})
        else()
            target_sources(${name} PRIVATE FILE_SET CXX_MODULES FILES ${argument_MODULES})
        endif()
    endif()
    target_link_libraries(${name} PRIVATE gfx::project_options GTest::gtest_main ${argument_LIBRARIES})
    # Runs before the discovery step below (POST_BUILD commands run in the order they are added).
    gfx_deploy_runtime_dependencies(${name})
    # POST_BUILD discovery registers every test case as a direct call of the test executable.
    # (PRE_TEST discovery wraps each call in a `cmake -P` launcher script, which debuggers such as
    # CMake Tools' "Debug Test" would attach to instead of the test.)
    gtest_discover_tests(${name} DISCOVERY_MODE POST_BUILD)
endfunction()

# gfx_add_compilation_failure_test(<name> SOURCES <file>... EXPECTED_DIAGNOSTIC <regex>
#                                  [MODULES <module interface>...] [LIBRARIES <target>...]
#                                  [WITHOUT_PROJECT_OPTIONS])
#   Registers CTest test CompilationFailure.<name>, which passes only if building SOURCES fails with
#   a diagnostic matching EXPECTED_DIAGNOSTIC (so code that should be rejected by the type system
#   stays rejected, and for the intended reason). The target is excluded from the default build.
#   WITHOUT_PROJECT_OPTIONS builds without warnings and toolchain workarounds; toolchain-defect
#   detectors use it to prove a workaround is still needed.
function(gfx_add_compilation_failure_test name)
    cmake_parse_arguments(PARSE_ARGV 1 argument "WITHOUT_PROJECT_OPTIONS" "EXPECTED_DIAGNOSTIC" "SOURCES;MODULES;LIBRARIES")
    set(target gfx_compilation_failure_${name})
    add_executable(${target} EXCLUDE_FROM_ALL ${argument_SOURCES})
    if(argument_MODULES)
        target_sources(${target} PRIVATE FILE_SET CXX_MODULES FILES ${argument_MODULES})
    endif()
    if(NOT argument_WITHOUT_PROJECT_OPTIONS)
        target_link_libraries(${target} PRIVATE gfx::project_options)
    endif()
    target_link_libraries(${target} PRIVATE ${argument_LIBRARIES})
    add_test(NAME CompilationFailure.${name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target})
    set_tests_properties(CompilationFailure.${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${argument_EXPECTED_DIAGNOSTIC}"
        # Ninja cannot run twice at once in one build tree: these tests run one at a time,
        # while the GoogleTest cases keep running in parallel around them.
        RESOURCE_LOCK gfx_build_tree
        LABELS compilation)
endfunction()

# gfx_add_platform_macro_collision_test(<name> SOURCE <file>)
#   Registers CTest test CompilationCheck.<name>, which fails if an enumerator or constant declared
#   in SOURCE has the name of a macro defined by common platform headers (windows.h, math.h, ...).
function(gfx_add_platform_macro_collision_test name)
    cmake_parse_arguments(PARSE_ARGV 1 argument "" "SOURCE" "")
    add_test(NAME CompilationCheck.${name}
             COMMAND ${CMAKE_COMMAND}
                     -DCOMPILER=${CMAKE_CXX_COMPILER}
                     -DSOURCE=${argument_SOURCE}
                     -DWORKING_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/${name}
                     -P ${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckPlatformMacroCollisions.cmake)
    set_tests_properties(CompilationCheck.${name} PROPERTIES LABELS compilation)
endfunction()
