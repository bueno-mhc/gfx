/// @file acceleration_structure.cppm
/// @brief AccelerationStructure: the spatial structures rays are traced against, and the inputs of
///        their builds (recorded with Recorder::build_acceleration_structures()).
///
/// Bottom-level structures hold triangles or bounding boxes; top-level structures hold instances of
/// bottom-level ones (AccelerationStructureInstance records in a buffer, the same 64-byte layout in
/// Vulkan and Direct3D 12). Structures live in buffers with ACCELERATION_STRUCTURE_STORAGE usage; ask
/// Device::acceleration_structure_build_sizes() how large that storage and the build scratch must be.
export module gfx.interface:acceleration_structure;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :bindless;

export namespace gfx
{

/// @brief How to create an AccelerationStructure (in existing buffer storage).
struct AccelerationStructureDescription
{
    AccelerationStructureType type = AccelerationStructureType::BOTTOM_LEVEL; ///< Level.
    BufferHandle buffer{};         ///< Storage (ACCELERATION_STRUCTURE_STORAGE usage).
    std::uint64_t offset = 0;      ///< First byte (a multiple of 256).
    std::uint64_t size = 0;        ///< Bytes (AccelerationStructureBuildSizes::structure_size).
    bool bindless = true;          ///< Register a top-level structure in the bindless table.
    std::string_view debug_name{}; ///< Name shown by debugging tools.
};

/// @brief Triangles of a bottom-level build.
struct TriangleGeometry
{
    BufferHandle vertex_buffer{};                     ///< Vertex positions (ACCELERATION_STRUCTURE_BUILD_INPUT usage).
    std::uint64_t vertex_offset = 0;                  ///< First byte of the first position.
    Format vertex_format = Format::R32G32B32_SFLOAT;  ///< Format of a position.
    std::uint64_t vertex_stride = 12;                 ///< Bytes between positions.
    std::uint32_t vertex_count = 0;                   ///< Vertices.
    BufferHandle index_buffer{};                      ///< Indices (null = non-indexed).
    std::uint64_t index_offset = 0;                   ///< First byte of the first index.
    IndexType index_type = IndexType::UNSIGNED_INTEGER_32; ///< Index size.
    std::uint32_t index_count = 0;                    ///< Indices (3 per triangle).
    BufferHandle transform_buffer{};                  ///< Optional 3x4 row-major float transform.
    std::uint64_t transform_offset = 0;               ///< First byte of the transform.
    GeometryOption options = GeometryOption::NONE;    ///< Opaque, ...
};

/// @brief Axis-aligned bounding boxes (6 floats: minimum then maximum corner) of a bottom-level build,
///        for procedural geometry intersected by intersection shaders.
struct BoundingBoxGeometry
{
    BufferHandle buffer{};                         ///< Boxes (ACCELERATION_STRUCTURE_BUILD_INPUT usage).
    std::uint64_t offset = 0;                      ///< First byte.
    std::uint64_t stride = 24;                     ///< Bytes between boxes.
    std::uint32_t count = 0;                       ///< Boxes.
    GeometryOption options = GeometryOption::NONE; ///< Opaque, ...
};

/// @brief Instances of a top-level build: AccelerationStructureInstance records in a buffer.
struct InstanceGeometry
{
    BufferHandle buffer{};    ///< Records (ACCELERATION_STRUCTURE_BUILD_INPUT usage).
    std::uint64_t offset = 0; ///< First byte (a multiple of 16).
    std::uint32_t count = 0;  ///< Instances.
};

/// @brief One build or update, recorded with Recorder::build_acceleration_structures(). Several builds
///        recorded together run in parallel on the GPU.
struct AccelerationStructureBuildDescription
{
    AccelerationStructureType type = AccelerationStructureType::BOTTOM_LEVEL;                       ///< Level.
    AccelerationStructureBuildOption options = AccelerationStructureBuildOption::PREFER_FAST_TRACE; ///< Trade-offs.
    AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::BUILD;                    ///< Build or update.
    std::span<const TriangleGeometry> triangles{};          ///< Bottom level: triangles.
    std::span<const BoundingBoxGeometry> bounding_boxes{};  ///< Bottom level: procedural boxes.
    InstanceGeometry instances{};                           ///< Top level: instances.
    AccelerationStructureHandle source{};                   ///< Structure refitted by an UPDATE.
    AccelerationStructureHandle destination{};              ///< Structure written.
    BufferHandle scratch_buffer{};                          ///< Temporary memory (STORAGE usage).
    std::uint64_t scratch_offset = 0;                       ///< First scratch byte.
};

/// @brief Memory a build needs (Device::acceleration_structure_build_sizes()).
struct AccelerationStructureBuildSizes
{
    std::uint64_t structure_size = 0;      ///< Storage of the structure.
    std::uint64_t build_scratch_size = 0;  ///< Scratch of a BUILD.
    std::uint64_t update_scratch_size = 0; ///< Scratch of an UPDATE.
};

/// @brief One instance of a top-level structure: the record layout Vulkan and Direct3D 12 share.
struct AccelerationStructureInstance
{
    std::array<float, 12> transform{1.0F, 0.0F, 0.0F, 0.0F,
                                    0.0F, 1.0F, 0.0F, 0.0F,
                                    0.0F, 0.0F, 1.0F, 0.0F}; ///< 3x4 row-major object-to-world transform.
    std::uint32_t custom_index : 24 = 0;                     ///< Value shaders read (gl_InstanceCustomIndexEXT).
    std::uint32_t mask : 8 = 0xFF;                           ///< Visibility mask ANDed with the ray's mask.
    std::uint32_t shader_binding_table_offset : 24 = 0;      ///< Hit group record offset.
    std::uint32_t options : 8 = 0;                           ///< AccelerationStructureInstanceOption bits.
    std::uint64_t acceleration_structure_address = 0;        ///< AccelerationStructure::device_address() of a bottom level.
};

/// @brief An instance of @p bottom_level at @p transform.
[[nodiscard]] constexpr auto
make_instance(std::uint64_t bottom_level_address,
              const std::array<float, 12>& transform,
              std::uint32_t custom_index = 0,
              std::uint32_t mask = 0xFF,
              std::uint32_t shader_binding_table_offset = 0,
              AccelerationStructureInstanceOption options = AccelerationStructureInstanceOption::NONE) noexcept
    -> AccelerationStructureInstance
{
    AccelerationStructureInstance instance;
    instance.transform = transform;
    instance.custom_index = custom_index & 0xFFFFFFU;
    instance.mask = mask & 0xFFU;
    instance.shader_binding_table_offset = shader_binding_table_offset & 0xFFFFFFU;
    instance.options = std::to_underlying(options);
    instance.acceleration_structure_address = bottom_level_address;
    return instance;
}

/// @brief A bottom- or top-level acceleration structure. Top-level structures are registered in the
///        bindless table (AccelerationStructureIndex) for ray queries and ray tracing pipelines.
class AccelerationStructure : public BackendObject
{
public:
    /// @brief Handle naming this structure in builds and copies.
    [[nodiscard]] virtual auto handle() const noexcept -> AccelerationStructureHandle = 0;

    /// @brief Level.
    [[nodiscard]] virtual auto type() const noexcept -> AccelerationStructureType = 0;

    /// @brief Bytes of storage.
    [[nodiscard]] virtual auto size() const noexcept -> std::uint64_t = 0;

    /// @brief GPU address, referenced by AccelerationStructureInstance records.
    [[nodiscard]] virtual auto device_address() const noexcept -> std::uint64_t = 0;

    /// @brief Index in the bindless table (top level with bindless = true; invalid otherwise).
    [[nodiscard]] virtual auto bindless_index() const noexcept -> AccelerationStructureIndex = 0;
};

} // namespace gfx

static_assert(sizeof(gfx::AccelerationStructureInstance) == gfx::ACCELERATION_STRUCTURE_INSTANCE_SIZE,
              "the instance record must match VkAccelerationStructureInstanceKHR and D3D12_RAYTRACING_INSTANCE_DESC");
