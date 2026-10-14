# ==============================================================================
# Third-party dependencies.
# Policy: use the package installed on the system when CMake can find it and it is
# usable, otherwise download and build it with FetchContent.
# ==============================================================================
include_guard(GLOBAL)
include(FetchContent)

# Windows application-control policies (Smart App Control is enforced on the development
# machine) refuse to load unsigned third-party DLLs without reputation, such as MSYS2's
# libgtest.dll. Building GoogleTest from source and linking it statically avoids loading them.
if(WIN32)
    set(_gfx_googletest_from_source_default ON)
else()
    set(_gfx_googletest_from_source_default OFF)
endif()
option(GFX_BUILD_GOOGLETEST_FROM_SOURCE
    "Build GoogleTest with FetchContent and link it statically instead of using the system package"
    ${_gfx_googletest_from_source_default})

# gfx_require_googletest()
#   Provides GTest::gtest and GTest::gtest_main.
function(gfx_require_googletest)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(_gfx_find_arguments "")
    if(NOT GFX_BUILD_GOOGLETEST_FROM_SOURCE)
        set(_gfx_find_arguments FIND_PACKAGE_ARGS NAMES GTest CONFIG)
    endif()

    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    TRUE
        ${_gfx_find_arguments})
    FetchContent_MakeAvailable(googletest)

    # FetchContent runs find_package(googletest NAMES GTest ...), so results use the "googletest" prefix.
    if(googletest_FOUND)
        message(STATUS "gfx: GoogleTest ${googletest_VERSION} found on the system (${googletest_DIR})")
    else()
        message(STATUS "gfx: GoogleTest v1.17.0 built from source and linked statically")
        # GoogleTest is plain C++17 code: keep it out of module scanning and the std module build.
        set_target_properties(gtest gtest_main PROPERTIES CXX_SCAN_FOR_MODULES OFF CXX_MODULE_STD OFF)
    endif()
    foreach(_gfx_target IN ITEMS GTest::gtest GTest::gtest_main)
        if(NOT TARGET ${_gfx_target})
            message(FATAL_ERROR "gfx: GoogleTest did not provide the target ${_gfx_target}.")
        endif()
    endforeach()
endfunction()
