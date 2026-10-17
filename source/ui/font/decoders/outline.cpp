/// @file outline.cpp
/// @brief GlyphOutline bounds and the OutlineBuilder.
module gfx.ui.atlas_tools;

import std;

namespace gfx::ui
{

auto
GlyphOutline::bounds() const noexcept -> OutlineBounds
{
    // Every point of every segment, control points included.
    bool first = true;
    OutlineBounds bounds;
    for (const OutlineContour& contour : contours)
    {
        for (const OutlineSegment& segment : contour.segments)
        {
            const std::size_t point_count = static_cast<std::size_t>(std::to_underlying(segment.kind)) + 2;
            for (std::size_t index = 0; index < point_count; ++index)
            {
                const OutlinePoint point = segment.points[index];
                if (first)
                {
                    bounds = {.left = point.x, .bottom = point.y, .right = point.x, .top = point.y};
                    first = false;
                }
                bounds.left = std::min(bounds.left, point.x);
                bounds.bottom = std::min(bounds.bottom, point.y);
                bounds.right = std::max(bounds.right, point.x);
                bounds.top = std::max(bounds.top, point.y);
            }
        }
    }
    return bounds;
}

auto
GlyphOutline::segment_count() const noexcept -> std::size_t
{
    std::size_t count = 0;
    for (const OutlineContour& contour : contours)
    {
        count += contour.segments.size();
    }
    return count;
}

void
OutlineBuilder::move_to(OutlinePoint point)
{
    close_contour();
    m_start = point;
    m_current = point;
}

void
OutlineBuilder::line_to(OutlinePoint point)
{
    if (point != m_current)
    {
        m_contour.segments.push_back({.kind = OutlineSegmentKind::LINE, .points = {m_current, point}});
        m_current = point;
    }
}

void
OutlineBuilder::quadratic_to(OutlinePoint control,
                             OutlinePoint point)
{
    if (point != m_current || control != m_current)
    {
        m_contour.segments.push_back({.kind = OutlineSegmentKind::QUADRATIC, .points = {m_current, control, point}});
        m_current = point;
    }
}

void
OutlineBuilder::cubic_to(OutlinePoint first_control,
                         OutlinePoint second_control,
                         OutlinePoint point)
{
    if (point != m_current || first_control != m_current || second_control != m_current)
    {
        m_contour.segments.push_back({.kind = OutlineSegmentKind::CUBIC, .points = {m_current, first_control, second_control, point}});
        m_current = point;
    }
}

auto
OutlineBuilder::finish() -> GlyphOutline
{
    close_contour();
    GlyphOutline outline = std::move(m_outline);
    m_outline = {};
    return outline;
}

void
OutlineBuilder::close_contour()
{
    // Back to the start with a line, then keep the contour unless it has nothing.
    line_to(m_start);
    if (!m_contour.segments.empty())
    {
        m_outline.contours.push_back(std::move(m_contour));
    }
    m_contour = {};
    m_current = m_start;
}

} // namespace gfx::ui
