/// @file sdf_generator.cppm
/// @brief SdfGenerator: turns a glyph outline into a distance field at atlas build time. gfx provides
///        msdfgen (SDF, PSDF, MSDF, MTSDF) and a built-in exact-distance kernel (SDF), which builds without
///        msdfgen; applications may implement the interface.
export module gfx.ui.atlas_tools:sdf_generator;

import std;
import gfx.ui.enums;
import gfx.ui.atlas_format;
import :outline;

export namespace gfx::ui
{

/// @brief How distance fields are generated.
struct SdfGenerationDescription
{
    SdfKind kind = SdfKind::MTSDF;                  ///< What the texels store.
    double em_size = 32.0;                          ///< Texels per em.
    double distance_range = 4.0;                    ///< Texels between the smallest and largest distance stored.
    double padding = 0.0;                           ///< Texels added around every glyph, inside its quad (room for effects).
    double angle_threshold = 3.0;                   ///< Radians: corners sharper than this get distinct channels (MSDF).
    double miter_limit = 1.0;                       ///< Room given to the miters of sharp corners (MSDF, PSDF); 0: none.
    EdgeColoring coloring = EdgeColoring::INK_TRAP; ///< Edge coloring (MSDF, MTSDF).
    std::uint64_t coloring_seed = 0;                ///< Seed of the edge coloring (fixed: atlases are reproducible).
    bool overlap_support = true;                    ///< Handle contours that overlap (variable fonts often have them).
    bool scanline_pass = true;                      ///< Correct the field's sign with a scanline fill (non-zero rule).
};

/// @brief Where a glyph's field lies: its box in texels and the transform from font units to texels
///        (texel = (font unit + translate) × scale, y up within the box).
struct GlyphBox
{
    std::uint32_t width = 0;  ///< Texels (0 for an empty outline).
    std::uint32_t height = 0; ///< Texels (0 for an empty outline).
    double scale = 0;         ///< Texels per font unit.
    double translate_x = 0;   ///< Font units added to x before scaling.
    double translate_y = 0;   ///< Font units added to y before scaling.

    /// @brief Whether the box has texels.
    [[nodiscard]] constexpr auto empty() const noexcept -> bool
    {
        return width == 0 || height == 0;
    }

    /// @brief The quad of the box: from the first texel center to the last, in em units (y up).
    [[nodiscard]] constexpr auto plane_bounds(std::uint32_t units_per_em) const noexcept -> GlyphPlaneBounds
    {
        if (empty())
        {
            return {};
        }
        const double em = units_per_em;
        return {.left = static_cast<float>((-translate_x + 0.5 / scale) / em),
                .bottom = static_cast<float>((-translate_y + 0.5 / scale) / em),
                .right = static_cast<float>((-translate_x + (width - 0.5) / scale) / em),
                .top = static_cast<float>((-translate_y + (height - 0.5) / scale) / em)};
    }
};

/// @brief A distance field: width × height texels of channel_count values, rows from the top. Values map
///        distances so that 0.5 is the outline, 1 is distance_range / 2 texels inside and 0 as far outside
///        (they may go beyond 0 and 1; quantization clamps them).
struct GlyphField
{
    std::uint32_t width = 0;         ///< Texels.
    std::uint32_t height = 0;        ///< Texels.
    std::uint32_t channel_count = 0; ///< 1 (SDF, PSDF), 3 (MSDF) or 4 (MTSDF).
    std::vector<float> values{};     ///< Row by row from the top, channels interleaved.

    /// @brief Channel @p channel of texel (@p x, @p y), y from the top.
    [[nodiscard]] auto at(std::uint32_t x,
                          std::uint32_t y,
                          std::uint32_t channel = 0) const noexcept -> float
    {
        return values[(std::size_t{y} * width + x) * channel_count + channel];
    }
};

/// @brief Generates distance fields. layout() and generate() may be called from several threads at once.
class SdfGenerator
{
public:
    virtual ~SdfGenerator() = default;

    /// @brief Which generator this is.
    [[nodiscard]] virtual auto kind() const noexcept -> SdfGeneratorKind = 0;

    /// @brief Whether it generates fields of @p kind.
    [[nodiscard]] virtual auto supports(SdfKind kind) const noexcept -> bool = 0;

    /// @brief The box @p outline needs (font units of a face of @p units_per_em) for @p description:
    ///        its bounds, half the distance range and the padding on every side.
    [[nodiscard]] virtual auto layout(const GlyphOutline& outline,
                                      std::uint32_t units_per_em,
                                      const SdfGenerationDescription& description) const -> GlyphBox = 0;

    /// @brief The field of @p outline in @p box (from layout()).
    [[nodiscard]] virtual auto generate(const GlyphOutline& outline,
                                        const GlyphBox& box,
                                        const SdfGenerationDescription& description) const -> GlyphField = 0;
};

/// @brief The generator of @p kind: MSDFGEN or BUILT_IN.
/// @throws gfx::InvalidUsageError for CUSTOM (implement SdfGenerator and give it to the AtlasBuilder).
[[nodiscard]] auto create_sdf_generator(SdfGeneratorKind kind) -> std::unique_ptr<SdfGenerator>;

} // namespace gfx::ui
