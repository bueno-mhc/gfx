/// @file msdfgen_generator.cpp
/// @brief The msdfgen generator (SDF, PSDF, MSDF, MTSDF), through msdfgen_kernel.hpp.
module;

#include "msdfgen_kernel.hpp"

module gfx.ui.atlas_tools;

import std;
import :implementations;

namespace gfx::ui
{

namespace
{

/// An outline as the kernel reads it, with the storage its pointers refer to.
class KernelOutline
{
public:
    explicit KernelOutline(const GlyphOutline& outline)
    {
        // Every segment first (the contours point into this storage), then the contours.
        m_segments.reserve(outline.segment_count());
        for (const OutlineContour& contour : outline.contours)
        {
            for (const OutlineSegment& segment : contour.segments)
            {
                msdfgen_kernel::Segment converted{.kind = static_cast<unsigned char>(std::to_underlying(segment.kind))};
                for (std::size_t index = 0; index < segment.points.size(); ++index)
                {
                    converted.points[index] = {.x = segment.points[index].x, .y = segment.points[index].y};
                }
                m_segments.push_back(converted);
            }
        }
        std::size_t first = 0;
        for (const OutlineContour& contour : outline.contours)
        {
            m_contours.push_back({.segments = m_segments.data() + first, .count = contour.segments.size()});
            first += contour.segments.size();
        }
        m_outline = {.contours = m_contours.data(), .count = m_contours.size()};
    }

    KernelOutline(const KernelOutline&) = delete;
    auto operator=(const KernelOutline&) -> KernelOutline& = delete;

    [[nodiscard]] auto outline() const noexcept -> const msdfgen_kernel::Outline&
    {
        return m_outline;
    }

private:
    std::vector<msdfgen_kernel::Segment> m_segments{};
    std::vector<msdfgen_kernel::Contour> m_contours{};
    msdfgen_kernel::Outline m_outline{};
};

/// The kernel's parameters for @p description at @p scale texels per font unit.
auto
parameters_of(const SdfGenerationDescription& description,
              double scale) -> msdfgen_kernel::Parameters
{
    return {.kind = static_cast<int>(std::to_underlying(description.kind)),
            .scale = scale,
            .range = description.distance_range,
            .padding = description.padding,
            .angle_threshold = description.angle_threshold,
            .miter_limit = description.miter_limit,
            .coloring = static_cast<int>(std::to_underlying(description.coloring)),
            .seed = description.coloring_seed,
            .overlap_support = description.overlap_support,
            .scanline_pass = description.scanline_pass};
}

/// The msdfgen generator.
class MsdfgenGenerator final : public SdfGenerator
{
public:
    [[nodiscard]] auto kind() const noexcept -> SdfGeneratorKind override
    {
        return SdfGeneratorKind::MSDFGEN;
    }

    [[nodiscard]] auto supports(SdfKind) const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto layout(const GlyphOutline& outline,
                              std::uint32_t units_per_em,
                              const SdfGenerationDescription& description) const -> GlyphBox override
    {
        const double scale = description.em_size / units_per_em;
        if (outline.empty())
        {
            return {.scale = scale};
        }
        const KernelOutline converted{outline};
        msdfgen_kernel::Box box;
        if (!msdfgen_kernel::layout(converted.outline(), parameters_of(description, scale), box))
        {
            throw_font_usage_error("SdfGenerator::layout", "a contour of the outline is not closed",
                                   "close every contour (OutlineBuilder does)");
        }
        return {.width = static_cast<std::uint32_t>(box.width),
                .height = static_cast<std::uint32_t>(box.height),
                .scale = scale,
                .translate_x = box.translate_x,
                .translate_y = box.translate_y};
    }

    [[nodiscard]] auto generate(const GlyphOutline& outline,
                                const GlyphBox& box,
                                const SdfGenerationDescription& description) const -> GlyphField override
    {
        const auto channels = static_cast<std::uint32_t>(atlas_layout::channel_count(description.kind));
        GlyphField field{.width = box.width, .height = box.height, .channel_count = channels};
        if (box.empty())
        {
            return field;
        }
        field.values.resize(std::size_t{box.width} * box.height * channels);
        const KernelOutline converted{outline};
        const msdfgen_kernel::Box kernel_box{.width = static_cast<int>(box.width),
                                             .height = static_cast<int>(box.height),
                                             .translate_x = box.translate_x,
                                             .translate_y = box.translate_y};
        if (!msdfgen_kernel::generate(converted.outline(), parameters_of(description, box.scale), kernel_box, field.values.data()))
        {
            throw_font_usage_error("SdfGenerator::generate", "a contour of the outline is not closed",
                                   "close every contour (OutlineBuilder does)");
        }
        return field;
    }
};

} // namespace

auto
make_msdfgen_generator() -> std::unique_ptr<SdfGenerator>
{
    return std::make_unique<MsdfgenGenerator>();
}

} // namespace gfx::ui
