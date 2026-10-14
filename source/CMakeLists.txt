# ==============================================================================
# The gfx library. Its public API is a set of C++ modules; `import gfx;` reaches all of it.
# ==============================================================================
add_library(gfx)
add_library(gfx::gfx ALIAS gfx)

# Module gfx.interface: its primary interface unit and one partition per concept or object.
set(GFX_INTERFACE_MODULES
    interface/interface.cppm
    interface/common_types.cppm
    interface/backend_object.cppm
    interface/shader_layout.cppm
    interface/layout_compatibility.cppm
    interface/synchronization.cppm
    interface/commands.cppm
    interface/device_memory.cppm
    interface/buffer.cppm
    interface/image.cppm
    interface/image_view.cppm
    interface/sampler.cppm
    interface/shader_module.cppm
    interface/descriptor_set_layout.cppm
    interface/pipeline_layout.cppm
    interface/descriptor_set.cppm
    interface/descriptor_pool.cppm
    interface/compute_pipeline.cppm
    interface/graphics_pipeline.cppm
    interface/fence.cppm
    interface/semaphore.cppm
    interface/timeline_semaphore.cppm
    interface/event.cppm
    interface/query_pool.cppm
    interface/command_buffer.cppm
    interface/command_pool.cppm
    interface/recorder.cppm
    interface/queue.cppm
    interface/swapchain.cppm
    interface/deleter.cppm
    interface/typed_objects.cppm
    interface/device.cppm
    interface/physical_device.cppm
    interface/instance.cppm)

target_sources(gfx
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            gfx.cppm
            enums.cppm
            errors.cppm
            handles.cppm
            ${GFX_INTERFACE_MODULES}
    PRIVATE
        errors.cpp)

# Headers exist only where a module cannot serve (toolchain workarounds), e.g. <gfx/windows_headers.hpp>.
target_include_directories(gfx PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(gfx PUBLIC cxx_std_23)
target_link_libraries(gfx
    PUBLIC gfx::toolchain_workarounds   # consumers need them too (see cmake/GfxProjectOptions.cmake)
    PRIVATE gfx::project_options)
