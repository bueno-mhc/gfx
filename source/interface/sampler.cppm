/// @file sampler.cppm
/// @brief Sampler: filtering and addressing applied when shaders sample images.
export module gfx.interface:sampler;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a Sampler.
struct SamplerDescription
{
    Filter magnification_filter = Filter::LINEAR;                   ///< Filter when magnifying.
    Filter minification_filter = Filter::LINEAR;                    ///< Filter when minifying.
    SamplerMipmapMode mipmap_mode = SamplerMipmapMode::LINEAR;      ///< Filter between mip levels.
    SamplerAddressMode address_mode_u = SamplerAddressMode::REPEAT; ///< Addressing along u.
    SamplerAddressMode address_mode_v = SamplerAddressMode::REPEAT; ///< Addressing along v.
    SamplerAddressMode address_mode_w = SamplerAddressMode::REPEAT; ///< Addressing along w.
    float level_of_detail_bias = 0.0F;                              ///< Added to the computed level of detail.
    std::optional<float> maximum_anisotropy{};                      ///< Anisotropic filtering (DeviceFeature::SAMPLER_ANISOTROPY).
    std::optional<CompareOperation> compare_operation{};            ///< Depth comparison (shadow samplers).
    float minimum_level_of_detail = 0.0F;                           ///< Clamp of the level of detail.
    float maximum_level_of_detail = 1000.0F;                        ///< Clamp of the level of detail.
    BorderColor border_color = BorderColor::OPAQUE_BLACK_FLOAT;     ///< Color for CLAMP_TO_BORDER.
    bool unnormalized_coordinates = false;                          ///< Texel instead of [0, 1] coordinates.
    std::string_view debug_name{};                                  ///< Name shown by debugging tools.
};

/// @brief Filtering and addressing state bound to descriptors.
class Sampler : public BackendObject
{
public:
    /// @brief Handle naming this sampler in descriptors.
    [[nodiscard]] virtual auto handle() const noexcept -> SamplerHandle = 0;
};

} // namespace gfx
