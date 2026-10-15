/// @file enums.cppm
/// @brief Every enumeration and constant of gfx. No other file of the library defines either.
///
/// Conventions:
/// - Enumerators and constants use SCREAMING_SNAKE_CASE and full words. Names that platform headers
///   define as macros (`NULL`, `ERROR`, `DELETE`, `OPAQUE`, `MIN`, `MAX`, ...) are avoided; a CTest
///   check compares every name declared here against the macros of the platform headers.
/// - Every enumeration has an EnumerationTraits specialization listing the spelling of each enumerator.
///   The tables are verified at compile time (order, uniqueness, single bits for bitmasks) and, on
///   GCC and Clang, against the compiler's own spelling of each enumerator.
export module gfx.enums;

import std;

// Set to 1 by CMake for Debug builds (source/CMakeLists.txt); everything else is a Release build.
#ifndef GFX_DEBUG_BUILD
#define GFX_DEBUG_BUILD 0
#endif

export namespace gfx 
{

// =================================================================================================
// Backends, queues and devices
// =================================================================================================

/// @brief Graphics API a backend is built on.
enum class BackendKind : std::uint8_t 
{
    NULL_BACKEND, ///< Backend without a GPU; reports every operation as unsupported.
    VULKAN,       ///< Vulkan 1.3.
    DIRECT3D12,   ///< Direct3D 12.
    METAL,        ///< Apple Metal.
};

/// @brief Kind of queue that command buffers are recorded for and submitted to.
enum class QueueType : std::uint8_t 
{
    GRAPHICS,       ///< Graphics, compute and transfer work.
    COMPUTE,        ///< Compute and transfer work; the asynchronous-compute queue when dedicated.
    TRANSFER,       ///< Copy work only; usually backed by DMA engines.
    SPARSE_BINDING, ///< Sparse memory binding operations.
};

/// @brief Kinds of work a queue (or a command argument) supports. Bitmask.
enum class QueueCapability : std::uint8_t 
{
    NONE           = 0,      ///< No capability.
    GRAPHICS       = 1U << 0, ///< Draw commands and render passes.
    COMPUTE        = 1U << 1, ///< Dispatch commands.
    TRANSFER       = 1U << 2, ///< Copy, fill and blit commands.
    SPARSE_BINDING = 1U << 3, ///< Sparse memory binding.
};

/// @brief Category of a physical device.
enum class PhysicalDeviceType : std::uint8_t 
{
    OTHER,          ///< Unknown kind of device.
    INTEGRATED_GPU, ///< GPU sharing memory with the CPU.
    DISCRETE_GPU,   ///< GPU with dedicated memory.
    VIRTUAL_GPU,    ///< GPU exposed through virtualization.
    CPU,            ///< Software rasterizer running on the CPU.
};

/// @brief Criterion used when the library selects a physical device automatically.
enum class PhysicalDevicePreference : std::uint8_t 
{
    HIGH_PERFORMANCE, ///< Prefer discrete GPUs.
    LOW_POWER,        ///< Prefer integrated GPUs.
};

/// @brief Optional device capabilities. Device creation fails with a BackendCapabilityError that
///        lists every requested feature the physical device lacks.
enum class DeviceFeature : std::uint8_t 
{
    TIMELINE_SEMAPHORE,        ///< Timeline semaphores for cross-queue synchronization.
    EVENTS,                    ///< Split barriers (set and wait events inside command buffers).
    DYNAMIC_RENDERING,         ///< Rendering without render pass and framebuffer objects.
    DESCRIPTOR_INDEXING,       ///< Bindless descriptor arrays (runtime-sized, partially bound).
    BUFFER_DEVICE_ADDRESS,     ///< GPU virtual addresses of buffers usable in shaders.
    ASYNCHRONOUS_COMPUTE_QUEUE,///< A compute queue family separate from the graphics family.
    DEDICATED_TRANSFER_QUEUE,  ///< A transfer-only queue family.
    SPARSE_BINDING,            ///< Sparse (partially resident) resources.
    SAMPLER_ANISOTROPY,        ///< Anisotropic texture filtering.
    MULTI_DRAW_INDIRECT,       ///< Several indirect draws per command.
    DRAW_INDIRECT_COUNT,       ///< Indirect draws whose count is read from a buffer.
    GEOMETRY_SHADER,           ///< Geometry shader stage.
    TESSELLATION_SHADER,       ///< Tessellation control and evaluation stages.
    MESH_SHADER,               ///< Task and mesh shader stages.
    RAY_TRACING,               ///< Ray tracing pipelines and acceleration structures.
    SHADER_FLOAT16,            ///< 16-bit floating-point arithmetic in shaders.
    SHADER_FLOAT64,            ///< 64-bit floating-point arithmetic in shaders.
    SHADER_INTEGER16,          ///< 16-bit integer arithmetic in shaders.
    SHADER_INTEGER64,          ///< 64-bit integer arithmetic in shaders.
    FILL_MODE_NON_SOLID,       ///< PolygonMode::LINE and PolygonMode::POINT.
    WIDE_LINES,                ///< Line widths other than 1.
    DEPTH_CLAMP,               ///< Clamping depth instead of clipping.
    INDEPENDENT_BLEND,         ///< Different blend state per color attachment.
    TIMESTAMP_QUERY,           ///< Timestamp queries on graphics and compute queues.
    PIPELINE_STATISTICS_QUERY, ///< Pipeline statistics queries.
    TEXTURE_COMPRESSION_BC,    ///< BC1-BC7 compressed formats.
    TEXTURE_COMPRESSION_ETC2,  ///< ETC2 compressed formats.
    TEXTURE_COMPRESSION_ASTC,  ///< ASTC LDR compressed formats.
    PRESENTATION,              ///< Presenting images to a window surface.
    EXTENDED_DYNAMIC_STATE,    ///< Cull mode, front face, topology and depth/stencil tests set while recording.
    DEPTH_BOUNDS,              ///< Depth bounds test.
    MULTIVIEW,                 ///< Rendering several views (view mask) in one pass.
    VARIABLE_RATE_SHADING,     ///< Fragment shading rates coarser than one per pixel.
    CONSERVATIVE_RASTERIZATION,///< Rasterizing every pixel a primitive touches.
    RAY_QUERY,                 ///< Inline ray tracing from any shader stage.
    SAMPLER_FILTER_MINMAX,     ///< Minimum/maximum sampler reduction.
    MEMORY_BUDGET,             ///< Memory budget and usage queries per heap.
    GPU_CRASH_MARKERS,         ///< GPU-written markers that locate a crash (breadcrumbs).
};

// =================================================================================================
// Formats
// =================================================================================================

/// @brief Texel formats. Names follow Vulkan's VkFormat spelling.
enum class Format : std::uint16_t 
{
    UNDEFINED,                 ///< No format.
    R8_UNORM,                  ///< 8-bit red, unsigned normalized.
    R8_SNORM,                  ///< 8-bit red, signed normalized.
    R8_UINT,                   ///< 8-bit red, unsigned integer.
    R8_SINT,                   ///< 8-bit red, signed integer.
    R8G8_UNORM,                ///< 8-bit red-green, unsigned normalized.
    R8G8_SNORM,                ///< 8-bit red-green, signed normalized.
    R8G8_UINT,                 ///< 8-bit red-green, unsigned integer.
    R8G8_SINT,                 ///< 8-bit red-green, signed integer.
    R8G8B8A8_UNORM,            ///< 8-bit RGBA, unsigned normalized.
    R8G8B8A8_SNORM,            ///< 8-bit RGBA, signed normalized.
    R8G8B8A8_UINT,             ///< 8-bit RGBA, unsigned integer.
    R8G8B8A8_SINT,             ///< 8-bit RGBA, signed integer.
    R8G8B8A8_SRGB,             ///< 8-bit RGBA, sRGB-encoded color.
    B8G8R8A8_UNORM,            ///< 8-bit BGRA, unsigned normalized (common swapchain format).
    B8G8R8A8_SRGB,             ///< 8-bit BGRA, sRGB-encoded color (common swapchain format).
    A2B10G10R10_UNORM_PACK32,  ///< 10-bit RGB + 2-bit alpha packed in 32 bits, unsigned normalized.
    A2B10G10R10_UINT_PACK32,   ///< 10-bit RGB + 2-bit alpha packed in 32 bits, unsigned integer.
    R16_UNORM,                 ///< 16-bit red, unsigned normalized.
    R16_SNORM,                 ///< 16-bit red, signed normalized.
    R16_UINT,                  ///< 16-bit red, unsigned integer.
    R16_SINT,                  ///< 16-bit red, signed integer.
    R16_SFLOAT,                ///< 16-bit red, floating point.
    R16G16_UNORM,              ///< 16-bit red-green, unsigned normalized.
    R16G16_SNORM,              ///< 16-bit red-green, signed normalized.
    R16G16_UINT,               ///< 16-bit red-green, unsigned integer.
    R16G16_SINT,               ///< 16-bit red-green, signed integer.
    R16G16_SFLOAT,             ///< 16-bit red-green, floating point.
    R16G16B16A16_UNORM,        ///< 16-bit RGBA, unsigned normalized.
    R16G16B16A16_SNORM,        ///< 16-bit RGBA, signed normalized.
    R16G16B16A16_UINT,         ///< 16-bit RGBA, unsigned integer.
    R16G16B16A16_SINT,         ///< 16-bit RGBA, signed integer.
    R16G16B16A16_SFLOAT,       ///< 16-bit RGBA, floating point.
    R32_UINT,                  ///< 32-bit red, unsigned integer.
    R32_SINT,                  ///< 32-bit red, signed integer.
    R32_SFLOAT,                ///< 32-bit red, floating point.
    R32G32_UINT,               ///< 32-bit red-green, unsigned integer.
    R32G32_SINT,               ///< 32-bit red-green, signed integer.
    R32G32_SFLOAT,             ///< 32-bit red-green, floating point.
    R32G32B32_UINT,            ///< 32-bit RGB, unsigned integer.
    R32G32B32_SINT,            ///< 32-bit RGB, signed integer.
    R32G32B32_SFLOAT,          ///< 32-bit RGB, floating point.
    R32G32B32A32_UINT,         ///< 32-bit RGBA, unsigned integer.
    R32G32B32A32_SINT,         ///< 32-bit RGBA, signed integer.
    R32G32B32A32_SFLOAT,       ///< 32-bit RGBA, floating point.
    B10G11R11_UFLOAT_PACK32,   ///< Packed 11-11-10-bit unsigned floating point RGB.
    E5B9G9R9_UFLOAT_PACK32,    ///< Shared-exponent 9-bit unsigned floating point RGB.
    D16_UNORM,                 ///< 16-bit depth, unsigned normalized.
    X8_D24_UNORM_PACK32,       ///< 24-bit depth in 32 bits, unsigned normalized.
    D32_SFLOAT,                ///< 32-bit depth, floating point.
    S8_UINT,                   ///< 8-bit stencil.
    D24_UNORM_S8_UINT,         ///< 24-bit depth + 8-bit stencil.
    D32_SFLOAT_S8_UINT,        ///< 32-bit floating-point depth + 8-bit stencil.
    BC1_RGBA_UNORM_BLOCK,      ///< BC1 (DXT1) RGBA, unsigned normalized.
    BC1_RGBA_SRGB_BLOCK,       ///< BC1 (DXT1) RGBA, sRGB.
    BC2_UNORM_BLOCK,           ///< BC2 (DXT3), unsigned normalized.
    BC2_SRGB_BLOCK,            ///< BC2 (DXT3), sRGB.
    BC3_UNORM_BLOCK,           ///< BC3 (DXT5), unsigned normalized.
    BC3_SRGB_BLOCK,            ///< BC3 (DXT5), sRGB.
    BC4_UNORM_BLOCK,           ///< BC4 single channel, unsigned normalized.
    BC4_SNORM_BLOCK,           ///< BC4 single channel, signed normalized.
    BC5_UNORM_BLOCK,           ///< BC5 two channels, unsigned normalized.
    BC5_SNORM_BLOCK,           ///< BC5 two channels, signed normalized.
    BC6H_UFLOAT_BLOCK,         ///< BC6H HDR RGB, unsigned floating point.
    BC6H_SFLOAT_BLOCK,         ///< BC6H HDR RGB, signed floating point.
    BC7_UNORM_BLOCK,           ///< BC7 RGBA, unsigned normalized.
    BC7_SRGB_BLOCK,            ///< BC7 RGBA, sRGB.
    ETC2_R8G8B8A8_UNORM_BLOCK, ///< ETC2 RGBA, unsigned normalized.
    ETC2_R8G8B8A8_SRGB_BLOCK,  ///< ETC2 RGBA, sRGB.
    ASTC_4x4_UNORM_BLOCK,      ///< ASTC 4x4 RGBA, unsigned normalized.
    ASTC_4x4_SRGB_BLOCK,       ///< ASTC 4x4 RGBA, sRGB.
};

/// @brief How the components of a format are interpreted.
enum class FormatNumericType : std::uint8_t 
{
    NONE,                ///< Format::UNDEFINED.
    UNSIGNED_NORMALIZED, ///< Unsigned integer mapped to [0, 1].
    SIGNED_NORMALIZED,   ///< Signed integer mapped to [-1, 1].
    UNSIGNED_INTEGER,    ///< Unsigned integer.
    SIGNED_INTEGER,      ///< Signed integer.
    UNSIGNED_FLOAT,      ///< Unsigned floating point.
    SIGNED_FLOAT,        ///< Signed floating point.
    SRGB,                ///< sRGB-encoded color, read as linear [0, 1].
};

/// @brief Aspects of an image: color, depth and/or stencil. Bitmask.
enum class ImageAspect : std::uint8_t 
{
    NONE    = 0,       ///< No aspect.
    COLOR   = 1U << 0, ///< Color data.
    DEPTH   = 1U << 1, ///< Depth data.
    STENCIL = 1U << 2, ///< Stencil data.
};

// =================================================================================================
// Memory and resources
// =================================================================================================

/// @brief Where the memory of a resource lives and how the CPU may access it.
enum class MemoryLocation : std::uint8_t 
{
    DEVICE_LOCAL,              ///< GPU memory, not visible to the CPU. Fastest for GPU access.
    HOST_UPLOAD,               ///< CPU-writable memory read by the GPU (staging, per-frame constants).
    HOST_READBACK,             ///< CPU-readable memory written by the GPU (readback, queries).
    DEVICE_LOCAL_HOST_VISIBLE, ///< GPU memory mapped into the CPU address space (resizable BAR).
};

/// @brief Properties of a memory type. Bitmask.
enum class MemoryProperty : std::uint8_t 
{
    NONE             = 0,       ///< No property.
    DEVICE_LOCAL     = 1U << 0, ///< Fast GPU access.
    HOST_VISIBLE     = 1U << 1, ///< Mappable by the CPU.
    HOST_COHERENT    = 1U << 2, ///< CPU writes are visible without explicit flushes.
    HOST_CACHED      = 1U << 3, ///< Cached on the CPU side (fast CPU reads).
    LAZILY_ALLOCATED = 1U << 4, ///< Backing memory allocated on demand (transient attachments).
};

/// @brief Memory heap an allocation failure refers to.
enum class MemoryDomain : std::uint8_t 
{
    HOST,   ///< System memory.
    DEVICE, ///< GPU memory.
};

/// @brief Kind of resource a state, barrier or allocation applies to. Bitmask.
enum class ResourceCategory : std::uint8_t 
{
    NONE   = 0,      ///< No resource.
    BUFFER = 1U << 0, ///< Buffers.
    IMAGE  = 1U << 1, ///< Images.
};

/// @brief Ways a buffer may be used. Bitmask.
enum class BufferUsage : std::uint16_t 
{
    NONE                  = 0,      ///< No usage.
    TRANSFER_SOURCE       = 1U << 0, ///< Source of copy commands.
    TRANSFER_DESTINATION  = 1U << 1, ///< Destination of copy and fill commands.
    UNIFORM_TEXEL         = 1U << 2, ///< Read through a uniform texel buffer view.
    STORAGE_TEXEL         = 1U << 3, ///< Read and written through a storage texel buffer view.
    UNIFORM               = 1U << 4, ///< Bound as a uniform buffer.
    STORAGE               = 1U << 5, ///< Bound as a storage buffer.
    INDEX                 = 1U << 6, ///< Bound as an index buffer.
    VERTEX                = 1U << 7, ///< Bound as a vertex buffer.
    INDIRECT              = 1U << 8, ///< Source of indirect draw and dispatch arguments.
    SHADER_DEVICE_ADDRESS = 1U << 9, ///< Its GPU address may be queried and used in shaders.
    ACCELERATION_STRUCTURE_STORAGE     = 1U << 10, ///< Holds acceleration structures.
    ACCELERATION_STRUCTURE_BUILD_INPUT = 1U << 11, ///< Vertices, indices, transforms or instances of builds.
    SHADER_BINDING_TABLE               = 1U << 12, ///< Holds a ray tracing shader binding table.
};

/// @brief Dimensionality of an image.
enum class ImageDimension : std::uint8_t 
{
    ONE_DIMENSIONAL,   ///< Width only.
    TWO_DIMENSIONAL,   ///< Width and height.
    THREE_DIMENSIONAL, ///< Width, height and depth.
};

/// @brief Ways an image may be used. Bitmask.
enum class ImageUsage : std::uint16_t 
{
    NONE                     = 0,      ///< No usage.
    TRANSFER_SOURCE          = 1U << 0, ///< Source of copy and blit commands.
    TRANSFER_DESTINATION     = 1U << 1, ///< Destination of copy, blit and clear commands.
    SAMPLED                  = 1U << 2, ///< Read by shaders through a sampler.
    STORAGE                  = 1U << 3, ///< Read and written by shaders as a storage image.
    COLOR_ATTACHMENT         = 1U << 4, ///< Rendered to as a color attachment.
    DEPTH_STENCIL_ATTACHMENT = 1U << 5, ///< Rendered to as a depth/stencil attachment.
    TRANSIENT_ATTACHMENT     = 1U << 6, ///< Attachment whose contents never leave the GPU tile memory.
    INPUT_ATTACHMENT         = 1U << 7, ///< Read as an input attachment in fragment shaders.
    SHADING_RATE_ATTACHMENT  = 1U << 8, ///< Holds per-region fragment shading rates.
};

/// @brief Optional behaviors requested when creating an image. Bitmask.
enum class ImageCreationOption : std::uint8_t 
{
    NONE            = 0,      ///< No option.
    CUBE_COMPATIBLE = 1U << 0, ///< Cube and cube-array views may be created (six layers per cube).
    MUTABLE_FORMAT  = 1U << 1, ///< Views may use a different but compatible format.
    ALIASED         = 1U << 2, ///< Shares memory with other resources (render graph aliasing).
    SPARSE_RESIDENCY = 1U << 3, ///< Partially resident: memory is bound per tile (Queue::bind_sparse()).
};

/// @brief Type of an image view.
enum class ImageViewType : std::uint8_t 
{
    ONE_DIMENSIONAL,       ///< 1D view.
    TWO_DIMENSIONAL,       ///< 2D view.
    THREE_DIMENSIONAL,     ///< 3D view.
    CUBE,                  ///< Cube view (six 2D layers).
    ONE_DIMENSIONAL_ARRAY, ///< Array of 1D layers.
    TWO_DIMENSIONAL_ARRAY, ///< Array of 2D layers.
    CUBE_ARRAY,            ///< Array of cubes.
};

/// @brief Arrangement of texels in memory.
enum class ImageTiling : std::uint8_t 
{
    OPTIMAL, ///< Implementation-defined arrangement; required for most GPU usages.
    LINEAR,  ///< Row-major arrangement, addressable by the CPU.
};

/// @brief Number of samples per texel. Bitmask (a set of counts describes device support).
enum class SampleCount : std::uint8_t 
{
    NONE       = 0,      ///< No sample count.
    ONE        = 1U << 0, ///< 1 sample (no multisampling).
    TWO        = 1U << 1, ///< 2 samples.
    FOUR       = 1U << 2, ///< 4 samples.
    EIGHT      = 1U << 3, ///< 8 samples.
    SIXTEEN    = 1U << 4, ///< 16 samples.
    THIRTY_TWO = 1U << 5, ///< 32 samples.
    SIXTY_FOUR = 1U << 6, ///< 64 samples.
};

/// @brief Memory layout of an image, required by the GPU for each kind of access.
enum class ImageLayout : std::uint8_t 
{
    UNDEFINED,                ///< Contents are undefined; valid only as the source of a transition.
    GENERAL,                  ///< Any access; required for storage images.
    COLOR_ATTACHMENT,         ///< Color attachment reads and writes.
    DEPTH_STENCIL_ATTACHMENT, ///< Depth/stencil attachment reads and writes.
    DEPTH_STENCIL_READ_ONLY,  ///< Depth/stencil test and shader reads without writes.
    SHADER_READ_ONLY,         ///< Sampled and input-attachment reads.
    TRANSFER_SOURCE,          ///< Source of copy and blit commands.
    TRANSFER_DESTINATION,     ///< Destination of copy, blit and clear commands.
    PRESENT_SOURCE,           ///< Presentation to a swapchain.
    SHADING_RATE_ATTACHMENT,  ///< Read as a fragment shading rate attachment.
};

/// @brief Source of each component of an image view.
enum class ComponentSwizzle : std::uint8_t 
{
    IDENTITY, ///< The same component.
    ZERO,     ///< Constant 0.
    ONE,      ///< Constant 1.
    RED,      ///< The red component.
    GREEN,    ///< The green component.
    BLUE,     ///< The blue component.
    ALPHA,    ///< The alpha component.
};

/// @brief Whether a resource is owned by one queue family at a time or shared by several.
enum class SharingMode : std::uint8_t 
{
    EXCLUSIVE,  ///< One owner; cross-family use requires ownership-transfer barriers.
    CONCURRENT, ///< Shared by the listed queue families without ownership transfers.
};

// =================================================================================================
// Synchronization and render graph states
// =================================================================================================

/// @brief Pipeline stages, used by barriers and semaphore waits (synchronization2 model). Bitmask.
enum class PipelineStage : std::uint32_t 
{
    NONE                           = 0,       ///< No stage.
    DRAW_INDIRECT                  = 1U << 0,  ///< Reading indirect arguments.
    VERTEX_INPUT                   = 1U << 1,  ///< Reading vertex and index buffers.
    VERTEX_SHADER                  = 1U << 2,  ///< Vertex shader.
    TESSELLATION_CONTROL_SHADER    = 1U << 3,  ///< Tessellation control shader.
    TESSELLATION_EVALUATION_SHADER = 1U << 4,  ///< Tessellation evaluation shader.
    GEOMETRY_SHADER                = 1U << 5,  ///< Geometry shader.
    FRAGMENT_SHADER                = 1U << 6,  ///< Fragment shader.
    EARLY_FRAGMENT_TESTS           = 1U << 7,  ///< Depth/stencil tests before fragment shading.
    LATE_FRAGMENT_TESTS            = 1U << 8,  ///< Depth/stencil tests after fragment shading.
    COLOR_ATTACHMENT_OUTPUT        = 1U << 9,  ///< Blending and color attachment writes.
    COMPUTE_SHADER                 = 1U << 10, ///< Compute shader.
    TRANSFER                       = 1U << 11, ///< Copy, blit, fill, clear and resolve commands.
    HOST                           = 1U << 12, ///< CPU access to mapped memory.
    TASK_SHADER                    = 1U << 13, ///< Task (amplification) shader.
    MESH_SHADER                    = 1U << 14, ///< Mesh shader.
    RAY_TRACING_SHADER             = 1U << 15, ///< Ray tracing shaders.
    ACCELERATION_STRUCTURE_BUILD   = 1U << 16, ///< Acceleration structure builds.
    ALL_GRAPHICS                   = 1U << 17, ///< Every graphics stage.
    ALL_COMMANDS                   = 1U << 18, ///< Every stage of every command.
    SHADING_RATE_ATTACHMENT        = 1U << 19, ///< Reading the fragment shading rate attachment.
};

/// @brief Memory accesses, used by barriers (synchronization2 model). Bitmask.
enum class MemoryAccess : std::uint32_t 
{
    NONE                           = 0,       ///< No access.
    INDIRECT_COMMAND_READ          = 1U << 0,  ///< Indirect argument reads.
    INDEX_READ                     = 1U << 1,  ///< Index buffer reads.
    VERTEX_ATTRIBUTE_READ          = 1U << 2,  ///< Vertex buffer reads.
    UNIFORM_READ                   = 1U << 3,  ///< Uniform buffer reads.
    INPUT_ATTACHMENT_READ          = 1U << 4,  ///< Input attachment reads.
    SHADER_SAMPLED_READ            = 1U << 5,  ///< Sampled image and uniform texel buffer reads.
    SHADER_STORAGE_READ            = 1U << 6,  ///< Storage buffer, storage image and storage texel reads.
    SHADER_STORAGE_WRITE           = 1U << 7,  ///< Storage buffer, storage image and storage texel writes.
    COLOR_ATTACHMENT_READ          = 1U << 8,  ///< Color attachment reads (blending, load operations).
    COLOR_ATTACHMENT_WRITE         = 1U << 9,  ///< Color attachment writes.
    DEPTH_STENCIL_ATTACHMENT_READ  = 1U << 10, ///< Depth/stencil attachment reads.
    DEPTH_STENCIL_ATTACHMENT_WRITE = 1U << 11, ///< Depth/stencil attachment writes.
    TRANSFER_READ                  = 1U << 12, ///< Copy source reads.
    TRANSFER_WRITE                 = 1U << 13, ///< Copy, fill and clear writes.
    HOST_READ                      = 1U << 14, ///< CPU reads of mapped memory.
    HOST_WRITE                     = 1U << 15, ///< CPU writes of mapped memory.
    MEMORY_READ                    = 1U << 16, ///< Any read.
    MEMORY_WRITE                   = 1U << 17, ///< Any write.
    ACCELERATION_STRUCTURE_READ    = 1U << 18, ///< Acceleration structure reads.
    ACCELERATION_STRUCTURE_WRITE   = 1U << 19, ///< Acceleration structure writes.
    SHADING_RATE_ATTACHMENT_READ   = 1U << 20, ///< Fragment shading rate attachment reads.
};

/// @brief High-level state of a resource between passes. Each state maps to the pipeline stages,
///        memory accesses and image layout of RESOURCE_STATE_SYNCHRONIZATION, which is what a render
///        graph needs to derive barriers from pairs of states.
enum class ResourceState : std::uint8_t 
{
    UNDEFINED,                         ///< Contents are not needed (first use, or discarded).
    GENERAL,                           ///< Any access from any stage; slow, use as a last resort.
    INDIRECT_ARGUMENT,                 ///< Indirect draw or dispatch arguments.
    VERTEX_BUFFER,                     ///< Vertex buffer.
    INDEX_BUFFER,                      ///< Index buffer.
    VERTEX_SHADER_UNIFORM_READ,        ///< Uniform buffer read by vertex shaders.
    VERTEX_SHADER_SAMPLED_READ,        ///< Sampled image or uniform texel buffer read by vertex shaders.
    VERTEX_SHADER_STORAGE_READ,        ///< Storage resource read by vertex shaders.
    FRAGMENT_SHADER_UNIFORM_READ,      ///< Uniform buffer read by fragment shaders.
    FRAGMENT_SHADER_SAMPLED_READ,      ///< Sampled image or uniform texel buffer read by fragment shaders.
    FRAGMENT_SHADER_STORAGE_READ,      ///< Storage resource read by fragment shaders.
    FRAGMENT_SHADER_STORAGE_WRITE,     ///< Storage resource written by fragment shaders.
    FRAGMENT_SHADER_INPUT_ATTACHMENT,  ///< Input attachment read by fragment shaders.
    COMPUTE_SHADER_UNIFORM_READ,       ///< Uniform buffer read by compute shaders.
    COMPUTE_SHADER_SAMPLED_READ,       ///< Sampled image or uniform texel buffer read by compute shaders.
    COMPUTE_SHADER_STORAGE_READ,       ///< Storage resource read by compute shaders.
    COMPUTE_SHADER_STORAGE_WRITE,      ///< Storage resource written by compute shaders.
    COMPUTE_SHADER_STORAGE_READ_WRITE, ///< Storage resource read and written by compute shaders.
    COLOR_ATTACHMENT_READ,             ///< Color attachment read (load operation or blending).
    COLOR_ATTACHMENT_WRITE,            ///< Color attachment written.
    COLOR_ATTACHMENT_READ_WRITE,       ///< Color attachment read and written (blending).
    DEPTH_STENCIL_ATTACHMENT_READ,     ///< Depth/stencil attachment tested without writes.
    DEPTH_STENCIL_ATTACHMENT_WRITE,    ///< Depth/stencil attachment tested and written.
    TRANSFER_SOURCE,                   ///< Source of copy commands.
    TRANSFER_DESTINATION,              ///< Destination of copy, fill and clear commands.
    HOST_READ,                         ///< Read by the CPU through mapped memory.
    HOST_WRITE,                        ///< Written by the CPU through mapped memory.
    PRESENT,                           ///< Presented by a swapchain.
    SHADER_SAMPLED_READ,               ///< Sampled by any shader stage (bindless textures).
    SHADER_STORAGE_READ,               ///< Storage resource read by any shader stage (bindless).
    SHADER_STORAGE_READ_WRITE,         ///< Storage resource read and written by any shader stage (bindless).
    ACCELERATION_STRUCTURE_BUILD_INPUT,///< Geometry, transforms or instances read by acceleration structure builds.
    ACCELERATION_STRUCTURE_WRITE,      ///< Acceleration structure being built, updated or copied.
    ACCELERATION_STRUCTURE_READ,       ///< Acceleration structure traced by shaders or read by builds and copies.
    SHADING_RATE_ATTACHMENT,           ///< Fragment shading rate attachment.
};

/// @brief Result of waiting on a fence or semaphore with a timeout.
enum class WaitStatus : std::uint8_t 
{
    SIGNALED, ///< The awaited value was reached.
    TIMEOUT,  ///< The timeout expired first.
};

// =================================================================================================
// Rendering
// =================================================================================================

/// @brief What happens to an attachment's contents at the start of rendering.
enum class LoadOperation : std::uint8_t 
{
    LOAD,      ///< Preserve the previous contents.
    CLEAR,     ///< Clear to the attachment's clear value.
    DONT_CARE, ///< Previous contents are not needed.
};

/// @brief What happens to an attachment's contents at the end of rendering.
enum class StoreOperation : std::uint8_t 
{
    STORE,     ///< Write the rendered contents to memory.
    DONT_CARE, ///< Rendered contents are not needed afterwards.
    NONE,      ///< Contents are not accessed and stay unchanged.
};

/// @brief How a multisampled attachment is resolved into a single-sampled one.
enum class ResolveMode : std::uint8_t 
{
    NONE,        ///< No resolve.
    AVERAGE,     ///< Average of the samples (color only).
    MINIMUM,     ///< Smallest sample.
    MAXIMUM,     ///< Largest sample.
    SAMPLE_ZERO, ///< First sample.
};

/// @brief Comparison used by depth/stencil tests and comparison samplers.
enum class CompareOperation : std::uint8_t 
{
    NEVER,            ///< Never passes.
    LESS,             ///< Passes if new < stored.
    EQUAL,            ///< Passes if new == stored.
    LESS_OR_EQUAL,    ///< Passes if new <= stored.
    GREATER,          ///< Passes if new > stored.
    NOT_EQUAL,        ///< Passes if new != stored.
    GREATER_OR_EQUAL, ///< Passes if new >= stored.
    ALWAYS,           ///< Always passes.
};

/// @brief Texel filtering for magnification and minification.
enum class Filter : std::uint8_t 
{
    NEAREST, ///< Closest texel.
    LINEAR,  ///< Weighted average of neighboring texels.
};

/// @brief Filtering between mipmap levels.
enum class SamplerMipmapMode : std::uint8_t 
{
    NEAREST, ///< Closest level.
    LINEAR,  ///< Blend of the two closest levels.
};

/// @brief Behavior of texture coordinates outside [0, 1].
enum class SamplerAddressMode : std::uint8_t 
{
    REPEAT,               ///< Wrap around.
    MIRRORED_REPEAT,      ///< Wrap around, mirroring every other repetition.
    CLAMP_TO_EDGE,        ///< Clamp to the edge texel.
    CLAMP_TO_BORDER,      ///< Use the sampler's border color.
    MIRROR_CLAMP_TO_EDGE, ///< Mirror once, then clamp to the edge texel.
};

/// @brief Border color of samplers using SamplerAddressMode::CLAMP_TO_BORDER.
enum class BorderColor : std::uint8_t 
{
    TRANSPARENT_BLACK_FLOAT,   ///< (0, 0, 0, 0) for floating-point formats.
    TRANSPARENT_BLACK_INTEGER, ///< (0, 0, 0, 0) for integer formats.
    OPAQUE_BLACK_FLOAT,        ///< (0, 0, 0, 1) for floating-point formats.
    OPAQUE_BLACK_INTEGER,      ///< (0, 0, 0, 1) for integer formats.
    OPAQUE_WHITE_FLOAT,        ///< (1, 1, 1, 1) for floating-point formats.
    OPAQUE_WHITE_INTEGER,      ///< (1, 1, 1, 1) for integer formats.
};

/// @brief How vertices are assembled into primitives.
enum class PrimitiveTopology : std::uint8_t 
{
    POINT_LIST,     ///< Independent points.
    LINE_LIST,      ///< Independent lines.
    LINE_STRIP,     ///< Connected lines.
    TRIANGLE_LIST,  ///< Independent triangles.
    TRIANGLE_STRIP, ///< Connected triangles sharing an edge.
    TRIANGLE_FAN,   ///< Triangles sharing the first vertex.
    PATCH_LIST,     ///< Tessellation patches.
};

/// @brief How polygons are rasterized.
enum class PolygonMode : std::uint8_t 
{
    FILL,  ///< Filled polygons.
    LINE,  ///< Polygon edges only (requires DeviceFeature::FILL_MODE_NON_SOLID).
    POINT, ///< Polygon vertices only (requires DeviceFeature::FILL_MODE_NON_SOLID).
};

/// @brief Which polygon faces are discarded.
enum class CullMode : std::uint8_t 
{
    NONE,           ///< No culling.
    FRONT,          ///< Discard front faces.
    BACK,           ///< Discard back faces.
    FRONT_AND_BACK, ///< Discard every polygon.
};

/// @brief Vertex winding order that defines a front face.
enum class FrontFace : std::uint8_t 
{
    COUNTER_CLOCKWISE, ///< Counter-clockwise polygons face the viewer.
    CLOCKWISE,         ///< Clockwise polygons face the viewer.
};

/// @brief Blend factor applied to the source or destination color.
enum class BlendFactor : std::uint8_t 
{
    ZERO,                         ///< 0.
    ONE,                          ///< 1.
    SOURCE_COLOR,                 ///< Source RGB.
    ONE_MINUS_SOURCE_COLOR,       ///< 1 - source RGB.
    DESTINATION_COLOR,            ///< Destination RGB.
    ONE_MINUS_DESTINATION_COLOR,  ///< 1 - destination RGB.
    SOURCE_ALPHA,                 ///< Source alpha.
    ONE_MINUS_SOURCE_ALPHA,       ///< 1 - source alpha.
    DESTINATION_ALPHA,            ///< Destination alpha.
    ONE_MINUS_DESTINATION_ALPHA,  ///< 1 - destination alpha.
    CONSTANT_COLOR,               ///< Blend constant RGB.
    ONE_MINUS_CONSTANT_COLOR,     ///< 1 - blend constant RGB.
    CONSTANT_ALPHA,               ///< Blend constant alpha.
    ONE_MINUS_CONSTANT_ALPHA,     ///< 1 - blend constant alpha.
    SOURCE_ALPHA_SATURATE,        ///< min(source alpha, 1 - destination alpha).
    SECOND_SOURCE_COLOR,          ///< Second fragment output RGB (dual-source blending).
    ONE_MINUS_SECOND_SOURCE_COLOR,///< 1 - second fragment output RGB.
    SECOND_SOURCE_ALPHA,          ///< Second fragment output alpha.
    ONE_MINUS_SECOND_SOURCE_ALPHA,///< 1 - second fragment output alpha.
};

/// @brief Operation combining the weighted source and destination colors.
enum class BlendOperation : std::uint8_t 
{
    ADD,              ///< source + destination.
    SUBTRACT,         ///< source - destination.
    REVERSE_SUBTRACT, ///< destination - source.
    MINIMUM,          ///< min(source, destination).
    MAXIMUM,          ///< max(source, destination).
};

/// @brief Color components written to an attachment. Bitmask.
enum class ColorComponent : std::uint8_t 
{
    NONE  = 0,       ///< No component.
    RED   = 1U << 0, ///< Red.
    GREEN = 1U << 1, ///< Green.
    BLUE  = 1U << 2, ///< Blue.
    ALPHA = 1U << 3, ///< Alpha.
    ALL   = RED | GREEN | BLUE | ALPHA, ///< Every component.
};

/// @brief Action on the stencil value after a stencil or depth test.
enum class StencilOperation : std::uint8_t 
{
    KEEP,                ///< Keep the stored value.
    ZERO,                ///< Set to 0.
    REPLACE,             ///< Set to the reference value.
    INCREMENT_AND_CLAMP, ///< Increment, clamping to the maximum.
    DECREMENT_AND_CLAMP, ///< Decrement, clamping to 0.
    INVERT,              ///< Bitwise inversion.
    INCREMENT_AND_WRAP,  ///< Increment, wrapping to 0.
    DECREMENT_AND_WRAP,  ///< Decrement, wrapping to the maximum.
};

/// @brief Type of the indices in an index buffer.
enum class IndexType : std::uint8_t 
{
    UNSIGNED_INTEGER_16, ///< 16-bit indices.
    UNSIGNED_INTEGER_32, ///< 32-bit indices.
};

/// @brief Rate at which a vertex buffer binding advances.
enum class VertexInputRate : std::uint8_t 
{
    VERTEX,   ///< Per vertex.
    INSTANCE, ///< Per instance.
};

// =================================================================================================
// Shaders, descriptors and pipelines
// =================================================================================================

/// @brief Shader stages. Bitmask.
enum class ShaderStage : std::uint16_t 
{
    NONE                    = 0,        ///< No stage.
    VERTEX                  = 1U << 0,  ///< Vertex shader.
    TESSELLATION_CONTROL    = 1U << 1,  ///< Tessellation control shader.
    TESSELLATION_EVALUATION = 1U << 2,  ///< Tessellation evaluation shader.
    GEOMETRY                = 1U << 3,  ///< Geometry shader.
    FRAGMENT                = 1U << 4,  ///< Fragment shader.
    COMPUTE                 = 1U << 5,  ///< Compute shader.
    TASK                    = 1U << 6,  ///< Task (amplification) shader.
    MESH                    = 1U << 7,  ///< Mesh shader.
    RAY_GENERATION          = 1U << 8,  ///< Ray generation shader.
    ANY_HIT                 = 1U << 9,  ///< Any-hit shader.
    CLOSEST_HIT             = 1U << 10, ///< Closest-hit shader.
    MISS                    = 1U << 11, ///< Miss shader.
    INTERSECTION            = 1U << 12, ///< Intersection shader.
    CALLABLE                = 1U << 13, ///< Callable shader.
    ALL_GRAPHICS = VERTEX | TESSELLATION_CONTROL | TESSELLATION_EVALUATION | GEOMETRY | FRAGMENT, ///< Classic graphics stages.
    ALL = ALL_GRAPHICS | COMPUTE | TASK | MESH | RAY_GENERATION | ANY_HIT | CLOSEST_HIT | MISS | INTERSECTION | CALLABLE, ///< Every stage.
};

/// @brief Form in which shader code is handed to the library.
enum class ShaderSourceFormat : std::uint8_t 
{
    GLSL,  ///< GLSL source text, compiled to SPIR-V at run time (Debug builds only; see SHADER_HOT_RELOAD).
    SPIRV, ///< SPIR-V binary compiled at build time (gfx_add_shaders) or by any other compiler.
    HLSL,  ///< HLSL source text, compiled to SPIR-V at run time with DXC (Debug builds only).
};

/// @brief Kind of resource bound to a descriptor.
enum class DescriptorType : std::uint8_t 
{
    SAMPLER,                ///< Sampler only.
    COMBINED_IMAGE_SAMPLER, ///< Sampled image and sampler together (GLSL sampler2D, ...).
    SAMPLED_IMAGE,          ///< Sampled image without sampler (GLSL texture2D, ...).
    STORAGE_IMAGE,          ///< Read/write image (GLSL image2D, ...).
    UNIFORM_TEXEL_BUFFER,   ///< Formatted read-only buffer view (GLSL samplerBuffer).
    STORAGE_TEXEL_BUFFER,   ///< Formatted read/write buffer view (GLSL imageBuffer).
    UNIFORM_BUFFER,         ///< Uniform block.
    STORAGE_BUFFER,         ///< Storage (shader storage) block.
    UNIFORM_BUFFER_DYNAMIC, ///< Uniform block with an offset supplied at bind time.
    STORAGE_BUFFER_DYNAMIC, ///< Storage block with an offset supplied at bind time.
    INPUT_ATTACHMENT,       ///< Attachment read in the fragment shader (GLSL subpassInput).
    ACCELERATION_STRUCTURE, ///< Ray tracing acceleration structure.
};

/// @brief Optional behaviors of a descriptor binding (descriptor indexing). Bitmask.
enum class DescriptorBindingOption : std::uint8_t 
{
    NONE                        = 0,      ///< No option.
    UPDATE_AFTER_BIND           = 1U << 0, ///< Descriptors may be updated after the set is bound.
    UPDATE_UNUSED_WHILE_PENDING = 1U << 1, ///< Unused descriptors may be updated while the set is in use.
    PARTIALLY_BOUND             = 1U << 2, ///< Descriptors not accessed by shaders may stay unwritten.
    VARIABLE_DESCRIPTOR_COUNT   = 1U << 3, ///< The last binding's array size is chosen at allocation.
};

/// @brief Pipeline category a descriptor set or push constants are bound to.
enum class PipelineBindPoint : std::uint8_t 
{
    GRAPHICS,    ///< Graphics pipelines.
    COMPUTE,     ///< Compute pipelines.
    RAY_TRACING, ///< Ray tracing pipelines.
};

// =================================================================================================
// Commands and queries
// =================================================================================================

/// @brief Whether a command buffer is submitted directly or executed by another one.
enum class CommandBufferLevel : std::uint8_t 
{
    PRIMARY,   ///< Submitted to a queue.
    SECONDARY, ///< Records part of a parallel rendering pass; executed by the primary that began the pass.
};

/// @brief Optional behaviors of a command pool. Bitmask.
enum class CommandPoolOption : std::uint8_t 
{
    NONE                         = 0,      ///< No option.
    TRANSIENT                    = 1U << 0, ///< Command buffers are short-lived (re-recorded every frame).
    RESETTABLE_COMMAND_BUFFERS   = 1U << 1, ///< Command buffers may be reset individually.
};

/// @brief Kind of query stored in a query pool.
enum class QueryType : std::uint8_t 
{
    OCCLUSION,           ///< Number of samples passing the depth/stencil tests.
    PIPELINE_STATISTICS, ///< Counters selected with PipelineStatistic.
    TIMESTAMP,           ///< GPU timestamps.
    ACCELERATION_STRUCTURE_COMPACTED_SIZE, ///< Size an acceleration structure takes once compacted.
};

/// @brief Counters collected by pipeline statistics queries. Bitmask.
enum class PipelineStatistic : std::uint8_t 
{
    NONE                        = 0,      ///< No counter.
    INPUT_ASSEMBLY_VERTICES     = 1U << 0, ///< Vertices assembled.
    INPUT_ASSEMBLY_PRIMITIVES   = 1U << 1, ///< Primitives assembled.
    VERTEX_SHADER_INVOCATIONS   = 1U << 2, ///< Vertex shader invocations.
    CLIPPING_INVOCATIONS        = 1U << 3, ///< Primitives processed by clipping.
    CLIPPING_PRIMITIVES         = 1U << 4, ///< Primitives output by clipping.
    FRAGMENT_SHADER_INVOCATIONS = 1U << 5, ///< Fragment shader invocations.
    COMPUTE_SHADER_INVOCATIONS  = 1U << 6, ///< Compute shader invocations.
};

// =================================================================================================
// Presentation
// =================================================================================================

/// @brief How a swapchain queues images for display.
enum class PresentMode : std::uint8_t 
{
    IMMEDIATE,    ///< No vertical synchronization; may tear.
    MAILBOX,      ///< Vertical synchronization; the newest image replaces any queued one.
    FIFO,         ///< Vertical synchronization; images queue in order. Always supported.
    FIFO_RELAXED, ///< Like FIFO, but late images are shown immediately (may tear).
};

/// @brief Color space of swapchain images.
enum class ColorSpace : std::uint8_t 
{
    SRGB_NONLINEAR,       ///< sRGB with its non-linear transfer function.
    EXTENDED_SRGB_LINEAR, ///< scRGB, linear transfer function.
    DISPLAY_P3_NONLINEAR, ///< Display P3 with the sRGB transfer function.
    HDR10_ST2084,         ///< HDR10 with the ST 2084 (PQ) transfer function.
};

/// @brief Outcome of acquiring or presenting a swapchain image.
enum class SwapchainStatus : std::uint8_t 
{
    OPTIMAL,      ///< The swapchain matches the surface.
    SUBOPTIMAL,   ///< Usable, but should be recreated (e.g. after a resize).
    OUT_OF_DATE,  ///< Unusable; must be recreated.
    SURFACE_LOST, ///< The window surface no longer exists.
};

// =================================================================================================
// Diagnostics
// =================================================================================================

/// @brief Severity of a message reported by a backend's debug facilities. Bitmask.
enum class DebugMessageSeverity : std::uint8_t 
{
    NONE        = 0,      ///< No severity.
    VERBOSE     = 1U << 0, ///< Diagnostic chatter.
    INFORMATION = 1U << 1, ///< Informational messages.
    WARNING     = 1U << 2, ///< Probable mistakes or performance problems.
    CRITICAL    = 1U << 3, ///< Invalid usage; behavior is undefined.
};

/// @brief Category of a message reported by a backend's debug facilities. Bitmask.
enum class DebugMessageType : std::uint8_t 
{
    NONE        = 0,      ///< No category.
    GENERAL     = 1U << 0, ///< General events.
    VALIDATION  = 1U << 1, ///< Specification violations.
    PERFORMANCE = 1U << 2, ///< Suboptimal usage.
};

/// @brief Why a shader's interface does not match a pipeline layout, vertex layout or attachments.
enum class LayoutMismatch : std::uint8_t
{
    NONE,                              ///< The layouts are compatible.
    WRONG_SHADER_STAGE,                ///< The shader was compiled for another stage than the pipeline slot.
    BINDING_MISSING_FROM_LAYOUT,       ///< The shader uses a (set, binding) the pipeline layout lacks.
    DESCRIPTOR_TYPE_MISMATCH,          ///< The shader and the layout declare different descriptor types.
    DESCRIPTOR_COUNT_MISMATCH,         ///< The shader and the layout declare different array sizes.
    STAGE_NOT_VISIBLE,                 ///< The layout does not make the binding visible to the shader's stage.
    PUSH_CONSTANTS_OUTSIDE_RANGE,      ///< The shader's push-constant bytes are not inside the layout's range.
    PUSH_CONSTANT_STAGE_NOT_VISIBLE,   ///< The layout's push constants are not visible to the shader's stage.
    VERTEX_INPUT_WITHOUT_ATTRIBUTE,    ///< A vertex shader input has no vertex attribute at its location.
    VERTEX_INPUT_TYPE_MISMATCH,        ///< A vertex attribute's format does not match the input's type.
    FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT,///< A fragment shader output has no color attachment at its location.
    FRAGMENT_OUTPUT_TYPE_MISMATCH,     ///< A color attachment's format does not match the output's type.
    CONFLICTING_BINDINGS,              ///< Two shaders declare the same (set, binding) with different types.
    STAGE_INTERFACE_MISMATCH,          ///< A stage reads a location the previous stage does not write, or with another type.
    UNKNOWN_SPECIALIZATION_CONSTANT,   ///< A pipeline sets a specialization constant no shader declares.
};

/// @brief Outcome of reloading one changed shader file (Debug builds).
enum class ShaderReloadResult : std::uint8_t
{
    RELOADED,           ///< The new code replaced the old one; pipelines using it were rebuilt.
    COMPILATION_FAILED, ///< The file does not compile; the old code stays in use.
    LAYOUT_MISMATCH,    ///< The new code no longer fits a pipeline layout using it; the old code stays in use.
};

// =================================================================================================
// Validation, bindless resources, pipelines, ray tracing and diagnostics
// =================================================================================================

/// @brief How much the library checks. Levels are ordered: each includes the checks of the previous one.
enum class ValidationLevel : std::uint8_t
{
    NONE,     ///< No checks: invalid usage is undefined behavior (the native API's own rules apply).
    CREATION, ///< Checks when objects are created and recordings begin; nothing per command (Release default).
    COMMANDS, ///< Also checks every command, descriptor write and push-constant write (Debug default).
};

/// @brief Kind of resource in the bindless table. The enumerator's value is its binding index in
///        descriptor set BINDLESS_SET.
enum class BindlessResourceKind : std::uint8_t
{
    SAMPLED_IMAGE,          ///< Images read through samplers (GLSL texture2D[], HLSL Texture2D[]).
    STORAGE_IMAGE,          ///< Images read and written by shaders (GLSL image2D[], HLSL RWTexture2D[]).
    SAMPLER,                ///< Samplers (GLSL sampler[], HLSL SamplerState[]).
    STORAGE_BUFFER,         ///< Buffers read and written by shaders (GLSL buffer blocks, HLSL ByteAddressBuffer[]).
    ACCELERATION_STRUCTURE, ///< Top-level acceleration structures for ray queries and ray tracing.
};

/// @brief Whether a pipeline can be bound.
enum class PipelineStatus : std::uint8_t
{
    READY,              ///< Compiled; can be bound.
    COMPILING,          ///< Still compiling in the background (PipelineCreationMode::ASYNCHRONOUS).
    COMPILATION_FAILED, ///< Compilation failed; wait() rethrows the error.
};

/// @brief When a pipeline is compiled.
enum class PipelineCreationMode : std::uint8_t
{
    IMMEDIATE,    ///< During the creation call.
    ASYNCHRONOUS, ///< In the background, on DeviceDescription::task_scheduler; poll status() before binding.
};

/// @brief Pipeline state set while recording instead of baked into the pipeline. Viewport, scissor,
///        stencil reference and blend constants are always dynamic. Bitmask.
enum class DynamicState : std::uint16_t
{
    NONE               = 0,        ///< Nothing beyond the always-dynamic state.
    DEPTH_BIAS         = 1U << 0,  ///< RenderingCommands::set_depth_bias().
    DEPTH_BOUNDS       = 1U << 1,  ///< RenderingCommands::set_depth_bounds() (DeviceFeature::DEPTH_BOUNDS).
    LINE_WIDTH         = 1U << 2,  ///< RenderingCommands::set_line_width() (DeviceFeature::WIDE_LINES).
    CULL_MODE          = 1U << 3,  ///< set_cull_mode() (DeviceFeature::EXTENDED_DYNAMIC_STATE).
    FRONT_FACE         = 1U << 4,  ///< set_front_face() (EXTENDED_DYNAMIC_STATE).
    PRIMITIVE_TOPOLOGY = 1U << 5,  ///< set_primitive_topology() within the pipeline's topology class (EXTENDED_DYNAMIC_STATE).
    DEPTH_TEST         = 1U << 6,  ///< set_depth_test() (EXTENDED_DYNAMIC_STATE).
    DEPTH_WRITE        = 1U << 7,  ///< set_depth_write() (EXTENDED_DYNAMIC_STATE).
    DEPTH_COMPARE      = 1U << 8,  ///< set_depth_compare() (EXTENDED_DYNAMIC_STATE).
    STENCIL_TEST       = 1U << 9,  ///< set_stencil_test() (EXTENDED_DYNAMIC_STATE).
    STENCIL_OPERATION  = 1U << 10, ///< set_stencil_operation() (EXTENDED_DYNAMIC_STATE).
};

/// @brief Faces a stencil setting applies to. Bitmask.
enum class StencilFace : std::uint8_t
{
    NONE           = 0,             ///< No face.
    FRONT          = 1U << 0,       ///< Front-facing primitives.
    BACK           = 1U << 1,       ///< Back-facing primitives.
    FRONT_AND_BACK = FRONT | BACK,  ///< Both.
};

/// @brief Level of an acceleration structure.
enum class AccelerationStructureType : std::uint8_t
{
    TOP_LEVEL,    ///< Instances of bottom-level structures; what shaders trace.
    BOTTOM_LEVEL, ///< Triangles or bounding boxes.
};

/// @brief Trade-offs of an acceleration structure build. Bitmask.
enum class AccelerationStructureBuildOption : std::uint8_t
{
    NONE              = 0,      ///< Driver defaults.
    ALLOW_UPDATE      = 1U << 0, ///< Later builds may update it in place (animated geometry).
    ALLOW_COMPACTION  = 1U << 1, ///< It may be copied into a smaller, compacted structure.
    PREFER_FAST_TRACE = 1U << 2, ///< Optimize for tracing speed.
    PREFER_FAST_BUILD = 1U << 3, ///< Optimize for build speed.
    MINIMIZE_MEMORY   = 1U << 4, ///< Optimize for size.
};

/// @brief Whether a build creates a structure or refits an existing one.
enum class AccelerationStructureBuildMode : std::uint8_t
{
    BUILD,  ///< Build from scratch.
    UPDATE, ///< Refit the source structure (built with ALLOW_UPDATE) to moved geometry.
};

/// @brief How rays treat a geometry. Bitmask.
enum class GeometryOption : std::uint8_t
{
    NONE                  = 0,      ///< Any-hit shaders run.
    OPAQUE_GEOMETRY       = 1U << 0, ///< No any-hit shader runs for it.
    NO_DUPLICATE_ANY_HIT  = 1U << 1, ///< Its any-hit shader runs at most once per primitive and ray.
};

/// @brief Per-instance options of a top-level acceleration structure (same bits in Vulkan and
///        Direct3D 12). Bitmask.
enum class AccelerationStructureInstanceOption : std::uint8_t
{
    NONE                      = 0,      ///< No option.
    DISABLE_TRIANGLE_CULLING  = 1U << 0, ///< Back-facing triangles are not culled.
    FLIP_TRIANGLE_FACING      = 1U << 1, ///< Clockwise triangles face forward.
    FORCE_OPAQUE_GEOMETRY     = 1U << 2, ///< Treat every geometry as opaque.
    FORCE_NON_OPAQUE_GEOMETRY = 1U << 3, ///< Treat every geometry as non-opaque.
};

/// @brief How an acceleration structure is copied.
enum class AccelerationStructureCopyMode : std::uint8_t
{
    CLONE,   ///< Identical copy.
    COMPACT, ///< Copy into the size reported by an ACCELERATION_STRUCTURE_COMPACTED_SIZE query.
};

/// @brief Kind of shader group of a ray tracing pipeline.
enum class RayTracingShaderGroupType : std::uint8_t
{
    GENERAL,        ///< One ray generation, miss or callable shader.
    TRIANGLES_HIT,  ///< Closest-hit and/or any-hit shaders for triangles.
    PROCEDURAL_HIT, ///< Intersection shader plus optional closest-hit and any-hit shaders.
};

/// @brief GPU crash diagnostics a device collects. Bitmask.
enum class CrashDiagnostic : std::uint8_t
{
    NONE          = 0,      ///< None.
    BREADCRUMBS   = 1U << 0, ///< Debug labels are also written by the GPU as it progresses, so a device loss
                             ///< reports the labels that were active (DeviceFeature::GPU_CRASH_MARKERS).
    VENDOR_REPORT = 1U << 1, ///< Vendor crash dumps (NVIDIA Aftermath, Direct3D 12 DRED) when available.
};

/// @brief Frame capture tool attached to the process.
enum class CaptureTool : std::uint8_t
{
    NONE,            ///< No capture tool.
    RENDERDOC,       ///< RenderDoc.
    PIX,             ///< Microsoft PIX.
    NSIGHT_GRAPHICS, ///< NVIDIA Nsight Graphics.
};

/// @brief How a sampler combines the texels of its footprint.
enum class SamplerReductionMode : std::uint8_t
{
    WEIGHTED_AVERAGE, ///< Filtering (the usual behavior).
    MINIMUM,          ///< Component-wise minimum (DeviceFeature::SAMPLER_FILTER_MINMAX).
    MAXIMUM,          ///< Component-wise maximum (DeviceFeature::SAMPLER_FILTER_MINMAX).
};

/// @brief Pixels covered by one fragment shader invocation (DeviceFeature::VARIABLE_RATE_SHADING).
enum class ShadingRate : std::uint8_t
{
    ONE_BY_ONE,   ///< One invocation per pixel.
    ONE_BY_TWO,   ///< One invocation per 1x2 pixels.
    TWO_BY_ONE,   ///< One invocation per 2x1 pixels.
    TWO_BY_TWO,   ///< One invocation per 2x2 pixels.
    TWO_BY_FOUR,  ///< One invocation per 2x4 pixels.
    FOUR_BY_TWO,  ///< One invocation per 4x2 pixels.
    FOUR_BY_FOUR, ///< One invocation per 4x4 pixels.
};

/// @brief How two shading rates combine (pipeline, primitive and attachment rates).
enum class ShadingRateCombiner : std::uint8_t
{
    KEEP,    ///< Keep the first rate.
    REPLACE, ///< Use the second rate.
    MINIMUM, ///< The finer of the two.
    MAXIMUM, ///< The coarser of the two.
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief Number of bits of a handle value holding the slot index.
inline constexpr std::uint32_t HANDLE_INDEX_BITS = 24;

/// @brief Number of bits of a handle value holding the slot generation.
inline constexpr std::uint32_t HANDLE_GENERATION_BITS = 8;

/// @brief Largest slot index a handle can address.
inline constexpr std::uint32_t MAXIMUM_HANDLE_INDEX = (1U << HANDLE_INDEX_BITS) - 1U;

/// @brief Largest slot generation; generations count from 1 to this value and then wrap to 1.
inline constexpr std::uint32_t MAXIMUM_HANDLE_GENERATION = (1U << HANDLE_GENERATION_BITS) - 1U;

/// @brief Maximum number of color attachments of a rendering pass or graphics pipeline.
inline constexpr std::uint32_t MAXIMUM_COLOR_ATTACHMENTS = 8;

/// @brief Maximum number of vertex attributes of a graphics pipeline.
inline constexpr std::uint32_t MAXIMUM_VERTEX_INPUT_ATTRIBUTES = 16;

/// @brief Maximum number of vertex buffer bindings of a graphics pipeline.
inline constexpr std::uint32_t MAXIMUM_VERTEX_INPUT_BINDINGS = 16;

/// @brief Maximum number of descriptor sets bound at once (and of set layouts in a pipeline layout).
inline constexpr std::uint32_t MAXIMUM_BOUND_DESCRIPTOR_SETS = 8;

/// @brief Push-constant bytes every backend guarantees.
inline constexpr std::uint32_t MAXIMUM_PUSH_CONSTANT_SIZE = 128;

/// @brief First word of every SPIR-V module.
inline constexpr std::uint32_t SPIRV_MAGIC_NUMBER = 0x07230203U;

/// @brief Whether this build of the library is a Debug build.
inline constexpr bool DEBUG_BUILD = GFX_DEBUG_BUILD != 0;

/// @brief Whether shader source is compiled at run time and changed shader files are reloaded
///        (Device::poll_shader_changes()): Debug builds only. Release builds only load SPIR-V compiled
///        at build time: GLSL and HLSL input is rejected and polling for changes does nothing.
inline constexpr bool SHADER_HOT_RELOAD = DEBUG_BUILD;

/// @brief Validation level of instances that do not choose one: every check in Debug builds, only the
///        checks that cost nothing per command in Release builds.
inline constexpr ValidationLevel DEFAULT_VALIDATION_LEVEL = DEBUG_BUILD ? ValidationLevel::COMMANDS : ValidationLevel::CREATION;

/// @brief Features every gfx device has: timelines, dynamic rendering and bindless descriptors.
///        Physical devices lacking one are not offered by Instance::select_physical_device().
inline constexpr std::array<DeviceFeature, 3> CORE_FEATURES{DeviceFeature::TIMELINE_SEMAPHORE, DeviceFeature::DYNAMIC_RENDERING,
                                                             DeviceFeature::DESCRIPTOR_INDEXING};

/// @brief Descriptor set holding the bindless table in every pipeline layout (binding = BindlessResourceKind).
inline constexpr std::uint32_t BINDLESS_SET = 0;

/// @brief Bindless index that names no resource.
inline constexpr std::uint32_t INVALID_BINDLESS_INDEX = std::numeric_limits<std::uint32_t>::max();

/// @brief Default capacity of the bindless sampled images.
inline constexpr std::uint32_t DEFAULT_BINDLESS_SAMPLED_IMAGE_CAPACITY = 65536;

/// @brief Default capacity of the bindless storage images.
inline constexpr std::uint32_t DEFAULT_BINDLESS_STORAGE_IMAGE_CAPACITY = 16384;

/// @brief Default capacity of the bindless samplers (Direct3D 12 allows at most 2048).
inline constexpr std::uint32_t DEFAULT_BINDLESS_SAMPLER_CAPACITY = 2048;

/// @brief Default capacity of the bindless storage buffers.
inline constexpr std::uint32_t DEFAULT_BINDLESS_STORAGE_BUFFER_CAPACITY = 65536;

/// @brief Default capacity of the bindless acceleration structures.
inline constexpr std::uint32_t DEFAULT_BINDLESS_ACCELERATION_STRUCTURE_CAPACITY = 1024;

/// @brief Push-constant bytes of the bindless pipeline layout, visible to every stage (the minimum every
///        backend guarantees, so one layout serves every pipeline).
inline constexpr std::uint32_t BINDLESS_PUSH_CONSTANT_SIZE = 128;

/// @brief Size in bytes of one top-level acceleration structure instance record (Vulkan and Direct3D 12).
inline constexpr std::uint32_t ACCELERATION_STRUCTURE_INSTANCE_SIZE = 64;

/// @brief Buffer range extending to the end of the buffer.
inline constexpr std::uint64_t WHOLE_SIZE = std::numeric_limits<std::uint64_t>::max();

/// @brief Mip-level count covering every level from the base level to the last one.
inline constexpr std::uint32_t REMAINING_MIP_LEVELS = std::numeric_limits<std::uint32_t>::max();

/// @brief Array-layer count covering every layer from the base layer to the last one.
inline constexpr std::uint32_t REMAINING_ARRAY_LAYERS = std::numeric_limits<std::uint32_t>::max();

/// @brief Timeout that never expires.
inline constexpr std::chrono::nanoseconds WAIT_INDEFINITELY = std::chrono::nanoseconds::max();

// =================================================================================================
// Enumeration descriptions
// =================================================================================================

/// @brief Associates an enumerator with its spelling, for diagnostics and formatting.
template<class Enumeration>
struct EnumeratorName 
{
    Enumeration value;     ///< Enumerator.
    std::string_view name; ///< Its spelling, without the enumeration name.
};

/// @brief Compile-time description of an enumeration. Specialized for every enumeration of the
///        library with `IS_BITMASK` and `NAMES` (the single-bit enumerators of a bitmask, or every
///        enumerator of a sequential enumeration in declaration order).
template<class Enumeration>
struct EnumerationTraits;

/// @brief An enumeration with an EnumerationTraits specialization.
template<class Enumeration>
concept DescribedEnumeration = std::is_enum_v<Enumeration> // Must be an enumeration type
                                    && requires {
                                        { EnumerationTraits<Enumeration>::IS_BITMASK } -> std::convertible_to<bool>; // Must have a specialization with IS_BITMASK
                                        EnumerationTraits<Enumeration>::NAMES.size(); // Must have a specialization with NAMES
                                    };

/// @brief An enumeration whose enumerators are single bits combined with |, & and ^.
template<class Enumeration>
concept BitmaskEnumeration = DescribedEnumeration<Enumeration> 
                                && EnumerationTraits<Enumeration>::IS_BITMASK;

/// @brief An enumeration whose enumerators are numbered 0, 1, 2, ... without gaps.
template<class Enumeration>
concept SequentialEnumeration = DescribedEnumeration<Enumeration> 
                                    && !EnumerationTraits<Enumeration>::IS_BITMASK;

/// @brief Number of enumerators of a sequential enumeration (e.g. to size translation tables).
template<SequentialEnumeration Enumeration>
inline constexpr std::size_t ENUMERATOR_COUNT = EnumerationTraits<Enumeration>::NAMES.size();

template<> 
struct EnumerationTraits<BackendKind> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{BackendKind::NULL_BACKEND, "NULL_BACKEND"}, 
        EnumeratorName{BackendKind::VULKAN, "VULKAN"},
        EnumeratorName{BackendKind::DIRECT3D12, "DIRECT3D12"}, 
        EnumeratorName{BackendKind::METAL, "METAL"},
    };
};

template<> 
struct EnumerationTraits<QueueType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{QueueType::GRAPHICS, "GRAPHICS"}, 
        EnumeratorName{QueueType::COMPUTE, "COMPUTE"},
        EnumeratorName{QueueType::TRANSFER, "TRANSFER"}, 
        EnumeratorName{QueueType::SPARSE_BINDING, "SPARSE_BINDING"},
    };
};

template<> 
struct EnumerationTraits<QueueCapability> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{QueueCapability::GRAPHICS, "GRAPHICS"}, 
        EnumeratorName{QueueCapability::COMPUTE, "COMPUTE"},
        EnumeratorName{QueueCapability::TRANSFER, "TRANSFER"}, 
        EnumeratorName{QueueCapability::SPARSE_BINDING, "SPARSE_BINDING"},
    };
};

template<> 
struct EnumerationTraits<PhysicalDeviceType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PhysicalDeviceType::OTHER, "OTHER"}, 
        EnumeratorName{PhysicalDeviceType::INTEGRATED_GPU, "INTEGRATED_GPU"},
        EnumeratorName{PhysicalDeviceType::DISCRETE_GPU, "DISCRETE_GPU"}, 
        EnumeratorName{PhysicalDeviceType::VIRTUAL_GPU, "VIRTUAL_GPU"},
        EnumeratorName{PhysicalDeviceType::CPU, "CPU"},
    };
};

template<> 
struct EnumerationTraits<PhysicalDevicePreference> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PhysicalDevicePreference::HIGH_PERFORMANCE, "HIGH_PERFORMANCE"},
        EnumeratorName{PhysicalDevicePreference::LOW_POWER, "LOW_POWER"},
    };
};

template<> 
struct EnumerationTraits<DeviceFeature> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{DeviceFeature::TIMELINE_SEMAPHORE, "TIMELINE_SEMAPHORE"},
        EnumeratorName{DeviceFeature::EVENTS, "EVENTS"},
        EnumeratorName{DeviceFeature::DYNAMIC_RENDERING, "DYNAMIC_RENDERING"},
        EnumeratorName{DeviceFeature::DESCRIPTOR_INDEXING, "DESCRIPTOR_INDEXING"},
        EnumeratorName{DeviceFeature::BUFFER_DEVICE_ADDRESS, "BUFFER_DEVICE_ADDRESS"},
        EnumeratorName{DeviceFeature::ASYNCHRONOUS_COMPUTE_QUEUE, "ASYNCHRONOUS_COMPUTE_QUEUE"},
        EnumeratorName{DeviceFeature::DEDICATED_TRANSFER_QUEUE, "DEDICATED_TRANSFER_QUEUE"},
        EnumeratorName{DeviceFeature::SPARSE_BINDING, "SPARSE_BINDING"},
        EnumeratorName{DeviceFeature::SAMPLER_ANISOTROPY, "SAMPLER_ANISOTROPY"},
        EnumeratorName{DeviceFeature::MULTI_DRAW_INDIRECT, "MULTI_DRAW_INDIRECT"},
        EnumeratorName{DeviceFeature::DRAW_INDIRECT_COUNT, "DRAW_INDIRECT_COUNT"},
        EnumeratorName{DeviceFeature::GEOMETRY_SHADER, "GEOMETRY_SHADER"},
        EnumeratorName{DeviceFeature::TESSELLATION_SHADER, "TESSELLATION_SHADER"},
        EnumeratorName{DeviceFeature::MESH_SHADER, "MESH_SHADER"},
        EnumeratorName{DeviceFeature::RAY_TRACING, "RAY_TRACING"},
        EnumeratorName{DeviceFeature::SHADER_FLOAT16, "SHADER_FLOAT16"},
        EnumeratorName{DeviceFeature::SHADER_FLOAT64, "SHADER_FLOAT64"},
        EnumeratorName{DeviceFeature::SHADER_INTEGER16, "SHADER_INTEGER16"},
        EnumeratorName{DeviceFeature::SHADER_INTEGER64, "SHADER_INTEGER64"},
        EnumeratorName{DeviceFeature::FILL_MODE_NON_SOLID, "FILL_MODE_NON_SOLID"},
        EnumeratorName{DeviceFeature::WIDE_LINES, "WIDE_LINES"},
        EnumeratorName{DeviceFeature::DEPTH_CLAMP, "DEPTH_CLAMP"},
        EnumeratorName{DeviceFeature::INDEPENDENT_BLEND, "INDEPENDENT_BLEND"},
        EnumeratorName{DeviceFeature::TIMESTAMP_QUERY, "TIMESTAMP_QUERY"},
        EnumeratorName{DeviceFeature::PIPELINE_STATISTICS_QUERY, "PIPELINE_STATISTICS_QUERY"},
        EnumeratorName{DeviceFeature::TEXTURE_COMPRESSION_BC, "TEXTURE_COMPRESSION_BC"},
        EnumeratorName{DeviceFeature::TEXTURE_COMPRESSION_ETC2, "TEXTURE_COMPRESSION_ETC2"},
        EnumeratorName{DeviceFeature::TEXTURE_COMPRESSION_ASTC, "TEXTURE_COMPRESSION_ASTC"},
        EnumeratorName{DeviceFeature::PRESENTATION, "PRESENTATION"},
        EnumeratorName{DeviceFeature::EXTENDED_DYNAMIC_STATE, "EXTENDED_DYNAMIC_STATE"},
        EnumeratorName{DeviceFeature::DEPTH_BOUNDS, "DEPTH_BOUNDS"},
        EnumeratorName{DeviceFeature::MULTIVIEW, "MULTIVIEW"},
        EnumeratorName{DeviceFeature::VARIABLE_RATE_SHADING, "VARIABLE_RATE_SHADING"},
        EnumeratorName{DeviceFeature::CONSERVATIVE_RASTERIZATION, "CONSERVATIVE_RASTERIZATION"},
        EnumeratorName{DeviceFeature::RAY_QUERY, "RAY_QUERY"},
        EnumeratorName{DeviceFeature::SAMPLER_FILTER_MINMAX, "SAMPLER_FILTER_MINMAX"},
        EnumeratorName{DeviceFeature::MEMORY_BUDGET, "MEMORY_BUDGET"},
        EnumeratorName{DeviceFeature::GPU_CRASH_MARKERS, "GPU_CRASH_MARKERS"},
    };
};

template<> 
struct EnumerationTraits<Format> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{Format::UNDEFINED, "UNDEFINED"},
        EnumeratorName{Format::R8_UNORM, "R8_UNORM"},
        EnumeratorName{Format::R8_SNORM, "R8_SNORM"},
        EnumeratorName{Format::R8_UINT, "R8_UINT"},
        EnumeratorName{Format::R8_SINT, "R8_SINT"},
        EnumeratorName{Format::R8G8_UNORM, "R8G8_UNORM"},
        EnumeratorName{Format::R8G8_SNORM, "R8G8_SNORM"},
        EnumeratorName{Format::R8G8_UINT, "R8G8_UINT"},
        EnumeratorName{Format::R8G8_SINT, "R8G8_SINT"},
        EnumeratorName{Format::R8G8B8A8_UNORM, "R8G8B8A8_UNORM"},
        EnumeratorName{Format::R8G8B8A8_SNORM, "R8G8B8A8_SNORM"},
        EnumeratorName{Format::R8G8B8A8_UINT, "R8G8B8A8_UINT"},
        EnumeratorName{Format::R8G8B8A8_SINT, "R8G8B8A8_SINT"},
        EnumeratorName{Format::R8G8B8A8_SRGB, "R8G8B8A8_SRGB"},
        EnumeratorName{Format::B8G8R8A8_UNORM, "B8G8R8A8_UNORM"},
        EnumeratorName{Format::B8G8R8A8_SRGB, "B8G8R8A8_SRGB"},
        EnumeratorName{Format::A2B10G10R10_UNORM_PACK32, "A2B10G10R10_UNORM_PACK32"},
        EnumeratorName{Format::A2B10G10R10_UINT_PACK32, "A2B10G10R10_UINT_PACK32"},
        EnumeratorName{Format::R16_UNORM, "R16_UNORM"},
        EnumeratorName{Format::R16_SNORM, "R16_SNORM"},
        EnumeratorName{Format::R16_UINT, "R16_UINT"},
        EnumeratorName{Format::R16_SINT, "R16_SINT"},
        EnumeratorName{Format::R16_SFLOAT, "R16_SFLOAT"},
        EnumeratorName{Format::R16G16_UNORM, "R16G16_UNORM"},
        EnumeratorName{Format::R16G16_SNORM, "R16G16_SNORM"},
        EnumeratorName{Format::R16G16_UINT, "R16G16_UINT"},
        EnumeratorName{Format::R16G16_SINT, "R16G16_SINT"},
        EnumeratorName{Format::R16G16_SFLOAT, "R16G16_SFLOAT"},
        EnumeratorName{Format::R16G16B16A16_UNORM, "R16G16B16A16_UNORM"},
        EnumeratorName{Format::R16G16B16A16_SNORM, "R16G16B16A16_SNORM"},
        EnumeratorName{Format::R16G16B16A16_UINT, "R16G16B16A16_UINT"},
        EnumeratorName{Format::R16G16B16A16_SINT, "R16G16B16A16_SINT"},
        EnumeratorName{Format::R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT"},
        EnumeratorName{Format::R32_UINT, "R32_UINT"},
        EnumeratorName{Format::R32_SINT, "R32_SINT"},
        EnumeratorName{Format::R32_SFLOAT, "R32_SFLOAT"},
        EnumeratorName{Format::R32G32_UINT, "R32G32_UINT"},
        EnumeratorName{Format::R32G32_SINT, "R32G32_SINT"},
        EnumeratorName{Format::R32G32_SFLOAT, "R32G32_SFLOAT"},
        EnumeratorName{Format::R32G32B32_UINT, "R32G32B32_UINT"},
        EnumeratorName{Format::R32G32B32_SINT, "R32G32B32_SINT"},
        EnumeratorName{Format::R32G32B32_SFLOAT, "R32G32B32_SFLOAT"},
        EnumeratorName{Format::R32G32B32A32_UINT, "R32G32B32A32_UINT"},
        EnumeratorName{Format::R32G32B32A32_SINT, "R32G32B32A32_SINT"},
        EnumeratorName{Format::R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT"},
        EnumeratorName{Format::B10G11R11_UFLOAT_PACK32, "B10G11R11_UFLOAT_PACK32"},
        EnumeratorName{Format::E5B9G9R9_UFLOAT_PACK32, "E5B9G9R9_UFLOAT_PACK32"},
        EnumeratorName{Format::D16_UNORM, "D16_UNORM"},
        EnumeratorName{Format::X8_D24_UNORM_PACK32, "X8_D24_UNORM_PACK32"},
        EnumeratorName{Format::D32_SFLOAT, "D32_SFLOAT"},
        EnumeratorName{Format::S8_UINT, "S8_UINT"},
        EnumeratorName{Format::D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT"},
        EnumeratorName{Format::D32_SFLOAT_S8_UINT, "D32_SFLOAT_S8_UINT"},
        EnumeratorName{Format::BC1_RGBA_UNORM_BLOCK, "BC1_RGBA_UNORM_BLOCK"},
        EnumeratorName{Format::BC1_RGBA_SRGB_BLOCK, "BC1_RGBA_SRGB_BLOCK"},
        EnumeratorName{Format::BC2_UNORM_BLOCK, "BC2_UNORM_BLOCK"},
        EnumeratorName{Format::BC2_SRGB_BLOCK, "BC2_SRGB_BLOCK"},
        EnumeratorName{Format::BC3_UNORM_BLOCK, "BC3_UNORM_BLOCK"},
        EnumeratorName{Format::BC3_SRGB_BLOCK, "BC3_SRGB_BLOCK"},
        EnumeratorName{Format::BC4_UNORM_BLOCK, "BC4_UNORM_BLOCK"},
        EnumeratorName{Format::BC4_SNORM_BLOCK, "BC4_SNORM_BLOCK"},
        EnumeratorName{Format::BC5_UNORM_BLOCK, "BC5_UNORM_BLOCK"},
        EnumeratorName{Format::BC5_SNORM_BLOCK, "BC5_SNORM_BLOCK"},
        EnumeratorName{Format::BC6H_UFLOAT_BLOCK, "BC6H_UFLOAT_BLOCK"},
        EnumeratorName{Format::BC6H_SFLOAT_BLOCK, "BC6H_SFLOAT_BLOCK"},
        EnumeratorName{Format::BC7_UNORM_BLOCK, "BC7_UNORM_BLOCK"},
        EnumeratorName{Format::BC7_SRGB_BLOCK, "BC7_SRGB_BLOCK"},
        EnumeratorName{Format::ETC2_R8G8B8A8_UNORM_BLOCK, "ETC2_R8G8B8A8_UNORM_BLOCK"},
        EnumeratorName{Format::ETC2_R8G8B8A8_SRGB_BLOCK, "ETC2_R8G8B8A8_SRGB_BLOCK"},
        EnumeratorName{Format::ASTC_4x4_UNORM_BLOCK, "ASTC_4x4_UNORM_BLOCK"},
        EnumeratorName{Format::ASTC_4x4_SRGB_BLOCK, "ASTC_4x4_SRGB_BLOCK"},
    };
};

template<> 
struct EnumerationTraits<FormatNumericType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{FormatNumericType::NONE, "NONE"},
        EnumeratorName{FormatNumericType::UNSIGNED_NORMALIZED, "UNSIGNED_NORMALIZED"},
        EnumeratorName{FormatNumericType::SIGNED_NORMALIZED, "SIGNED_NORMALIZED"},
        EnumeratorName{FormatNumericType::UNSIGNED_INTEGER, "UNSIGNED_INTEGER"},
        EnumeratorName{FormatNumericType::SIGNED_INTEGER, "SIGNED_INTEGER"},
        EnumeratorName{FormatNumericType::UNSIGNED_FLOAT, "UNSIGNED_FLOAT"},
        EnumeratorName{FormatNumericType::SIGNED_FLOAT, "SIGNED_FLOAT"},
        EnumeratorName{FormatNumericType::SRGB, "SRGB"},
    };
};

template<> 
struct EnumerationTraits<ImageAspect> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ImageAspect::COLOR, "COLOR"},
        EnumeratorName{ImageAspect::DEPTH, "DEPTH"},
        EnumeratorName{ImageAspect::STENCIL, "STENCIL"},
    };
};

template<> 
struct EnumerationTraits<MemoryLocation> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{MemoryLocation::DEVICE_LOCAL, "DEVICE_LOCAL"},
        EnumeratorName{MemoryLocation::HOST_UPLOAD, "HOST_UPLOAD"},
        EnumeratorName{MemoryLocation::HOST_READBACK, "HOST_READBACK"},
        EnumeratorName{MemoryLocation::DEVICE_LOCAL_HOST_VISIBLE, "DEVICE_LOCAL_HOST_VISIBLE"},
    };
};

template<> 
struct EnumerationTraits<MemoryProperty> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{MemoryProperty::DEVICE_LOCAL, "DEVICE_LOCAL"},
        EnumeratorName{MemoryProperty::HOST_VISIBLE, "HOST_VISIBLE"},
        EnumeratorName{MemoryProperty::HOST_COHERENT, "HOST_COHERENT"},
        EnumeratorName{MemoryProperty::HOST_CACHED, "HOST_CACHED"},
        EnumeratorName{MemoryProperty::LAZILY_ALLOCATED, "LAZILY_ALLOCATED"},
    };
};

template<> 
struct EnumerationTraits<MemoryDomain> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{MemoryDomain::HOST, "HOST"}, 
        EnumeratorName{MemoryDomain::DEVICE, "DEVICE"},
    };
};

template<> 
struct EnumerationTraits<ResourceCategory> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ResourceCategory::BUFFER, "BUFFER"}, 
        EnumeratorName{ResourceCategory::IMAGE, "IMAGE"},
    };
};

template<> 
struct EnumerationTraits<BufferUsage> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{BufferUsage::TRANSFER_SOURCE, "TRANSFER_SOURCE"},
        EnumeratorName{BufferUsage::TRANSFER_DESTINATION, "TRANSFER_DESTINATION"},
        EnumeratorName{BufferUsage::UNIFORM_TEXEL, "UNIFORM_TEXEL"},
        EnumeratorName{BufferUsage::STORAGE_TEXEL, "STORAGE_TEXEL"},
        EnumeratorName{BufferUsage::UNIFORM, "UNIFORM"},
        EnumeratorName{BufferUsage::STORAGE, "STORAGE"},
        EnumeratorName{BufferUsage::INDEX, "INDEX"},
        EnumeratorName{BufferUsage::VERTEX, "VERTEX"},
        EnumeratorName{BufferUsage::INDIRECT, "INDIRECT"},
        EnumeratorName{BufferUsage::SHADER_DEVICE_ADDRESS, "SHADER_DEVICE_ADDRESS"},
        EnumeratorName{BufferUsage::ACCELERATION_STRUCTURE_STORAGE, "ACCELERATION_STRUCTURE_STORAGE"},
        EnumeratorName{BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT, "ACCELERATION_STRUCTURE_BUILD_INPUT"},
        EnumeratorName{BufferUsage::SHADER_BINDING_TABLE, "SHADER_BINDING_TABLE"},
    };
};

template<> 
struct EnumerationTraits<ImageDimension> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ImageDimension::ONE_DIMENSIONAL, "ONE_DIMENSIONAL"},
        EnumeratorName{ImageDimension::TWO_DIMENSIONAL, "TWO_DIMENSIONAL"},
        EnumeratorName{ImageDimension::THREE_DIMENSIONAL, "THREE_DIMENSIONAL"},
    };
};

template<> 
struct EnumerationTraits<ImageUsage> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ImageUsage::TRANSFER_SOURCE, "TRANSFER_SOURCE"},
        EnumeratorName{ImageUsage::TRANSFER_DESTINATION, "TRANSFER_DESTINATION"},
        EnumeratorName{ImageUsage::SAMPLED, "SAMPLED"},
        EnumeratorName{ImageUsage::STORAGE, "STORAGE"},
        EnumeratorName{ImageUsage::COLOR_ATTACHMENT, "COLOR_ATTACHMENT"},
        EnumeratorName{ImageUsage::DEPTH_STENCIL_ATTACHMENT, "DEPTH_STENCIL_ATTACHMENT"},
        EnumeratorName{ImageUsage::TRANSIENT_ATTACHMENT, "TRANSIENT_ATTACHMENT"},
        EnumeratorName{ImageUsage::INPUT_ATTACHMENT, "INPUT_ATTACHMENT"},
        EnumeratorName{ImageUsage::SHADING_RATE_ATTACHMENT, "SHADING_RATE_ATTACHMENT"},
    };
};

template<> 
struct EnumerationTraits<ImageCreationOption> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ImageCreationOption::CUBE_COMPATIBLE, "CUBE_COMPATIBLE"},
        EnumeratorName{ImageCreationOption::MUTABLE_FORMAT, "MUTABLE_FORMAT"},
        EnumeratorName{ImageCreationOption::ALIASED, "ALIASED"},
        EnumeratorName{ImageCreationOption::SPARSE_RESIDENCY, "SPARSE_RESIDENCY"},
    };
};

template<> 
struct EnumerationTraits<ImageViewType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ImageViewType::ONE_DIMENSIONAL, "ONE_DIMENSIONAL"},
        EnumeratorName{ImageViewType::TWO_DIMENSIONAL, "TWO_DIMENSIONAL"},
        EnumeratorName{ImageViewType::THREE_DIMENSIONAL, "THREE_DIMENSIONAL"},
        EnumeratorName{ImageViewType::CUBE, "CUBE"},
        EnumeratorName{ImageViewType::ONE_DIMENSIONAL_ARRAY, "ONE_DIMENSIONAL_ARRAY"},
        EnumeratorName{ImageViewType::TWO_DIMENSIONAL_ARRAY, "TWO_DIMENSIONAL_ARRAY"},
        EnumeratorName{ImageViewType::CUBE_ARRAY, "CUBE_ARRAY"},
    };
};

template<> 
struct EnumerationTraits<ImageTiling> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ImageTiling::OPTIMAL, "OPTIMAL"}, 
        EnumeratorName{ImageTiling::LINEAR, "LINEAR"},
    };
};

template<> 
struct EnumerationTraits<SampleCount> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{SampleCount::ONE, "ONE"}, 
        EnumeratorName{SampleCount::TWO, "TWO"},
        EnumeratorName{SampleCount::FOUR, "FOUR"}, 
        EnumeratorName{SampleCount::EIGHT, "EIGHT"},
        EnumeratorName{SampleCount::SIXTEEN, "SIXTEEN"}, 
        EnumeratorName{SampleCount::THIRTY_TWO, "THIRTY_TWO"},
        EnumeratorName{SampleCount::SIXTY_FOUR, "SIXTY_FOUR"},
    };
};

template<> 
struct EnumerationTraits<ImageLayout> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ImageLayout::UNDEFINED, "UNDEFINED"},
        EnumeratorName{ImageLayout::GENERAL, "GENERAL"},
        EnumeratorName{ImageLayout::COLOR_ATTACHMENT, "COLOR_ATTACHMENT"},
        EnumeratorName{ImageLayout::DEPTH_STENCIL_ATTACHMENT, "DEPTH_STENCIL_ATTACHMENT"},
        EnumeratorName{ImageLayout::DEPTH_STENCIL_READ_ONLY, "DEPTH_STENCIL_READ_ONLY"},
        EnumeratorName{ImageLayout::SHADER_READ_ONLY, "SHADER_READ_ONLY"},
        EnumeratorName{ImageLayout::TRANSFER_SOURCE, "TRANSFER_SOURCE"},
        EnumeratorName{ImageLayout::TRANSFER_DESTINATION, "TRANSFER_DESTINATION"},
        EnumeratorName{ImageLayout::PRESENT_SOURCE, "PRESENT_SOURCE"},
        EnumeratorName{ImageLayout::SHADING_RATE_ATTACHMENT, "SHADING_RATE_ATTACHMENT"},
    };
};

template<> 
struct EnumerationTraits<ComponentSwizzle> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ComponentSwizzle::IDENTITY, "IDENTITY"}, 
        EnumeratorName{ComponentSwizzle::ZERO, "ZERO"},
        EnumeratorName{ComponentSwizzle::ONE, "ONE"}, 
        EnumeratorName{ComponentSwizzle::RED, "RED"},
        EnumeratorName{ComponentSwizzle::GREEN, "GREEN"}, 
        EnumeratorName{ComponentSwizzle::BLUE, "BLUE"},
        EnumeratorName{ComponentSwizzle::ALPHA, "ALPHA"},
    };
};

template<> 
struct EnumerationTraits<SharingMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{SharingMode::EXCLUSIVE, "EXCLUSIVE"}, 
        EnumeratorName{SharingMode::CONCURRENT, "CONCURRENT"},
    };
};

template<> 
struct EnumerationTraits<PipelineStage> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{PipelineStage::DRAW_INDIRECT, "DRAW_INDIRECT"},
        EnumeratorName{PipelineStage::VERTEX_INPUT, "VERTEX_INPUT"},
        EnumeratorName{PipelineStage::VERTEX_SHADER, "VERTEX_SHADER"},
        EnumeratorName{PipelineStage::TESSELLATION_CONTROL_SHADER, "TESSELLATION_CONTROL_SHADER"},
        EnumeratorName{PipelineStage::TESSELLATION_EVALUATION_SHADER, "TESSELLATION_EVALUATION_SHADER"},
        EnumeratorName{PipelineStage::GEOMETRY_SHADER, "GEOMETRY_SHADER"},
        EnumeratorName{PipelineStage::FRAGMENT_SHADER, "FRAGMENT_SHADER"},
        EnumeratorName{PipelineStage::EARLY_FRAGMENT_TESTS, "EARLY_FRAGMENT_TESTS"},
        EnumeratorName{PipelineStage::LATE_FRAGMENT_TESTS, "LATE_FRAGMENT_TESTS"},
        EnumeratorName{PipelineStage::COLOR_ATTACHMENT_OUTPUT, "COLOR_ATTACHMENT_OUTPUT"},
        EnumeratorName{PipelineStage::COMPUTE_SHADER, "COMPUTE_SHADER"},
        EnumeratorName{PipelineStage::TRANSFER, "TRANSFER"},
        EnumeratorName{PipelineStage::HOST, "HOST"},
        EnumeratorName{PipelineStage::TASK_SHADER, "TASK_SHADER"},
        EnumeratorName{PipelineStage::MESH_SHADER, "MESH_SHADER"},
        EnumeratorName{PipelineStage::RAY_TRACING_SHADER, "RAY_TRACING_SHADER"},
        EnumeratorName{PipelineStage::ACCELERATION_STRUCTURE_BUILD, "ACCELERATION_STRUCTURE_BUILD"},
        EnumeratorName{PipelineStage::ALL_GRAPHICS, "ALL_GRAPHICS"},
        EnumeratorName{PipelineStage::ALL_COMMANDS, "ALL_COMMANDS"},
        EnumeratorName{PipelineStage::SHADING_RATE_ATTACHMENT, "SHADING_RATE_ATTACHMENT"},
    };
};

template<> 
struct EnumerationTraits<MemoryAccess> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{MemoryAccess::INDIRECT_COMMAND_READ, "INDIRECT_COMMAND_READ"},
        EnumeratorName{MemoryAccess::INDEX_READ, "INDEX_READ"},
        EnumeratorName{MemoryAccess::VERTEX_ATTRIBUTE_READ, "VERTEX_ATTRIBUTE_READ"},
        EnumeratorName{MemoryAccess::UNIFORM_READ, "UNIFORM_READ"},
        EnumeratorName{MemoryAccess::INPUT_ATTACHMENT_READ, "INPUT_ATTACHMENT_READ"},
        EnumeratorName{MemoryAccess::SHADER_SAMPLED_READ, "SHADER_SAMPLED_READ"},
        EnumeratorName{MemoryAccess::SHADER_STORAGE_READ, "SHADER_STORAGE_READ"},
        EnumeratorName{MemoryAccess::SHADER_STORAGE_WRITE, "SHADER_STORAGE_WRITE"},
        EnumeratorName{MemoryAccess::COLOR_ATTACHMENT_READ, "COLOR_ATTACHMENT_READ"},
        EnumeratorName{MemoryAccess::COLOR_ATTACHMENT_WRITE, "COLOR_ATTACHMENT_WRITE"},
        EnumeratorName{MemoryAccess::DEPTH_STENCIL_ATTACHMENT_READ, "DEPTH_STENCIL_ATTACHMENT_READ"},
        EnumeratorName{MemoryAccess::DEPTH_STENCIL_ATTACHMENT_WRITE, "DEPTH_STENCIL_ATTACHMENT_WRITE"},
        EnumeratorName{MemoryAccess::TRANSFER_READ, "TRANSFER_READ"},
        EnumeratorName{MemoryAccess::TRANSFER_WRITE, "TRANSFER_WRITE"},
        EnumeratorName{MemoryAccess::HOST_READ, "HOST_READ"},
        EnumeratorName{MemoryAccess::HOST_WRITE, "HOST_WRITE"},
        EnumeratorName{MemoryAccess::MEMORY_READ, "MEMORY_READ"},
        EnumeratorName{MemoryAccess::MEMORY_WRITE, "MEMORY_WRITE"},
        EnumeratorName{MemoryAccess::ACCELERATION_STRUCTURE_READ, "ACCELERATION_STRUCTURE_READ"},
        EnumeratorName{MemoryAccess::ACCELERATION_STRUCTURE_WRITE, "ACCELERATION_STRUCTURE_WRITE"},
        EnumeratorName{MemoryAccess::SHADING_RATE_ATTACHMENT_READ, "SHADING_RATE_ATTACHMENT_READ"},
    };
};

template<> 
struct EnumerationTraits<ResourceState> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ResourceState::UNDEFINED, "UNDEFINED"},
        EnumeratorName{ResourceState::GENERAL, "GENERAL"},
        EnumeratorName{ResourceState::INDIRECT_ARGUMENT, "INDIRECT_ARGUMENT"},
        EnumeratorName{ResourceState::VERTEX_BUFFER, "VERTEX_BUFFER"},
        EnumeratorName{ResourceState::INDEX_BUFFER, "INDEX_BUFFER"},
        EnumeratorName{ResourceState::VERTEX_SHADER_UNIFORM_READ, "VERTEX_SHADER_UNIFORM_READ"},
        EnumeratorName{ResourceState::VERTEX_SHADER_SAMPLED_READ, "VERTEX_SHADER_SAMPLED_READ"},
        EnumeratorName{ResourceState::VERTEX_SHADER_STORAGE_READ, "VERTEX_SHADER_STORAGE_READ"},
        EnumeratorName{ResourceState::FRAGMENT_SHADER_UNIFORM_READ, "FRAGMENT_SHADER_UNIFORM_READ"},
        EnumeratorName{ResourceState::FRAGMENT_SHADER_SAMPLED_READ, "FRAGMENT_SHADER_SAMPLED_READ"},
        EnumeratorName{ResourceState::FRAGMENT_SHADER_STORAGE_READ, "FRAGMENT_SHADER_STORAGE_READ"},
        EnumeratorName{ResourceState::FRAGMENT_SHADER_STORAGE_WRITE, "FRAGMENT_SHADER_STORAGE_WRITE"},
        EnumeratorName{ResourceState::FRAGMENT_SHADER_INPUT_ATTACHMENT, "FRAGMENT_SHADER_INPUT_ATTACHMENT"},
        EnumeratorName{ResourceState::COMPUTE_SHADER_UNIFORM_READ, "COMPUTE_SHADER_UNIFORM_READ"},
        EnumeratorName{ResourceState::COMPUTE_SHADER_SAMPLED_READ, "COMPUTE_SHADER_SAMPLED_READ"},
        EnumeratorName{ResourceState::COMPUTE_SHADER_STORAGE_READ, "COMPUTE_SHADER_STORAGE_READ"},
        EnumeratorName{ResourceState::COMPUTE_SHADER_STORAGE_WRITE, "COMPUTE_SHADER_STORAGE_WRITE"},
        EnumeratorName{ResourceState::COMPUTE_SHADER_STORAGE_READ_WRITE, "COMPUTE_SHADER_STORAGE_READ_WRITE"},
        EnumeratorName{ResourceState::COLOR_ATTACHMENT_READ, "COLOR_ATTACHMENT_READ"},
        EnumeratorName{ResourceState::COLOR_ATTACHMENT_WRITE, "COLOR_ATTACHMENT_WRITE"},
        EnumeratorName{ResourceState::COLOR_ATTACHMENT_READ_WRITE, "COLOR_ATTACHMENT_READ_WRITE"},
        EnumeratorName{ResourceState::DEPTH_STENCIL_ATTACHMENT_READ, "DEPTH_STENCIL_ATTACHMENT_READ"},
        EnumeratorName{ResourceState::DEPTH_STENCIL_ATTACHMENT_WRITE, "DEPTH_STENCIL_ATTACHMENT_WRITE"},
        EnumeratorName{ResourceState::TRANSFER_SOURCE, "TRANSFER_SOURCE"},
        EnumeratorName{ResourceState::TRANSFER_DESTINATION, "TRANSFER_DESTINATION"},
        EnumeratorName{ResourceState::HOST_READ, "HOST_READ"},
        EnumeratorName{ResourceState::HOST_WRITE, "HOST_WRITE"},
        EnumeratorName{ResourceState::PRESENT, "PRESENT"},
        EnumeratorName{ResourceState::SHADER_SAMPLED_READ, "SHADER_SAMPLED_READ"},
        EnumeratorName{ResourceState::SHADER_STORAGE_READ, "SHADER_STORAGE_READ"},
        EnumeratorName{ResourceState::SHADER_STORAGE_READ_WRITE, "SHADER_STORAGE_READ_WRITE"},
        EnumeratorName{ResourceState::ACCELERATION_STRUCTURE_BUILD_INPUT, "ACCELERATION_STRUCTURE_BUILD_INPUT"},
        EnumeratorName{ResourceState::ACCELERATION_STRUCTURE_WRITE, "ACCELERATION_STRUCTURE_WRITE"},
        EnumeratorName{ResourceState::ACCELERATION_STRUCTURE_READ, "ACCELERATION_STRUCTURE_READ"},
        EnumeratorName{ResourceState::SHADING_RATE_ATTACHMENT, "SHADING_RATE_ATTACHMENT"},
    };
};

template<> 
struct EnumerationTraits<WaitStatus> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{WaitStatus::SIGNALED, "SIGNALED"}, 
        EnumeratorName{WaitStatus::TIMEOUT, "TIMEOUT"},
    };
};

template<> 
struct EnumerationTraits<LoadOperation> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{LoadOperation::LOAD, "LOAD"}, 
        EnumeratorName{LoadOperation::CLEAR, "CLEAR"},
        EnumeratorName{LoadOperation::DONT_CARE, "DONT_CARE"},
    };
};

template<> 
struct EnumerationTraits<StoreOperation> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{StoreOperation::STORE, "STORE"}, 
        EnumeratorName{StoreOperation::DONT_CARE, "DONT_CARE"},
        EnumeratorName{StoreOperation::NONE, "NONE"},
    };
};

template<> struct EnumerationTraits<ResolveMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ResolveMode::NONE, "NONE"}, 
        EnumeratorName{ResolveMode::AVERAGE, "AVERAGE"},
        EnumeratorName{ResolveMode::MINIMUM, "MINIMUM"}, 
        EnumeratorName{ResolveMode::MAXIMUM, "MAXIMUM"},
        EnumeratorName{ResolveMode::SAMPLE_ZERO, "SAMPLE_ZERO"},
    };
};

template<> 
struct EnumerationTraits<CompareOperation> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{CompareOperation::NEVER, "NEVER"}, 
        EnumeratorName{CompareOperation::LESS, "LESS"},
        EnumeratorName{CompareOperation::EQUAL, "EQUAL"}, 
        EnumeratorName{CompareOperation::LESS_OR_EQUAL, "LESS_OR_EQUAL"},
        EnumeratorName{CompareOperation::GREATER, "GREATER"}, 
        EnumeratorName{CompareOperation::NOT_EQUAL, "NOT_EQUAL"},
        EnumeratorName{CompareOperation::GREATER_OR_EQUAL, "GREATER_OR_EQUAL"}, 
        EnumeratorName{CompareOperation::ALWAYS, "ALWAYS"},
    };
};

template<> 
struct EnumerationTraits<Filter> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{Filter::NEAREST, "NEAREST"}, 
        EnumeratorName{Filter::LINEAR, "LINEAR"},
    };
};

template<> 
struct EnumerationTraits<SamplerMipmapMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{SamplerMipmapMode::NEAREST, "NEAREST"}, 
        EnumeratorName{SamplerMipmapMode::LINEAR, "LINEAR"},
    };
};

template<> 
struct EnumerationTraits<SamplerAddressMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{SamplerAddressMode::REPEAT, "REPEAT"},
        EnumeratorName{SamplerAddressMode::MIRRORED_REPEAT, "MIRRORED_REPEAT"},
        EnumeratorName{SamplerAddressMode::CLAMP_TO_EDGE, "CLAMP_TO_EDGE"},
        EnumeratorName{SamplerAddressMode::CLAMP_TO_BORDER, "CLAMP_TO_BORDER"},
        EnumeratorName{SamplerAddressMode::MIRROR_CLAMP_TO_EDGE, "MIRROR_CLAMP_TO_EDGE"},
    };
};

template<> 
struct EnumerationTraits<BorderColor> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{BorderColor::TRANSPARENT_BLACK_FLOAT, "TRANSPARENT_BLACK_FLOAT"},
        EnumeratorName{BorderColor::TRANSPARENT_BLACK_INTEGER, "TRANSPARENT_BLACK_INTEGER"},
        EnumeratorName{BorderColor::OPAQUE_BLACK_FLOAT, "OPAQUE_BLACK_FLOAT"},
        EnumeratorName{BorderColor::OPAQUE_BLACK_INTEGER, "OPAQUE_BLACK_INTEGER"},
        EnumeratorName{BorderColor::OPAQUE_WHITE_FLOAT, "OPAQUE_WHITE_FLOAT"},
        EnumeratorName{BorderColor::OPAQUE_WHITE_INTEGER, "OPAQUE_WHITE_INTEGER"},
    };
};

template<> 
struct EnumerationTraits<PrimitiveTopology> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PrimitiveTopology::POINT_LIST, "POINT_LIST"},
        EnumeratorName{PrimitiveTopology::LINE_LIST, "LINE_LIST"},
        EnumeratorName{PrimitiveTopology::LINE_STRIP, "LINE_STRIP"},
        EnumeratorName{PrimitiveTopology::TRIANGLE_LIST, "TRIANGLE_LIST"},
        EnumeratorName{PrimitiveTopology::TRIANGLE_STRIP, "TRIANGLE_STRIP"},
        EnumeratorName{PrimitiveTopology::TRIANGLE_FAN, "TRIANGLE_FAN"},
        EnumeratorName{PrimitiveTopology::PATCH_LIST, "PATCH_LIST"},
    };
};

template<> 
struct EnumerationTraits<PolygonMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PolygonMode::FILL, "FILL"}, 
        EnumeratorName{PolygonMode::LINE, "LINE"},
        EnumeratorName{PolygonMode::POINT, "POINT"},
    };
};

template<> 
struct EnumerationTraits<CullMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{CullMode::NONE, "NONE"}, 
        EnumeratorName{CullMode::FRONT, "FRONT"},
        EnumeratorName{CullMode::BACK, "BACK"}, 
        EnumeratorName{CullMode::FRONT_AND_BACK, "FRONT_AND_BACK"},
    };
};

template<> 
struct EnumerationTraits<FrontFace> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{FrontFace::COUNTER_CLOCKWISE, "COUNTER_CLOCKWISE"},
        EnumeratorName{FrontFace::CLOCKWISE, "CLOCKWISE"},
    };
};

template<> 
struct EnumerationTraits<BlendFactor> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{BlendFactor::ZERO, "ZERO"},
        EnumeratorName{BlendFactor::ONE, "ONE"},
        EnumeratorName{BlendFactor::SOURCE_COLOR, "SOURCE_COLOR"},
        EnumeratorName{BlendFactor::ONE_MINUS_SOURCE_COLOR, "ONE_MINUS_SOURCE_COLOR"},
        EnumeratorName{BlendFactor::DESTINATION_COLOR, "DESTINATION_COLOR"},
        EnumeratorName{BlendFactor::ONE_MINUS_DESTINATION_COLOR, "ONE_MINUS_DESTINATION_COLOR"},
        EnumeratorName{BlendFactor::SOURCE_ALPHA, "SOURCE_ALPHA"},
        EnumeratorName{BlendFactor::ONE_MINUS_SOURCE_ALPHA, "ONE_MINUS_SOURCE_ALPHA"},
        EnumeratorName{BlendFactor::DESTINATION_ALPHA, "DESTINATION_ALPHA"},
        EnumeratorName{BlendFactor::ONE_MINUS_DESTINATION_ALPHA, "ONE_MINUS_DESTINATION_ALPHA"},
        EnumeratorName{BlendFactor::CONSTANT_COLOR, "CONSTANT_COLOR"},
        EnumeratorName{BlendFactor::ONE_MINUS_CONSTANT_COLOR, "ONE_MINUS_CONSTANT_COLOR"},
        EnumeratorName{BlendFactor::CONSTANT_ALPHA, "CONSTANT_ALPHA"},
        EnumeratorName{BlendFactor::ONE_MINUS_CONSTANT_ALPHA, "ONE_MINUS_CONSTANT_ALPHA"},
        EnumeratorName{BlendFactor::SOURCE_ALPHA_SATURATE, "SOURCE_ALPHA_SATURATE"},
        EnumeratorName{BlendFactor::SECOND_SOURCE_COLOR, "SECOND_SOURCE_COLOR"},
        EnumeratorName{BlendFactor::ONE_MINUS_SECOND_SOURCE_COLOR, "ONE_MINUS_SECOND_SOURCE_COLOR"},
        EnumeratorName{BlendFactor::SECOND_SOURCE_ALPHA, "SECOND_SOURCE_ALPHA"},
        EnumeratorName{BlendFactor::ONE_MINUS_SECOND_SOURCE_ALPHA, "ONE_MINUS_SECOND_SOURCE_ALPHA"},
    };
};

template<> 
struct EnumerationTraits<BlendOperation>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{BlendOperation::ADD, "ADD"}, 
        EnumeratorName{BlendOperation::SUBTRACT, "SUBTRACT"},
        EnumeratorName{BlendOperation::REVERSE_SUBTRACT, "REVERSE_SUBTRACT"},
        EnumeratorName{BlendOperation::MINIMUM, "MINIMUM"}, 
        EnumeratorName{BlendOperation::MAXIMUM, "MAXIMUM"},
    };
};

template<> 
struct EnumerationTraits<ColorComponent> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ColorComponent::RED, "RED"},
        EnumeratorName{ColorComponent::GREEN, "GREEN"},
        EnumeratorName{ColorComponent::BLUE, "BLUE"}, 
        EnumeratorName{ColorComponent::ALPHA, "ALPHA"},
    };
};

template<> 
struct EnumerationTraits<StencilOperation> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{StencilOperation::KEEP, "KEEP"},
        EnumeratorName{StencilOperation::ZERO, "ZERO"},
        EnumeratorName{StencilOperation::REPLACE, "REPLACE"},
        EnumeratorName{StencilOperation::INCREMENT_AND_CLAMP, "INCREMENT_AND_CLAMP"},
        EnumeratorName{StencilOperation::DECREMENT_AND_CLAMP, "DECREMENT_AND_CLAMP"},
        EnumeratorName{StencilOperation::INVERT, "INVERT"},
        EnumeratorName{StencilOperation::INCREMENT_AND_WRAP, "INCREMENT_AND_WRAP"},
        EnumeratorName{StencilOperation::DECREMENT_AND_WRAP, "DECREMENT_AND_WRAP"},
    };
};

template<> 
struct EnumerationTraits<IndexType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{IndexType::UNSIGNED_INTEGER_16, "UNSIGNED_INTEGER_16"},
        EnumeratorName{IndexType::UNSIGNED_INTEGER_32, "UNSIGNED_INTEGER_32"},
    };
};

template<> 
struct EnumerationTraits<VertexInputRate> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{VertexInputRate::VERTEX, "VERTEX"}, 
        EnumeratorName{VertexInputRate::INSTANCE, "INSTANCE"},
    };
};

template<> 
struct EnumerationTraits<ShaderStage> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ShaderStage::VERTEX, "VERTEX"},
        EnumeratorName{ShaderStage::TESSELLATION_CONTROL, "TESSELLATION_CONTROL"},
        EnumeratorName{ShaderStage::TESSELLATION_EVALUATION, "TESSELLATION_EVALUATION"},
        EnumeratorName{ShaderStage::GEOMETRY, "GEOMETRY"},
        EnumeratorName{ShaderStage::FRAGMENT, "FRAGMENT"},
        EnumeratorName{ShaderStage::COMPUTE, "COMPUTE"},
        EnumeratorName{ShaderStage::TASK, "TASK"},
        EnumeratorName{ShaderStage::MESH, "MESH"},
        EnumeratorName{ShaderStage::RAY_GENERATION, "RAY_GENERATION"},
        EnumeratorName{ShaderStage::ANY_HIT, "ANY_HIT"},
        EnumeratorName{ShaderStage::CLOSEST_HIT, "CLOSEST_HIT"},
        EnumeratorName{ShaderStage::MISS, "MISS"},
        EnumeratorName{ShaderStage::INTERSECTION, "INTERSECTION"},
        EnumeratorName{ShaderStage::CALLABLE, "CALLABLE"},
    };
};

template<> 
struct EnumerationTraits<ShaderSourceFormat> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ShaderSourceFormat::GLSL, "GLSL"}, 
        EnumeratorName{ShaderSourceFormat::SPIRV, "SPIRV"},
        EnumeratorName{ShaderSourceFormat::HLSL, "HLSL"},
    };
};

template<> 
struct EnumerationTraits<DescriptorType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{DescriptorType::SAMPLER, "SAMPLER"},
        EnumeratorName{DescriptorType::COMBINED_IMAGE_SAMPLER, "COMBINED_IMAGE_SAMPLER"},
        EnumeratorName{DescriptorType::SAMPLED_IMAGE, "SAMPLED_IMAGE"},
        EnumeratorName{DescriptorType::STORAGE_IMAGE, "STORAGE_IMAGE"},
        EnumeratorName{DescriptorType::UNIFORM_TEXEL_BUFFER, "UNIFORM_TEXEL_BUFFER"},
        EnumeratorName{DescriptorType::STORAGE_TEXEL_BUFFER, "STORAGE_TEXEL_BUFFER"},
        EnumeratorName{DescriptorType::UNIFORM_BUFFER, "UNIFORM_BUFFER"},
        EnumeratorName{DescriptorType::STORAGE_BUFFER, "STORAGE_BUFFER"},
        EnumeratorName{DescriptorType::UNIFORM_BUFFER_DYNAMIC, "UNIFORM_BUFFER_DYNAMIC"},
        EnumeratorName{DescriptorType::STORAGE_BUFFER_DYNAMIC, "STORAGE_BUFFER_DYNAMIC"},
        EnumeratorName{DescriptorType::INPUT_ATTACHMENT, "INPUT_ATTACHMENT"},
        EnumeratorName{DescriptorType::ACCELERATION_STRUCTURE, "ACCELERATION_STRUCTURE"},
    };
};

template<> 
struct EnumerationTraits<DescriptorBindingOption> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{DescriptorBindingOption::UPDATE_AFTER_BIND, "UPDATE_AFTER_BIND"},
        EnumeratorName{DescriptorBindingOption::UPDATE_UNUSED_WHILE_PENDING, "UPDATE_UNUSED_WHILE_PENDING"},
        EnumeratorName{DescriptorBindingOption::PARTIALLY_BOUND, "PARTIALLY_BOUND"},
        EnumeratorName{DescriptorBindingOption::VARIABLE_DESCRIPTOR_COUNT, "VARIABLE_DESCRIPTOR_COUNT"},
    };
};

template<> 
struct EnumerationTraits<PipelineBindPoint> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PipelineBindPoint::GRAPHICS, "GRAPHICS"}, 
        EnumeratorName{PipelineBindPoint::COMPUTE, "COMPUTE"},
        EnumeratorName{PipelineBindPoint::RAY_TRACING, "RAY_TRACING"},
    };
};

template<> 
struct EnumerationTraits<CommandBufferLevel> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{CommandBufferLevel::PRIMARY, "PRIMARY"},
        EnumeratorName{CommandBufferLevel::SECONDARY, "SECONDARY"},
    };
};

template<> 
struct EnumerationTraits<CommandPoolOption> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{CommandPoolOption::TRANSIENT, "TRANSIENT"},
        EnumeratorName{CommandPoolOption::RESETTABLE_COMMAND_BUFFERS, "RESETTABLE_COMMAND_BUFFERS"},
    };
};

template<> 
struct EnumerationTraits<QueryType> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{QueryType::OCCLUSION, "OCCLUSION"},
        EnumeratorName{QueryType::PIPELINE_STATISTICS, "PIPELINE_STATISTICS"},
        EnumeratorName{QueryType::TIMESTAMP, "TIMESTAMP"},
        EnumeratorName{QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE, "ACCELERATION_STRUCTURE_COMPACTED_SIZE"},
    };
};

template<> 
struct EnumerationTraits<PipelineStatistic> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{PipelineStatistic::INPUT_ASSEMBLY_VERTICES, "INPUT_ASSEMBLY_VERTICES"},
        EnumeratorName{PipelineStatistic::INPUT_ASSEMBLY_PRIMITIVES, "INPUT_ASSEMBLY_PRIMITIVES"},
        EnumeratorName{PipelineStatistic::VERTEX_SHADER_INVOCATIONS, "VERTEX_SHADER_INVOCATIONS"},
        EnumeratorName{PipelineStatistic::CLIPPING_INVOCATIONS, "CLIPPING_INVOCATIONS"},
        EnumeratorName{PipelineStatistic::CLIPPING_PRIMITIVES, "CLIPPING_PRIMITIVES"},
        EnumeratorName{PipelineStatistic::FRAGMENT_SHADER_INVOCATIONS, "FRAGMENT_SHADER_INVOCATIONS"},
        EnumeratorName{PipelineStatistic::COMPUTE_SHADER_INVOCATIONS, "COMPUTE_SHADER_INVOCATIONS"},
    };
};

template<> 
struct EnumerationTraits<PresentMode> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PresentMode::IMMEDIATE, "IMMEDIATE"},
        EnumeratorName{PresentMode::MAILBOX, "MAILBOX"},
        EnumeratorName{PresentMode::FIFO, "FIFO"},
        EnumeratorName{PresentMode::FIFO_RELAXED, "FIFO_RELAXED"},
    };
};

template<> 
struct EnumerationTraits<ColorSpace> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ColorSpace::SRGB_NONLINEAR, "SRGB_NONLINEAR"},
        EnumeratorName{ColorSpace::EXTENDED_SRGB_LINEAR, "EXTENDED_SRGB_LINEAR"},
        EnumeratorName{ColorSpace::DISPLAY_P3_NONLINEAR, "DISPLAY_P3_NONLINEAR"},
        EnumeratorName{ColorSpace::HDR10_ST2084, "HDR10_ST2084"},
    };
};

template<> 
struct EnumerationTraits<SwapchainStatus> 
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{SwapchainStatus::OPTIMAL, "OPTIMAL"},
        EnumeratorName{SwapchainStatus::SUBOPTIMAL, "SUBOPTIMAL"},
        EnumeratorName{SwapchainStatus::OUT_OF_DATE, "OUT_OF_DATE"},
        EnumeratorName{SwapchainStatus::SURFACE_LOST, "SURFACE_LOST"},
    };
};

template<> 
struct EnumerationTraits<DebugMessageSeverity> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{DebugMessageSeverity::VERBOSE, "VERBOSE"},
        EnumeratorName{DebugMessageSeverity::INFORMATION, "INFORMATION"},
        EnumeratorName{DebugMessageSeverity::WARNING, "WARNING"},
        EnumeratorName{DebugMessageSeverity::CRITICAL, "CRITICAL"},
    };
};

template<> 
struct EnumerationTraits<DebugMessageType> 
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{DebugMessageType::GENERAL, "GENERAL"},
        EnumeratorName{DebugMessageType::VALIDATION, "VALIDATION"},
        EnumeratorName{DebugMessageType::PERFORMANCE, "PERFORMANCE"},
    };
};

template<>
struct EnumerationTraits<LayoutMismatch>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{LayoutMismatch::NONE, "NONE"},
        EnumeratorName{LayoutMismatch::WRONG_SHADER_STAGE, "WRONG_SHADER_STAGE"},
        EnumeratorName{LayoutMismatch::BINDING_MISSING_FROM_LAYOUT, "BINDING_MISSING_FROM_LAYOUT"},
        EnumeratorName{LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH, "DESCRIPTOR_TYPE_MISMATCH"},
        EnumeratorName{LayoutMismatch::DESCRIPTOR_COUNT_MISMATCH, "DESCRIPTOR_COUNT_MISMATCH"},
        EnumeratorName{LayoutMismatch::STAGE_NOT_VISIBLE, "STAGE_NOT_VISIBLE"},
        EnumeratorName{LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE, "PUSH_CONSTANTS_OUTSIDE_RANGE"},
        EnumeratorName{LayoutMismatch::PUSH_CONSTANT_STAGE_NOT_VISIBLE, "PUSH_CONSTANT_STAGE_NOT_VISIBLE"},
        EnumeratorName{LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE, "VERTEX_INPUT_WITHOUT_ATTRIBUTE"},
        EnumeratorName{LayoutMismatch::VERTEX_INPUT_TYPE_MISMATCH, "VERTEX_INPUT_TYPE_MISMATCH"},
        EnumeratorName{LayoutMismatch::FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT, "FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT"},
        EnumeratorName{LayoutMismatch::FRAGMENT_OUTPUT_TYPE_MISMATCH, "FRAGMENT_OUTPUT_TYPE_MISMATCH"},
        EnumeratorName{LayoutMismatch::CONFLICTING_BINDINGS, "CONFLICTING_BINDINGS"},
        EnumeratorName{LayoutMismatch::STAGE_INTERFACE_MISMATCH, "STAGE_INTERFACE_MISMATCH"},
        EnumeratorName{LayoutMismatch::UNKNOWN_SPECIALIZATION_CONSTANT, "UNKNOWN_SPECIALIZATION_CONSTANT"},
    };
};

template<>
struct EnumerationTraits<ValidationLevel>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ValidationLevel::NONE, "NONE"},
        EnumeratorName{ValidationLevel::CREATION, "CREATION"},
        EnumeratorName{ValidationLevel::COMMANDS, "COMMANDS"},
    };
};

template<>
struct EnumerationTraits<BindlessResourceKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{BindlessResourceKind::SAMPLED_IMAGE, "SAMPLED_IMAGE"},
        EnumeratorName{BindlessResourceKind::STORAGE_IMAGE, "STORAGE_IMAGE"},
        EnumeratorName{BindlessResourceKind::SAMPLER, "SAMPLER"},
        EnumeratorName{BindlessResourceKind::STORAGE_BUFFER, "STORAGE_BUFFER"},
        EnumeratorName{BindlessResourceKind::ACCELERATION_STRUCTURE, "ACCELERATION_STRUCTURE"},
    };
};

template<>
struct EnumerationTraits<PipelineStatus>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PipelineStatus::READY, "READY"},
        EnumeratorName{PipelineStatus::COMPILING, "COMPILING"},
        EnumeratorName{PipelineStatus::COMPILATION_FAILED, "COMPILATION_FAILED"},
    };
};

template<>
struct EnumerationTraits<PipelineCreationMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{PipelineCreationMode::IMMEDIATE, "IMMEDIATE"},
        EnumeratorName{PipelineCreationMode::ASYNCHRONOUS, "ASYNCHRONOUS"},
    };
};

template<>
struct EnumerationTraits<DynamicState>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{DynamicState::DEPTH_BIAS, "DEPTH_BIAS"},
        EnumeratorName{DynamicState::DEPTH_BOUNDS, "DEPTH_BOUNDS"},
        EnumeratorName{DynamicState::LINE_WIDTH, "LINE_WIDTH"},
        EnumeratorName{DynamicState::CULL_MODE, "CULL_MODE"},
        EnumeratorName{DynamicState::FRONT_FACE, "FRONT_FACE"},
        EnumeratorName{DynamicState::PRIMITIVE_TOPOLOGY, "PRIMITIVE_TOPOLOGY"},
        EnumeratorName{DynamicState::DEPTH_TEST, "DEPTH_TEST"},
        EnumeratorName{DynamicState::DEPTH_WRITE, "DEPTH_WRITE"},
        EnumeratorName{DynamicState::DEPTH_COMPARE, "DEPTH_COMPARE"},
        EnumeratorName{DynamicState::STENCIL_TEST, "STENCIL_TEST"},
        EnumeratorName{DynamicState::STENCIL_OPERATION, "STENCIL_OPERATION"},
    };
};

template<>
struct EnumerationTraits<StencilFace>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{StencilFace::FRONT, "FRONT"},
        EnumeratorName{StencilFace::BACK, "BACK"},
    };
};

template<>
struct EnumerationTraits<AccelerationStructureType>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{AccelerationStructureType::TOP_LEVEL, "TOP_LEVEL"},
        EnumeratorName{AccelerationStructureType::BOTTOM_LEVEL, "BOTTOM_LEVEL"},
    };
};

template<>
struct EnumerationTraits<AccelerationStructureBuildOption>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{AccelerationStructureBuildOption::ALLOW_UPDATE, "ALLOW_UPDATE"},
        EnumeratorName{AccelerationStructureBuildOption::ALLOW_COMPACTION, "ALLOW_COMPACTION"},
        EnumeratorName{AccelerationStructureBuildOption::PREFER_FAST_TRACE, "PREFER_FAST_TRACE"},
        EnumeratorName{AccelerationStructureBuildOption::PREFER_FAST_BUILD, "PREFER_FAST_BUILD"},
        EnumeratorName{AccelerationStructureBuildOption::MINIMIZE_MEMORY, "MINIMIZE_MEMORY"},
    };
};

template<>
struct EnumerationTraits<AccelerationStructureBuildMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{AccelerationStructureBuildMode::BUILD, "BUILD"},
        EnumeratorName{AccelerationStructureBuildMode::UPDATE, "UPDATE"},
    };
};

template<>
struct EnumerationTraits<GeometryOption>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{GeometryOption::OPAQUE_GEOMETRY, "OPAQUE_GEOMETRY"},
        EnumeratorName{GeometryOption::NO_DUPLICATE_ANY_HIT, "NO_DUPLICATE_ANY_HIT"},
    };
};

template<>
struct EnumerationTraits<AccelerationStructureInstanceOption>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{AccelerationStructureInstanceOption::DISABLE_TRIANGLE_CULLING, "DISABLE_TRIANGLE_CULLING"},
        EnumeratorName{AccelerationStructureInstanceOption::FLIP_TRIANGLE_FACING, "FLIP_TRIANGLE_FACING"},
        EnumeratorName{AccelerationStructureInstanceOption::FORCE_OPAQUE_GEOMETRY, "FORCE_OPAQUE_GEOMETRY"},
        EnumeratorName{AccelerationStructureInstanceOption::FORCE_NON_OPAQUE_GEOMETRY, "FORCE_NON_OPAQUE_GEOMETRY"},
    };
};

template<>
struct EnumerationTraits<AccelerationStructureCopyMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{AccelerationStructureCopyMode::CLONE, "CLONE"},
        EnumeratorName{AccelerationStructureCopyMode::COMPACT, "COMPACT"},
    };
};

template<>
struct EnumerationTraits<RayTracingShaderGroupType>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{RayTracingShaderGroupType::GENERAL, "GENERAL"},
        EnumeratorName{RayTracingShaderGroupType::TRIANGLES_HIT, "TRIANGLES_HIT"},
        EnumeratorName{RayTracingShaderGroupType::PROCEDURAL_HIT, "PROCEDURAL_HIT"},
    };
};

template<>
struct EnumerationTraits<CrashDiagnostic>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{CrashDiagnostic::BREADCRUMBS, "BREADCRUMBS"},
        EnumeratorName{CrashDiagnostic::VENDOR_REPORT, "VENDOR_REPORT"},
    };
};

template<>
struct EnumerationTraits<CaptureTool>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{CaptureTool::NONE, "NONE"},
        EnumeratorName{CaptureTool::RENDERDOC, "RENDERDOC"},
        EnumeratorName{CaptureTool::PIX, "PIX"},
        EnumeratorName{CaptureTool::NSIGHT_GRAPHICS, "NSIGHT_GRAPHICS"},
    };
};

template<>
struct EnumerationTraits<SamplerReductionMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{SamplerReductionMode::WEIGHTED_AVERAGE, "WEIGHTED_AVERAGE"},
        EnumeratorName{SamplerReductionMode::MINIMUM, "MINIMUM"},
        EnumeratorName{SamplerReductionMode::MAXIMUM, "MAXIMUM"},
    };
};

template<>
struct EnumerationTraits<ShadingRate>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ShadingRate::ONE_BY_ONE, "ONE_BY_ONE"},
        EnumeratorName{ShadingRate::ONE_BY_TWO, "ONE_BY_TWO"},
        EnumeratorName{ShadingRate::TWO_BY_ONE, "TWO_BY_ONE"},
        EnumeratorName{ShadingRate::TWO_BY_TWO, "TWO_BY_TWO"},
        EnumeratorName{ShadingRate::TWO_BY_FOUR, "TWO_BY_FOUR"},
        EnumeratorName{ShadingRate::FOUR_BY_TWO, "FOUR_BY_TWO"},
        EnumeratorName{ShadingRate::FOUR_BY_FOUR, "FOUR_BY_FOUR"},
    };
};

template<>
struct EnumerationTraits<ShadingRateCombiner>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ShadingRateCombiner::KEEP, "KEEP"},
        EnumeratorName{ShadingRateCombiner::REPLACE, "REPLACE"},
        EnumeratorName{ShadingRateCombiner::MINIMUM, "MINIMUM"},
        EnumeratorName{ShadingRateCombiner::MAXIMUM, "MAXIMUM"},
    };
};

template<>
struct EnumerationTraits<ShaderReloadResult>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ShaderReloadResult::RELOADED, "RELOADED"},
        EnumeratorName{ShaderReloadResult::COMPILATION_FAILED, "COMPILATION_FAILED"},
        EnumeratorName{ShaderReloadResult::LAYOUT_MISMATCH, "LAYOUT_MISMATCH"},
    };
};

// =================================================================================================
// Bitmask operators
// =================================================================================================

/// @brief Union of two sets of bits.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
operator|(Enumeration left, Enumeration right) noexcept -> Enumeration 
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) | std::to_underlying(right)));
}

/// @brief Intersection of two sets of bits.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
operator&(Enumeration left, Enumeration right) noexcept -> Enumeration 
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) & std::to_underlying(right)));
}

/// @brief Bits set in exactly one of the operands.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
operator^(Enumeration left, Enumeration right) noexcept -> Enumeration 
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) ^ std::to_underlying(right)));
}

/// @brief Complement of a set of bits, restricted to the bits named by the enumeration.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
operator~(Enumeration value) noexcept -> Enumeration 
{
    using Underlying = std::underlying_type_t<Enumeration>;
    Underlying named_bits = 0;
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES) 
    {
        named_bits = static_cast<Underlying>(named_bits | std::to_underlying(entry.value));
    }
    return static_cast<Enumeration>(static_cast<Underlying>(named_bits & static_cast<Underlying>(~std::to_underlying(value))));
}

/// @brief Adds the bits of @p right to @p left.
template<BitmaskEnumeration Enumeration>
constexpr auto 
operator|=(Enumeration& left, Enumeration right) noexcept -> Enumeration& 
{
    return left = left | right;
}

/// @brief Keeps in @p left only the bits also set in @p right.
template<BitmaskEnumeration Enumeration>
constexpr auto 
operator&=(Enumeration& left, Enumeration right) noexcept -> Enumeration& 
{
    return left = left & right;
}

/// @brief Toggles in @p left the bits set in @p right.
template<BitmaskEnumeration Enumeration>
constexpr auto 
operator^=(Enumeration& left, Enumeration right) noexcept -> Enumeration& 
{
    return left = left ^ right;
}

/// @brief Whether every bit of @p bits is set in @p value (true when @p bits is empty).
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
has_all(Enumeration value, Enumeration bits) noexcept -> bool 
{
    return (value & bits) == bits;
}

/// @brief Whether at least one bit of @p bits is set in @p value.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
has_any(Enumeration value, Enumeration bits) noexcept -> bool 
{
    return std::to_underlying(value & bits) != 0;
}

/// @brief Whether no bit is set.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
is_empty(Enumeration value) noexcept -> bool 
{
    return std::to_underlying(value) == 0;
}

// =================================================================================================
// Spelling
// =================================================================================================

/// @brief Spelling of an enumerator of a sequential enumeration, e.g. "R8G8B8A8_UNORM".
/// @return The spelling, or "<invalid>" for a value that names no enumerator.
template<SequentialEnumeration Enumeration>
[[nodiscard]] constexpr auto 
to_string(Enumeration value) noexcept -> std::string_view 
{
    const auto index = static_cast<std::size_t>(std::to_underlying(value));
    const auto& names = EnumerationTraits<Enumeration>::NAMES;
    return index < names.size() ? names[index].name : std::string_view{"<invalid>"};
}

/// @brief Spelling of a set of bits, e.g. "VERTEX | FRAGMENT"; "NONE" when empty. Bits that no
///        enumerator names are appended in hexadecimal.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto 
to_string(Enumeration value) -> std::string 
{
    using Underlying = std::underlying_type_t<Enumeration>;
    auto remaining = std::to_underlying(value);

    // Check for the empty set first, so that we don't print "NONE | 0x..." for a value with no named bits.
    if (remaining == 0) 
    {
        return "NONE";
    }

    // Iterate over the enumerators in order, appending their names to the text if their bit is set.
    std::string text;
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES) 
    {
        // Skip enumerators that are not single bits, so that we don't print "ALL_GRAPHICS" when the value is "VERTEX | FRAGMENT | COMPUTE".
        const auto bit = std::to_underlying(entry.value);
        if ((remaining & bit) != 0) 
        {
            // Append a separator if this is not the first enumerator name.
            if (!text.empty()) 
            {
                text += " | ";
            }

            // Append the enumerator name and remove its bit from the remaining bits.
            text += entry.name;

            // Remove the bit from the remaining bits, so that we don't print it again if another enumerator has the same bit set.
            remaining = static_cast<Underlying>(remaining & static_cast<Underlying>(~bit));
        }
    }

    // If any bits remain that no enumerator names, append them in hexadecimal.
    if (remaining != 0) 
    {
        // Append the remaining bits in hexadecimal, with the most significant digit first. The digits are appended to the front of the string, so that the final string is in big-endian order.
        constexpr std::string_view digits = "0123456789ABCDEF";
        std::string hexadecimal;

        // Shift the remaining bits right by 4 each iteration, appending the least significant hex digit to the front of the string.
        for (auto rest = static_cast<std::uint64_t>(remaining); rest != 0; rest >>= 4U)
         {
            hexadecimal.insert(hexadecimal.begin(), digits[static_cast<std::size_t>(rest & 0xFU)]);
        }

        // Append a separator if this is not the first enumerator name, then append the hexadecimal representation of the remaining bits.
        if (!text.empty()) 
        {
            text += " | ";
        }

        // Append the hexadecimal representation of the remaining bits, prefixed with "0x".
        text += "0x" + hexadecimal;
    }
    return text;
}

// =================================================================================================
// Format properties
// =================================================================================================

/// @brief Memory and interpretation properties of a format.
struct FormatProperties 
{
    Format format;                  ///< Described format.
    std::uint8_t bytes_per_block;   ///< Bytes of one texel block (one texel for uncompressed formats).
    std::uint8_t block_width;       ///< Texels per block horizontally.
    std::uint8_t block_height;      ///< Texels per block vertically.
    std::uint8_t component_count;   ///< Number of components (depth and stencil count as one each).
    ImageAspect aspects;            ///< Aspects the format contains.
    FormatNumericType numeric_type; ///< Interpretation of the components (of depth for depth/stencil).
};

/// @brief Properties of every Format, indexed by the enumerator's value.
inline constexpr std::array<FormatProperties, ENUMERATOR_COUNT<Format>> FORMAT_PROPERTIES = [] {
    using enum Format;
    using enum FormatNumericType;
    constexpr auto color = ImageAspect::COLOR;
    constexpr auto depth = ImageAspect::DEPTH;
    constexpr auto stencil = ImageAspect::STENCIL;
    return std::array<FormatProperties, ENUMERATOR_COUNT<Format>>{{
        {UNDEFINED, 0, 0, 0, 0, ImageAspect::NONE, NONE},
        {R8_UNORM, 1, 1, 1, 1, color, UNSIGNED_NORMALIZED},
        {R8_SNORM, 1, 1, 1, 1, color, SIGNED_NORMALIZED},
        {R8_UINT, 1, 1, 1, 1, color, UNSIGNED_INTEGER},
        {R8_SINT, 1, 1, 1, 1, color, SIGNED_INTEGER},
        {R8G8_UNORM, 2, 1, 1, 2, color, UNSIGNED_NORMALIZED},
        {R8G8_SNORM, 2, 1, 1, 2, color, SIGNED_NORMALIZED},
        {R8G8_UINT, 2, 1, 1, 2, color, UNSIGNED_INTEGER},
        {R8G8_SINT, 2, 1, 1, 2, color, SIGNED_INTEGER},
        {R8G8B8A8_UNORM, 4, 1, 1, 4, color, UNSIGNED_NORMALIZED},
        {R8G8B8A8_SNORM, 4, 1, 1, 4, color, SIGNED_NORMALIZED},
        {R8G8B8A8_UINT, 4, 1, 1, 4, color, UNSIGNED_INTEGER},
        {R8G8B8A8_SINT, 4, 1, 1, 4, color, SIGNED_INTEGER},
        {R8G8B8A8_SRGB, 4, 1, 1, 4, color, SRGB},
        {B8G8R8A8_UNORM, 4, 1, 1, 4, color, UNSIGNED_NORMALIZED},
        {B8G8R8A8_SRGB, 4, 1, 1, 4, color, SRGB},
        {A2B10G10R10_UNORM_PACK32, 4, 1, 1, 4, color, UNSIGNED_NORMALIZED},
        {A2B10G10R10_UINT_PACK32, 4, 1, 1, 4, color, UNSIGNED_INTEGER},
        {R16_UNORM, 2, 1, 1, 1, color, UNSIGNED_NORMALIZED},
        {R16_SNORM, 2, 1, 1, 1, color, SIGNED_NORMALIZED},
        {R16_UINT, 2, 1, 1, 1, color, UNSIGNED_INTEGER},
        {R16_SINT, 2, 1, 1, 1, color, SIGNED_INTEGER},
        {R16_SFLOAT, 2, 1, 1, 1, color, SIGNED_FLOAT},
        {R16G16_UNORM, 4, 1, 1, 2, color, UNSIGNED_NORMALIZED},
        {R16G16_SNORM, 4, 1, 1, 2, color, SIGNED_NORMALIZED},
        {R16G16_UINT, 4, 1, 1, 2, color, UNSIGNED_INTEGER},
        {R16G16_SINT, 4, 1, 1, 2, color, SIGNED_INTEGER},
        {R16G16_SFLOAT, 4, 1, 1, 2, color, SIGNED_FLOAT},
        {R16G16B16A16_UNORM, 8, 1, 1, 4, color, UNSIGNED_NORMALIZED},
        {R16G16B16A16_SNORM, 8, 1, 1, 4, color, SIGNED_NORMALIZED},
        {R16G16B16A16_UINT, 8, 1, 1, 4, color, UNSIGNED_INTEGER},
        {R16G16B16A16_SINT, 8, 1, 1, 4, color, SIGNED_INTEGER},
        {R16G16B16A16_SFLOAT, 8, 1, 1, 4, color, SIGNED_FLOAT},
        {R32_UINT, 4, 1, 1, 1, color, UNSIGNED_INTEGER},
        {R32_SINT, 4, 1, 1, 1, color, SIGNED_INTEGER},
        {R32_SFLOAT, 4, 1, 1, 1, color, SIGNED_FLOAT},
        {R32G32_UINT, 8, 1, 1, 2, color, UNSIGNED_INTEGER},
        {R32G32_SINT, 8, 1, 1, 2, color, SIGNED_INTEGER},
        {R32G32_SFLOAT, 8, 1, 1, 2, color, SIGNED_FLOAT},
        {R32G32B32_UINT, 12, 1, 1, 3, color, UNSIGNED_INTEGER},
        {R32G32B32_SINT, 12, 1, 1, 3, color, SIGNED_INTEGER},
        {R32G32B32_SFLOAT, 12, 1, 1, 3, color, SIGNED_FLOAT},
        {R32G32B32A32_UINT, 16, 1, 1, 4, color, UNSIGNED_INTEGER},
        {R32G32B32A32_SINT, 16, 1, 1, 4, color, SIGNED_INTEGER},
        {R32G32B32A32_SFLOAT, 16, 1, 1, 4, color, SIGNED_FLOAT},
        {B10G11R11_UFLOAT_PACK32, 4, 1, 1, 3, color, UNSIGNED_FLOAT},
        {E5B9G9R9_UFLOAT_PACK32, 4, 1, 1, 3, color, UNSIGNED_FLOAT},
        {D16_UNORM, 2, 1, 1, 1, depth, UNSIGNED_NORMALIZED},
        {X8_D24_UNORM_PACK32, 4, 1, 1, 1, depth, UNSIGNED_NORMALIZED},
        {D32_SFLOAT, 4, 1, 1, 1, depth, SIGNED_FLOAT},
        {S8_UINT, 1, 1, 1, 1, stencil, UNSIGNED_INTEGER},
        {D24_UNORM_S8_UINT, 4, 1, 1, 2, depth | stencil, UNSIGNED_NORMALIZED},
        {D32_SFLOAT_S8_UINT, 5, 1, 1, 2, depth | stencil, SIGNED_FLOAT},
        {BC1_RGBA_UNORM_BLOCK, 8, 4, 4, 4, color, UNSIGNED_NORMALIZED},
        {BC1_RGBA_SRGB_BLOCK, 8, 4, 4, 4, color, SRGB},
        {BC2_UNORM_BLOCK, 16, 4, 4, 4, color, UNSIGNED_NORMALIZED},
        {BC2_SRGB_BLOCK, 16, 4, 4, 4, color, SRGB},
        {BC3_UNORM_BLOCK, 16, 4, 4, 4, color, UNSIGNED_NORMALIZED},
        {BC3_SRGB_BLOCK, 16, 4, 4, 4, color, SRGB},
        {BC4_UNORM_BLOCK, 8, 4, 4, 1, color, UNSIGNED_NORMALIZED},
        {BC4_SNORM_BLOCK, 8, 4, 4, 1, color, SIGNED_NORMALIZED},
        {BC5_UNORM_BLOCK, 16, 4, 4, 2, color, UNSIGNED_NORMALIZED},
        {BC5_SNORM_BLOCK, 16, 4, 4, 2, color, SIGNED_NORMALIZED},
        {BC6H_UFLOAT_BLOCK, 16, 4, 4, 3, color, UNSIGNED_FLOAT},
        {BC6H_SFLOAT_BLOCK, 16, 4, 4, 3, color, SIGNED_FLOAT},
        {BC7_UNORM_BLOCK, 16, 4, 4, 4, color, UNSIGNED_NORMALIZED},
        {BC7_SRGB_BLOCK, 16, 4, 4, 4, color, SRGB},
        {ETC2_R8G8B8A8_UNORM_BLOCK, 16, 4, 4, 4, color, UNSIGNED_NORMALIZED},
        {ETC2_R8G8B8A8_SRGB_BLOCK, 16, 4, 4, 4, color, SRGB},
        {ASTC_4x4_UNORM_BLOCK, 16, 4, 4, 4, color, UNSIGNED_NORMALIZED},
        {ASTC_4x4_SRGB_BLOCK, 16, 4, 4, 4, color, SRGB},
    }};
}();

/// @brief Properties of @p format.
[[nodiscard]] constexpr auto 
format_properties(Format format) noexcept -> const FormatProperties& 
{
    const auto index = static_cast<std::size_t>(std::to_underlying(format));
    return FORMAT_PROPERTIES[index < FORMAT_PROPERTIES.size() ? index : 0];
}

// =================================================================================================
// Resource state synchronization (render graph barrier derivation)
// =================================================================================================

/// @brief Synchronization scope a ResourceState stands for.
struct ResourceStateSynchronization 
{
    ResourceState state;          ///< Described state.
    PipelineStage stages;         ///< Stages that access the resource in this state.
    MemoryAccess access;          ///< Accesses performed in this state.
    ImageLayout layout;           ///< Required image layout (images only).
    ResourceCategory categories;  ///< Resources this state applies to.
    bool writes;                  ///< Whether the state writes (requires a barrier before any later access).
};

/// @brief Synchronization scope of every ResourceState, indexed by the enumerator's value.
inline constexpr std::array<ResourceStateSynchronization, ENUMERATOR_COUNT<ResourceState>> RESOURCE_STATE_SYNCHRONIZATION = [] {
    using enum ResourceState;
    using Stage = PipelineStage;
    using Access = MemoryAccess;
    using Layout = ImageLayout;
    constexpr auto buffer = ResourceCategory::BUFFER;
    constexpr auto image = ResourceCategory::IMAGE;
    constexpr auto any = ResourceCategory::BUFFER | ResourceCategory::IMAGE;
    constexpr auto fragment_tests = Stage::EARLY_FRAGMENT_TESTS | Stage::LATE_FRAGMENT_TESTS;
    constexpr auto all_shaders = Stage::VERTEX_SHADER | Stage::TESSELLATION_CONTROL_SHADER | Stage::TESSELLATION_EVALUATION_SHADER |
                                 Stage::GEOMETRY_SHADER | Stage::FRAGMENT_SHADER | Stage::COMPUTE_SHADER | Stage::TASK_SHADER |
                                 Stage::MESH_SHADER | Stage::RAY_TRACING_SHADER;
    constexpr auto structure_users = all_shaders | Stage::ACCELERATION_STRUCTURE_BUILD;
    return std::array<ResourceStateSynchronization, ENUMERATOR_COUNT<ResourceState>>{{
        {UNDEFINED, Stage::NONE, Access::NONE, Layout::UNDEFINED, any, false},
        {GENERAL, Stage::ALL_COMMANDS, Access::MEMORY_READ | Access::MEMORY_WRITE, Layout::GENERAL, any, true},
        {INDIRECT_ARGUMENT, Stage::DRAW_INDIRECT, Access::INDIRECT_COMMAND_READ, Layout::UNDEFINED, buffer, false},
        {VERTEX_BUFFER, Stage::VERTEX_INPUT, Access::VERTEX_ATTRIBUTE_READ, Layout::UNDEFINED, buffer, false},
        {INDEX_BUFFER, Stage::VERTEX_INPUT, Access::INDEX_READ, Layout::UNDEFINED, buffer, false},
        {VERTEX_SHADER_UNIFORM_READ, Stage::VERTEX_SHADER, Access::UNIFORM_READ, Layout::UNDEFINED, buffer, false},
        {VERTEX_SHADER_SAMPLED_READ, Stage::VERTEX_SHADER, Access::SHADER_SAMPLED_READ, Layout::SHADER_READ_ONLY, any, false},
        {VERTEX_SHADER_STORAGE_READ, Stage::VERTEX_SHADER, Access::SHADER_STORAGE_READ, Layout::GENERAL, any, false},
        {FRAGMENT_SHADER_UNIFORM_READ, Stage::FRAGMENT_SHADER, Access::UNIFORM_READ, Layout::UNDEFINED, buffer, false},
        {FRAGMENT_SHADER_SAMPLED_READ, Stage::FRAGMENT_SHADER, Access::SHADER_SAMPLED_READ, Layout::SHADER_READ_ONLY, any, false},
        {FRAGMENT_SHADER_STORAGE_READ, Stage::FRAGMENT_SHADER, Access::SHADER_STORAGE_READ, Layout::GENERAL, any, false},
        {FRAGMENT_SHADER_STORAGE_WRITE, Stage::FRAGMENT_SHADER, Access::SHADER_STORAGE_WRITE, Layout::GENERAL, any, true},
        {FRAGMENT_SHADER_INPUT_ATTACHMENT, Stage::FRAGMENT_SHADER, Access::INPUT_ATTACHMENT_READ, Layout::SHADER_READ_ONLY, image, false},
        {COMPUTE_SHADER_UNIFORM_READ, Stage::COMPUTE_SHADER, Access::UNIFORM_READ, Layout::UNDEFINED, buffer, false},
        {COMPUTE_SHADER_SAMPLED_READ, Stage::COMPUTE_SHADER, Access::SHADER_SAMPLED_READ, Layout::SHADER_READ_ONLY, any, false},
        {COMPUTE_SHADER_STORAGE_READ, Stage::COMPUTE_SHADER, Access::SHADER_STORAGE_READ, Layout::GENERAL, any, false},
        {COMPUTE_SHADER_STORAGE_WRITE, Stage::COMPUTE_SHADER, Access::SHADER_STORAGE_WRITE, Layout::GENERAL, any, true},
        {COMPUTE_SHADER_STORAGE_READ_WRITE, Stage::COMPUTE_SHADER, Access::SHADER_STORAGE_READ | Access::SHADER_STORAGE_WRITE, Layout::GENERAL, any, true},
        {COLOR_ATTACHMENT_READ, Stage::COLOR_ATTACHMENT_OUTPUT, Access::COLOR_ATTACHMENT_READ, Layout::COLOR_ATTACHMENT, image, false},
        {COLOR_ATTACHMENT_WRITE, Stage::COLOR_ATTACHMENT_OUTPUT, Access::COLOR_ATTACHMENT_WRITE, Layout::COLOR_ATTACHMENT, image, true},
        {COLOR_ATTACHMENT_READ_WRITE, Stage::COLOR_ATTACHMENT_OUTPUT, Access::COLOR_ATTACHMENT_READ | Access::COLOR_ATTACHMENT_WRITE, Layout::COLOR_ATTACHMENT, image, true},
        {DEPTH_STENCIL_ATTACHMENT_READ, fragment_tests, Access::DEPTH_STENCIL_ATTACHMENT_READ, Layout::DEPTH_STENCIL_READ_ONLY, image, false},
        {DEPTH_STENCIL_ATTACHMENT_WRITE, fragment_tests, Access::DEPTH_STENCIL_ATTACHMENT_READ | Access::DEPTH_STENCIL_ATTACHMENT_WRITE, Layout::DEPTH_STENCIL_ATTACHMENT, image, true},
        {TRANSFER_SOURCE, Stage::TRANSFER, Access::TRANSFER_READ, Layout::TRANSFER_SOURCE, any, false},
        {TRANSFER_DESTINATION, Stage::TRANSFER, Access::TRANSFER_WRITE, Layout::TRANSFER_DESTINATION, any, true},
        {HOST_READ, Stage::HOST, Access::HOST_READ, Layout::GENERAL, any, false},
        {HOST_WRITE, Stage::HOST, Access::HOST_WRITE, Layout::GENERAL, any, true},
        {PRESENT, Stage::NONE, Access::NONE, Layout::PRESENT_SOURCE, image, false},
        {SHADER_SAMPLED_READ, all_shaders, Access::SHADER_SAMPLED_READ, Layout::SHADER_READ_ONLY, any, false},
        {SHADER_STORAGE_READ, all_shaders, Access::SHADER_STORAGE_READ, Layout::GENERAL, any, false},
        {SHADER_STORAGE_READ_WRITE, all_shaders, Access::SHADER_STORAGE_READ | Access::SHADER_STORAGE_WRITE, Layout::GENERAL, any, true},
        {ACCELERATION_STRUCTURE_BUILD_INPUT, Stage::ACCELERATION_STRUCTURE_BUILD, Access::SHADER_STORAGE_READ, Layout::UNDEFINED, buffer, false},
        {ACCELERATION_STRUCTURE_WRITE, Stage::ACCELERATION_STRUCTURE_BUILD, Access::ACCELERATION_STRUCTURE_READ | Access::ACCELERATION_STRUCTURE_WRITE, Layout::UNDEFINED, buffer, true},
        {ACCELERATION_STRUCTURE_READ, structure_users, Access::ACCELERATION_STRUCTURE_READ, Layout::UNDEFINED, buffer, false},
        {SHADING_RATE_ATTACHMENT, Stage::SHADING_RATE_ATTACHMENT, Access::SHADING_RATE_ATTACHMENT_READ, Layout::SHADING_RATE_ATTACHMENT, image, false},
    }};
}();

/// @brief Synchronization scope of @p state.
[[nodiscard]] constexpr auto 
resource_state_synchronization(ResourceState state) noexcept -> const ResourceStateSynchronization& 
{
    const auto index = static_cast<std::size_t>(std::to_underlying(state));
    return RESOURCE_STATE_SYNCHRONIZATION[index < RESOURCE_STATE_SYNCHRONIZATION.size() ? index : 0];
}

// =================================================================================================
// Queue capabilities
// =================================================================================================

/// @brief Kinds of work each QueueType accepts, indexed by the enumerator's value. A graphics queue
///        also runs compute and transfer work; a compute queue also runs transfer work.
inline constexpr std::array<QueueCapability, ENUMERATOR_COUNT<QueueType>> QUEUE_TYPE_CAPABILITIES{
    QueueCapability::GRAPHICS | QueueCapability::COMPUTE | QueueCapability::TRANSFER,
    QueueCapability::COMPUTE | QueueCapability::TRANSFER,
    QueueCapability::TRANSFER,
    QueueCapability::SPARSE_BINDING,
};

/// @brief Kinds of work a queue of type @p type accepts.
[[nodiscard]] constexpr auto
queue_type_capabilities(QueueType type) noexcept -> QueueCapability
{
    const auto index = static_cast<std::size_t>(std::to_underlying(type));
    return index < QUEUE_TYPE_CAPABILITIES.size() ? QUEUE_TYPE_CAPABILITIES[index] : QueueCapability::NONE;
}

} // namespace gfx

// =================================================================================================
// Compile-time verification of the tables above
// =================================================================================================
namespace gfx::enumeration_verification 
{

/// Checks order (sequential: value == index; bitmask: single ascending bits) and unique spellings.
template<class Enumeration, std::size_t Count>
consteval auto 
names_are_consistent(const std::array<EnumeratorName<Enumeration>, Count>& names, bool is_bitmask) -> bool 
{
    // The underlying type of the enumeration, converted to unsigned so that we can compare values without worrying about signedness.
    using Unsigned = std::make_unsigned_t<std::underlying_type_t<Enumeration>>;

    // Check that every enumerator has a non-empty name, that the enumerators are in the right order, and that no two enumerators have the same name.
    for (std::size_t index = 0; index < Count; ++index) 
    {
        // The enumerator's value, converted to unsigned so that we can compare values without worrying about signedness.
        const auto value = static_cast<Unsigned>(std::to_underlying(names[index].value));

        // Check that the enumerator has a non-empty name.
        if (names[index].name.empty()) 
        {
            return false;
        }

        // Check that the enumerators are in the right order, and that no two enumerators have the same name.
        if (is_bitmask) 
        {
            // Check that the enumerators are in ascending order, and that each enumerator is a single bit (1, 2, 4, 8, ...).
            if (!std::has_single_bit(value)) 
            {
                return false;
            }

            // Check that the enumerators are in ascending order, and that each enumerator is a single bit (1, 2, 4, 8, ...).
            if (index > 0 && value <= static_cast<Unsigned>(std::to_underlying(names[index - 1].value))) 
            {
                return false;
            }
        } 
        else if (value != index) // Sequential enumerations must have values equal to their index.
        {
            return false;
        }

        // Iterate over all previous enumerators to check that no two enumerators have the same name.
        for (std::size_t other = 0; other < index; ++other) 
        {
            // Check that no two enumerators have the same name.
            if (names[other].name == names[index].name) 
            {
                return false;
            }
        }
    }
    return true;
}

#if defined(__GNUC__)
/// The compiler's own spelling of the enumerator Value: its last `::` component, or an empty view
/// when Value names no enumerator (GCC and Clang then print a cast such as "(gfx::Format)72").
template<auto Value>
consteval auto 
compiler_spelling() -> std::string_view 
{
    // The compiler's own spelling of the enumerator Value, as printed by __PRETTY_FUNCTION__.
    const std::string_view signature = __PRETTY_FUNCTION__;

    // The signature is of the form:
    const auto start = signature.find("Value = ");

    // If not found, the signature is not in the expected format, so we return an empty view.
    if (start == std::string_view::npos) 
    {
        return {};
    }

    // The signature is of the form:
    const auto end = signature.find_first_of(";]", start);
    const auto value = signature.substr(start + 8, end - start - 8);

    // If not found, the signature is not in the expected format, so we return an empty view.
    if (value.find(')') != std::string_view::npos) 
    {
        return {};
    }

    // The signature is of the form:
    const auto separator = value.rfind("::");

    // If not found, the signature is not in the expected format, so we return the whole value.
    return separator == std::string_view::npos ? value 
                                               : value.substr(separator + 2);
}

/// Every NAMES entry is spelled exactly like its enumerator, and the enumeration declares no
/// enumerator missing from NAMES (sequential: right after the last entry; bitmask: any other bit).
template<DescribedEnumeration Enumeration>
consteval auto 
names_match_compiler() -> bool 
{
    using Traits = EnumerationTraits<Enumeration>;
    using Underlying = std::underlying_type_t<Enumeration>;

    // The number of entries in NAMES, so that we can check every entry for a spelling match.
    constexpr auto count = Traits::NAMES.size();

    // Check that every NAMES entry is spelled exactly like its enumerator.
    const bool entries_match = []<std::size_t... Index>(std::index_sequence<Index...>) {
        return ((compiler_spelling<Traits::NAMES[Index].value>() == Traits::NAMES[Index].name) && ...);
    }(std::make_index_sequence<count>{});

    // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
    if constexpr (Traits::IS_BITMASK) 
    {
        // The number of bits in the underlying type of the enumeration, so that we can check every bit for a missing enumerator.
        constexpr auto bit_count = std::numeric_limits<std::make_unsigned_t<Underlying>>::digits;

        // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
        const bool no_unnamed_bit = []<std::size_t... Bit>(std::index_sequence<Bit...>) 
        {
            constexpr auto named = [] {
                // The union of all bits named by the enumerators in NAMES, so that we can check every bit for a missing enumerator.
                std::make_unsigned_t<Underlying> bits = 0;

                // Compute the union of all bits named by the enumerators in NAMES.
                for (const auto& entry : Traits::NAMES) 
                {
                    bits = static_cast<std::make_unsigned_t<Underlying>>(bits | static_cast<std::make_unsigned_t<Underlying>>(std::to_underlying(entry.value)));
                }

                // Return the union of all bits named by the enumerators in NAMES.
                return bits;
            }();

            // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
            return ((((named >> Bit) & 1U) != 0 ||
                     compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(std::uint64_t{1} << Bit))>().empty()) && ...);
        }(std::make_index_sequence<bit_count>{});

        // The entries in NAMES match the compiler's spelling, and the enumeration declares no enumerator missing from NAMES.
        return entries_match && no_unnamed_bit;
    } 
    else 
    {
        // The entries in NAMES match the compiler's spelling, and the enumeration declares no enumerator missing from NAMES (the last enumerator is right after the last entry in NAMES).
        return entries_match && compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(count))>().empty();
    }
}
#else
template<DescribedEnumeration Enumeration>
consteval auto names_match_compiler() -> bool {
    return true;
}
#endif

template<DescribedEnumeration Enumeration>
consteval auto 
verify() -> bool 
{
    // Check that the entries in NAMES are consistent with the enumeration, and that they match the compiler's spelling of the enumerators.
    return names_are_consistent(EnumerationTraits<Enumeration>::NAMES, EnumerationTraits<Enumeration>::IS_BITMASK)
        && names_match_compiler<Enumeration>();
}


// Compile-time verification of the tables above, to ensure that they are consistent with the enumerations they describe.
static_assert(verify<BackendKind>(), "EnumerationTraits<BackendKind>::NAMES is out of sync with the enumeration");
static_assert(verify<QueueType>(), "EnumerationTraits<QueueType>::NAMES is out of sync with the enumeration");
static_assert(verify<QueueCapability>(), "EnumerationTraits<QueueCapability>::NAMES is out of sync with the enumeration");
static_assert(verify<PhysicalDeviceType>(), "EnumerationTraits<PhysicalDeviceType>::NAMES is out of sync with the enumeration");
static_assert(verify<PhysicalDevicePreference>(), "EnumerationTraits<PhysicalDevicePreference>::NAMES is out of sync with the enumeration");
static_assert(verify<DeviceFeature>(), "EnumerationTraits<DeviceFeature>::NAMES is out of sync with the enumeration");
static_assert(verify<Format>(), "EnumerationTraits<Format>::NAMES is out of sync with the enumeration");
static_assert(verify<FormatNumericType>(), "EnumerationTraits<FormatNumericType>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageAspect>(), "EnumerationTraits<ImageAspect>::NAMES is out of sync with the enumeration");
static_assert(verify<MemoryLocation>(), "EnumerationTraits<MemoryLocation>::NAMES is out of sync with the enumeration");
static_assert(verify<MemoryProperty>(), "EnumerationTraits<MemoryProperty>::NAMES is out of sync with the enumeration");
static_assert(verify<MemoryDomain>(), "EnumerationTraits<MemoryDomain>::NAMES is out of sync with the enumeration");
static_assert(verify<ResourceCategory>(), "EnumerationTraits<ResourceCategory>::NAMES is out of sync with the enumeration");
static_assert(verify<BufferUsage>(), "EnumerationTraits<BufferUsage>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageDimension>(), "EnumerationTraits<ImageDimension>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageUsage>(), "EnumerationTraits<ImageUsage>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageCreationOption>(), "EnumerationTraits<ImageCreationOption>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageViewType>(), "EnumerationTraits<ImageViewType>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageTiling>(), "EnumerationTraits<ImageTiling>::NAMES is out of sync with the enumeration");
static_assert(verify<SampleCount>(), "EnumerationTraits<SampleCount>::NAMES is out of sync with the enumeration");
static_assert(verify<ImageLayout>(), "EnumerationTraits<ImageLayout>::NAMES is out of sync with the enumeration");
static_assert(verify<ComponentSwizzle>(), "EnumerationTraits<ComponentSwizzle>::NAMES is out of sync with the enumeration");
static_assert(verify<SharingMode>(), "EnumerationTraits<SharingMode>::NAMES is out of sync with the enumeration");
static_assert(verify<PipelineStage>(), "EnumerationTraits<PipelineStage>::NAMES is out of sync with the enumeration");
static_assert(verify<MemoryAccess>(), "EnumerationTraits<MemoryAccess>::NAMES is out of sync with the enumeration");
static_assert(verify<ResourceState>(), "EnumerationTraits<ResourceState>::NAMES is out of sync with the enumeration");
static_assert(verify<WaitStatus>(), "EnumerationTraits<WaitStatus>::NAMES is out of sync with the enumeration");
static_assert(verify<LoadOperation>(), "EnumerationTraits<LoadOperation>::NAMES is out of sync with the enumeration");
static_assert(verify<StoreOperation>(), "EnumerationTraits<StoreOperation>::NAMES is out of sync with the enumeration");
static_assert(verify<ResolveMode>(), "EnumerationTraits<ResolveMode>::NAMES is out of sync with the enumeration");
static_assert(verify<CompareOperation>(), "EnumerationTraits<CompareOperation>::NAMES is out of sync with the enumeration");
static_assert(verify<Filter>(), "EnumerationTraits<Filter>::NAMES is out of sync with the enumeration");
static_assert(verify<SamplerMipmapMode>(), "EnumerationTraits<SamplerMipmapMode>::NAMES is out of sync with the enumeration");
static_assert(verify<SamplerAddressMode>(), "EnumerationTraits<SamplerAddressMode>::NAMES is out of sync with the enumeration");
static_assert(verify<BorderColor>(), "EnumerationTraits<BorderColor>::NAMES is out of sync with the enumeration");
static_assert(verify<PrimitiveTopology>(), "EnumerationTraits<PrimitiveTopology>::NAMES is out of sync with the enumeration");
static_assert(verify<PolygonMode>(), "EnumerationTraits<PolygonMode>::NAMES is out of sync with the enumeration");
static_assert(verify<CullMode>(), "EnumerationTraits<CullMode>::NAMES is out of sync with the enumeration");
static_assert(verify<FrontFace>(), "EnumerationTraits<FrontFace>::NAMES is out of sync with the enumeration");
static_assert(verify<BlendFactor>(), "EnumerationTraits<BlendFactor>::NAMES is out of sync with the enumeration");
static_assert(verify<BlendOperation>(), "EnumerationTraits<BlendOperation>::NAMES is out of sync with the enumeration");
static_assert(verify<ColorComponent>(), "EnumerationTraits<ColorComponent>::NAMES is out of sync with the enumeration");
static_assert(verify<StencilOperation>(), "EnumerationTraits<StencilOperation>::NAMES is out of sync with the enumeration");
static_assert(verify<IndexType>(), "EnumerationTraits<IndexType>::NAMES is out of sync with the enumeration");
static_assert(verify<VertexInputRate>(), "EnumerationTraits<VertexInputRate>::NAMES is out of sync with the enumeration");
static_assert(verify<ShaderStage>(), "EnumerationTraits<ShaderStage>::NAMES is out of sync with the enumeration");
static_assert(verify<ShaderSourceFormat>(), "EnumerationTraits<ShaderSourceFormat>::NAMES is out of sync with the enumeration");
static_assert(verify<DescriptorType>(), "EnumerationTraits<DescriptorType>::NAMES is out of sync with the enumeration");
static_assert(verify<DescriptorBindingOption>(), "EnumerationTraits<DescriptorBindingOption>::NAMES is out of sync with the enumeration");
static_assert(verify<PipelineBindPoint>(), "EnumerationTraits<PipelineBindPoint>::NAMES is out of sync with the enumeration");
static_assert(verify<CommandBufferLevel>(), "EnumerationTraits<CommandBufferLevel>::NAMES is out of sync with the enumeration");
static_assert(verify<CommandPoolOption>(), "EnumerationTraits<CommandPoolOption>::NAMES is out of sync with the enumeration");
static_assert(verify<QueryType>(), "EnumerationTraits<QueryType>::NAMES is out of sync with the enumeration");
static_assert(verify<PipelineStatistic>(), "EnumerationTraits<PipelineStatistic>::NAMES is out of sync with the enumeration");
static_assert(verify<PresentMode>(), "EnumerationTraits<PresentMode>::NAMES is out of sync with the enumeration");
static_assert(verify<ColorSpace>(), "EnumerationTraits<ColorSpace>::NAMES is out of sync with the enumeration");
static_assert(verify<SwapchainStatus>(), "EnumerationTraits<SwapchainStatus>::NAMES is out of sync with the enumeration");
static_assert(verify<DebugMessageSeverity>(), "EnumerationTraits<DebugMessageSeverity>::NAMES is out of sync with the enumeration");
static_assert(verify<DebugMessageType>(), "EnumerationTraits<DebugMessageType>::NAMES is out of sync with the enumeration");
static_assert(verify<LayoutMismatch>(), "EnumerationTraits<LayoutMismatch>::NAMES is out of sync with the enumeration");
static_assert(verify<ShaderReloadResult>(), "EnumerationTraits<ShaderReloadResult>::NAMES is out of sync with the enumeration");
static_assert(verify<ValidationLevel>(), "EnumerationTraits<ValidationLevel>::NAMES is out of sync with the enumeration");
static_assert(verify<BindlessResourceKind>(), "EnumerationTraits<BindlessResourceKind>::NAMES is out of sync with the enumeration");
static_assert(verify<PipelineStatus>(), "EnumerationTraits<PipelineStatus>::NAMES is out of sync with the enumeration");
static_assert(verify<PipelineCreationMode>(), "EnumerationTraits<PipelineCreationMode>::NAMES is out of sync with the enumeration");
static_assert(verify<DynamicState>(), "EnumerationTraits<DynamicState>::NAMES is out of sync with the enumeration");
static_assert(verify<StencilFace>(), "EnumerationTraits<StencilFace>::NAMES is out of sync with the enumeration");
static_assert(verify<AccelerationStructureType>(), "EnumerationTraits<AccelerationStructureType>::NAMES is out of sync with the enumeration");
static_assert(verify<AccelerationStructureBuildOption>(), "EnumerationTraits<AccelerationStructureBuildOption>::NAMES is out of sync with the enumeration");
static_assert(verify<AccelerationStructureBuildMode>(), "EnumerationTraits<AccelerationStructureBuildMode>::NAMES is out of sync with the enumeration");
static_assert(verify<GeometryOption>(), "EnumerationTraits<GeometryOption>::NAMES is out of sync with the enumeration");
static_assert(verify<AccelerationStructureInstanceOption>(), "EnumerationTraits<AccelerationStructureInstanceOption>::NAMES is out of sync with the enumeration");
static_assert(verify<AccelerationStructureCopyMode>(), "EnumerationTraits<AccelerationStructureCopyMode>::NAMES is out of sync with the enumeration");
static_assert(verify<RayTracingShaderGroupType>(), "EnumerationTraits<RayTracingShaderGroupType>::NAMES is out of sync with the enumeration");
static_assert(verify<CrashDiagnostic>(), "EnumerationTraits<CrashDiagnostic>::NAMES is out of sync with the enumeration");
static_assert(verify<CaptureTool>(), "EnumerationTraits<CaptureTool>::NAMES is out of sync with the enumeration");
static_assert(verify<SamplerReductionMode>(), "EnumerationTraits<SamplerReductionMode>::NAMES is out of sync with the enumeration");
static_assert(verify<ShadingRate>(), "EnumerationTraits<ShadingRate>::NAMES is out of sync with the enumeration");
static_assert(verify<ShadingRateCombiner>(), "EnumerationTraits<ShadingRateCombiner>::NAMES is out of sync with the enumeration");

consteval auto 
format_properties_are_ordered() -> bool 
{
    // Check that every enumerator's value matches its index in the array, so that we can index the array by the enumerator's value.
    for (std::size_t index = 0; index < FORMAT_PROPERTIES.size(); ++index) 
    {
        // Check that the enumerator's value matches its index in the array, so that we can index the array by the enumerator's value.
        if (static_cast<std::size_t>(std::to_underlying(FORMAT_PROPERTIES[index].format)) != index) 
        {
            return false;
        }
    }
    return true;
}
static_assert(format_properties_are_ordered(), "FORMAT_PROPERTIES must list every Format in declaration order");

consteval auto 
resource_states_are_ordered() -> bool 
{
    // Check that every enumerator's value matches its index in the array, so that we can index the array by the enumerator's value.
    for (std::size_t index = 0; index < RESOURCE_STATE_SYNCHRONIZATION.size(); ++index) 
    {
        // Get the entry for this index in RESOURCE_STATE_SYNCHRONIZATION.
        const auto& entry = RESOURCE_STATE_SYNCHRONIZATION[index];

        // Check that the enumerator's value matches its index in the array, so that we can index the array by the enumerator's value.
        if (static_cast<std::size_t>(std::to_underlying(entry.state)) != index) 
        {
            return false;
        }
        // A state flagged as writing must perform a write access, and vice versa.
        constexpr auto write_accesses = MemoryAccess::SHADER_STORAGE_WRITE | MemoryAccess::COLOR_ATTACHMENT_WRITE |
            MemoryAccess::DEPTH_STENCIL_ATTACHMENT_WRITE | MemoryAccess::TRANSFER_WRITE | MemoryAccess::HOST_WRITE |
            MemoryAccess::MEMORY_WRITE | MemoryAccess::ACCELERATION_STRUCTURE_WRITE;
        // Check that the `writes` flag matches the presence of write accesses.
        if (entry.writes != has_any(entry.access, write_accesses)) 
        {
            return false;
        }
    }
    return true;
}
static_assert(resource_states_are_ordered(),
              "RESOURCE_STATE_SYNCHRONIZATION must list every ResourceState in order, with `writes` matching its accesses");

} // namespace gfx::enumeration_verification

// =================================================================================================
// std::format support: std::format("{}", gfx::Format::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM"
// =================================================================================================

/// @brief Formats any described gfx enumeration by its spelling (bitmasks as "A | B").
template<gfx::DescribedEnumeration Enumeration>
struct std::formatter<Enumeration, char> : std::formatter<std::string_view, char> 
{
    template<class FormatContext>
    auto format(Enumeration value, FormatContext& context) const -> FormatContext::iterator 
    {
        // Check if the enumeration is a bitmask enumeration, and format it accordingly.
        if constexpr (gfx::BitmaskEnumeration<Enumeration>) 
        {
            // For bitmask enumerations, we can format the value as a string by using the to_string function, which will return a string with the names of 
            // the enumerators that are set in the bitmask, separated by " | ". If no enumerators are set, it will return "0x0". We can then use the 
            // std::formatter for std::string_view to format this string.
            const std::string text = gfx::to_string(value);
            return std::formatter<std::string_view, char>::format(text, context);
        } 
        else 
        {
            // For non-bitmask enumerations, we can use the underlying type to format the value as a string.
            return std::formatter<std::string_view, char>::format(gfx::to_string(value), context);
        }
    }
};
