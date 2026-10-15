/// @file graphics_pipeline.cppm
/// @brief GraphicsPipeline: vertex and fragment shaders with their fixed-function state, for dynamic
///        rendering (attachment formats instead of a render pass).
export module gfx.interface:graphics_pipeline;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :shader_module;

export namespace gfx
{

/// @brief A vertex buffer binding: stride and step rate.
struct VertexInputBinding
{
    std::uint32_t binding = 0;                         ///< Binding index.
    std::uint32_t stride = 0;                          ///< Bytes between consecutive elements.
    VertexInputRate rate = VertexInputRate::VERTEX;    ///< Per vertex or per instance.

    constexpr bool operator==(const VertexInputBinding&) const = default;
};

/// @brief A vertex attribute: where a shader input location reads from.
struct VertexInputAttribute
{
    std::uint32_t location = 0;        ///< Shader input location.
    std::uint32_t binding = 0;         ///< Vertex buffer binding it reads.
    Format format = Format::UNDEFINED; ///< Format of the data in the buffer.
    std::uint32_t offset = 0;          ///< Byte offset inside one element.

    constexpr bool operator==(const VertexInputAttribute&) const = default;
};

/// @brief Stencil test and operations for one face orientation.
struct StencilOperationState
{
    StencilOperation fail = StencilOperation::KEEP;       ///< When the stencil test fails.
    StencilOperation pass = StencilOperation::KEEP;       ///< When both tests pass.
    StencilOperation depth_fail = StencilOperation::KEEP; ///< When the depth test fails.
    CompareOperation compare = CompareOperation::ALWAYS;  ///< Stencil comparison.
    std::uint32_t compare_mask = 0xFF;                    ///< Bits compared.
    std::uint32_t write_mask = 0xFF;                      ///< Bits written.
    std::uint32_t reference = 0;                          ///< Reference value.
};

/// @brief Rasterization state.
struct RasterizationState
{
    PolygonMode polygon_mode = PolygonMode::FILL;         ///< Fill, lines or points.
    CullMode cull_mode = CullMode::BACK;                  ///< Faces discarded.
    FrontFace front_face = FrontFace::COUNTER_CLOCKWISE;  ///< Winding of front faces.
    bool depth_clamp = false;                             ///< Clamp depth (DeviceFeature::DEPTH_CLAMP).
    bool depth_bias = false;                              ///< Apply depth bias.
    float depth_bias_constant = 0.0F;                     ///< Constant depth bias.
    float depth_bias_slope = 0.0F;                        ///< Slope-scaled depth bias.
    float line_width = 1.0F;                              ///< Line width (DeviceFeature::WIDE_LINES for != 1).
    bool conservative = false;                            ///< Conservative rasterization (DeviceFeature::CONSERVATIVE_RASTERIZATION).
};

/// @brief Depth and stencil tests.
struct DepthStencilState
{
    bool depth_test = false;                                   ///< Test depth.
    bool depth_write = false;                                  ///< Write depth.
    CompareOperation depth_compare = CompareOperation::LESS_OR_EQUAL; ///< Depth comparison.
    bool stencil_test = false;                                 ///< Test stencil.
    StencilOperationState front{};                             ///< Front faces.
    StencilOperationState back{};                              ///< Back faces.
    bool depth_bounds_test = false;                            ///< Depth bounds test (DeviceFeature::DEPTH_BOUNDS).
    float minimum_depth_bound = 0.0F;                          ///< Lower depth bound.
    float maximum_depth_bound = 1.0F;                          ///< Upper depth bound.
};

/// @brief Blending of one color attachment.
struct ColorBlendAttachment
{
    bool blend = false;                                              ///< Enable blending.
    BlendFactor source_color_factor = BlendFactor::ONE;              ///< Source color factor.
    BlendFactor destination_color_factor = BlendFactor::ZERO;        ///< Destination color factor.
    BlendOperation color_operation = BlendOperation::ADD;            ///< Color operation.
    BlendFactor source_alpha_factor = BlendFactor::ONE;              ///< Source alpha factor.
    BlendFactor destination_alpha_factor = BlendFactor::ZERO;        ///< Destination alpha factor.
    BlendOperation alpha_operation = BlendOperation::ADD;            ///< Alpha operation.
    ColorComponent write_mask = ColorComponent::ALL;                 ///< Components written.
};

/// @brief Fixed-function state of a graphics pipeline (everything but shaders, layouts and formats).
struct GraphicsPipelineState
{
    PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST; ///< Primitive assembly.
    RasterizationState rasterization{};                            ///< Rasterization.
    DepthStencilState depth_stencil{};                             ///< Depth and stencil tests.
    bool alpha_to_coverage = false;                                ///< Alpha to coverage (multisampling).
    std::span<const ColorBlendAttachment> color_blend{};           ///< Per attachment (empty = no blending).
    std::array<float, 4> blend_constants{};                        ///< Initial BlendFactor::CONSTANT_* values.
    DynamicState dynamic_states = DynamicState::NONE;              ///< State set while recording (fewer pipelines).
    bool primitive_restart = false;                                ///< A maximal index restarts strips and fans.
    std::uint32_t sample_mask = 0xFFFFFFFFU;                       ///< Samples written.
    ShadingRate shading_rate = ShadingRate::ONE_BY_ONE;            ///< Pipeline shading rate (DeviceFeature::VARIABLE_RATE_SHADING).
    std::uint32_t patch_control_points = 0;                        ///< Vertices per patch (tessellation pipelines).
    std::string_view debug_name{};                                 ///< Name shown by debugging tools.
};

/// @brief How to create a GraphicsPipeline. Creation checks the shaders against the pipeline layout,
///        the vertex attributes and the color formats (validate_graphics_pipeline()).
struct GraphicsPipelineDescription
{
    PipelineLayoutHandle layout{};                          ///< Pipeline layout (null = the bindless layout).
    ShaderModuleHandle vertex_shader{};                     ///< Vertex shader (null for mesh pipelines).
    ShaderModuleHandle fragment_shader{};                   ///< Fragment shader module (null = depth only).
    ShaderModuleHandle task_shader{};                       ///< Task shader of a mesh pipeline (optional).
    ShaderModuleHandle mesh_shader{};                       ///< Mesh shader (DeviceFeature::MESH_SHADER; replaces vertex input).
    ShaderModuleHandle geometry_shader{};                   ///< Geometry shader (DeviceFeature::GEOMETRY_SHADER).
    ShaderModuleHandle tessellation_control_shader{};       ///< Tessellation control shader (TESSELLATION_SHADER).
    ShaderModuleHandle tessellation_evaluation_shader{};    ///< Tessellation evaluation shader (TESSELLATION_SHADER).
    std::span<const VertexInputBinding> vertex_bindings{};  ///< Vertex buffer bindings (none for bindless vertex pulling).
    std::span<const VertexInputAttribute> vertex_attributes{}; ///< Vertex attributes.
    std::span<const Format> color_formats{};                ///< Formats of the color attachments.
    Format depth_format = Format::UNDEFINED;                ///< Depth attachment format (UNDEFINED = none).
    Format stencil_format = Format::UNDEFINED;              ///< Stencil attachment format (UNDEFINED = none).
    SampleCount samples = SampleCount::ONE;                 ///< Samples per pixel.
    std::uint32_t view_mask = 0;                            ///< Views rendered at once (DeviceFeature::MULTIVIEW; 0 = one).
    GraphicsPipelineState state{};                          ///< Fixed-function state.
    std::span<const SpecializationConstant> specialization_constants{}; ///< Constant values of every stage.
    PipelineCacheHandle cache{};                            ///< Cache to use (null = the device's own).
    PipelineCreationMode creation_mode = PipelineCreationMode::IMMEDIATE; ///< Compile now or in the background.
};

/// @brief Vertex and fragment shaders with fixed-function state; a peer of ComputePipeline.
class GraphicsPipeline : public BackendObject
{
public:
    /// @brief Handle naming this pipeline when binding it.
    [[nodiscard]] virtual auto handle() const noexcept -> GraphicsPipelineHandle = 0;

    /// @brief Pipeline layout.
    [[nodiscard]] virtual auto layout() const noexcept -> PipelineLayoutHandle = 0;

    /// @brief Whether it can be bound (see PipelineCreationMode::ASYNCHRONOUS).
    [[nodiscard]] virtual auto status() const noexcept -> PipelineStatus = 0;

    /// @brief Blocks until compiled.
    /// @throws The compilation error when it failed.
    virtual void wait() const = 0;
};

void
GraphicsPipeline::wait() const
{
    throw_not_implemented("GraphicsPipeline::wait");
}

} // namespace gfx
