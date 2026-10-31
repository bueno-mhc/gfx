/// @file gpu_xpbd_settings.cppm
/// @brief The GPU backend's settings and what it finds on a device (exported by gfx.physics.gpu_xpbd).
export module gfx.physics.gpu_xpbd:settings;

import std;
import gfx.enums;
import gfx.physics.enums;

export namespace gfx::physics::gpu_xpbd
{

/// @brief How the backend chooses the variant of the passes that can use an optional GPU feature.
struct GpuXpbdVariants
{
    GpuVariantChoice subgroups = GpuVariantChoice::AUTOMATIC;         ///< Scans, reductions and sorts with subgroup arithmetic.
    GpuVariantChoice integer64_atomics = GpuVariantChoice::AUTOMATIC; ///< Nearest hits of queries in one pass of 64-bit atomics.
};

/// @brief The backend's own settings (those PhysicsWorldDescription has no place for); worlds made by the factory use
///        the defaults.
struct GpuXpbdDescription
{
    GpuXpbdVariants variants{}; ///< Which variants the passes use.
};

/// @brief What the backend found on a device and chose to run there.
struct GpuXpbdCapabilities
{
    std::string device_name{};                                       ///< The physical device.
    QueueType queue_type = QueueType::COMPUTE;                       ///< The queue the steps are submitted to.
    std::uint32_t queue_family = 0;                                  ///< Its family.
    std::uint32_t maximum_workgroup_invocations = 0;                 ///< Invocations of a workgroup at most.
    std::uint32_t maximum_compute_shared_memory_size = 0;            ///< Bytes of shared memory of a workgroup.
    std::uint64_t maximum_storage_buffer_range = 0;                  ///< Bytes of a storage buffer a shader addresses.
    std::uint32_t minimum_subgroup_size = 0;                         ///< Smallest subgroup.
    std::uint32_t maximum_subgroup_size = 0;                         ///< Largest subgroup.
    SubgroupOperation subgroup_operations = SubgroupOperation::NONE; ///< Subgroup operations of compute shaders.
    std::uint32_t free_storage_buffers = 0;                          ///< Storage buffers the bindless table still holds.
    bool subgroups = false;                                          ///< The subgroup variant runs (else shared memory).
    bool integer64_atomics = false;                                  ///< The 64-bit atomic variant runs (else two 32-bit passes).
    bool timestamps = false;                                         ///< Its queue writes timestamps (phases timed on the GPU).
    bool float16 = false;                                            ///< 16-bit floating point (reported only: contacts need 32 bits).
    bool storage_images = false;                                     ///< Bindless storage images (reported only: no pass is image-shaped).
};

} // namespace gfx::physics::gpu_xpbd
