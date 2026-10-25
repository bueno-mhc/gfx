// Triangle meshes and height fields: validation, the hierarchy over a mesh's triangles, active edges (a mesh's from
// its edges sorted by their vertices; a height field's from the grid, on demand).
module gfx.physics.shapes;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.primitives;

namespace gfx::physics
{

namespace
{

/// Throws the PhysicsError of an invalid shape description.
[[noreturn]] void
reject(std::string_view operation,
       std::string problem,
       std::string_view suggestion)
{
    throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::string{suggestion}, .context = {}});
}

[[nodiscard]] auto
is_finite(Vector3 vector) noexcept -> bool
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

/// An edge of a triangle: its vertices in increasing order, the triangle and the edge's number in it.
struct Edge
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint32_t triangle = 0;
    std::uint32_t edge = 0;
};

} // namespace

// ===================================================================================================== triangle meshes

TriangleMeshShape::TriangleMeshShape(const TriangleMeshDescription& description)
    : m_vertices(description.vertices.begin(), description.vertices.end())
    , m_triangles(description.triangles.begin(), description.triangles.end())
    , m_two_sided{description.two_sided}
{
    // Something to collide with, finite, indices in range.
    constexpr std::string_view OPERATION = "TriangleMeshShape::TriangleMeshShape";
    if (m_vertices.empty() || m_triangles.empty())
    {
        reject(OPERATION, std::format("a mesh needs vertices and triangles; it has {} vertices and {} triangles", m_vertices.size(), m_triangles.size()),
               "give the vertices and the triangles' vertex indices");
    }
    if (!(description.active_edge_cosine >= -1.0F && description.active_edge_cosine <= 1.0F))
    {
        reject(OPERATION, std::format("the active edge cosine must be in [-1, 1] (it is {})", description.active_edge_cosine),
               "give the cosine of the angle below which triangles meet flat (DEFAULT_ACTIVE_EDGE_COSINE: about 5°)");
    }
    for (std::size_t index = 0; index < m_vertices.size(); ++index)
    {
        if (!is_finite(m_vertices[index]))
        {
            reject(OPERATION, std::format("vertex {} is not finite", index), "give finite coordinates");
        }
    }
    for (std::size_t index = 0; index < m_triangles.size(); ++index)
    {
        for (const std::uint32_t corner : m_triangles[index])
        {
            if (corner >= m_vertices.size())
            {
                reject(OPERATION, std::format("triangle {} uses vertex {}; the mesh has {} vertices", index, corner, m_vertices.size()),
                       "give indices of the vertices given");
            }
        }
    }

    // The hierarchy over the triangles' boxes.
    std::vector<Aabb> boxes;
    boxes.reserve(m_triangles.size());
    for (std::uint32_t index = 0; index < m_triangles.size(); ++index)
    {
        boxes.push_back(triangle(index).bounds());
    }
    m_bvh = StaticBvh{boxes};

    // Active edges: every edge of every triangle, sorted by its vertices so that shared edges are side by side.
    std::vector<Edge> edges;
    edges.reserve(m_triangles.size() * 3);
    for (std::uint32_t index = 0; index < m_triangles.size(); ++index)
    {
        for (std::uint32_t edge = 0; edge < 3; ++edge)
        {
            const std::uint32_t from = m_triangles[index][edge];
            const std::uint32_t to = m_triangles[index][(edge + 1) % 3];
            edges.push_back({.low = std::min(from, to), .high = std::max(from, to), .triangle = index, .edge = edge});
        }
    }
    std::ranges::sort(edges, [](const Edge& first, const Edge& second) {
        return first.low != second.low ? first.low < second.low : first.high != second.high ? first.high < second.high : first.triangle < second.triangle;
    });
    m_active_edges.assign(m_triangles.size(), 0x7);
    m_closed = true;
    for (std::size_t begin = 0; begin < edges.size();)
    {
        std::size_t end = begin + 1;
        while (end < edges.size() && edges[end].low == edges[begin].low && edges[end].high == edges[begin].high)
        {
            ++end;
        }
        // Two triangles: inactive when they meet flat or concave (for both). Boundary and non-manifold edges stay active.
        m_closed = m_closed && end - begin == 2;
        if (end - begin == 2)
        {
            const Edge& first = edges[begin];
            const Edge& second = edges[begin + 1];
            const TriangleShape a = triangle(first.triangle);
            const TriangleShape b = triangle(second.triangle);
            const Vector3 opposite_of_b = m_vertices[m_triangles[second.triangle][(second.edge + 2) % 3]];
            const Vector3 opposite_of_a = m_vertices[m_triangles[first.triangle][(first.edge + 2) % 3]];
            const Vector3 on_edge = m_vertices[first.low];
            if (!is_active_edge(a.normal(), on_edge, b.normal(), opposite_of_b, description.active_edge_cosine))
            {
                m_active_edges[first.triangle] = static_cast<std::uint8_t>(m_active_edges[first.triangle] & ~(1U << first.edge));
            }
            if (!is_active_edge(b.normal(), on_edge, a.normal(), opposite_of_a, description.active_edge_cosine))
            {
                m_active_edges[second.triangle] = static_cast<std::uint8_t>(m_active_edges[second.triangle] & ~(1U << second.edge));
            }
        }
        begin = end;
    }
}

// ===================================================================================================== height fields

HeightFieldShape::HeightFieldShape(const HeightFieldDescription& description)
    : m_rows{static_cast<std::uint32_t>(description.heights.extent(0))}
    , m_columns{static_cast<std::uint32_t>(description.heights.extent(1))}
    , m_scale{description.scale}
    , m_active_edge_cosine{description.active_edge_cosine}
{
    // A grid of at least one cell, finite heights, positive spacing.
    constexpr std::string_view OPERATION = "HeightFieldShape::HeightFieldShape";
    if (description.heights.extent(0) < 2 || description.heights.extent(1) < 2 || description.heights.extent(0) > 0xFFFFU || description.heights.extent(1) > 0xFFFFU)
    {
        reject(OPERATION, std::format("a height field needs 2 to 65535 rows and columns; it has {} × {}", description.heights.extent(0), description.heights.extent(1)),
               "give a grid of at least 2 × 2 heights");
    }
    if (!is_finite(m_scale) || !(m_scale.x > 0.0F && m_scale.y > 0.0F && m_scale.z > 0.0F))
    {
        reject(OPERATION, std::format("the scale must be finite and positive (it is ({}, {}, {}))", m_scale.x, m_scale.y, m_scale.z),
               "give the column spacing, the height factor and the row spacing");
    }
    if (!(description.active_edge_cosine >= -1.0F && description.active_edge_cosine <= 1.0F))
    {
        reject(OPERATION, std::format("the active edge cosine must be in [-1, 1] (it is {})", description.active_edge_cosine),
               "give the cosine of the angle below which triangles meet flat");
    }
    m_heights.reserve(static_cast<std::size_t>(m_rows) * m_columns);
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
    for (std::size_t row = 0; row < m_rows; ++row)
    {
        for (std::size_t column = 0; column < m_columns; ++column)
        {
            const float height = description.heights[row, column];
            if (!std::isfinite(height))
            {
                reject(OPERATION, std::format("the height at row {}, column {} is not finite", row, column), "give finite heights");
            }
            m_heights.push_back(height);
            lowest = std::min(lowest, height * m_scale.y);
            highest = std::max(highest, height * m_scale.y);
        }
    }
    m_bounds = {.minimum = {.y = lowest}, .maximum = {.x = static_cast<float>(m_columns - 1) * m_scale.x, .y = highest, .z = static_cast<float>(m_rows - 1) * m_scale.z}};
}

auto
HeightFieldShape::triangle(std::uint32_t index) const noexcept -> TriangleShape
{
    // Half 0: (r, c), (r + 1, c), (r + 1, c + 1); half 1: (r, c), (r + 1, c + 1), (r, c + 1) — both facing +Y.
    const std::uint32_t cell = index / 2;
    const std::uint32_t row = cell / (m_columns - 1);
    const std::uint32_t column = cell % (m_columns - 1);
    if (index % 2 == 0)
    {
        return {.first = sample(row, column), .second = sample(row + 1, column), .third = sample(row + 1, column + 1)};
    }
    return {.first = sample(row, column), .second = sample(row + 1, column + 1), .third = sample(row, column + 1)};
}

auto
HeightFieldShape::active_edges(std::uint32_t index) const noexcept -> std::uint8_t
{
    // Each edge's neighbor across it, by the vertex it has off the edge (none at the grid's border: active).
    const std::uint32_t cell = index / 2;
    const auto row = static_cast<std::int64_t>(cell / (m_columns - 1));
    const auto column = static_cast<std::int64_t>(cell % (m_columns - 1));
    const TriangleShape self = triangle(index);
    const Vector3 normal = self.normal();
    struct Neighbor
    {
        std::int64_t triangle_row;
        std::int64_t triangle_column;
        std::uint32_t half;
        std::int64_t opposite_row;
        std::int64_t opposite_column;
    };
    // Half 0's edges: (r,c)-(r+1,c) [column line, left cell's half 1], (r+1,c)-(r+1,c+1) [row line, upper cell's
    // half 1], diagonal [this cell's half 1]. Half 1's: diagonal [half 0], (r+1,c+1)-(r,c+1) [right cell's half 0],
    // (r,c+1)-(r,c) [lower cell's half 0].
    const std::array<Neighbor, 3> neighbors = index % 2 == 0
                                                  ? std::array<Neighbor, 3>{Neighbor{row, column - 1, 1, row, column - 1}, Neighbor{row + 1, column, 1, row + 2, column + 1},
                                                                            Neighbor{row, column, 1, row, column + 1}}
                                                  : std::array<Neighbor, 3>{Neighbor{row, column, 0, row + 1, column}, Neighbor{row, column + 1, 0, row + 1, column + 2},
                                                                            Neighbor{row - 1, column, 0, row - 1, column}};
    const std::array<Vector3, 3> corners{self.first, self.second, self.third};
    std::uint8_t active = 0;
    for (std::uint32_t edge = 0; edge < 3; ++edge)
    {
        const Neighbor& neighbor = neighbors[edge];
        const bool inside = neighbor.triangle_row >= 0 && neighbor.triangle_column >= 0 && neighbor.triangle_row < static_cast<std::int64_t>(m_rows - 1)
                            && neighbor.triangle_column < static_cast<std::int64_t>(m_columns - 1);
        if (!inside)
        {
            active = static_cast<std::uint8_t>(active | (1U << edge));
            continue;
        }
        const auto neighbor_index =
            static_cast<std::uint32_t>(2 * (neighbor.triangle_row * static_cast<std::int64_t>(m_columns - 1) + neighbor.triangle_column) + neighbor.half);
        const Vector3 opposite = sample(static_cast<std::uint32_t>(neighbor.opposite_row), static_cast<std::uint32_t>(neighbor.opposite_column));
        if (is_active_edge(normal, corners[edge], triangle(neighbor_index).normal(), opposite, m_active_edge_cosine))
        {
            active = static_cast<std::uint8_t>(active | (1U << edge));
        }
    }
    return active;
}

} // namespace gfx::physics
