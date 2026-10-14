/// @file interface.cppm
/// @brief The abstract interfaces of gfx: the objects every backend implements, their descriptions,
///        typed command recording, parallel rendering passes, and the validation of reflected shader
///        layouts.
///
/// Object model (Vulkan-like): Instance -> PhysicalDevice -> Device -> Queue, resources (DeviceMemory,
/// Buffer, Image, ImageView, Sampler), binding model (ShaderModule, DescriptorSetLayout, PipelineLayout,
/// ComputePipeline, GraphicsPipeline, DescriptorPool, DescriptorSet), commands (CommandPool,
/// CommandBuffer, Recorder), synchronization (Fence, Semaphore, TimelineSemaphore, Event), QueryPool,
/// Swapchain and Deleter.
export module gfx.interface;

export import :common_types;
export import :backend_object;
export import :shader_layout;
export import :synchronization;
export import :commands;
export import :device_memory;
export import :buffer;
export import :image;
export import :image_view;
export import :sampler;
export import :shader_module;
export import :descriptor_set_layout;
export import :pipeline_layout;
export import :descriptor_set;
export import :descriptor_pool;
export import :compute_pipeline;
export import :graphics_pipeline;
export import :fence;
export import :semaphore;
export import :timeline_semaphore;
export import :event;
export import :query_pool;
export import :command_buffer;
export import :command_pool;
export import :recorder;
export import :queue;
export import :swapchain;
export import :deleter;
export import :shader_validation;
export import :device;
export import :frame_command_pools;
export import :physical_device;
export import :instance;
