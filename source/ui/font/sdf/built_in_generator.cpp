/// @file built_in_generator.cpp
/// @brief The built-in generator: true signed distance fields (SDF) without msdfgen, and
///        create_sdf_generator(). Curves are flattened into lines within a hundredth of a texel; the
///        pieces of lines on the outline of the filled area are kept (overlapping contours, common in
///        variable fonts, have edges inside the glyph); every texel takes its distance to the nearest
///        piece and its sign from the non-zero winding rule. Slower than msdfgen, exact to that tolerance.
module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import :implementations;

namespace gfx::ui
{

namespace
{

/// Largest distance, in texels, between a curve and the lines that replace it.
constexpr double FLATTENING_TOLERANCE = 0.01;

/// A line of a flattened outline, in font units.
struct Line
{
    OutlinePoint start{};
    OutlinePoint end{};
};

/// Point of a Bezier segment at @p t.
auto
point_at(const OutlineSegment& segment,
         double t) noexcept -> OutlinePoint
{
    const std::array<OutlinePoint, 4>& p = segment.points;
    const double u = 1.0 - t;
    switch (segment.kind)
    {
        case OutlineSegmentKind::LINE:
            return {u * p[0].x + t * p[1].x, u * p[0].y + t * p[1].y};
        case OutlineSegmentKind::QUADRATIC:
            return {u * u * p[0].x + 2 * u * t * p[1].x + t * t * p[2].x, u * u * p[0].y + 2 * u * t * p[1].y + t * t * p[2].y};
        case OutlineSegmentKind::CUBIC:
            return {u * u * u * p[0].x + 3 * u * u * t * p[1].x + 3 * u * t * t * p[2].x + t * t * t * p[3].x,
                    u * u * u * p[0].y + 3 * u * u * t * p[1].y + 3 * u * t * t * p[2].y + t * t * t * p[3].y};
    }
    return p[0];
}

/// Length of the second difference p0 - 2 p1 + p2.
auto
second_difference(OutlinePoint p0,
                  OutlinePoint p1,
                  OutlinePoint p2) noexcept -> double
{
    return std::hypot(p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y);
}

/// The lines of @p outline, curves split uniformly into enough pieces to stay within @p tolerance (font
/// units): a chord of a piece of length h deviates at most max|B''| h² / 8 from its curve.
auto
flatten(const GlyphOutline& outline,
        double tolerance) -> std::vector<Line>
{
    std::vector<Line> lines;
    for (const OutlineContour& contour : outline.contours)
    {
        for (const OutlineSegment& segment : contour.segments)
        {
            const std::array<OutlinePoint, 4>& p = segment.points;
            double curvature = 0;
            if (segment.kind == OutlineSegmentKind::QUADRATIC)
            {
                curvature = 2 * second_difference(p[0], p[1], p[2]);
            }
            else if (segment.kind == OutlineSegmentKind::CUBIC)
            {
                curvature = 6 * std::max(second_difference(p[0], p[1], p[2]), second_difference(p[1], p[2], p[3]));
            }
            const auto pieces = static_cast<int>(std::clamp(std::ceil(std::sqrt(curvature / (8 * tolerance))), 1.0, 4096.0));
            OutlinePoint previous = p[0];
            for (int piece = 1; piece <= pieces; ++piece)
            {
                const OutlinePoint next = piece == pieces ? segment.end() : point_at(segment, static_cast<double>(piece) / pieces);
                lines.push_back({previous, next});
                previous = next;
            }
        }
    }
    return lines;
}

/// Distance from @p point to @p line.
auto
distance_to(const Line& line,
            OutlinePoint point) noexcept -> double
{
    const double dx = line.end.x - line.start.x;
    const double dy = line.end.y - line.start.y;
    const double length = dx * dx + dy * dy;
    const double t = length > 0 ? std::clamp(((point.x - line.start.x) * dx + (point.y - line.start.y) * dy) / length, 0.0, 1.0) : 0.0;
    return std::hypot(point.x - (line.start.x + t * dx), point.y - (line.start.y + t * dy));
}

/// Winding number of the closed lines around @p point.
auto
winding_number(std::span<const Line> lines,
               OutlinePoint point) noexcept -> int
{
    int winding = 0;
    for (const Line& line : lines)
    {
        const double side = (line.end.x - line.start.x) * (point.y - line.start.y) - (point.x - line.start.x) * (line.end.y - line.start.y);
        if (line.start.y <= point.y && line.end.y > point.y && side > 0)
        {
            ++winding;
        }
        else if (line.start.y > point.y && line.end.y <= point.y && side < 0)
        {
            --winding;
        }
    }
    return winding;
}

/// Where @p line crosses @p other: the parameter along @p line (strictly inside it), or nullopt when the
/// lines are parallel or do not meet.
auto
crossing(const Line& line,
         const Line& other) noexcept -> std::optional<double>
{
    const double line_x = line.end.x - line.start.x;
    const double line_y = line.end.y - line.start.y;
    const double other_x = other.end.x - other.start.x;
    const double other_y = other.end.y - other.start.y;
    const double denominator = line_x * other_y - line_y * other_x;
    if (std::abs(denominator) < 1e-12)
    {
        return std::nullopt;
    }
    const double offset_x = other.start.x - line.start.x;
    const double offset_y = other.start.y - line.start.y;
    const double t = (offset_x * other_y - offset_y * other_x) / denominator;
    const double u = (offset_x * line_y - offset_y * line_x) / denominator;
    if (t > 1e-9 && t < 1 - 1e-9 && u >= 0 && u <= 1)
    {
        return t;
    }
    return std::nullopt;
}

/// The pieces of @p lines on the outline of the filled area (non-zero rule): each line is cut where
/// others cross it, and a piece is kept when the area is filled on one of its sides only (tested
/// @p epsilon font units away).
auto
boundary(std::span<const Line> lines,
         double epsilon) -> std::vector<Line>
{
    std::vector<Line> pieces;
    std::vector<double> cuts;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const Line& line = lines[index];
        const double dx = line.end.x - line.start.x;
        const double dy = line.end.y - line.start.y;
        const double length = std::hypot(dx, dy);
        if (length == 0)
        {
            continue;
        }
        // Where the other lines cross it.
        cuts.assign({0.0, 1.0});
        for (std::size_t other = 0; other < lines.size(); ++other)
        {
            if (other != index)
            {
                if (const std::optional<double> t = crossing(line, lines[other]); t.has_value())
                {
                    cuts.push_back(*t);
                }
            }
        }
        std::ranges::sort(cuts);

        // The pieces with the filled area on one side.
        const auto at = [&](double t) { return OutlinePoint{line.start.x + t * dx, line.start.y + t * dy}; };
        const OutlinePoint normal{-dy / length * epsilon, dx / length * epsilon};
        for (std::size_t cut = 0; cut + 1 < cuts.size(); ++cut)
        {
            if (cuts[cut + 1] - cuts[cut] < 1e-12)
            {
                continue;
            }
            const OutlinePoint middle = at((cuts[cut] + cuts[cut + 1]) / 2);
            const bool left = winding_number(lines, {middle.x + normal.x, middle.y + normal.y}) != 0;
            const bool right = winding_number(lines, {middle.x - normal.x, middle.y - normal.y}) != 0;
            if (left != right)
            {
                pieces.push_back({at(cuts[cut]), at(cuts[cut + 1])});
            }
        }
    }
    return pieces;
}

/// The built-in generator.
class BuiltInGenerator final : public SdfGenerator
{
public:
    [[nodiscard]] auto kind() const noexcept -> SdfGeneratorKind override
    {
        return SdfGeneratorKind::BUILT_IN;
    }

    [[nodiscard]] auto supports(SdfKind kind) const noexcept -> bool override
    {
        return kind == SdfKind::SDF;
    }

    [[nodiscard]] auto layout(const GlyphOutline& outline,
                              std::uint32_t units_per_em,
                              const SdfGenerationDescription& description) const -> GlyphBox override
    {
        require_supported(description.kind);
        const double scale = description.em_size / units_per_em;
        const std::vector<Line> lines = flatten(outline, FLATTENING_TOLERANCE / scale);
        if (lines.empty())
        {
            return {.scale = scale};
        }
        // Bounds of the flattened outline, grown by half the range and the padding (msdf-atlas-gen's box).
        double left = lines.front().start.x;
        double bottom = lines.front().start.y;
        double right = left;
        double top = bottom;
        for (const Line& line : lines)
        {
            left = std::min({left, line.start.x, line.end.x});
            bottom = std::min({bottom, line.start.y, line.end.y});
            right = std::max({right, line.start.x, line.end.x});
            top = std::max({top, line.start.y, line.end.y});
        }
        const double margin = (description.distance_range / 2 + description.padding) / scale;
        left -= margin;
        bottom -= margin;
        right += margin;
        top += margin;
        const double width = scale * (right - left);
        const double height = scale * (top - bottom);
        GlyphBox box{.width = static_cast<std::uint32_t>(std::ceil(width)) + 1,
                     .height = static_cast<std::uint32_t>(std::ceil(height)) + 1,
                     .scale = scale};
        box.translate_x = -left + 0.5 * (box.width - width) / scale;
        box.translate_y = -bottom + 0.5 * (box.height - height) / scale;
        return box;
    }

    [[nodiscard]] auto generate(const GlyphOutline& outline,
                                const GlyphBox& box,
                                const SdfGenerationDescription& description) const -> GlyphField override
    {
        require_supported(description.kind);
        GlyphField field{.width = box.width, .height = box.height, .channel_count = 1};
        if (box.empty())
        {
            return field;
        }
        field.values.resize(std::size_t{box.width} * box.height);
        const std::vector<Line> lines = flatten(outline, FLATTENING_TOLERANCE / box.scale);
        const std::vector<Line> outline_pieces = boundary(lines, FLATTENING_TOLERANCE / box.scale / 100);
        for (std::uint32_t row = 0; row < box.height; ++row)
        {
            for (std::uint32_t column = 0; column < box.width; ++column)
            {
                // The texel's center in font units (rows from the top, y up).
                const OutlinePoint point{(column + 0.5) / box.scale - box.translate_x, (box.height - row - 0.5) / box.scale - box.translate_y};
                double distance = std::numeric_limits<double>::max();
                for (const Line& piece : outline_pieces)
                {
                    distance = std::min(distance, distance_to(piece, point));
                }
                const double sign = winding_number(lines, point) != 0 ? 1.0 : -1.0;
                field.values[std::size_t{row} * box.width + column] = static_cast<float>(0.5 + sign * distance * box.scale / description.distance_range);
            }
        }
        return field;
    }

private:
    static void require_supported(SdfKind kind)
    {
        if (kind != SdfKind::SDF)
        {
            throw_unsupported(UiSubsystem::FONT, "SdfGenerator::generate", std::format("{} fields", to_string(kind)),
                              "the built-in generator computes true signed distances only (SDF)",
                              "generate PSDF, MSDF and MTSDF fields with SdfGeneratorKind::MSDFGEN");
        }
    }
};

} // namespace

auto
make_built_in_generator() -> std::unique_ptr<SdfGenerator>
{
    return std::make_unique<BuiltInGenerator>();
}

auto
create_sdf_generator(SdfGeneratorKind kind) -> std::unique_ptr<SdfGenerator>
{
    switch (kind)
    {
        case SdfGeneratorKind::MSDFGEN:
            return make_msdfgen_generator();
        case SdfGeneratorKind::BUILT_IN:
            return make_built_in_generator();
        case SdfGeneratorKind::CUSTOM:
            break;
    }
    throw_font_usage_error("create_sdf_generator", "SdfGeneratorKind::CUSTOM names a generator of the application",
                           "implement gfx::ui::SdfGenerator and give it to AtlasBuilder::use_generator()");
}

} // namespace gfx::ui
