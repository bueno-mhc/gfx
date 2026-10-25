/// @file gjk.cppm
/// @brief GJK (Gilbert–Johnson–Keerthi) over any two convex shapes at their poses: the distance between their cores
///        (margins excluded), the closest points, and — when the cores overlap — the simplex EPA starts from. The
///        simplex's closest point to the origin comes from Voronoi-region tests (Ericson, Real-Time Collision Detection
///        5.1), with the barycentric weights that give the closest points on each shape. constexpr: shapes known at
///        compile time collide at compile time.
export module gfx.physics.narrow_phase:gjk;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;

export namespace gfx::physics
{

/// @brief A point of the Minkowski difference (first - second) and the points of each shape that made it.
struct SupportPoint
{
    Vector3 first{};      ///< On the first shape's core (world space).
    Vector3 second{};     ///< On the second shape's core.
    Vector3 difference{}; ///< first - second.
};

/// @brief What GJK found.
struct GjkResult
{
    GjkStatus status = GjkStatus::SEPARATED;  ///< Apart, or touching or overlapping.
    float distance = 0.0F;                    ///< Between the cores (0 when they overlap).
    Vector3 point_first{};                    ///< The first core's closest point (world space).
    Vector3 point_second{};                   ///< The second core's closest point.
    std::array<SupportPoint, 4> simplex{};    ///< The final simplex (EPA's start).
    std::uint32_t simplex_size = 0;           ///< Its points in use.
    std::uint32_t iterations = 0;             ///< Support points asked for.
};

/// @brief The Minkowski difference of two posed convex cores: its support function in world space.
template<ConvexShape First, ConvexShape Second>
struct MinkowskiDifference
{
    const First& first;
    Transform first_pose{};
    const Second& second;
    Transform second_pose{};

    /// @brief The difference's farthest point along @p direction (world space).
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> SupportPoint
    {
        const Vector3 on_first = transform_point(first_pose, first.support(rotate(conjugate(first_pose.orientation), direction)));
        const Vector3 on_second = transform_point(second_pose, second.support(rotate(conjugate(second_pose.orientation), -direction)));
        return {.first = on_first, .second = on_second, .difference = on_first - on_second};
    }
};

namespace gjk_detail
{

/// The closest point of a simplex to the origin, with the simplex reduced to the points that span it and their
/// barycentric weights.
struct Simplex
{
    std::array<SupportPoint, 4> points{};
    std::array<float, 4> weights{};
    std::uint32_t size = 0;
    Vector3 closest{};

    constexpr void keep(std::initializer_list<std::pair<std::uint32_t, float>> chosen) noexcept
    {
        std::array<SupportPoint, 4> kept{};
        std::uint32_t count = 0;
        closest = {};
        for (const auto& [index, weight] : chosen)
        {
            kept[count] = points[index];
            weights[count] = weight;
            closest += points[index].difference * weight;
            ++count;
        }
        points = kept;
        size = count;
    }
};

/// The segment (a, b) of @p simplex.
constexpr void
solve_segment(Simplex& simplex) noexcept
{
    const Vector3 a = simplex.points[0].difference;
    const Vector3 b = simplex.points[1].difference;
    const Vector3 ab = b - a;
    const float length = dot(ab, ab);
    const float t = length > 0.0F ? -dot(a, ab) / length : 0.0F;
    if (t <= 0.0F)
    {
        simplex.keep({{0, 1.0F}});
    }
    else if (t >= 1.0F)
    {
        simplex.keep({{1, 1.0F}});
    }
    else
    {
        simplex.keep({{0, 1.0F - t}, {1, t}});
    }
}

/// The triangle (a, b, c) of @p simplex: Ericson's ClosestPtPointTriangle for the origin.
constexpr void
solve_triangle(Simplex& simplex) noexcept
{
    const Vector3 a = simplex.points[0].difference;
    const Vector3 b = simplex.points[1].difference;
    const Vector3 c = simplex.points[2].difference;
    const Vector3 ab = b - a;
    const Vector3 ac = c - a;
    const float d1 = -dot(ab, a);
    const float d2 = -dot(ac, a);
    if (d1 <= 0.0F && d2 <= 0.0F)
    {
        simplex.keep({{0, 1.0F}});
        return;
    }
    const float d3 = -dot(ab, b);
    const float d4 = -dot(ac, b);
    if (d3 >= 0.0F && d4 <= d3)
    {
        simplex.keep({{1, 1.0F}});
        return;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F)
    {
        const float v = d1 / (d1 - d3);
        simplex.keep({{0, 1.0F - v}, {1, v}});
        return;
    }
    const float d5 = -dot(ab, c);
    const float d6 = -dot(ac, c);
    if (d6 >= 0.0F && d5 <= d6)
    {
        simplex.keep({{2, 1.0F}});
        return;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F)
    {
        const float w = d2 / (d2 - d6);
        simplex.keep({{0, 1.0F - w}, {2, w}});
        return;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && d4 - d3 >= 0.0F && d5 - d6 >= 0.0F)
    {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        simplex.keep({{1, 1.0F - w}, {2, w}});
        return;
    }
    const float denominator = va + vb + vc;
    if (!(denominator > 0.0F))
    {
        // A degenerate triangle: the best of its edges.
        Simplex edge = simplex;
        edge.size = 2;
        solve_segment(edge);
        simplex = edge;
        return;
    }
    const float v = vb / denominator;
    const float w = vc / denominator;
    simplex.keep({{0, 1.0F - v - w}, {1, v}, {2, w}});
}

/// Whether the origin and @p d are on opposite sides of the plane (a, b, c) (degenerate planes count as opposite).
[[nodiscard]] constexpr auto
origin_outside(Vector3 a,
               Vector3 b,
               Vector3 c,
               Vector3 d) noexcept -> bool
{
    const Vector3 normal = cross(b - a, c - a);
    const float origin_side = -dot(a, normal);
    const float other_side = dot(d - a, normal);
    return origin_side * other_side <= 0.0F && !(origin_side == 0.0F && other_side != 0.0F);
}

/// The tetrahedron (a, b, c, d) of @p simplex: the closest of the faces the origin is outside of; false when the
/// origin is inside (the cores overlap).
constexpr auto
solve_tetrahedron(Simplex& simplex) noexcept -> bool
{
    constexpr std::array<std::array<std::uint32_t, 4>, 4> FACES{{{0, 1, 2, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {1, 3, 2, 0}}};
    bool outside_any = false;
    float best = std::numeric_limits<float>::max();
    Simplex chosen = simplex;
    for (const auto& face : FACES)
    {
        const SupportPoint* p = simplex.points.data();
        if (!origin_outside(p[face[0]].difference, p[face[1]].difference, p[face[2]].difference, p[face[3]].difference))
        {
            continue;
        }
        outside_any = true;
        Simplex triangle{.points = {p[face[0]], p[face[1]], p[face[2]]}, .size = 3};
        solve_triangle(triangle);
        const float distance = length_squared(triangle.closest);
        if (distance < best)
        {
            best = distance;
            chosen = triangle;
        }
    }
    if (!outside_any)
    {
        return false;
    }
    simplex = chosen;
    return true;
}

} // namespace gjk_detail

/// @brief GJK between the cores of @p first at @p first_pose and @p second at @p second_pose.
template<ConvexShape First, ConvexShape Second>
[[nodiscard]] constexpr auto
gjk(const First& first,
    const Transform& first_pose,
    const Second& second,
    const Transform& second_pose) noexcept -> GjkResult
{
    const MinkowskiDifference<First, Second> difference{.first = first, .first_pose = first_pose, .second = second, .second_pose = second_pose};

    // Start from the support point toward the second shape's center from the first's.
    Vector3 start = second_pose.position - first_pose.position;
    start = length_squared(start) > 0.0F ? start : Vector3{.x = 1.0F};
    gjk_detail::Simplex simplex{};
    simplex.points[0] = difference.support(-start);
    simplex.weights[0] = 1.0F;
    simplex.size = 1;
    simplex.closest = simplex.points[0].difference;
    const float scale = std::max(length(simplex.closest), length(start)) + 1.0F;
    const float touching = 1e-6F * scale;

    GjkResult result{};
    bool overlapping = false;
    for (result.iterations = 1; result.iterations < MAXIMUM_GJK_ITERATIONS; ++result.iterations)
    {
        const Vector3 v = simplex.closest;
        const float squared = dot(v, v);
        if (squared <= touching * touching)
        {
            overlapping = true;
            break;
        }

        // The support point against v; no progress (within the tolerance), or a point already in the simplex: done.
        const SupportPoint w = difference.support(-v);
        if (squared - dot(v, w.difference) <= 1e-5F * squared)
        {
            break;
        }
        bool repeated = false;
        for (std::uint32_t index = 0; index < simplex.size; ++index)
        {
            repeated = repeated || simplex.points[index].difference == w.difference;
        }
        if (repeated)
        {
            break;
        }

        // The grown simplex's closest point; a simplex that stops getting closer keeps the previous one.
        gjk_detail::Simplex grown = simplex;
        grown.points[grown.size++] = w;
        if (grown.size == 2)
        {
            gjk_detail::solve_segment(grown);
        }
        else if (grown.size == 3)
        {
            gjk_detail::solve_triangle(grown);
        }
        else if (!gjk_detail::solve_tetrahedron(grown))
        {
            simplex = grown;
            overlapping = true;
            break;
        }
        if (length_squared(grown.closest) >= squared)
        {
            break;
        }
        simplex = grown;
    }

    // The closest points from the weights, or the simplex for EPA.
    result.simplex = simplex.points;
    result.simplex_size = simplex.size;
    if (overlapping)
    {
        result.status = GjkStatus::OVERLAPPING;
        result.distance = 0.0F;
    }
    else
    {
        result.status = GjkStatus::SEPARATED;
        result.distance = length(simplex.closest);
    }
    for (std::uint32_t index = 0; index < simplex.size; ++index)
    {
        result.point_first += simplex.points[index].first * simplex.weights[index];
        result.point_second += simplex.points[index].second * simplex.weights[index];
    }
    return result;
}

} // namespace gfx::physics
