module gfx.physics.primitives;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;

namespace gfx::physics
{

namespace
{

/// Throws the PhysicsError of an invalid hull.
[[noreturn]] void
reject_hull(std::string problem)
{
    throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                        .operation = "ConvexHull::ConvexHull",
                        .problem = std::move(problem),
                        .suggestion = "give the hull's vertices and the planes of its faces with unit normals pointing out (box_hull() makes a box's)",
                        .context = {}});
}

} // namespace

ConvexHull::ConvexHull(std::span<const Vector3> vertices,
                       std::span<const Plane> faces)
    : m_vertices(vertices.begin(), vertices.end())
    , m_faces(faces.begin(), faces.end())
    , m_bounds{Aabb::from_points(vertices)}
{
    // Something to hold, faces with unit normals.
    if (m_vertices.empty() || m_faces.empty())
    {
        reject_hull(std::format("a hull needs vertices and faces; it has {} vertices and {} faces", m_vertices.size(), m_faces.size()));
    }
    for (std::size_t index = 0; index < m_faces.size(); ++index)
    {
        const float size = length(m_faces[index].normal);
        if (std::abs(size - 1.0F) > 1e-4F)
        {
            reject_hull(std::format("the normal of face {} has a length of {}, not 1", index, size));
        }
    }

    // Every vertex behind every face, within a tolerance that grows with the hull's size.
    const float tolerance = GEOMETRIC_TOLERANCE * std::max(1.0F, length(m_bounds.extents())) * 16.0F;
    for (std::size_t face = 0; face < m_faces.size(); ++face)
    {
        for (std::size_t vertex = 0; vertex < m_vertices.size(); ++vertex)
        {
            const float height = m_faces[face].signed_distance(m_vertices[vertex]);
            if (height > tolerance)
            {
                reject_hull(std::format("vertex {} is {} in front of face {}: the faces do not bound the vertices", vertex, height, face));
            }
        }
    }
}

} // namespace gfx::physics
