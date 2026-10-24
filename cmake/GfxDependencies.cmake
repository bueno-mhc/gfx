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

# Release of the Vulkan SDK the fetched Khronos repositories (and volk) are taken from (a cache variable:
# the include guard is global, so directories including this file later do not run this line).
set(GFX_VULKAN_SDK_TAG "vulkan-sdk-1.4.350.0" CACHE INTERNAL "Vulkan SDK release of the fetched Khronos repositories")

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

# gfx_require_accesskit()
#   Provides gfx::accesskit: AccessKit's C bindings 0.23.1 (static libraries built by the AccessKit project for each
#   platform, in one archive of 67 MB fetched by URL and SHA-256; no package manager has them), which the UI's
#   UiAccessibilityAdapter gives accessibility trees to. Their header is reached by accesskit_bridge.cpp only.
function(gfx_require_accesskit)
    if(TARGET gfx::accesskit)
        return()
    endif()
    include(FetchContent)
    FetchContent_Declare(gfx_accesskit_c
        URL https://github.com/AccessKit/accesskit-c/releases/download/0.23.1/accesskit-c-0.23.1.zip
        URL_HASH SHA256=35b7ca8a6f1e038b5da35e1e9e5a0adaed9bfcf21e1496d29598fbbadcc7043f
        SOURCE_SUBDIR no-cmake-project)
    FetchContent_MakeAvailable(gfx_accesskit_c)
    find_package(accesskit CONFIG REQUIRED PATHS "${gfx_accesskit_c_SOURCE_DIR}" NO_DEFAULT_PATH)
    add_library(gfx_accesskit INTERFACE)
    add_library(gfx::accesskit ALIAS gfx_accesskit)
    target_link_libraries(gfx_accesskit INTERFACE accesskit-static)
    message(STATUS "gfx: AccessKit C bindings 0.23.1 fetched")
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

# ==============================================================================
# The UI library's font pipeline (docs/ui-design.md A5). What the atlas tool uses to produce atlas
# bytes - FreeType, msdfgen, msdf-atlas-gen, HarfBuzz (subsetting) and zstd - is always fetched at a
# pinned version, so that the same font and descriptor give the same .uiatlas on every machine. What
# only the run time uses - SheenBidi, libunibreak - comes from the system when installed.
# ==============================================================================

# gfx_require_brotli_decoder()
#   Provides gfx::brotli_decoder: the decoder of Brotli 1.2.0 (MIT), which FreeType needs to read WOFF2
#   fonts, built as a static library from its C sources (Brotli's own CMake project would switch
#   BUILD_SHARED_LIBS on for the whole build).
function(gfx_require_brotli_decoder)
    if(TARGET gfx::brotli_decoder)
        return()
    endif()
    FetchContent_Declare(gfx_brotli
        URL https://github.com/google/brotli/archive/refs/tags/v1.2.0.tar.gz
        URL_HASH SHA256=816c96e8e8f193b40151dad7e8ff37b1221d019dbcb9c35cd3fadbfe6477dfec
        SOURCE_SUBDIR gfx-sources-only)
    FetchContent_MakeAvailable(gfx_brotli)
    file(GLOB _gfx_brotli_sources ${gfx_brotli_SOURCE_DIR}/c/common/*.c ${gfx_brotli_SOURCE_DIR}/c/dec/*.c)
    add_library(gfx_brotli_decoder STATIC ${_gfx_brotli_sources})
    add_library(gfx::brotli_decoder ALIAS gfx_brotli_decoder)
    target_include_directories(gfx_brotli_decoder SYSTEM PUBLIC ${gfx_brotli_SOURCE_DIR}/c/include)
    message(STATUS "gfx: Brotli 1.2.0 decoder fetched (for WOFF2 fonts)")
endfunction()

# gfx_require_freetype()
#   Provides gfx::freetype (and Freetype::Freetype, which msdfgen expects): FreeType 2.14.3 (FreeType
#   License), the font decoder of the atlas tool: TrueType, OpenType CFF/CFF2, collections, WOFF (with
#   FreeType's own zlib copy), WOFF2 (Brotli) and variable fonts. Always fetched and pinned (reproducible
#   atlases), built static without PNG, BZip2 or HarfBuzz. HarfBuzz is configured first (see
#   gfx_require_harfbuzz()).
function(gfx_require_freetype)
    if(TARGET gfx::freetype)
        return()
    endif()
    gfx_require_harfbuzz()
    gfx_require_brotli_decoder()
    foreach(_gfx_option IN ITEMS FT_DISABLE_HARFBUZZ FT_DISABLE_BZIP2 FT_DISABLE_PNG FT_REQUIRE_BROTLI
                                 FT_ENABLE_ERROR_STRINGS SKIP_INSTALL_ALL)
        set(${_gfx_option} ON CACHE BOOL "Set by gfx_require_freetype()" FORCE)
    endforeach()
    set(FT_DISABLE_BROTLI OFF CACHE BOOL "Set by gfx_require_freetype()" FORCE)
    # FreeType finds Brotli with find_path and find_library: these answers point it at the decoder built here.
    FetchContent_GetProperties(gfx_brotli SOURCE_DIR _gfx_brotli_directory)
    set(BROTLIDEC_INCLUDE_DIRS ${_gfx_brotli_directory}/c/include CACHE PATH "Set by gfx_require_freetype()" FORCE)
    set(BROTLIDEC_LIBRARIES gfx_brotli_decoder CACHE STRING "Set by gfx_require_freetype()" FORCE)
    # The zlib copy inside FreeType rather than the system one: the same code everywhere, no DLL to deploy.
    set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE)
    FetchContent_Declare(freetype
        URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz
        URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
        SYSTEM)
    FetchContent_MakeAvailable(freetype)
    _gfx_require_targets("FreeType" freetype)
    add_library(Freetype::Freetype ALIAS freetype)
    add_library(gfx::freetype ALIAS freetype)
    message(STATUS "gfx: FreeType 2.14.3 fetched and built as a static library (WOFF, WOFF2, variable fonts)")
endfunction()

# gfx_require_msdfgen()
#   Provides gfx::msdfgen (msdfgen 1.13, MIT: distance fields of glyph outlines, loaded through FreeType)
#   and gfx::msdf_atlas_gen (msdf-atlas-gen 1.4, MIT: atlas packing and generation). Always fetched and
#   pinned, built static without vcpkg, Skia, SVG, PNG or the Artery Font format.
function(gfx_require_msdfgen)
    if(TARGET gfx::msdfgen)
        return()
    endif()
    gfx_require_freetype()
    foreach(_gfx_option IN ITEMS MSDFGEN_CORE_ONLY MSDFGEN_BUILD_STANDALONE MSDFGEN_USE_VCPKG MSDFGEN_USE_SKIA
                                 MSDFGEN_USE_OPENMP MSDFGEN_INSTALL MSDF_ATLAS_BUILD_STANDALONE MSDF_ATLAS_USE_VCPKG
                                 MSDF_ATLAS_USE_SKIA MSDF_ATLAS_INSTALL BUILD_SHARED_LIBS)
        set(${_gfx_option} OFF CACHE BOOL "Set by gfx_require_msdfgen()" FORCE)
    endforeach()
    foreach(_gfx_option IN ITEMS MSDFGEN_DISABLE_SVG MSDFGEN_DISABLE_PNG MSDF_ATLAS_NO_ARTERY_FONT MSDF_ATLAS_MSDFGEN_EXTERNAL)
        set(${_gfx_option} ON CACHE BOOL "Set by gfx_require_msdfgen()" FORCE)
    endforeach()
    FetchContent_Declare(msdfgen
        URL https://github.com/Chlumsky/msdfgen/archive/refs/tags/v1.13.tar.gz
        URL_HASH SHA256=93cd1ad8918c1a78c5c96e82d4f4c77f0eb86c2e7e8579a0967e54196c4b7167
        SYSTEM)
    FetchContent_Declare(msdf_atlas_gen
        URL https://github.com/Chlumsky/msdf-atlas-gen/archive/refs/tags/v1.4.tar.gz
        URL_HASH SHA256=57db9548b30905b18640ceab5011144e9d362f33a53748bef6be53dd743c9992
        SYSTEM)
    FetchContent_MakeAvailable(msdfgen msdf_atlas_gen)
    _gfx_require_targets("msdfgen" msdfgen::msdfgen-core msdfgen::msdfgen-ext msdf-atlas-gen::msdf-atlas-gen)
    _gfx_exclude_from_modules(msdfgen-core msdfgen-ext msdf-atlas-gen)
    add_library(gfx_msdfgen INTERFACE)
    add_library(gfx::msdfgen ALIAS gfx_msdfgen)
    target_link_libraries(gfx_msdfgen INTERFACE msdfgen::msdfgen-core msdfgen::msdfgen-ext)
    add_library(gfx_msdf_atlas_gen INTERFACE)
    add_library(gfx::msdf_atlas_gen ALIAS gfx_msdf_atlas_gen)
    target_link_libraries(gfx_msdf_atlas_gen INTERFACE msdf-atlas-gen::msdf-atlas-gen)
    message(STATUS "gfx: msdfgen 1.13 and msdf-atlas-gen 1.4 fetched and built as static libraries")
endfunction()

# gfx_require_harfbuzz()
#   Provides gfx::harfbuzz (text shaping) and gfx::harfbuzz_subset (font subsetting, used by the atlas
#   tool): HarfBuzz 14.2.1 (MIT), always fetched and pinned, built static without GLib, ICU, the
#   rasterizer, the vector and GPU libraries or the utilities. HarfBuzz builds its FreeType helpers (hb-ft)
#   whenever a `freetype` target exists, whatever HB_HAVE_FREETYPE says, and the UI library links HarfBuzz
#   into applications: gfx_require_freetype() configures HarfBuzz before FreeType, so HarfBuzz never links
#   FreeType, whichever function a project calls first (gfx shapes with HarfBuzz's own font functions).
function(gfx_require_harfbuzz)
    if(TARGET gfx::harfbuzz)
        return()
    endif()
    set(HB_HAVE_FREETYPE OFF CACHE BOOL "Set by gfx_require_harfbuzz()" FORCE)
    foreach(_gfx_option IN ITEMS HB_HAVE_GLIB HB_HAVE_ICU HB_HAVE_GRAPHITE2 HB_HAVE_CAIRO HB_BUILD_UTILS
                                 HB_BUILD_RASTER HB_BUILD_VECTOR HB_BUILD_GPU HB_HAVE_GOBJECT HB_HAVE_INTROSPECTION
                                 BUILD_SHARED_LIBS)
        set(${_gfx_option} OFF CACHE BOOL "Set by gfx_require_harfbuzz()" FORCE)
    endforeach()
    set(HB_BUILD_SUBSET ON CACHE BOOL "Set by gfx_require_harfbuzz()" FORCE)
    FetchContent_Declare(harfbuzz
        URL https://github.com/harfbuzz/harfbuzz/releases/download/14.2.1/harfbuzz-14.2.1.tar.xz
        URL_HASH SHA256=a54a5d8e9380a41fbb762ce367bcbf7704792dfca0d93f1bbca86c5a57902e0e
        SYSTEM)
    FetchContent_MakeAvailable(harfbuzz)
    _gfx_require_targets("HarfBuzz" harfbuzz harfbuzz-subset)
    _gfx_exclude_from_modules(harfbuzz harfbuzz-subset)
    # Optimized GCC builds report the memcmp() of HarfBuzz's hb_memcmp() wrapper with a length range GCC's
    # value-range analysis cannot exclude (-Wstringop-overread); the warning is about HarfBuzz's code only.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(harfbuzz PRIVATE -Wno-stringop-overread)
        target_compile_options(harfbuzz-subset PRIVATE -Wno-stringop-overread)
    endif()
    add_library(gfx_harfbuzz INTERFACE)
    add_library(gfx::harfbuzz ALIAS gfx_harfbuzz)
    target_link_libraries(gfx_harfbuzz INTERFACE harfbuzz)
    add_library(gfx_harfbuzz_subset INTERFACE)
    add_library(gfx::harfbuzz_subset ALIAS gfx_harfbuzz_subset)
    target_link_libraries(gfx_harfbuzz_subset INTERFACE harfbuzz-subset)
    message(STATUS "gfx: HarfBuzz 14.2.1 fetched and built as static libraries (shaping, subsetting)")
endfunction()

# gfx_require_zstd()
#   Provides gfx::zstd: zstd 1.5.7 (BSD), which compresses atlas pixels at build time and decompresses
#   them when an atlas is loaded. Always fetched and pinned (compressed bytes may differ between versions),
#   built static without programs, tests, legacy formats, dictionary builder or threads.
function(gfx_require_zstd)
    if(TARGET gfx::zstd)
        return()
    endif()
    foreach(_gfx_option IN ITEMS ZSTD_BUILD_PROGRAMS ZSTD_BUILD_TESTS ZSTD_BUILD_SHARED ZSTD_BUILD_CONTRIB ZSTD_LEGACY_SUPPORT
                                 ZSTD_MULTITHREAD_SUPPORT ZSTD_BUILD_DICTBUILDER)
        set(${_gfx_option} OFF CACHE BOOL "Set by gfx_require_zstd()" FORCE)
    endforeach()
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "Set by gfx_require_zstd()" FORCE)
    FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        SOURCE_SUBDIR build/cmake
        SYSTEM)
    FetchContent_MakeAvailable(zstd)
    _gfx_require_targets("zstd" libzstd_static)
    add_library(gfx_zstd INTERFACE)
    add_library(gfx::zstd ALIAS gfx_zstd)
    target_link_libraries(gfx_zstd INTERFACE libzstd_static)
    target_include_directories(gfx_zstd SYSTEM INTERFACE ${zstd_SOURCE_DIR}/lib)
    message(STATUS "gfx: zstd 1.5.7 fetched and built as a static library")
endfunction()

# gfx_require_sheenbidi()
#   Provides gfx::sheenbidi: SheenBidi (Apache-2.0), the Unicode bidirectional algorithm (UAX #9) and
#   script runs (UAX #24). An installed SheenBidi is used when present; otherwise release 3.0.0 is
#   downloaded, checked against its hash and built static (one unity source file).
function(gfx_require_sheenbidi)
    if(TARGET gfx::sheenbidi)
        return()
    endif()
    add_library(gfx_sheenbidi INTERFACE)
    add_library(gfx::sheenbidi ALIAS gfx_sheenbidi)
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        find_package(SheenBidi CONFIG QUIET)
    endif()
    if(TARGET SheenBidi::SheenBidi)
        message(STATUS "gfx: SheenBidi ${SheenBidi_VERSION} found")
        target_link_libraries(gfx_sheenbidi INTERFACE SheenBidi::SheenBidi)
        return()
    endif()
    set(SB_CONFIG_UNITY ON CACHE BOOL "Set by gfx_require_sheenbidi()" FORCE)
    foreach(_gfx_option IN ITEMS SB_CONFIG_EXPERIMENTAL_TEXT_API BUILD_GENERATOR BUILD_SHARED_LIBS)
        set(${_gfx_option} OFF CACHE BOOL "Set by gfx_require_sheenbidi()" FORCE)
    endforeach()
    FetchContent_Declare(sheenbidi
        URL https://github.com/Tehreer/SheenBidi/archive/refs/tags/v3.0.0.tar.gz
        URL_HASH SHA256=86c56014034739ba39a24c23eb00323b0bf6f737354f665786015fca842af786
        SYSTEM)
    FetchContent_MakeAvailable(sheenbidi)
    _gfx_require_targets("SheenBidi" SheenBidi::SheenBidi)
    _gfx_exclude_from_modules(SheenBidi)
    target_link_libraries(gfx_sheenbidi INTERFACE SheenBidi::SheenBidi)
    message(STATUS "gfx: SheenBidi 3.0.0 fetched and built as a static library")
endfunction()

# gfx_require_libunibreak()
#   Provides gfx::libunibreak: libunibreak (zlib license), line breaking (UAX #14) and grapheme and word
#   boundaries (UAX #29). An installed libunibreak (MSYS2: mingw-w64-ucrt-x86_64-libunibreak) is used when
#   present; otherwise release 7.0 is downloaded, checked against its hash and the sources of its library
#   are built static (it has no CMake project).
function(gfx_require_libunibreak)
    if(TARGET gfx::libunibreak)
        return()
    endif()
    add_library(gfx_libunibreak INTERFACE)
    add_library(gfx::libunibreak ALIAS gfx_libunibreak)
    if(NOT FETCHCONTENT_TRY_FIND_PACKAGE_MODE STREQUAL "NEVER")
        find_path(GFX_LIBUNIBREAK_INCLUDE_DIRECTORY NAMES linebreak.h graphemebreak.h)
        find_library(GFX_LIBUNIBREAK_LIBRARY NAMES libunibreak.a unibreak)
    endif()
    if(GFX_LIBUNIBREAK_INCLUDE_DIRECTORY AND GFX_LIBUNIBREAK_LIBRARY)
        message(STATUS "gfx: libunibreak found (${GFX_LIBUNIBREAK_LIBRARY})")
        target_include_directories(gfx_libunibreak SYSTEM INTERFACE ${GFX_LIBUNIBREAK_INCLUDE_DIRECTORY})
        target_link_libraries(gfx_libunibreak INTERFACE ${GFX_LIBUNIBREAK_LIBRARY})
        return()
    endif()
    FetchContent_Declare(libunibreak
        URL https://github.com/adah1972/libunibreak/releases/download/libunibreak_7_0/libunibreak-7.0.tar.gz
        URL_HASH SHA256=8c9a6e121736cd0d5c890ae3ae96f3f4010a19aa040f1dbded833a62a87717d3)
    FetchContent_MakeAvailable(libunibreak)
    # The library sources of its makefiles (the *data.c tables are included by them).
    set(_gfx_libunibreak_sources linebreak.c linebreakdata.c linebreakdef.c wordbreak.c graphemebreak.c
                                 eastasianwidthdef.c emojidef.c unibreakbase.c unibreakdef.c)
    list(TRANSFORM _gfx_libunibreak_sources PREPEND ${libunibreak_SOURCE_DIR}/src/)
    add_library(gfx_libunibreak_static STATIC ${_gfx_libunibreak_sources})
    target_include_directories(gfx_libunibreak_static SYSTEM PUBLIC ${libunibreak_SOURCE_DIR}/src)
    target_link_libraries(gfx_libunibreak INTERFACE gfx_libunibreak_static)
    message(STATUS "gfx: libunibreak 7.0 fetched and built as a static library")
endfunction()

# gfx_require_stb_truetype()
#   Provides gfx::stb_truetype: stb_truetype.h (public domain / MIT), the second font decoder (TrueType,
#   OpenType CFF, collections) and the outline source of the fallback SDF kernel. Same pinned stb commit
#   as stb_image.
function(gfx_require_stb_truetype)
    if(TARGET gfx::stb_truetype)
        return()
    endif()
    add_library(gfx_stb_truetype INTERFACE)
    add_library(gfx::stb_truetype ALIAS gfx_stb_truetype)
    FetchContent_Declare(stb_truetype
        URL https://raw.githubusercontent.com/nothings/stb/f1c79c02822848a9bed4315b12c8c8f3761e1296/stb_truetype.h
        URL_HASH SHA256=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(stb_truetype)
    message(STATUS "gfx: stb_truetype 1.26 fetched")
    target_include_directories(gfx_stb_truetype SYSTEM INTERFACE ${stb_truetype_SOURCE_DIR})
endfunction()

# gfx_require_ui_test_fonts()
#   Downloads the fonts the UI tests use (SIL Open Font License 1.1; never committed), checked against
#   their hashes, into <build>/ui-test-fonts, and sets GFX_UI_TEST_FONT_DIRECTORY in the caller's scope:
#   from a pinned commit of github.com/google/fonts, Noto Sans (Latin, Greek, Cyrillic), Noto Sans Arabic
#   and Noto Sans Hebrew (variable TrueType, axes wdth and wght); from a pinned commit of the release
#   branch of github.com/adobe-fonts/source-sans, Source Sans 3 as OpenType CFF, variable OpenType CFF2
#   (axis wght), WOFF and WOFF2 (of its TrueType build); with their licenses.
function(gfx_require_ui_test_fonts)
    set(_gfx_directory ${CMAKE_BINARY_DIR}/ui-test-fonts)
    set(_gfx_noto https://raw.githubusercontent.com/google/fonts/47d54ca1ebb14fd864695f1d39dc734f39017c33/ofl)
    set(_gfx_source_sans https://raw.githubusercontent.com/adobe-fonts/source-sans/87b37a2daaed80fcb8e8ccb0085c4d72ddade12e)
    set(_gfx_fonts
        "NotoSans.ttf|${_gfx_noto}/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf|bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d"
        "NotoSans-OFL.txt|${_gfx_noto}/notosans/OFL.txt|cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a"
        "NotoSansArabic.ttf|${_gfx_noto}/notosansarabic/NotoSansArabic%5Bwdth%2Cwght%5D.ttf|63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e"
        "NotoSansArabic-OFL.txt|${_gfx_noto}/notosansarabic/OFL.txt|07fc70bfeb985cc1a87a8587d0a0c80bab11c86c9dc3fd95b6f0cb332f983e96"
        "NotoSansHebrew.ttf|${_gfx_noto}/notosanshebrew/NotoSansHebrew%5Bwdth%2Cwght%5D.ttf|7ef36a2c3593758cdb622e1bdef4f84523e92fbc3ccc667438dd80ff54c2de88"
        "NotoSansHebrew-OFL.txt|${_gfx_noto}/notosanshebrew/OFL.txt|9b9fe028b5ba74d231659a1bbaf0ed09b11e759d1ca6a070999e16d151616b47"
        "SourceSans3-Regular.otf|${_gfx_source_sans}/OTF/SourceSans3-Regular.otf|08df266400933d3178d081a45f94a08814c3e55b4b7dd2e0ff69cb1329f13ab6"
        "SourceSans3VF-Upright.otf|${_gfx_source_sans}/VF/SourceSans3VF-Upright.otf|3d0dfd6a3a644ab3d462a737923ffac41fb0ae007ce9ba83c24e6bfa76aa56c7"
        "SourceSans3-Regular.ttf.woff|${_gfx_source_sans}/WOFF/TTF/SourceSans3-Regular.ttf.woff|56413337e96f5764882f3e18eaa654eec27515ec7c3de3520eec9e36b25371b9"
        "SourceSans3-Regular.ttf.woff2|${_gfx_source_sans}/WOFF2/TTF/SourceSans3-Regular.ttf.woff2|53492fb3a0def77354f166a55d09b63a10855e91c206c7620a81cf56e97f8ec3"
        "SourceSans3-LICENSE.md|${_gfx_source_sans}/LICENSE.md|56af9b9c6715597e458284a474dc118a50a4150e9d547c70f7b4a33c3e6a9328")
    foreach(_gfx_font IN LISTS _gfx_fonts)
        string(REPLACE "|" ";" _gfx_parts "${_gfx_font}")
        list(GET _gfx_parts 0 _gfx_name)
        list(GET _gfx_parts 1 _gfx_url)
        list(GET _gfx_parts 2 _gfx_hash)
        set(_gfx_file ${_gfx_directory}/${_gfx_name})
        if(EXISTS ${_gfx_file})
            file(SHA256 ${_gfx_file} _gfx_actual)
            if(_gfx_actual STREQUAL _gfx_hash)
                continue()
            endif()
        endif()
        file(DOWNLOAD ${_gfx_url} ${_gfx_file} EXPECTED_HASH SHA256=${_gfx_hash} TLS_VERIFY ON STATUS _gfx_status)
        list(GET _gfx_status 0 _gfx_code)
        if(NOT _gfx_code EQUAL 0)
            list(GET _gfx_status 1 _gfx_message)
            message(FATAL_ERROR "gfx: the UI test font ${_gfx_name} could not be downloaded from ${_gfx_url}: "
                                "${_gfx_message}. Turn GFX_BUILD_TESTS off, or retry with network access.")
        endif()
    endforeach()
    message(STATUS "gfx: UI test fonts (OFL) in ${_gfx_directory}")
    set(GFX_UI_TEST_FONT_DIRECTORY ${_gfx_directory} PARENT_SCOPE)
endfunction()

# ==============================================================================
# The math library's run-time SIMD paths (docs/physics-design.md, "Math and SIMD"). gfx.core.math
# computes in constant expressions when it can, and through these libraries at run time. Both are
# header-only C, always fetched and pinned: the same version on every machine gives the same results
# (deterministic simulations). Their headers stay in the math module's global module fragment.
# ==============================================================================

# gfx_require_cglm()
#   Provides gfx::cglm: cglm 0.9.6 (MIT), SIMD vectors, matrices and quaternions (SSE/SSE2/AVX/NEON/WASM,
#   scalar elsewhere). Its static inline functions are used, not its compiled library. CGLM_ALL_UNALIGNED:
#   gfx.core.math's types are aligned, but no cglm load relies on it.
function(gfx_require_cglm)
    if(TARGET gfx::cglm)
        return()
    endif()
    FetchContent_Declare(gfx_cglm
        URL https://github.com/recp/cglm/archive/refs/tags/v0.9.6.tar.gz
        URL_HASH SHA256=be5e7d384561eb0fca59724a92b7fb44bf03e588a7eae5123a7d796002928184
        SOURCE_SUBDIR gfx-headers-only)
    FetchContent_MakeAvailable(gfx_cglm)
    add_library(gfx_cglm INTERFACE)
    add_library(gfx::cglm ALIAS gfx_cglm)
    target_include_directories(gfx_cglm SYSTEM INTERFACE ${gfx_cglm_SOURCE_DIR}/include)
    target_compile_definitions(gfx_cglm INTERFACE CGLM_ALL_UNALIGNED)
    message(STATUS "gfx: cglm 0.9.6 fetched (headers)")
endfunction()

# gfx_require_simde()
#   Provides gfx::simde: SIMD Everywhere 0.8.2 (MIT; a few test headers CC0-1.0), portable SIMD
#   intrinsics for the wide kernels over arrays (native SSE/AVX on x86, NEON on ARM, WASM SIMD, scalar
#   elsewhere), from its amalgamated release (one header per instruction set).
function(gfx_require_simde)
    if(TARGET gfx::simde)
        return()
    endif()
    FetchContent_Declare(gfx_simde
        URL https://github.com/simd-everywhere/simde/releases/download/v0.8.2/simde-amalgamated-0.8.2.tar.xz
        URL_HASH SHA256=59068edc3420e75c5ff85ecfd80a77196fb3a151227a666cc20abb313a5360bf
        SOURCE_SUBDIR gfx-headers-only)
    FetchContent_MakeAvailable(gfx_simde)
    add_library(gfx_simde INTERFACE)
    add_library(gfx::simde ALIAS gfx_simde)
    target_include_directories(gfx_simde SYSTEM INTERFACE ${gfx_simde_SOURCE_DIR})
    message(STATUS "gfx: SIMDe 0.8.2 fetched (amalgamated headers)")
endfunction()