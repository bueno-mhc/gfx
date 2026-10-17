/// @file outline.cppm
/// @brief GlyphOutline: the contours of a glyph as lines and quadratic and cubic Bezier curves in font
///        units (y up), what every FontDecoder produces and every SdfGenerator consumes, and the
///        OutlineBuilder decoders fill it with.
export module gfx.ui.atlas_tools:outline;

import std;
import gfx.ui.enums;

export namespace gfx::ui
{

/// @brief A point of an outline, in font units (y up from the baseline).
struct OutlinePoint
{
    double x = 0; ///< Horizontal coordinate.
    double y = 0; ///< Vertical coordinate.

    /// @brief Points compare coordinate by coordinate.
    [[nodiscard]] constexpr auto operator==(const OutlinePoint&) const noexcept -> bool = default;
};

/// @brief A segment of a contour: points[0] is its start; a line ends at points[1], a quadratic curve
///        has its control point at points[1] and ends at points[2], a cubic curve has its control points
///        at points[1] and points[2] and ends at points[3].
struct OutlineSegment
{
    OutlineSegmentKind kind = OutlineSegmentKind::LINE; ///< Line or curve.
    std::array<OutlinePoint, 4> points{};               ///< Start, control points, end.

    /// @brief The last point of the segment.
    [[nodiscard]] constexpr auto end() const noexcept -> OutlinePoint
    {
        switch (kind)
        {
            case OutlineSegmentKind::LINE:      return points[1];
            case OutlineSegmentKind::QUADRATIC: return points[2];
            case OutlineSegmentKind::CUBIC:     return points[3];
        }
        return points[1];
    }
};

/// @brief A closed contour: each segment starts where the previous one ends, the last ends at the start.
struct OutlineContour
{
    std::vector<OutlineSegment> segments{}; ///< In drawing order.
};

/// @brief The control-point bounds of an outline, in font units.
struct OutlineBounds
{
    double left = 0;   ///< Smallest x.
    double bottom = 0; ///< Smallest y.
    double right = 0;  ///< Largest x.
    double top = 0;    ///< Largest y.
};

/// @brief The outline of a glyph: closed contours whose winding gives the filled area (non-zero rule).
struct GlyphOutline
{
    std::vector<OutlineContour> contours{}; ///< Every contour.

    /// @brief Whether the outline has no contour (a space).
    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return contours.empty();
    }

    /// @brief Bounds of every point (control points included), or all zero for an empty outline.
    [[nodiscard]] auto bounds() const noexcept -> OutlineBounds;

    /// @brief Total number of segments.
    [[nodiscard]] auto segment_count() const noexcept -> std::size_t;
};

/// @brief Builds a GlyphOutline from drawing commands (move, line, curve), as FreeType and stb_truetype
///        report them: contours are closed with a line when they do not end where they start, and
///        segments of zero length are dropped.
class OutlineBuilder
{
public:
    /// @brief Starts a contour at @p point (closing the current one).
    void move_to(OutlinePoint point);

    /// @brief A line to @p point.
    void line_to(OutlinePoint point);

    /// @brief A quadratic curve through @p control to @p point.
    void quadratic_to(OutlinePoint control,
                      OutlinePoint point);

    /// @brief A cubic curve through @p first_control and @p second_control to @p point.
    void cubic_to(OutlinePoint first_control,
                  OutlinePoint second_control,
                  OutlinePoint point);

    /// @brief Closes the current contour and returns the outline (the builder is then empty).
    [[nodiscard]] auto finish() -> GlyphOutline;

private:
    /// @brief Closes the current contour with a line to its start, and keeps it unless it is empty.
    void close_contour();

    GlyphOutline m_outline{};
    OutlineContour m_contour{};
    OutlinePoint m_start{};
    OutlinePoint m_current{};
};

} // namespace gfx::ui
