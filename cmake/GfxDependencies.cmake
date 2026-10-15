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

# ------------------------------------------------------------------------------
# Vulkan and SPIR-V dependencies.
# Each one is looked for with find_package (CONFIG) first and fetched otherwise; configure with
# -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER to build every one of them from source.
# ------------------------------------------------------------------------------

# Release of the Vulkan SDK the fetched Khronos repositories (and volk) are taken from.
set(GFX_VULKAN_SDK_TAG "vulkan-sdk-1.4.350.0")

# _gfx_report_dependency(<package> <description>)
#   Tells whether FetchContent_MakeAvailable(<package>) found <package> on the system or built it.
function(_gfx_report_dependency package description)
    if(${package}_FOUND)
        message(STATUS "gfx: ${description} found on the system (${${package}_DIR})")
    else()
        message(STATUS "gfx: ${description} built from source (${GFX_VULKAN_SDK_TAG})")
    endif()
endfunction()

# _gfx_require_targets(<description> <target>...)
#   Stops configuration with a descriptive error when a dependency did not provide a target.
function(_gfx_require_targets description)
    foreach(_gfx_target IN LISTS ARGN)
        if(NOT TARGET ${_gfx_target})
            message(FATAL_ERROR "gfx: ${description} did not provide the target ${_gfx_target}.")
        endif()
    endforeach()
endfunction()

# _gfx_exclude_from_modules(<target>...)
#   Third-party C++ code built here: keep it out of module scanning and of the std module build.
function(_gfx_exclude_from_modules)
    foreach(_gfx_target IN LISTS ARGN)
        if(TARGET ${_gfx_target})
            get_target_property(_gfx_type ${_gfx_target} TYPE)
            get_target_property(_gfx_imported ${_gfx_target} IMPORTED)
            if(NOT _gfx_imported AND NOT _gfx_type STREQUAL "INTERFACE_LIBRARY")
                set_target_properties(${_gfx_target} PROPERTIES CXX_SCAN_FOR_MODULES OFF CXX_MODULE_STD OFF)
            endif()
        endif()
    endforeach()
endfunction()

# gfx_require_vulkan_headers()
#   Provides Vulkan::Headers. Nothing links the Vulkan loader: volk loads it at run time.
function(gfx_require_vulkan_headers)
    if(TARGET Vulkan::Headers)
        return()
    endif()
    # gfx links its dependencies privately; consumers resolve their names from other directories.
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    set(VULKAN_HEADERS_ENABLE_MODULE OFF)
    set(VULKAN_HEADERS_ENABLE_TESTS OFF)
    set(VULKAN_HEADERS_ENABLE_INSTALL OFF)
    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        ${GFX_VULKAN_SDK_TAG}
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS CONFIG)
    FetchContent_MakeAvailable(VulkanHeaders)
    _gfx_report_dependency(VulkanHeaders "Vulkan headers")
    _gfx_require_targets("Vulkan-Headers" Vulkan::Headers)
endfunction()

# gfx_require_volk()
#   Provides gfx::volk: the Vulkan meta-loader. It loads the Vulkan loader (vulkan-1.dll,
#   libvulkan.so.1) at run time, so applications start on machines without Vulkan and the factory
#   reports the backend as unavailable instead.
function(gfx_require_volk)
    if(TARGET gfx::volk)
        return()
    endif()
    gfx_require_vulkan_headers()
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    set(VOLK_PULL_IN_VULKAN OFF)
    set(VOLK_INSTALL OFF)
    FetchContent_Declare(volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG        ${GFX_VULKAN_SDK_TAG}
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS CONFIG)
    FetchContent_MakeAvailable(volk)
    _gfx_report_dependency(volk "volk")
    _gfx_require_targets("volk" volk::volk)
    if(NOT volk_FOUND)
        # Built here without VOLK_PULL_IN_VULKAN: give it the headers found above.
        target_link_libraries(volk PUBLIC Vulkan::Headers)
        set_target_properties(volk PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    add_library(gfx_volk INTERFACE)
    add_library(gfx::volk ALIAS gfx_volk)
    target_link_libraries(gfx_volk INTERFACE volk::volk Vulkan::Headers)
endfunction()

# gfx_require_vulkan_memory_allocator()
#   Provides gfx::vulkan_memory_allocator (header only: one gfx translation unit compiles its
#   implementation). MSYS2 and some distributions install the header without a CMake package.
function(gfx_require_vulkan_memory_allocator)
    if(TARGET gfx::vulkan_memory_allocator)
        return()
    endif()
    gfx_require_vulkan_headers()
    add_library(gfx_vulkan_memory_allocator INTERFACE)
    add_library(gfx::vulkan_memory_allocator ALIAS gfx_vulkan_memory_allocator)
    target_link_libraries(gfx_vulkan_memory_allocator INTERFACE Vulkan::Headers)

    # The header alone, as MSYS2 installs it (include/vma/vk_mem_alloc.h), usually beside the
    # Vulkan headers.
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        get_target_property(_gfx_vulkan_include_directories Vulkan::Headers INTERFACE_INCLUDE_DIRECTORIES)
        find_path(GFX_VULKAN_MEMORY_ALLOCATOR_INCLUDE_DIRECTORY vk_mem_alloc.h
            HINTS ${_gfx_vulkan_include_directories}
            PATH_SUFFIXES vma)
        if(GFX_VULKAN_MEMORY_ALLOCATOR_INCLUDE_DIRECTORY)
            message(STATUS "gfx: Vulkan Memory Allocator found on the system (${GFX_VULKAN_MEMORY_ALLOCATOR_INCLUDE_DIRECTORY})")
            target_include_directories(gfx_vulkan_memory_allocator SYSTEM INTERFACE
                ${GFX_VULKAN_MEMORY_ALLOCATOR_INCLUDE_DIRECTORY})
            return()
        endif()
    endif()

    # Otherwise its package, or its sources.
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    set(VMA_ENABLE_INSTALL OFF)
    FetchContent_Declare(VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS CONFIG)
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    if(VulkanMemoryAllocator_FOUND)
        message(STATUS "gfx: Vulkan Memory Allocator found on the system (${VulkanMemoryAllocator_DIR})")
    else()
        message(STATUS "gfx: Vulkan Memory Allocator v3.4.0 built from source")
    endif()
    _gfx_require_targets("Vulkan Memory Allocator" GPUOpen::VulkanMemoryAllocator)
    target_link_libraries(gfx_vulkan_memory_allocator INTERFACE GPUOpen::VulkanMemoryAllocator)
endfunction()

# gfx_require_renderdoc_api()
#   Provides gfx::renderdoc_api: RenderDoc's in-application API header (renderdoc_app.h, MIT license),
#   with which the Vulkan backend starts and ends captures when RenderDoc is injected into the process.
function(gfx_require_renderdoc_api)
    if(TARGET gfx::renderdoc_api)
        return()
    endif()
    FetchContent_Declare(renderdoc_api
        URL https://raw.githubusercontent.com/baldurk/renderdoc/v1.45/renderdoc/api/app/renderdoc_app.h
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(renderdoc_api)
    message(STATUS "gfx: RenderDoc in-application API v1.45 fetched")
    add_library(gfx_renderdoc_api INTERFACE)
    add_library(gfx::renderdoc_api ALIAS gfx_renderdoc_api)
    target_include_directories(gfx_renderdoc_api SYSTEM INTERFACE ${renderdoc_api_SOURCE_DIR})
endfunction()

# gfx_require_spirv_cross()
#   Provides gfx::spirv_cross: the SPIRV-Cross C API, which reflects shader layouts from SPIR-V.
function(gfx_require_spirv_cross)
    if(TARGET gfx::spirv_cross)
        return()
    endif()
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

    # The C API's package names the libraries of its back ends without finding their packages.
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        foreach(_gfx_component IN ITEMS core glsl hlsl msl cpp reflect)
            find_package(spirv_cross_${_gfx_component} CONFIG QUIET)
        endforeach()
    endif()

    # Built from source: the C API with the GLSL back end only (reflection needs nothing more).
    set(SPIRV_CROSS_CLI OFF)
    set(SPIRV_CROSS_ENABLE_TESTS OFF)
    set(SPIRV_CROSS_SKIP_INSTALL ON)
    set(SPIRV_CROSS_SHARED OFF)
    set(SPIRV_CROSS_STATIC ON)
    set(SPIRV_CROSS_ENABLE_C_API ON)
    set(SPIRV_CROSS_ENABLE_GLSL ON)
    set(SPIRV_CROSS_ENABLE_HLSL OFF)
    set(SPIRV_CROSS_ENABLE_MSL OFF)
    set(SPIRV_CROSS_ENABLE_CPP OFF)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF)
    set(SPIRV_CROSS_ENABLE_UTIL OFF)
    FetchContent_Declare(spirv_cross_c
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG        ${GFX_VULKAN_SDK_TAG}
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS CONFIG)
    FetchContent_MakeAvailable(spirv_cross_c)
    _gfx_report_dependency(spirv_cross_c "SPIRV-Cross")
    _gfx_require_targets("SPIRV-Cross" spirv-cross-c)
    _gfx_exclude_from_modules(spirv-cross-c spirv-cross-core spirv-cross-glsl)

    add_library(gfx_spirv_cross INTERFACE)
    add_library(gfx::spirv_cross ALIAS gfx_spirv_cross)
    target_link_libraries(gfx_spirv_cross INTERFACE spirv-cross-c)
endfunction()

# gfx_require_glslang()
#   Provides gfx::glslang: glslang's C API, which compiles GLSL and HLSL to SPIR-V at run time
#   (Debug builds only: Release builds load SPIR-V compiled at build time).
function(gfx_require_glslang)
    if(TARGET gfx::glslang)
        return()
    endif()
    set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_OPT OFF)
    set(ENABLE_HLSL ON)
    set(ENABLE_GLSLANG_BINARIES ON)
    set(ENABLE_SPVREMAPPER OFF)
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_ENABLE_INSTALL OFF)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        ${GFX_VULKAN_SDK_TAG}
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS CONFIG)
    FetchContent_MakeAvailable(glslang)
    _gfx_report_dependency(glslang "glslang")
    _gfx_require_targets("glslang" glslang::glslang glslang::glslang-default-resource-limits)
    if(NOT glslang_FOUND)
        _gfx_exclude_from_modules(glslang glslang-default-resource-limits OSDependent GenericCodeGen
                                  MachineIndependent SPIRV glslang-standalone)
    endif()

    add_library(gfx_glslang INTERFACE)
    add_library(gfx::glslang ALIAS gfx_glslang)
    target_link_libraries(gfx_glslang INTERFACE glslang::glslang glslang::glslang-default-resource-limits)
endfunction()
