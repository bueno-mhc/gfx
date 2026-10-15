# ==============================================================================
# The gfx graphics library. Its public API is a set of C++ modules; `import gfx;` reaches all of it
# (gfx.core included).
# ==============================================================================
add_library(gfx)
add_library(gfx::gfx ALIAS gfx)
add_library(gfx::graphics ALIAS gfx)

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
            backend_factory.cppm
            shaders/shader_tools.cppm
            ${GFX_INTERFACE_MODULES}
    PRIVATE
        errors.cpp
        backend_factory.cpp
        shaders/spirv_reflection.cpp
        shaders/shader_compiler.cpp)

# Backends: modules of a PRIVATE file set (consumers cannot import them); only the implementation of
# gfx.backend_factory imports them. GFX_WITH_<NAME>_BACKEND tells it which ones were built.
set(GFX_BACKEND_MODULES "")
set(GFX_BACKEND_SOURCES "")
if(GFX_BACKEND_VULKAN)
    list(APPEND GFX_BACKEND_MODULES
        backends/vulkan/vulkan_backend.cppm
        backends/vulkan/vulkan_common.cppm
        backends/vulkan/vulkan_translation.cppm
        backends/vulkan/vulkan_instance.cppm
        backends/vulkan/vulkan_device.cppm
        backends/vulkan/vulkan_pipelines.cppm)
    list(APPEND GFX_BACKEND_SOURCES
        backends/vulkan/vulkan_backend.cpp
        backends/vulkan/vulkan_common.cpp
        backends/vulkan/vulkan_instance.cpp
        backends/vulkan/vulkan_device.cpp
        backends/vulkan/vulkan_queue.cpp
        backends/vulkan/vulkan_bindless.cpp
        backends/vulkan/vulkan_layouts.cpp
        backends/vulkan/vulkan_synchronization.cpp
        backends/vulkan/vulkan_resources.cpp
        backends/vulkan/vulkan_pipelines.cpp
        backends/vulkan/vulkan_commands.cpp
        backends/vulkan/vulkan_diagnostics.cpp
        backends/vulkan/renderdoc_capture.cpp
        backends/vulkan/vma_implementation.cpp)
    target_compile_definitions(gfx PRIVATE GFX_WITH_VULKAN_BACKEND=1)
    # Vulkan structures are initialized with designated initializers that leave the members they do not
    # name zeroed, as the API intends; GCC's -Wextra would flag every one of them.
    set_source_files_properties(${GFX_BACKEND_MODULES} ${GFX_BACKEND_SOURCES}
        PROPERTIES COMPILE_OPTIONS $<$<CXX_COMPILER_ID:GNU>:-Wno-missing-field-initializers>)
endif()
if(GFX_BACKEND_NULL)
    list(APPEND GFX_BACKEND_MODULES backends/null/null_backend.cppm)
    target_compile_definitions(gfx PRIVATE GFX_WITH_NULL_BACKEND=1)
endif()
if(GFX_BACKEND_DIRECT3D12)
    list(APPEND GFX_BACKEND_MODULES backends/d3d12/direct3d12_backend.cppm)
    target_compile_definitions(gfx PRIVATE GFX_WITH_DIRECT3D12_BACKEND=1)
endif()
if(GFX_BACKEND_METAL)
    list(APPEND GFX_BACKEND_MODULES backends/metal/metal_backend.cppm)
    target_compile_definitions(gfx PRIVATE GFX_WITH_METAL_BACKEND=1)
endif()
if(GFX_BACKEND_MODULES)
    target_sources(gfx
        PRIVATE
            FILE_SET backend_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES ${GFX_BACKEND_MODULES}
        PRIVATE
            ${GFX_BACKEND_SOURCES})
endif()

target_compile_features(gfx PUBLIC cxx_std_23)

# Third-party libraries (cmake/GfxDependencies.cmake): system packages first, FetchContent otherwise.
include(GfxDependencies)
gfx_require_spirv_cross()
gfx_require_glslang()
# Reflection in every build; the run-time compiler only in Debug builds (Release loads build-time SPIR-V).
target_link_libraries(gfx PRIVATE gfx::spirv_cross $<$<CONFIG:Debug>:gfx::glslang>)
if(GFX_BACKEND_VULKAN)
    gfx_require_volk()
    gfx_require_vulkan_memory_allocator()
    gfx_require_renderdoc_api()
    target_link_libraries(gfx PRIVATE gfx::volk gfx::vulkan_memory_allocator gfx::renderdoc_api)
endif()
# gfx.core (shared with the platform library) carries the toolchain workarounds and GFX_DEBUG_BUILD:
# Debug builds compile GLSL at run time and reload changed shader files and validate every command;
# Release builds only load SPIR-V compiled at build time.
target_link_libraries(gfx
    PUBLIC gfx::core
    PRIVATE gfx::project_options)
