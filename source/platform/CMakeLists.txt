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
