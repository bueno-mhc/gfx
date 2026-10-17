/// @file atlas_packer.cppm
/// @brief AtlasPacker: places the boxes of glyphs on atlas pages. gfx provides a tight packer
///        (msdf-atlas-gen's rectangle packer, smallest square page) and a grid packer; applications may
///        implement the interface.
export module gfx.ui.atlas_tools:atlas_packer;

import std;
import gfx.ui.enums;

export namespace gfx::ui
{

/// @brief Size of a box to place, in texels (0 × 0 boxes, whitespace, are not placed).
struct AtlasBoxSize
{
    std::uint32_t width = 0;  ///< Texels.
    std::uint32_t height = 0; ///< Texels.
};

/// @brief Where a box went: its page and top-left texel (y down).
struct AtlasPlacement
{
    std::uint32_t page = 0; ///< Page index.
    std::uint32_t x = 0;    ///< Left texel.
    std::uint32_t y = 0;    ///< Top texel.
};

/// @brief Constraints on the pages.
struct AtlasPackingDescription
{
    std::uint32_t maximum_dimension = 4096; ///< Largest width and height of a page; more pages are used beyond.
    std::uint32_t spacing = 0;              ///< Texels left empty between boxes.
    bool power_of_two = true;               ///< Page sides are powers of two (otherwise multiples of 4).
};

/// @brief The pages and where every box went.
struct AtlasPacking
{
    std::uint32_t page_width = 0;               ///< Texels; every page has the same size.
    std::uint32_t page_height = 0;              ///< Texels.
    std::uint32_t page_count = 0;               ///< 0 when no box has texels (only whitespace).
    std::vector<AtlasPlacement> placements{};   ///< One per box, in the order of the boxes.
};

/// @brief Places boxes on pages. Must be deterministic: the same boxes give the same placements.
class AtlasPacker
{
public:
    virtual ~AtlasPacker() = default;

    /// @brief Which packer this is.
    [[nodiscard]] virtual auto kind() const noexcept -> AtlasPackerKind = 0;

    /// @brief Places @p boxes within @p description.
    /// @throws gfx::InvalidUsageError when a box is larger than the maximum dimension.
    [[nodiscard]] virtual auto pack(std::span<const AtlasBoxSize> boxes,
                                    const AtlasPackingDescription& description) const -> AtlasPacking = 0;
};

/// @brief The packer of @p kind: TIGHT or GRID.
/// @throws gfx::InvalidUsageError for CUSTOM (implement AtlasPacker and give it to the AtlasBuilder).
[[nodiscard]] auto create_atlas_packer(AtlasPackerKind kind) -> std::unique_ptr<AtlasPacker>;

} // namespace gfx::ui
