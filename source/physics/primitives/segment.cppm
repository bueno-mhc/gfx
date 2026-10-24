/// @file segment.cppm
/// @brief Segment: the points between two end points (the core of capsules).
export module gfx.physics.primitives:segment;

import std;
import gfx.core.math;
import gfx.physics.enums;
import :aabb;

export namespace gfx::physics
{

/// @brief The points `start + (end - start) * f` for f in [0, 1].
struct Segment
{
    Vector3 start{}; ///< The first end.
    Vector3 end{};   ///< The second end.

    /// @brief Equal ends (exactly).
    [[nodiscard]] constexpr auto operator==(const Segment&) const noexcept -> bool = default;

    /// @brief From the start to the end (not normalized).
    [[nodiscard]] constexpr auto direction() const noexcept -> Vector3
    {
        return end - start;
    }

    /// @brief The length.
    [[nodiscard]] constexpr auto length() const noexcept -> float
    {
        return gfx::length(end - start);
    }

    /// @brief The point at @p fraction (0: the start, 1: the end).
    [[nodiscard]] constexpr auto at(float fraction) const noexcept -> Vector3
    {
        return start + (end - start) * fraction;
    }

    /// @brief The fraction (in [0, 1]) of the point closest to @p point.
    [[nodiscard]] constexpr auto closest_fraction(Vector3 point) const noexcept -> float
    {
        const Vector3 along = end - start;
        const float squared = length_squared(along);
        return squared <= GEOMETRIC_TOLERANCE * GEOMETRIC_TOLERANCE ? 0.0F : std::clamp(dot(point - start, along) / squared, 0.0F, 1.0F);
    }

    /// @brief The point closest to @p point.
    [[nodiscard]] constexpr auto closest_point(Vector3 point) const noexcept -> Vector3
    {
        return at(closest_fraction(point));
    }

    /// @brief The squared distance from @p point.
    [[nodiscard]] constexpr auto squared_distance(Vector3 point) const noexcept -> float
    {
        return distance_squared(point, closest_point(point));
    }

    /// @brief The end farthest along @p direction (the support function of the segment).
    [[nodiscard]] constexpr auto support(Vector3 toward) const noexcept -> Vector3
    {
        return dot(end - start, toward) > 0 ? end : start;
    }

    /// @brief No rounding: 0 (a capsule adds its radius).
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0;
    }

    /// @brief The bounding box.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return {.minimum = component_minimum(start, end), .maximum = component_maximum(start, end)};
    }
};

/// @brief The closest points of two segments (their fractions and positions).
struct SegmentClosestPoints
{
    float first_fraction = 0;   ///< Along the first segment.
    float second_fraction = 0;  ///< Along the second segment.
    Vector3 first_point{};      ///< On the first segment.
    Vector3 second_point{};     ///< On the second segment.
};

/// @brief The closest points of @p first and @p second (for parallel segments, one of the closest pairs).
[[nodiscard]] constexpr auto
closest_points(const Segment& first,
               const Segment& second) noexcept -> SegmentClosestPoints
{
    // Ericson, Real-Time Collision Detection 5.1.9: minimize |P(s) - Q(t)|² over the unit square.
    const Vector3 d1 = first.direction();
    const Vector3 d2 = second.direction();
    const Vector3 r = first.start - second.start;
    const float a = dot(d1, d1);
    const float e = dot(d2, d2);
    const float f = dot(d2, r);
    constexpr float degenerate = GEOMETRIC_TOLERANCE * GEOMETRIC_TOLERANCE;
    float s = 0;
    float t = 0;
    if (a <= degenerate && e <= degenerate)
    {
        s = 0;
        t = 0;
    }
    else if (a <= degenerate)
    {
        t = std::clamp(f / e, 0.0F, 1.0F);
    }
    else
    {
        const float c = dot(d1, r);
        if (e <= degenerate)
        {
            s = std::clamp(-c / a, 0.0F, 1.0F);
        }
        else
        {
            const float b = dot(d1, d2);
            const float denominator = a * e - b * b;
            s = denominator > degenerate ? std::clamp((b * f - c * e) / denominator, 0.0F, 1.0F) : 0.0F;
            t = (b * s + f) / e;
            if (t < 0)
            {
                t = 0;
                s = std::clamp(-c / a, 0.0F, 1.0F);
            }
            else if (t > 1)
            {
                t = 1;
                s = std::clamp((b - c) / a, 0.0F, 1.0F);
            }
        }
    }
    return {.first_fraction = s, .second_fraction = t, .first_point = first.at(s), .second_point = second.at(t)};
}

} // namespace gfx::physics
