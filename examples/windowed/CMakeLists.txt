# The windowed example: gfx_windowed_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation]
add_executable(gfx_windowed_example windowed_example.cpp)
target_link_libraries(gfx_windowed_example PRIVATE gfx::platform_graphics gfx::project_options)
gfx_deploy_runtime_dependencies(gfx_windowed_example)

# One automated run per platform backend: 120 frames, with the validation layers when the tests found
# them (tests/graphics/vulkan); any validation error fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_example_arguments --frames 120)
    set(_gfx_example_environment "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        list(APPEND _gfx_example_arguments --validation)
        set(_gfx_example_environment "VK_ADD_LAYER_PATH=${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    foreach(_gfx_backend IN ITEMS SDL3 GLFW)
        if(GFX_PLATFORM_BACKEND_${_gfx_backend})
            string(TOLOWER ${_gfx_backend} _gfx_backend_name)
            add_test(NAME WindowedExample.${_gfx_backend}
                     COMMAND gfx_windowed_example --backend ${_gfx_backend_name} ${_gfx_example_arguments})
            set_tests_properties(WindowedExample.${_gfx_backend} PROPERTIES
                ENVIRONMENT "${_gfx_example_environment}"
                LABELS example)
        endif()
    endforeach()
endif()
