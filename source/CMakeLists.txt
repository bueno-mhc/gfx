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
    interface/bindless.cppm
    interface/synchronization.cppm
    interface/commands.cppm
    interface/device_memory.cppm
    interface/buffer.cppm
    interface/image.cppm
    interface/image_view.cppm
    interface/sampler.cppm
    interface/buffer_view.cppm
    interface/sparse_binding.cppm
    interface/shader_module.cppm
    interface/descriptor_set_layout.cppm
    interface/pipeline_layout.cppm
    interface/descriptor_set.cppm
    interface/descriptor_pool.cppm
    interface/compute_pipeline.cppm
    interface/graphics_pipeline.cppm
    interface/ray_tracing_pipeline.cppm
    interface/pipeline_cache.cppm
    interface/acceleration_structure.cppm
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
    interface/shader_validation.cppm
    interface/device.cppm
    interface/frame_command_pools.cppm
    interface/frame_timeline.cppm
    interface/transient_buffer_allocator.cppm
    interface/uploader.cppm
    interface/gpu_profiler.cppm
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

# Debug builds compile GLSL at run time and reload changed shader files; Release builds only load
# SPIR-V compiled at build time; Debug builds also validate every command (gfx::DEBUG_BUILD in enums.cppm).
target_compile_definitions(gfx PUBLIC $<$<CONFIG:Debug>:GFX_DEBUG_BUILD=1>)

target_compile_features(gfx PUBLIC cxx_std_23)
target_link_libraries(gfx
    PUBLIC gfx::toolchain_workarounds   # consumers need them too (see cmake/GfxProjectOptions.cmake)
    PRIVATE gfx::project_options)
