/// @file convex_hull.cppm
/// @brief Convex hulls: vertices and the planes of the faces (normals outward: inside is behind every face).
///        ConvexHullView reads any storage; FixedConvexHull holds a hull in arrays (usable in constant expressions);
///        ConvexHull owns one built at run time and checks it. Building a hull from bare points (quickhull) belongs to
///        the shapes (phase PH2).
export module gfx.physics.primitives:convex_hull;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import :plane;
import :aabb;

export namespace gfx::physics
{

/// @brief A convex hull seen through spans: its vertices and its faces' planes (normals outward).
struct ConvexHullView
{
    std::span<const Vector3> vertices{}; ///< The corners.
    std::span<const Plane> faces{};      ///< The planes of the faces, normals pointing out.

    /// @brief The vertex farthest along @p direction (the first of equals; the origin for an empty hull).
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        if (vertices.empty())
        {
            return {};
        }
        Vector3 best = vertices.front();
        float best_distance = dot(best, direction);
        for (const Vector3 vertex : vertices.subspan(1))
        {
            const float along = dot(vertex, direction);
            if (along > best_distance)
            {
                best = vertex;
                best_distance = along;
            }
        }
        return best;
    }

    /// @brief No rounding: 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0;
    }

    /// @brief The bounding box of the vertices.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_points(vertices);
    }

    /// @brief Whether @p point is inside or on the boundary (behind every face, within GEOMETRIC_TOLERANCE).
    [[nodiscard]] constexpr auto contains(Vector3 point) const noexcept -> bool
    {
        return std::ranges::all_of(faces, [point](const Plane& face) { return face.signed_distance(point) <= GEOMETRIC_TOLERANCE; });
    }
};

/// @brief A convex hull of @p VertexCount vertices and @p FaceCount faces in arrays: a literal type, so hulls known
///        at compile time are built, checked and queried by the compiler.
template<std::size_t VertexCount, std::size_t FaceCount>
struct FixedConvexHull
{
    std::array<Vector3, VertexCount> vertices{}; ///< The corners.
    std::array<Plane, FaceCount> faces{};        ///< The planes of the faces, normals pointing out.

    /// @brief The hull seen through spans.
    [[nodiscard]] constexpr auto view() const noexcept -> ConvexHullView
    {
        return {.vertices = vertices, .faces = faces};
    }

    /// @brief The vertex count.
    [[nodiscard]] static constexpr auto vertex_count() noexcept -> std::size_t
    {
        return VertexCount;
    }

    /// @brief The face count.
    [[nodiscard]] static constexpr auto face_count() noexcept -> std::size_t
    {
        return FaceCount;
    }

    /// @brief The vertex farthest along @p direction.
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        return view().support(direction);
    }

    /// @brief No rounding: 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0;
    }

    /// @brief The bounding box of the vertices.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return view().bounds();
    }

    /// @brief Whether @p point is inside or on the boundary.
    [[nodiscard]] constexpr auto contains(Vector3 point) const noexcept -> bool
    {
        return view().contains(point);
    }
};

/// @brief The box of half extents @p half_size about the origin, as a hull: corners as Aabb::corner() numbers them,
///        faces -X, +X, -Y, +Y, -Z, +Z.
[[nodiscard]] constexpr auto
box_hull(Vector3 half_size) noexcept -> FixedConvexHull<8, 6>
{
    FixedConvexHull<8, 6> hull{};
    const Aabb box = Aabb::from_center({}, half_size);
    for (std::size_t index = 0; index < 8; ++index)
    {
        hull.vertices[index] = box.corner(index);
    }
    hull.faces = {Plane{.normal = {.x = -1}, .distance = half_size.x}, Plane{.normal = {.x = 1}, .distance = half_size.x},
                  Plane{.normal = {.y = -1}, .distance = half_size.y}, Plane{.normal = {.y = 1}, .distance = half_size.y},
                  Plane{.normal = {.z = -1}, .distance = half_size.z}, Plane{.normal = {.z = 1}, .distance = half_size.z}};
    return hull;
}

/// @brief A convex hull owning its vertices and faces, checked when made.
class ConvexHull
{
public:
    /// @brief The hull of @p vertices and @p faces (copied).
    /// @throws PhysicsError (GEOMETRY) when there is no vertex or no face, a face's normal is not of unit length, or a
    ///         vertex is in front of a face by more than the tolerance (relative to the hull's size): the planes do not
    ///         bound the vertices.
    ConvexHull(std::span<const Vector3> vertices,
               std::span<const Plane> faces);

    /// @brief A copy of a hull held in arrays.
    template<std::size_t VertexCount, std::size_t FaceCount>
    explicit ConvexHull(const FixedConvexHull<VertexCount, FaceCount>& hull)
        : ConvexHull(hull.vertices, hull.faces)
    {
    }

    /// @brief The hull seen through spans.
    [[nodiscard]] auto view() const noexcept -> ConvexHullView
    {
        return {.vertices = m_vertices, .faces = m_faces};
    }

    /// @brief The vertex farthest along @p direction.
    [[nodiscard]] auto support(Vector3 direction) const noexcept -> Vector3
    {
        return view().support(direction);
    }

    /// @brief No rounding: 0.
    [[nodiscard]] auto margin() const noexcept -> float
    {
        return 0;
    }

    /// @brief The bounding box (computed once).
    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_bounds;
    }

    /// @brief Whether @p point is inside or on the boundary.
    [[nodiscard]] auto contains(Vector3 point) const noexcept -> bool
    {
        return view().contains(point);
    }

private:
    std::vector<Vector3> m_vertices{};
    std::vector<Plane> m_faces{};
    Aabb m_bounds{};
};

} // namespace gfx::physics
