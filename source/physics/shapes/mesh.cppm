/// @file mesh.cppm
/// @brief Triangle meshes and height fields: shapes made of triangles (counter-clockwise around their front normal),
///        for static geometry. Each triangle's edges are marked *active* or not: an edge between two triangles that
///        meet flat (within active_edge_cosine) or folded inward (concave) is inactive, so that a contact the narrow
///        phase finds on it takes the triangle's normal — a box sliding over a flat mesh does not catch on the edges
///        between its triangles. Boundary edges and convex folds are active.
export module gfx.physics.shapes:mesh;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import :convex;
import :static_bvh;

export namespace gfx::physics
{

/// @brief What a triangle mesh is made of (copied by the shape).
struct TriangleMeshDescription
{
    std::span<const Vector3> vertices{};                       ///< The vertices.
    std::span<const std::array<std::uint32_t, 3>> triangles{}; ///< Vertex indices, counter-clockwise around the front.
    bool two_sided = false;                                    ///< Whether contacts are made from behind too.
    float active_edge_cosine = DEFAULT_ACTIVE_EDGE_COSINE;     ///< Edges between triangles whose normals' cosine is above it are inactive.
};

/// @brief A triangle mesh with a bounding volume hierarchy over its triangles.
class TriangleMeshShape
{
public:
    /// @brief The mesh of @p description.
    /// @throws PhysicsError if it has no vertex or triangle, a vertex that is not finite, an index out of range, or an
    ///         active-edge cosine outside [-1, 1].
    explicit TriangleMeshShape(const TriangleMeshDescription& description);

    [[nodiscard]] auto vertices() const noexcept -> std::span<const Vector3>
    {
        return m_vertices;
    }

    [[nodiscard]] auto triangles() const noexcept -> std::span<const std::array<std::uint32_t, 3>>
    {
        return m_triangles;
    }

    [[nodiscard]] auto triangle_count() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_triangles.size());
    }

    /// @brief Triangle @p index.
    [[nodiscard]] auto triangle(std::uint32_t index) const noexcept -> TriangleShape
    {
        const std::array<std::uint32_t, 3>& corners = m_triangles[index];
        return {.first = m_vertices[corners[0]], .second = m_vertices[corners[1]], .third = m_vertices[corners[2]]};
    }

    /// @brief Bit e set: edge e (from corner e to corner e + 1) of triangle @p index is active.
    [[nodiscard]] auto active_edges(std::uint32_t index) const noexcept -> std::uint8_t
    {
        return m_active_edges[index];
    }

    /// @brief Whether contacts are made from behind the triangles too.
    [[nodiscard]] auto two_sided() const noexcept -> bool
    {
        return m_two_sided;
    }

    /// @brief Whether every edge has exactly two triangles (the mesh bounds a volume: it has mass properties).
    [[nodiscard]] auto closed() const noexcept -> bool
    {
        return m_closed;
    }

    /// @brief The hierarchy over the triangles.
    [[nodiscard]] auto bvh() const noexcept -> const StaticBvh&
    {
        return m_bvh;
    }

    /// @brief The box around the mesh.
    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_bvh.bounds();
    }

    /// @brief Calls `visitor(index, triangle)` for each triangle (of non-zero area) whose box overlaps @p box.
    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        m_bvh.query(box, [this, &visitor](std::uint32_t index) {
            const TriangleShape shape = triangle(index);
            if (length_squared(cross(shape.second - shape.first, shape.third - shape.first)) > 0.0F)
            {
                visitor(index, shape);
            }
        });
    }

    [[nodiscard]] auto operator==(const TriangleMeshShape& other) const noexcept -> bool
    {
        return m_vertices == other.m_vertices && m_triangles == other.m_triangles && m_two_sided == other.m_two_sided;
    }

private:
    std::vector<Vector3> m_vertices{};
    std::vector<std::array<std::uint32_t, 3>> m_triangles{};
    std::vector<std::uint8_t> m_active_edges{};
    StaticBvh m_bvh{};
    bool m_two_sided = false;
    bool m_closed = false;
};

/// @brief What a height field is made of (copied by the shape).
struct HeightFieldDescription
{
    std::mdspan<const float, std::dextents<std::size_t, 2>> heights{}; ///< heights[row][column]: rows along +Z, columns along +X; at least 2 × 2.
    Vector3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};                     ///< Column spacing (x), height factor (y), row spacing (z), each greater than 0.
    float active_edge_cosine = DEFAULT_ACTIVE_EDGE_COSINE;              ///< As TriangleMeshDescription::active_edge_cosine.
};

/// @brief A grid of heights: sample (row, column) at (column · scale.x, height · scale.y, row · scale.z); each cell is two
///        triangles split along the diagonal from (row, column) to (row + 1, column + 1), facing +Y: triangle
///        2 (row (columns - 1) + column) + half, half 0 = (r, c), (r + 1, c), (r + 1, c + 1) and half 1 =
///        (r, c), (r + 1, c + 1), (r, c + 1).
class HeightFieldShape
{
public:
    /// @brief The height field of @p description.
    /// @throws PhysicsError if it has fewer than 2 rows or columns, a height that is not finite, a scale that is not
    ///         positive, or an active-edge cosine outside [-1, 1].
    explicit HeightFieldShape(const HeightFieldDescription& description);

    [[nodiscard]] auto rows() const noexcept -> std::uint32_t
    {
        return m_rows;
    }

    [[nodiscard]] auto columns() const noexcept -> std::uint32_t
    {
        return m_columns;
    }

    [[nodiscard]] auto scale() const noexcept -> Vector3
    {
        return m_scale;
    }

    /// @brief The heights as given (before scale.y).
    [[nodiscard]] auto heights() const noexcept -> std::mdspan<const float, std::dextents<std::size_t, 2>>
    {
        return std::mdspan<const float, std::dextents<std::size_t, 2>>{m_heights.data(), m_rows, m_columns};
    }

    [[nodiscard]] auto triangle_count() const noexcept -> std::uint32_t
    {
        return 2 * (m_rows - 1) * (m_columns - 1);
    }

    /// @brief Triangle @p index.
    [[nodiscard]] auto triangle(std::uint32_t index) const noexcept -> TriangleShape;

    /// @brief Bit e set: edge e of triangle @p index is active (computed from its neighbors).
    [[nodiscard]] auto active_edges(std::uint32_t index) const noexcept -> std::uint8_t;

    /// @brief The box around the field.
    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_bounds;
    }

    /// @brief Calls `visitor(index, triangle)` for each triangle whose box overlaps @p box.
    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        // The cells under the box's x and z range, clamped to the grid.
        if (!intersects(box, m_bounds))
        {
            return;
        }
        const auto cell_range = [](float low, float high, float spacing, std::uint32_t cells) {
            const float first = std::floor(low / spacing);
            const float last = std::floor(high / spacing);
            const auto clamp = [cells](float value) { return static_cast<std::uint32_t>(std::clamp(value, 0.0F, static_cast<float>(cells - 1))); };
            return std::pair{clamp(first), clamp(last)};
        };
        const auto [first_column, last_column] = cell_range(box.minimum.x, box.maximum.x, m_scale.x, m_columns - 1);
        const auto [first_row, last_row] = cell_range(box.minimum.z, box.maximum.z, m_scale.z, m_rows - 1);
        for (std::uint32_t row = first_row; row <= last_row; ++row)
        {
            for (std::uint32_t column = first_column; column <= last_column; ++column)
            {
                for (std::uint32_t half = 0; half < 2; ++half)
                {
                    const std::uint32_t index = 2 * (row * (m_columns - 1) + column) + half;
                    const TriangleShape shape = triangle(index);
                    if (intersects(shape.bounds(), box))
                    {
                        visitor(index, shape);
                    }
                }
            }
        }
    }

    [[nodiscard]] auto operator==(const HeightFieldShape& other) const noexcept -> bool
    {
        return m_heights == other.m_heights && m_rows == other.m_rows && m_columns == other.m_columns && m_scale == other.m_scale;
    }

private:
    /// The scaled sample (@p row, @p column); rows and columns outside the grid are not asked for.
    [[nodiscard]] auto sample(std::uint32_t row,
                              std::uint32_t column) const noexcept -> Vector3
    {
        return {.x = static_cast<float>(column) * m_scale.x, .y = m_heights[row * m_columns + column] * m_scale.y, .z = static_cast<float>(row) * m_scale.z};
    }

private:
    std::vector<float> m_heights{};
    std::uint32_t m_rows = 0;
    std::uint32_t m_columns = 0;
    Vector3 m_scale{};
    float m_active_edge_cosine = DEFAULT_ACTIVE_EDGE_COSINE;
    Aabb m_bounds{};
};

/// @brief Whether the edge shared by the triangle of normal @p normal and a neighbor whose vertex off the edge is
///        @p opposite (any vertex of the edge: @p on_edge) is active: a convex fold sharper than @p flat_cosine.
[[nodiscard]] constexpr auto
is_active_edge(Vector3 normal,
               Vector3 on_edge,
               Vector3 neighbor_normal,
               Vector3 opposite,
               float flat_cosine) noexcept -> bool
{
    // Flat within the cosine: inactive; folded down (the neighbor's far vertex below this plane): convex, active.
    if (dot(normal, neighbor_normal) > flat_cosine)
    {
        return false;
    }
    return dot(normal, opposite - on_edge) < 0.0F;
}

} // namespace gfx::physics
