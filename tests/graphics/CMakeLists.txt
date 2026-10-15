# Tests of the graphics library (`import gfx;`).
add_subdirectory(backend_factory)
add_subdirectory(enums)
add_subdirectory(errors)
add_subdirectory(handles)
add_subdirectory(interface)
add_subdirectory(shader_layout)
add_subdirectory(shader_tools)
if(WIN32)
    add_subdirectory(windows_headers)
endif()
if(GFX_BACKEND_VULKAN)
    add_subdirectory(vulkan)    # the Vulkan backend on the machine's GPUs
endif()
