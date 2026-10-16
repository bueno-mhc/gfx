# ==============================================================================
# gfx::platform: windowing and input above platform backends (SDL3, GLFW). `import gfx.platform;`
# (gfx.core included). Backends are private modules reached only through gfx.platform.backend_factory.
# ==============================================================================
add_library(gfx_platform)
add_library(gfx::platform ALIAS gfx_platform)

target_sources(gfx_platform
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            platform.cppm
            enums.cppm
            handles.cppm
            backend_factory.cppm
            interface/interface.cppm
            interface/platform_object.cppm
            interface/event.cppm
            interface/image.cppm
            interface/monitor.cppm
            interface/cursor.cppm
            interface/window.cppm
            interface/input_devices.cppm
            interface/platform.cppm
            input/input.cppm
            input/binding.cppm
            input/schema.cppm
            input/frame.cppm
            input/event_channel.cppm
            input/input_system.cppm
    PRIVATE
        backend_factory.cpp
        interface/image.cpp
        interface/stb_image_implementation.cpp
        input/schema.cpp
        input/frame.cpp
        input/event_channel.cpp
        input/input_system.cpp
        input/binding_storage.cpp
        input/json_reader.cpp)

target_compile_features(gfx_platform PUBLIC cxx_std_23)

# Third-party code stays private: stb_image decodes PNG icons and cursors, nlohmann/json parses saved
# input bindings (in json_reader.cpp only).
include(GfxDependencies)
gfx_require_stb_image()
gfx_require_nlohmann_json()
target_link_libraries(gfx_platform
    PUBLIC gfx::core
    PRIVATE gfx::project_options gfx::stb_image gfx::nlohmann_json)

# Backends: modules of a PRIVATE file set (consumers cannot import them); only the implementation of
# gfx.platform.backend_factory imports them. GFX_WITH_PLATFORM_<NAME>_BACKEND tells it which were built.
set(GFX_PLATFORM_BACKEND_MODULES "")
set(GFX_PLATFORM_BACKEND_SOURCES "")
if(GFX_PLATFORM_BACKEND_SDL3)
    gfx_require_sdl3()
    list(APPEND GFX_PLATFORM_BACKEND_MODULES
        backends/sdl3/sdl3_backend.cppm
        backends/sdl3/sdl3_internal.cppm)
    list(APPEND GFX_PLATFORM_BACKEND_SOURCES
        backends/sdl3/sdl3_translation.cpp
        backends/sdl3/sdl3_platform.cpp
        backends/sdl3/sdl3_events.cpp
        backends/sdl3/sdl3_window.cpp
        backends/sdl3/sdl3_input_devices.cpp)
    target_compile_definitions(gfx_platform PRIVATE GFX_WITH_PLATFORM_SDL3_BACKEND=1)
    target_link_libraries(gfx_platform PRIVATE gfx::sdl3)
endif()
if(GFX_PLATFORM_BACKEND_GLFW)
    gfx_require_glfw()
    list(APPEND GFX_PLATFORM_BACKEND_MODULES
        backends/glfw/glfw_backend.cppm
        backends/glfw/glfw_internal.cppm)
    list(APPEND GFX_PLATFORM_BACKEND_SOURCES
        backends/glfw/glfw_translation.cpp
        backends/glfw/glfw_platform.cpp
        backends/glfw/glfw_events.cpp
        backends/glfw/glfw_window.cpp
        backends/glfw/glfw_input_devices.cpp
        backends/glfw/glfw_native.cpp)
    target_compile_definitions(gfx_platform PRIVATE GFX_WITH_PLATFORM_GLFW_BACKEND=1)
    target_link_libraries(gfx_platform PRIVATE gfx::glfw)
endif()
if(GFX_PLATFORM_BACKEND_MODULES)
    target_sources(gfx_platform
        PRIVATE
            FILE_SET backend_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES ${GFX_PLATFORM_BACKEND_MODULES}
        PRIVATE
            ${GFX_PLATFORM_BACKEND_SOURCES})
endif()
