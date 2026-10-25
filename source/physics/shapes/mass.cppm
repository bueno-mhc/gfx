/// @file mass.cppm
/// @brief Mass properties: mass, center of mass and inertia tensor (about the center of mass, in the shape's frame) of
///        each convex shape at a density, their transforms and sums (the parallel axis theorem), and the principal axes
///        of an inertia tensor. Closed-form shapes use their formulas; hulls (and closed meshes) the covariance method
///        (a tetrahedron from a reference point to each triangle of the boundary). Rounding radii are not counted: the
///        mass is the core's.
export module gfx.physics.shapes:mass;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import :convex;
import :hull;

export namespace gfx::physics
{

/// @brief Mass, center of mass and inertia about the center of mass.
struct MassProperties
{
    float mass = 0.0F;                                                       ///< At least 0.
    Vector3 center_of_mass{};                                                ///< In the shape's frame.
    Matrix3 inertia{.columns = {Vector3{}, Vector3{}, Vector3{}}};           ///< About the center of mass, in the shape's frame.
};

/// @brief The principal moments of an inertia tensor and the rotation of its principal axes (columns of the
///        rotation: the axes of the moments, in order).
struct PrincipalAxes
{
    Vector3 moments{};        ///< The diagonal of the tensor in the principal frame.
    Quaternion orientation{}; ///< The principal frame (a rotation: right-handed).
};

namespace mass_detail
{

inline constexpr float PI = std::numbers::pi_v<float>;

/// The tensor with diagonal (@p x, @p y, @p z).
[[nodiscard]] constexpr auto
diagonal(float x,
         float y,
         float z) noexcept -> Matrix3
{
    return Matrix3::from_diagonal({.x = x, .y = y, .z = z});
}

/// The sum of the diagonal of @p matrix.
[[nodiscard]] constexpr auto
trace(const Matrix3& matrix) noexcept -> float
{
    return matrix.columns[0].x + matrix.columns[1].y + matrix.columns[2].z;
}

/// The inertia of a point mass @p mass at @p offset about the origin: m (|d|² E - d dᵀ).
[[nodiscard]] constexpr auto
point_inertia(float mass,
              Vector3 offset) noexcept -> Matrix3
{
    return (Matrix3::from_diagonal({.x = 1, .y = 1, .z = 1}) * length_squared(offset) - Matrix3::outer_product(offset, offset)) * mass;
}

} // namespace mass_detail

/// @brief A sphere's: 4/3 π r³ ρ, inertia 2/5 m r².
[[nodiscard]] constexpr auto
mass_properties(const SphereShape& sphere,
                float density) noexcept -> MassProperties
{
    const float r = sphere.radius;
    const float mass = density * 4.0F / 3.0F * mass_detail::PI * r * r * r;
    const float moment = 0.4F * mass * r * r;
    return {.mass = mass, .inertia = mass_detail::diagonal(moment, moment, moment)};
}

/// @brief A box's (its core: the rounding is not counted): 8 hx hy hz ρ, inertia m/3 (hy² + hz², …).
[[nodiscard]] constexpr auto
mass_properties(const BoxShape& box,
                float density) noexcept -> MassProperties
{
    const Vector3 h = box.half_extents;
    const float mass = density * 8.0F * h.x * h.y * h.z;
    return {.mass = mass,
            .inertia = mass_detail::diagonal(mass / 3.0F * (h.y * h.y + h.z * h.z), mass / 3.0F * (h.x * h.x + h.z * h.z), mass / 3.0F * (h.x * h.x + h.y * h.y))};
}

/// @brief A capsule's: a cylinder of height 2h and two hemispheres (their centers of mass 3r/8 from the caps).
[[nodiscard]] constexpr auto
mass_properties(const CapsuleShape& capsule,
                float density) noexcept -> MassProperties
{
    const float r = capsule.radius;
    const float height = 2.0F * capsule.half_height;
    const float cylinder = density * mass_detail::PI * r * r * height;
    const float spheres = density * 4.0F / 3.0F * mass_detail::PI * r * r * r;
    const float axial = cylinder * r * r * 0.5F + spheres * r * r * 0.4F;
    const float across = cylinder * (height * height / 12.0F + r * r / 4.0F) + spheres * (0.4F * r * r + height * height / 4.0F + 3.0F * height * r / 8.0F);
    return {.mass = cylinder + spheres, .inertia = mass_detail::diagonal(across, axial, across)};
}

/// @brief A cylinder's: π r² 2h ρ, inertia m r²/2 about its axis, m (3r² + (2h)²)/12 across.
[[nodiscard]] constexpr auto
mass_properties(const CylinderShape& cylinder,
                float density) noexcept -> MassProperties
{
    const float r = cylinder.radius;
    const float height = 2.0F * cylinder.half_height;
    const float mass = density * mass_detail::PI * r * r * height;
    const float across = mass * (3.0F * r * r + height * height) / 12.0F;
    return {.mass = mass, .inertia = mass_detail::diagonal(across, mass * r * r * 0.5F, across)};
}

/// @brief A cone's: π r² H ρ / 3 (H = 2h), center of mass H/4 above the base, inertia 3/10 m r² about its axis and
///        m (3/20 r² + 3/80 H²) across.
[[nodiscard]] constexpr auto
mass_properties(const ConeShape& cone,
                float density) noexcept -> MassProperties
{
    const float r = cone.radius;
    const float height = 2.0F * cone.half_height;
    const float mass = density * mass_detail::PI * r * r * height / 3.0F;
    const float across = mass * (3.0F / 20.0F * r * r + 3.0F / 80.0F * height * height);
    return {.mass = mass, .center_of_mass = {.y = -cone.half_height + height / 4.0F}, .inertia = mass_detail::diagonal(across, mass * 0.3F * r * r, across)};
}

/// @brief A triangle's: none (it has no volume).
[[nodiscard]] constexpr auto
mass_properties(const TriangleShape& /*triangle*/,
                float /*density*/) noexcept -> MassProperties
{
    return {};
}

/// @brief The mass properties of the closed surface of triangles @p triangles (counter-clockwise seen from outside)
///        at @p density, by the covariance method about @p reference (any point; a point near the middle is precise).
/// @tparam Triangles A range of TriangleShape.
template<class Triangles>
[[nodiscard]] constexpr auto
surface_mass_properties(Triangles&& triangles,
                        Vector3 reference,
                        float density) noexcept -> MassProperties
{
    // Per tetrahedron (reference, a, b, c): 6 × volume = a · (b × c); covariance det/120 (aaᵀ + bbᵀ + ccᵀ + ssᵀ),
    // s = a + b + c; center of mass s/4.
    float six_volume = 0.0F;
    Vector3 weighted_center{};
    Matrix3 covariance{.columns = {Vector3{}, Vector3{}, Vector3{}}};
    for (const TriangleShape& triangle : triangles)
    {
        const Vector3 a = triangle.first - reference;
        const Vector3 b = triangle.second - reference;
        const Vector3 c = triangle.third - reference;
        const float determinant = dot(a, cross(b, c));
        const Vector3 sum = a + b + c;
        six_volume += determinant;
        weighted_center += sum * (determinant / 4.0F);
        covariance = covariance
                     + (Matrix3::outer_product(a, a) + Matrix3::outer_product(b, b) + Matrix3::outer_product(c, c) + Matrix3::outer_product(sum, sum))
                           * (determinant / 120.0F);
    }
    if (!(six_volume > 0.0F))
    {
        return {};
    }
    const float mass = density * six_volume / 6.0F;
    const Vector3 center = weighted_center / six_volume;
    const Matrix3 about_center = covariance * density - Matrix3::outer_product(center, center) * mass;
    return {.mass = mass,
            .center_of_mass = reference + center,
            .inertia = Matrix3::from_diagonal({.x = 1, .y = 1, .z = 1}) * mass_detail::trace(about_center) - about_center};
}

/// @brief A hull's (its core), by the covariance method over its polygons' fans of triangles.
[[nodiscard]] constexpr auto
mass_properties(const HullView& hull,
                float density) noexcept -> MassProperties
{
    // The fans, as a range of triangles about the average vertex.
    Vector3 reference{};
    for (const Vector3 vertex : hull.vertices)
    {
        reference += vertex;
    }
    reference = hull.vertices.empty() ? Vector3{} : reference / static_cast<float>(hull.vertices.size());
    auto fans = hull.faces | std::views::transform([&hull](const HullFace& face) {
                          const std::size_t first = face.first;
                          return std::views::iota(std::size_t{1}, std::size_t{face.count} - 1)
                                 | std::views::transform([&hull, first](std::size_t corner) {
                                       return TriangleShape{.first = hull.vertices[hull.face_vertices[first]],
                                                            .second = hull.vertices[hull.face_vertices[first + corner]],
                                                            .third = hull.vertices[hull.face_vertices[first + corner + 1]]};
                                   });
                      })
                      | std::views::join;
    return surface_mass_properties(fans, reference, density);
}

/// @brief @p properties of a part posed by @p pose in its parent's frame: the center of mass moved, the inertia rotated.
[[nodiscard]] constexpr auto
transformed(const MassProperties& properties,
            const Transform& pose) noexcept -> MassProperties
{
    const Matrix3 rotation = Matrix3::from_rotation(pose.orientation);
    return {.mass = properties.mass,
            .center_of_mass = transform_point(pose, properties.center_of_mass),
            .inertia = rotation * properties.inertia * transpose(rotation)};
}

/// @brief The mass properties of two parts together (the parallel axis theorem about their common center of mass).
[[nodiscard]] constexpr auto
combined(const MassProperties& first,
         const MassProperties& second) noexcept -> MassProperties
{
    const float mass = first.mass + second.mass;
    if (!(mass > 0.0F))
    {
        return {};
    }
    const Vector3 center = (first.center_of_mass * first.mass + second.center_of_mass * second.mass) / mass;
    return {.mass = mass,
            .center_of_mass = center,
            .inertia = first.inertia + mass_detail::point_inertia(first.mass, first.center_of_mass - center) + second.inertia
                       + mass_detail::point_inertia(second.mass, second.center_of_mass - center)};
}

/// @brief The principal moments and axes of the symmetric tensor @p inertia (cyclic Jacobi rotations).
[[nodiscard]] constexpr auto
principal_axes(const Matrix3& inertia) noexcept -> PrincipalAxes
{
    // Rotate the largest off-diagonal element to zero until none is left; the rotations' product holds the axes.
    std::array<std::array<float, 3>, 3> a{};
    std::array<std::array<float, 3>, 3> v{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            a[row][column] = inertia.element(row, column);
        }
    }
    const float scale = std::abs(a[0][0]) + std::abs(a[1][1]) + std::abs(a[2][2]);
    for (int sweep = 0; sweep < 32; ++sweep)
    {
        std::size_t p = 0;
        std::size_t q = 1;
        for (const auto& [row, column] : {std::pair<std::size_t, std::size_t>{0, 2}, std::pair<std::size_t, std::size_t>{1, 2}})
        {
            if (std::abs(a[row][column]) > std::abs(a[p][q]))
            {
                p = row;
                q = column;
            }
        }
        if (!(std::abs(a[p][q]) > 1e-7F * scale))
        {
            break;
        }
        const float theta = (a[q][q] - a[p][p]) / (2.0F * a[p][q]);
        const float t = (theta >= 0.0F ? 1.0F : -1.0F) / (std::abs(theta) + square_root(theta * theta + 1.0F));
        const float c = 1.0F / square_root(t * t + 1.0F);
        const float s = t * c;
        for (std::size_t k = 0; k < 3; ++k)
        {
            // A ← Jᵀ A J for the rotation J in the (p, q) plane; V ← V J.
            const float akp = a[k][p];
            const float akq = a[k][q];
            a[k][p] = c * akp - s * akq;
            a[k][q] = s * akp + c * akq;
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            const float apk = a[p][k];
            const float aqk = a[q][k];
            a[p][k] = c * apk - s * aqk;
            a[q][k] = s * apk + c * aqk;
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            const float vkp = v[k][p];
            const float vkq = v[k][q];
            v[k][p] = c * vkp - s * vkq;
            v[k][q] = s * vkp + c * vkq;
        }
    }

    // The axes as a rotation (the third column flipped if they came out left-handed).
    Matrix3 axes{.columns = {Vector3{.x = v[0][0], .y = v[1][0], .z = v[2][0]}, Vector3{.x = v[0][1], .y = v[1][1], .z = v[2][1]},
                             Vector3{.x = v[0][2], .y = v[1][2], .z = v[2][2]}}};
    if (determinant(axes) < 0.0F)
    {
        axes.columns[2] = -axes.columns[2];
    }
    return {.moments = {.x = a[0][0], .y = a[1][1], .z = a[2][2]}, .orientation = Quaternion::from_rotation_matrix(axes)};
}

} // namespace gfx::physics
