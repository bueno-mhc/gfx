/// @file epa.cppm
/// @brief EPA (expanding polytope algorithm): from GJK's final simplex around the origin, grows a polytope inside the
///        Minkowski difference of two overlapping cores until its face nearest the origin is on the difference's
///        boundary — the penetration depth, the normal (from the first shape to the second) and the deepest points of
///        each core. Fixed storage (MAXIMUM_EPA_VERTICES, MAXIMUM_EPA_FACES), constexpr.
export module gfx.physics.narrow_phase:epa;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import :gjk;

export namespace gfx::physics
{

/// @brief What EPA found.
struct EpaResult
{
    EpaStatus status = EpaStatus::CONVERGED; ///< How it ended.
    Vector3 normal{.y = 1.0F};               ///< Moving the second shape by depth along it separates the cores.
    float depth = 0.0F;                      ///< The penetration of the cores, at least 0.
    Vector3 point_first{};                   ///< The first core's deepest point (world space).
    Vector3 point_second{};                  ///< The second core's.
    std::uint32_t iterations = 0;            ///< Support points asked for.
};

namespace epa_detail
{

/// A face of the polytope: counter-clockwise seen from outside, its outward normal and distance from the origin.
struct Face
{
    std::array<std::uint16_t, 3> vertex{};
    Vector3 normal{};
    float distance = 0.0F;
    bool alive = false;
};

/// The polytope.
struct Polytope
{
    std::array<SupportPoint, MAXIMUM_EPA_VERTICES> vertices{};
    std::array<Face, MAXIMUM_EPA_FACES> faces{};
    std::uint32_t vertex_count = 0;
    std::uint32_t face_count = 0;

    /// Adds the face (a, b, c); false when the storage is full.
    constexpr auto add_face(std::uint16_t a,
                            std::uint16_t b,
                            std::uint16_t c) noexcept -> bool
    {
        // Reuse a dead slot before growing.
        std::uint32_t slot = face_count;
        for (std::uint32_t index = 0; index < face_count; ++index)
        {
            if (!faces[index].alive)
            {
                slot = index;
                break;
            }
        }
        if (slot >= MAXIMUM_EPA_FACES)
        {
            return false;
        }
        const Vector3 pa = vertices[a].difference;
        const Vector3 normal = normalized(cross(vertices[b].difference - pa, vertices[c].difference - pa));
        faces[slot] = {.vertex = {a, b, c}, .normal = normal, .distance = dot(normal, pa), .alive = length_squared(normal) > 0.0F};
        face_count = std::max(face_count, slot + 1);
        return true;
    }

    /// The live face nearest the origin.
    [[nodiscard]] constexpr auto nearest() const noexcept -> std::uint32_t
    {
        std::uint32_t best = MAXIMUM_EPA_FACES;
        for (std::uint32_t index = 0; index < face_count; ++index)
        {
            if (faces[index].alive && (best == MAXIMUM_EPA_FACES || faces[index].distance < faces[best].distance))
            {
                best = index;
            }
        }
        return best;
    }
};

/// The barycentric coordinates of @p point in the triangle (a, b, c).
[[nodiscard]] constexpr auto
barycentric(Vector3 point,
            Vector3 a,
            Vector3 b,
            Vector3 c) noexcept -> std::array<float, 3>
{
    const Vector3 v0 = b - a;
    const Vector3 v1 = c - a;
    const Vector3 v2 = point - a;
    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float d20 = dot(v2, v0);
    const float d21 = dot(v2, v1);
    const float denominator = d00 * d11 - d01 * d01;
    if (!(std::abs(denominator) > 0.0F))
    {
        return {1.0F, 0.0F, 0.0F};
    }
    const float v = (d11 * d20 - d01 * d21) / denominator;
    const float w = (d00 * d21 - d01 * d20) / denominator;
    return {1.0F - v - w, v, w};
}

} // namespace epa_detail

/// @brief EPA for the cores of @p first and @p second, which GJK (@p start) found overlapping.
template<ConvexShape First, ConvexShape Second>
[[nodiscard]] constexpr auto
epa(const First& first,
    const Transform& first_pose,
    const Second& second,
    const Transform& second_pose,
    const GjkResult& start) noexcept -> EpaResult
{
    const MinkowskiDifference<First, Second> difference{.first = first, .first_pose = first_pose, .second = second, .second_pose = second_pose};
    epa_detail::Polytope polytope{};
    EpaResult result{};

    // A tetrahedron from the simplex: points added along directions that give volume.
    std::array<SupportPoint, 4> tetrahedron{};
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < start.simplex_size; ++index)
    {
        tetrahedron[count++] = start.simplex[index];
    }
    const auto add_distinct = [&](Vector3 direction) {
        const SupportPoint point = difference.support(direction);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (distance_squared(tetrahedron[index].difference, point.difference) <= 1e-12F)
            {
                return false;
            }
        }
        tetrahedron[count++] = point;
        return true;
    };
    constexpr std::array<Vector3, 6> AXES{Vector3{.x = 1}, Vector3{.x = -1}, Vector3{.y = 1}, Vector3{.y = -1}, Vector3{.z = 1}, Vector3{.z = -1}};
    if (count == 0)
    {
        add_distinct(AXES[0]);
    }
    for (std::size_t axis = 0; count == 1 && axis < AXES.size(); ++axis)
    {
        add_distinct(AXES[axis]);
    }
    if (count == 2)
    {
        const Vector3 line = tetrahedron[1].difference - tetrahedron[0].difference;
        const Vector3 across = any_perpendicular(line);
        const std::array<Vector3, 4> around{across, -across, cross(line, across), -cross(line, across)};
        for (std::size_t index = 0; count == 2 && index < around.size(); ++index)
        {
            const SupportPoint point = difference.support(around[index]);
            if (length_squared(cross(point.difference - tetrahedron[0].difference, line)) > 1e-12F * length_squared(line))
            {
                tetrahedron[count++] = point;
            }
        }
    }
    if (count == 3)
    {
        const Vector3 normal = cross(tetrahedron[1].difference - tetrahedron[0].difference, tetrahedron[2].difference - tetrahedron[0].difference);
        const SupportPoint up = difference.support(normal);
        const SupportPoint down = difference.support(-normal);
        const float up_height = std::abs(dot(up.difference - tetrahedron[0].difference, normal));
        const float down_height = std::abs(dot(down.difference - tetrahedron[0].difference, normal));
        if (std::max(up_height, down_height) > 1e-9F * length(normal))
        {
            tetrahedron[count++] = up_height >= down_height ? up : down;
        }
        else
        {
            // Flat: the cores only touch.
            result.status = EpaStatus::DEGENERATE;
            result.normal = normalized(normal);
            result.point_first = start.point_first;
            result.point_second = start.point_second;
            return result;
        }
    }
    if (count < 4)
    {
        result.status = EpaStatus::DEGENERATE;
        result.point_first = start.point_first;
        result.point_second = start.point_second;
        return result;
    }

    // The tetrahedron's faces, wound to face away from its fourth vertex.
    for (const SupportPoint& point : tetrahedron)
    {
        polytope.vertices[polytope.vertex_count++] = point;
    }
    const bool flip = dot(cross(tetrahedron[1].difference - tetrahedron[0].difference, tetrahedron[2].difference - tetrahedron[0].difference),
                          tetrahedron[3].difference - tetrahedron[0].difference)
                      > 0.0F;
    const auto b = static_cast<std::uint16_t>(flip ? 2 : 1);
    const auto c = static_cast<std::uint16_t>(flip ? 1 : 2);
    polytope.add_face(0, b, c);
    polytope.add_face(0, 3, b);
    polytope.add_face(b, 3, c);
    polytope.add_face(c, 3, 0);

    // Expand: the nearest face's support point; stop when it adds (almost) nothing.
    std::array<std::pair<std::uint16_t, std::uint16_t>, MAXIMUM_EPA_FACES * 3> edges{};
    std::uint32_t nearest = polytope.nearest();
    result.status = EpaStatus::ITERATION_LIMIT;
    for (result.iterations = 0; result.iterations < MAXIMUM_EPA_VERTICES && nearest < MAXIMUM_EPA_FACES; ++result.iterations)
    {
        const epa_detail::Face face = polytope.faces[nearest];
        const SupportPoint point = difference.support(face.normal);
        const float reach = dot(point.difference, face.normal);
        if (reach - face.distance <= std::max(1e-4F * std::abs(reach), 1e-6F))
        {
            result.status = EpaStatus::CONVERGED;
            break;
        }
        if (polytope.vertex_count >= MAXIMUM_EPA_VERTICES)
        {
            result.status = EpaStatus::CAPACITY_REACHED;
            break;
        }
        const auto added = static_cast<std::uint16_t>(polytope.vertex_count);
        polytope.vertices[polytope.vertex_count++] = point;

        // The faces the new point sees go; their edges seen once form the horizon.
        std::uint32_t edge_count = 0;
        for (std::uint32_t index = 0; index < polytope.face_count; ++index)
        {
            epa_detail::Face& visible = polytope.faces[index];
            if (!visible.alive || dot(visible.normal, point.difference - polytope.vertices[visible.vertex[0]].difference) <= 0.0F)
            {
                continue;
            }
            visible.alive = false;
            for (std::uint32_t corner = 0; corner < 3; ++corner)
            {
                const std::uint16_t from = visible.vertex[corner];
                const std::uint16_t to = visible.vertex[(corner + 1) % 3];
                bool shared = false;
                for (std::uint32_t other = 0; other < edge_count; ++other)
                {
                    if (edges[other].first == to && edges[other].second == from)
                    {
                        edges[other] = edges[--edge_count];
                        shared = true;
                        break;
                    }
                }
                if (!shared && edge_count < edges.size())
                {
                    edges[edge_count++] = {from, to};
                }
            }
        }
        bool stored = true;
        for (std::uint32_t index = 0; index < edge_count && stored; ++index)
        {
            stored = polytope.add_face(edges[index].first, edges[index].second, added);
        }
        nearest = polytope.nearest();
        if (!stored)
        {
            result.status = EpaStatus::CAPACITY_REACHED;
            break;
        }
    }
    if (nearest >= MAXIMUM_EPA_FACES)
    {
        result.status = EpaStatus::DEGENERATE;
        result.point_first = start.point_first;
        result.point_second = start.point_second;
        return result;
    }

    // The nearest face's point closest to the origin, in barycentric coordinates: the cores' deepest points.
    const epa_detail::Face face = polytope.faces[nearest];
    const SupportPoint& a = polytope.vertices[face.vertex[0]];
    const SupportPoint& b_point = polytope.vertices[face.vertex[1]];
    const SupportPoint& c_point = polytope.vertices[face.vertex[2]];
    const std::array<float, 3> weights = epa_detail::barycentric(face.normal * face.distance, a.difference, b_point.difference, c_point.difference);
    result.normal = face.normal;
    result.depth = std::max(face.distance, 0.0F);
    result.point_first = a.first * weights[0] + b_point.first * weights[1] + c_point.first * weights[2];
    result.point_second = a.second * weights[0] + b_point.second * weights[1] + c_point.second * weights[2];
    return result;
}

} // namespace gfx::physics
