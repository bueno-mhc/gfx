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
    GpuVariantChoice integer64_atomics = GpuVariantChoice::AUTOMATIC; ///< 64-bit atomics on the factory's device when the GPU has
                                                                      ///< them. Reported only: no pass uses them (queries pick their
                                                                      ///< nearest hit on the CPU).
};

/// @brief The backend's own settings (those PhysicsWorldDescription has no place for); worlds made by the factory use
///        the defaults.
struct GpuXpbdDescription
{
    GpuXpbdVariants variants{};                               ///< Which variants the passes use.
    GpuSolverMode solver_mode = GpuSolverMode::GRAPH_COLORING; ///< How contact constraints are projected.
    GpuBvhLayout bvh_layout = GpuBvhLayout::FOUR_WIDE;        ///< The collider trees' traversal layout (four-wide: G2's
                                                              ///< measurements, 38 to 59 % fewer pair-search steps).
    GpuOptimization optimizations = DEFAULT_GPU_OPTIMIZATIONS; ///< Which optimizations run (each: GpuOptimization; NONE: the
                                                              ///< baseline, as the backend was before them).
    std::filesystem::path pipeline_cache_path{};              ///< With SHARED_PIPELINES: the file the device's pipeline cache
                                                              ///< is read from and written to (empty: none; a file another
                                                              ///< driver wrote is ignored). The first world of a device
                                                              ///< names it for the device's later worlds.
};

/// @brief What the GPU worlds of a device share with SHARED_PIPELINES (shared_pipeline_statistics()).
struct SharedPipelineStatistics
{
    std::uint32_t worlds = 0;       ///< Worlds holding the device's pipelines (0: none, the rest is zero too).
    std::uint32_t pipelines = 0;    ///< Compute pipelines made.
    bool cache_file_loaded = false; ///< The cache file was read and its driver accepted it.
    std::uint32_t cache_writes = 0; ///< Times the cache file was written (after pipelines were made).
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
    bool subgroups = false;                                          ///< The subgroup variant runs (else shared memory): subgroup
                                                                     ///< arithmetic, subgroups of GPU_XPBD_MINIMUM_SUBGROUP_SIZE at least.
    bool integer64_atomics = false;                                  ///< 64-bit atomics enabled on the device and allowed by the
                                                                     ///< variants (reported only: no pass uses them).
    bool timestamps = false;                                         ///< Its queue writes timestamps (phases timed on the GPU).
    bool float16 = false;                                            ///< 16-bit floating point (reported only: contacts need 32 bits).
    bool storage_images = false;                                     ///< Bindless storage images (reported only: no pass is image-shaped).
    GpuOptimization optimizations = GpuOptimization::NONE;           ///< The optimizations in effect (those asked for that the
                                                                     ///< device allows).
};

} // namespace gfx::physics::gpu_xpbd
