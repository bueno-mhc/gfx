/// @file atlas_packer.cpp
/// @brief The tight packer (msdf-atlas-gen's rectangle packer, through rectangle_packer.hpp) and the grid
///        packer, and create_atlas_packer(). Both use the smallest square page that holds every box, and
///        pages of the maximum dimension when one is not enough.
module;

#include "rectangle_packer.hpp"

module gfx.ui.atlas_tools;

import std;
import :implementations;

namespace gfx::ui
{

namespace
{

/// The side after @p side when looking for a page: the next power of two, or the next multiple of 4
///        an eighth larger.
auto
next_side(std::uint32_t side,
          bool power_of_two) noexcept -> std::uint32_t
{
    if (power_of_two)
    {
        return side * 2;
    }
    return (side + std::max(4U, side / 8) + 3) / 4 * 4;
}

/// The first side to try for boxes covering @p area texels.
auto
first_side(std::uint64_t area,
           bool power_of_two) noexcept -> std::uint32_t
{
    const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(area))));
    return power_of_two ? std::bit_ceil(std::max(side, 1U)) : (std::max(side, 4U) + 3) / 4 * 4;
}

/// Checks that every box fits in a page, spacing included.
void
require_boxes_fit(std::span<const AtlasBoxSize> boxes,
                  const AtlasPackingDescription& description)
{
    for (std::size_t index = 0; index < boxes.size(); ++index)
    {
        const AtlasBoxSize box = boxes[index];
        if (box.width > description.maximum_dimension || box.height > description.maximum_dimension)
        {
            throw_font_usage_error("AtlasPacker::pack",
                                   std::format("box {} ({} × {} texels) is larger than a page ({} texels at most)", index, box.width,
                                               box.height, description.maximum_dimension),
                                   "lower the em size, the distance range or the padding, or raise the maximum dimension");
        }
    }
}

/// The tight packer.
class TightPacker final : public AtlasPacker
{
public:
    [[nodiscard]] auto kind() const noexcept -> AtlasPackerKind override
    {
        return AtlasPackerKind::TIGHT;
    }

    [[nodiscard]] auto pack(std::span<const AtlasBoxSize> boxes,
                            const AtlasPackingDescription& description) const -> AtlasPacking override
    {
        require_boxes_fit(boxes, description);
        AtlasPacking packing{.placements = std::vector<AtlasPlacement>(boxes.size())};
        const std::uint32_t spacing = description.spacing;

        // The boxes with texels, each grown by the spacing (pages too, so the last row needs none).
        std::vector<std::size_t> indices;
        std::uint64_t area = 0;
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            if (boxes[index].width != 0 && boxes[index].height != 0)
            {
                indices.push_back(index);
                area += std::uint64_t{boxes[index].width + spacing} * (boxes[index].height + spacing);
            }
        }
        if (indices.empty())
        {
            return packing;
        }
        const auto rectangles_of = [&](std::span<const std::size_t> selection)
        {
            std::vector<rectangle_packing::Rectangle> rectangles;
            for (const std::size_t index : selection)
            {
                rectangles.push_back({.width = static_cast<int>(boxes[index].width + spacing), .height = static_cast<int>(boxes[index].height + spacing)});
            }
            return rectangles;
        };
        const auto try_page = [&](std::span<const std::size_t> selection, std::uint32_t side, std::vector<rectangle_packing::Rectangle>& rectangles)
        {
            rectangles = rectangles_of(selection);
            return rectangle_packing::pack(rectangles.data(), static_cast<int>(rectangles.size()), static_cast<int>(side + spacing),
                                           static_cast<int>(side + spacing));
        };

        // One page: the smallest square side that holds every box.
        std::vector<rectangle_packing::Rectangle> rectangles;
        for (std::uint32_t side = std::min(first_side(area, description.power_of_two), description.maximum_dimension);;
             side = std::min(next_side(side, description.power_of_two), description.maximum_dimension))
        {
            if (try_page(indices, side, rectangles) == 0)
            {
                packing.page_width = side;
                packing.page_height = side;
                packing.page_count = 1;
                for (std::size_t box = 0; box < indices.size(); ++box)
                {
                    packing.placements[indices[box]] = {.page = 0,
                                                        .x = static_cast<std::uint32_t>(rectangles[box].x),
                                                        .y = static_cast<std::uint32_t>(rectangles[box].y)};
                }
                return packing;
            }
            if (side == description.maximum_dimension)
            {
                break;
            }
        }

        // Several pages of the maximum dimension, each filled with what remains.
        const std::uint32_t side = description.maximum_dimension;
        packing.page_width = side;
        packing.page_height = side;
        std::vector<std::size_t> remaining = indices;
        while (!remaining.empty())
        {
            try_page(remaining, side, rectangles);
            std::vector<std::size_t> unplaced;
            for (std::size_t box = 0; box < remaining.size(); ++box)
            {
                if (rectangles[box].x < 0)
                {
                    unplaced.push_back(remaining[box]);
                    continue;
                }
                packing.placements[remaining[box]] = {.page = packing.page_count,
                                                      .x = static_cast<std::uint32_t>(rectangles[box].x),
                                                      .y = static_cast<std::uint32_t>(rectangles[box].y)};
            }
            if (unplaced.size() == remaining.size())
            {
                throw_font_usage_error("AtlasPacker::pack", "a box does not fit in an empty page", "raise the maximum dimension");
            }
            ++packing.page_count;
            remaining = std::move(unplaced);
        }
        return packing;
    }
};

/// The grid packer: cells of the largest box, row by row.
class GridPacker final : public AtlasPacker
{
public:
    [[nodiscard]] auto kind() const noexcept -> AtlasPackerKind override
    {
        return AtlasPackerKind::GRID;
    }

    [[nodiscard]] auto pack(std::span<const AtlasBoxSize> boxes,
                            const AtlasPackingDescription& description) const -> AtlasPacking override
    {
        require_boxes_fit(boxes, description);
        AtlasPacking packing{.placements = std::vector<AtlasPlacement>(boxes.size())};
        const std::uint32_t spacing = description.spacing;

        // The cell: the largest box and the spacing.
        std::uint32_t cell_width = 0;
        std::uint32_t cell_height = 0;
        std::size_t count = 0;
        for (const AtlasBoxSize box : boxes)
        {
            if (box.width != 0 && box.height != 0)
            {
                cell_width = std::max(cell_width, box.width + spacing);
                cell_height = std::max(cell_height, box.height + spacing);
                ++count;
            }
        }
        if (count == 0)
        {
            return packing;
        }
        const auto cells_of = [&](std::uint32_t side) -> std::uint64_t
        {
            return std::uint64_t{(side + spacing) / cell_width} * ((side + spacing) / cell_height);
        };

        // The smallest square side holding every cell, else pages of the maximum dimension.
        std::uint32_t side = std::min(first_side(std::uint64_t{cell_width} * cell_height * count, description.power_of_two),
                                      description.maximum_dimension);
        while (cells_of(side) < count && side < description.maximum_dimension)
        {
            side = std::min(next_side(side, description.power_of_two), description.maximum_dimension);
        }
        const std::uint32_t columns = (side + spacing) / cell_width;
        const std::uint64_t cells_per_page = cells_of(side);
        packing.page_width = side;
        packing.page_height = side;
        packing.page_count = static_cast<std::uint32_t>((count + cells_per_page - 1) / cells_per_page);
        std::uint64_t cell = 0;
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            if (boxes[index].width == 0 || boxes[index].height == 0)
            {
                continue;
            }
            const std::uint64_t in_page = cell % cells_per_page;
            packing.placements[index] = {.page = static_cast<std::uint32_t>(cell / cells_per_page),
                                         .x = static_cast<std::uint32_t>(in_page % columns * cell_width),
                                         .y = static_cast<std::uint32_t>(in_page / columns * cell_height)};
            ++cell;
        }
        return packing;
    }
};

} // namespace

auto
create_atlas_packer(AtlasPackerKind kind) -> std::unique_ptr<AtlasPacker>
{
    switch (kind)
    {
        case AtlasPackerKind::TIGHT:
            return std::make_unique<TightPacker>();
        case AtlasPackerKind::GRID:
            return std::make_unique<GridPacker>();
        case AtlasPackerKind::CUSTOM:
            break;
    }
    throw_font_usage_error("create_atlas_packer", "AtlasPackerKind::CUSTOM names a packer of the application",
                           "implement gfx::ui::AtlasPacker and give it to AtlasBuilder::use_packer()");
}

} // namespace gfx::ui
