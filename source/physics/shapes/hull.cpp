// HullShape: hulls built at run time by quickhull (the constexpr algorithm of :hull), copies of fixed hulls, and the
// support function on SIMD for hulls of many vertices.
module;
#include "shape_kernels.hpp"
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

/// Vertices from which the support function takes the SIMD kernel.
constexpr std::size_t SIMD_VERTICES = 16;

static_assert(sizeof(Vector3) == 3 * sizeof(float), "the support kernel reads vertices as packed floats");

} // namespace

auto
HullShape::from_points(std::span<const Vector3> points,
                       float radius) -> HullShape
{
    if (!std::isfinite(radius) || !(radius >= 0.0F))
    {
        throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                            .operation = "HullShape::from_points",
                            .problem = std::format("the rounding radius must be finite and at least 0 (it is {})", radius),
                            .suggestion = "give 0 for sharp edges",
                            .context = {}});
    }
    hull_detail::HullBuild build = hull_detail::quickhull(points);
    if (!build.problem.empty())
    {
        throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                            .operation = "HullShape::from_points",
                            .problem = std::format("{} ({} points given)", build.problem, points.size()),
                            .suggestion = "give at least 4 finite points that are not all in one plane",
                            .context = {}});
    }
    return HullShape{std::move(build.vertices), std::move(build.planes), std::move(build.faces), std::move(build.face_vertices), radius};
}

HullShape::HullShape(const HullView& hull)
    : HullShape(std::vector<Vector3>(hull.vertices.begin(), hull.vertices.end()), std::vector<Plane>(hull.planes.begin(), hull.planes.end()),
                std::vector<HullFace>(hull.faces.begin(), hull.faces.end()), std::vector<std::uint16_t>(hull.face_vertices.begin(), hull.face_vertices.end()),
                hull.radius)
{
}

HullShape::HullShape(std::vector<Vector3> vertices,
                     std::vector<Plane> planes,
                     std::vector<HullFace> faces,
                     std::vector<std::uint16_t> face_vertices,
                     float radius)
    : m_vertices{std::move(vertices)}
    , m_planes{std::move(planes)}
    , m_faces{std::move(faces)}
    , m_face_vertices{std::move(face_vertices)}
    , m_radius{radius}
    , m_bounds{Aabb::from_points(m_vertices).expanded(radius)}
{
}

auto
HullShape::support(Vector3 direction) const noexcept -> Vector3
{
    if (m_vertices.size() < SIMD_VERTICES)
    {
        return view().support(direction);
    }
    return m_vertices[shape_kernels::support_index(&m_vertices.data()->x, m_vertices.size(), direction.x, direction.y, direction.z)];
}

} // namespace gfx::physics
