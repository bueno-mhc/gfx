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

# gfx_require_stb_image()
#   Provides gfx::stb_image: stb_image.h (public domain / MIT), with which the platform library decodes
#   PNG window icons and cursors. The system header (MSYS2: mingw-w64-ucrt-x86_64-stb) is used when
#   present; otherwise version 2.30 is downloaded from a pinned commit and checked against its hash.
function(gfx_require_stb_image)
    if(TARGET gfx::stb_image)
        return()
    endif()
    add_library(gfx_stb_image INTERFACE)
    add_library(gfx::stb_image ALIAS gfx_stb_image)
    find_path(GFX_STB_IMAGE_INCLUDE_DIRECTORY stb_image.h PATH_SUFFIXES stb)
    if(GFX_STB_IMAGE_INCLUDE_DIRECTORY AND NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        message(STATUS "gfx: stb_image found in ${GFX_STB_IMAGE_INCLUDE_DIRECTORY}")
        target_include_directories(gfx_stb_image SYSTEM INTERFACE ${GFX_STB_IMAGE_INCLUDE_DIRECTORY})
        return()
    endif()
    FetchContent_Declare(stb_image
        URL https://raw.githubusercontent.com/nothings/stb/f1c79c02822848a9bed4315b12c8c8f3761e1296/stb_image.h
        URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(stb_image)
    message(STATUS "gfx: stb_image 2.30 fetched")
    target_include_directories(gfx_stb_image SYSTEM INTERFACE ${stb_image_SOURCE_DIR})
endfunction()

# gfx_require_sdl3()
#   Provides gfx::sdl3: SDL 3 (zlib license), the SDL3 platform backend's windowing and input library,
#   linked statically. An installed SDL3 3.4 or newer is used when present (its static library when
#   available); otherwise release 3.4.10 is downloaded, checked against its hash and built with only the
#   subsystems the backend uses (video, events, joysticks and gamepads, HIDAPI, haptics, sensors): no
#   audio, renderer, GPU API, camera, dialog or tray.
function(gfx_require_sdl3)
    if(TARGET gfx::sdl3)
        return()
    endif()
    add_library(gfx_sdl3 INTERFACE)
    add_library(gfx::sdl3 ALIAS gfx_sdl3)
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        find_package(SDL3 3.4 CONFIG QUIET)
    endif()
    if(TARGET SDL3::SDL3-static)
        message(STATUS "gfx: SDL3 ${SDL3_VERSION} found (static library)")
        target_link_libraries(gfx_sdl3 INTERFACE SDL3::SDL3-static)
        return()
    elseif(TARGET SDL3::SDL3)
        message(STATUS "gfx: SDL3 ${SDL3_VERSION} found (shared library)")
        target_link_libraries(gfx_sdl3 INTERFACE SDL3::SDL3)
        return()
    endif()

    # Built from source: a static library with the subsystems the backend uses.
    foreach(_gfx_option IN ITEMS SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL
                                 SDL_AUDIO SDL_GPU SDL_RENDER SDL_CAMERA SDL_DIALOG SDL_TRAY SDL_POWER
                                 SDL_OPENGL SDL_OPENGLES)
        set(${_gfx_option} OFF CACHE BOOL "Set by gfx_require_sdl3()" FORCE)
    endforeach()
    foreach(_gfx_option IN ITEMS SDL_STATIC SDL_VIDEO SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_SENSOR SDL_VULKAN
                                 SDL_VIRTUAL_JOYSTICK)
        set(${_gfx_option} ON CACHE BOOL "Set by gfx_require_sdl3()" FORCE)
    endforeach()
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz
        URL_HASH SHA256=12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
    _gfx_exclude_from_modules(SDL3-static SDL_uclibc)
    message(STATUS "gfx: SDL3 3.4.10 fetched and built as a static library")
    target_link_libraries(gfx_sdl3 INTERFACE SDL3::SDL3-static)
endfunction()

# gfx_require_glfw()
#   Provides gfx::glfw: GLFW 3.4 (zlib license), the GLFW platform backend's windowing and input
#   library, linked statically. An installed GLFW 3.4 is used when present; otherwise release 3.4 is
#   downloaded, checked against its hash and built static (no examples, tests, documentation or
#   installation).
function(gfx_require_glfw)
    if(TARGET gfx::glfw)
        return()
    endif()
    add_library(gfx_glfw INTERFACE)
    add_library(gfx::glfw ALIAS gfx_glfw)
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        find_package(glfw3 3.4 CONFIG QUIET)
    endif()
    if(TARGET glfw)
        get_target_property(_gfx_imported glfw IMPORTED)
        if(_gfx_imported)
            message(STATUS "gfx: GLFW ${glfw3_VERSION} found")
            target_link_libraries(gfx_glfw INTERFACE glfw)
            return()
        endif()
    endif()

    # Built from source: a static library without the extras.
    foreach(_gfx_option IN ITEMS GLFW_BUILD_EXAMPLES GLFW_BUILD_TESTS GLFW_BUILD_DOCS GLFW_INSTALL)
        set(${_gfx_option} OFF CACHE BOOL "Set by gfx_require_glfw()" FORCE)
    endforeach()
    FetchContent_Declare(glfw3
        URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
        URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
        SYSTEM)
    FetchContent_MakeAvailable(glfw3)
    message(STATUS "gfx: GLFW 3.4 fetched and built as a static library")
    target_link_libraries(gfx_glfw INTERFACE glfw)
endfunction()

# gfx_require_nlohmann_json()
#   Provides gfx::nlohmann_json: nlohmann/json (MIT), whose SAX parser reads saved input bindings. The
#   installed package (MSYS2: mingw-w64-ucrt-x86_64-nlohmann-json) is used when present; otherwise the
#   single header of release 3.12.0 is downloaded and checked against its hash.
function(gfx_require_nlohmann_json)
    if(TARGET gfx::nlohmann_json)
        return()
    endif()
    add_library(gfx_nlohmann_json INTERFACE)
    add_library(gfx::nlohmann_json ALIAS gfx_nlohmann_json)
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        find_package(nlohmann_json 3.11 CONFIG QUIET)
    endif()
    if(TARGET nlohmann_json::nlohmann_json)
        message(STATUS "gfx: nlohmann_json ${nlohmann_json_VERSION} found")
        target_link_libraries(gfx_nlohmann_json INTERFACE nlohmann_json::nlohmann_json)
        return()
    endif()
    FetchContent_Declare(nlohmann_json_header
        URL https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
        URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
        DOWNLOAD_NO_EXTRACT TRUE
        SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/nlohmann_json_header/nlohmann)
    FetchContent_MakeAvailable(nlohmann_json_header)
    message(STATUS "gfx: nlohmann_json 3.12.0 fetched")
    target_include_directories(gfx_nlohmann_json SYSTEM INTERFACE ${CMAKE_BINARY_DIR}/_deps/nlohmann_json_header)
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
