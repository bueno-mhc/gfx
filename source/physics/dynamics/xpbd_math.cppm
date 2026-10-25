// The constraint functions the soft bodies' units share (internal to gfx.physics.dynamics): a tetrahedron's volume, the
// dihedral angle between two triangles and its gradients.
module gfx.physics.dynamics:xpbd_math;

import std;
import gfx.core.math;

namespace gfx::physics::xpbd
{

/// Below it, a length or an area counts as zero.
inline constexpr float TINY = 1.0e-9F;

/// Six times the signed volume of the tetrahedron @p a, @p b, @p c, @p d.
[[nodiscard]] inline auto
six_volume(Vector3 a,
           Vector3 b,
           Vector3 c,
           Vector3 d) noexcept -> float
{
    return dot(cross(b - a, c - a), d - a);
}

/// The angle between the triangles (a, b, c) and (b, a, d), which share the edge a–b, and its gradients.
struct Dihedral
{
    bool valid = false;                 ///< False when the edge or a triangle is degenerate.
    float angle = 0.0F;                 ///< In (−π, π]; 0 when flat.
    std::array<Vector3, 4> gradients{}; ///< With respect to a, b, c, d.
};

/// The dihedral angle at the edge @p a – @p b between the triangles of the wings @p c and @p d (Bridson et al. 2003),
/// with its gradients.
[[nodiscard]] inline auto
dihedral(Vector3 a,
         Vector3 b,
         Vector3 c,
         Vector3 d) noexcept -> Dihedral
{
    // The edge and the two (unnormalized) triangle normals.
    const Vector3 edge = b - a;
    const float edge_length = length(edge);
    const Vector3 first_normal = cross(c - a, c - b);
    const Vector3 second_normal = cross(d - b, d - a);
    const float first_squared = length_squared(first_normal);
    const float second_squared = length_squared(second_normal);
    if (!(edge_length > TINY) || !(first_squared > TINY * TINY) || !(second_squared > TINY * TINY))
    {
        return {};
    }

    // The angle between the normals, signed about the edge.
    const Vector3 first_unit = first_normal / square_root(first_squared);
    const Vector3 second_unit = second_normal / square_root(second_squared);
    const float angle = std::atan2(dot(cross(first_unit, second_unit), edge / edge_length), dot(first_unit, second_unit));

    // Bridson's bending modes are the angle's gradients, negated.
    const Vector3 first_mode = first_normal / first_squared;
    const Vector3 second_mode = second_normal / second_squared;
    const Vector3 wing_c = first_mode * edge_length;
    const Vector3 wing_d = second_mode * edge_length;
    const Vector3 edge_a = first_mode * (dot(c - b, edge) / edge_length) + second_mode * (dot(d - b, edge) / edge_length);
    const Vector3 edge_b = -(first_mode * (dot(c - a, edge) / edge_length) + second_mode * (dot(d - a, edge) / edge_length));
    return {.valid = true, .angle = angle, .gradients = {-edge_a, -edge_b, -wing_c, -wing_d}};
}

/// @p angle in (−π, π].
[[nodiscard]] inline auto
wrapped_angle(float angle) noexcept -> float
{
    constexpr float PI = std::numbers::pi_v<float>;
    while (angle > PI)
    {
        angle -= 2.0F * PI;
    }
    while (angle <= -PI)
    {
        angle += 2.0F * PI;
    }
    return angle;
}

} // namespace gfx::physics::xpbd
