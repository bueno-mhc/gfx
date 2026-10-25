/// @file shape.cppm
/// @brief Shape: immutable geometry of any kind (ShapeKind's order), shared between colliders, and compounds of shapes.
///        A shape is a std::variant of the geometries; it checks them when made (descriptive PhysicsErrors) and keeps
///        its local bounds. Compounds hold their children through std::shared_ptr<const Shape> (a child can be shared
///        by many compounds) with a hierarchy over the children's boxes.
export module gfx.physics.shapes:shape;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import :convex;
import :hull;
import :mass;
import :static_bvh;
import :mesh;

export namespace gfx::physics
{

class Shape;

/// @brief A child of a compound: a shape at a pose in the compound's frame.
struct CompoundChild
{
    std::shared_ptr<const Shape> shape{}; ///< Not null.
    Transform pose{};                     ///< In the compound's frame.
};

/// @brief Shapes put together, each at its pose.
class CompoundShape
{
public:
    /// @brief The compound of @p children.
    /// @throws PhysicsError if there is no child or a child has no shape.
    explicit CompoundShape(std::vector<CompoundChild> children);

    [[nodiscard]] auto children() const noexcept -> std::span<const CompoundChild>
    {
        return m_children;
    }

    /// @brief The hierarchy over the children's boxes in the compound's frame.
    [[nodiscard]] auto bvh() const noexcept -> const StaticBvh&
    {
        return m_bvh;
    }

    /// @brief The box around every child.
    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_bvh.bounds();
    }

    /// @brief Calls `visitor(index, child)` for each child whose box overlaps @p box.
    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        m_bvh.query(box, [this, &visitor](std::uint32_t index) { visitor(index, m_children[index]); });
    }

    [[nodiscard]] auto operator==(const CompoundShape& other) const noexcept -> bool;

private:
    std::vector<CompoundChild> m_children{};
    StaticBvh m_bvh{};
};

/// @brief Every kind of geometry, in ShapeKind's order.
using ShapeGeometry =
    std::variant<SphereShape, CapsuleShape, CylinderShape, ConeShape, BoxShape, HullShape, TriangleMeshShape, HeightFieldShape, CompoundShape>;

/// @brief Immutable geometry of any kind.
class Shape
{
public:
    /// @brief A shape of @p geometry.
    /// @throws PhysicsError if a size is not finite and positive (radii, half heights, half extents) or a rounding is
    ///         negative.
    explicit Shape(ShapeGeometry geometry);

    /// @brief Its kind (the alternative of its geometry).
    [[nodiscard]] auto kind() const noexcept -> ShapeKind
    {
        return static_cast<ShapeKind>(m_geometry.index());
    }

    /// @brief Whether it is one of the convex kinds (sphere, capsule, cylinder, cone, box, hull).
    [[nodiscard]] auto is_convex() const noexcept -> bool
    {
        return m_geometry.index() <= static_cast<std::size_t>(ShapeKind::CONVEX_HULL);
    }

    [[nodiscard]] auto geometry() const noexcept -> const ShapeGeometry&
    {
        return m_geometry;
    }

    /// @brief Its geometry as @p Geometry, or null.
    template<class Geometry>
    [[nodiscard]] auto get_if() const noexcept -> const Geometry*
    {
        return std::get_if<Geometry>(&m_geometry);
    }

    /// @brief The box around it in its own frame.
    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_bounds;
    }

    /// @brief The box around it at @p pose (tight for convex shapes: from their support functions).
    [[nodiscard]] auto bounds(const Transform& pose) const noexcept -> Aabb;

    /// @brief Its mass properties at @p density: convex shapes' cores, closed meshes', compounds' children together;
    ///        none for open meshes and height fields (static geometry).
    [[nodiscard]] auto mass_properties(float density) const -> MassProperties;

    [[nodiscard]] auto operator==(const Shape&) const noexcept -> bool = default;

private:
    ShapeGeometry m_geometry;
    Aabb m_bounds{};
};

/// @brief A shared shape of @p geometry (what compounds and colliders hold).
/// @throws PhysicsError as Shape::Shape.
[[nodiscard]] auto make_shape(ShapeGeometry geometry) -> std::shared_ptr<const Shape>;

/// @brief The box around the convex shape @p shape at @p pose, from its support function along the six axes.
template<ConvexShape Convex>
[[nodiscard]] constexpr auto
convex_bounds(const Convex& shape,
              const Transform& pose) noexcept -> Aabb
{
    const Quaternion back = conjugate(pose.orientation);
    const auto reach = [&](Vector3 axis) { return dot(transform_point(pose, shape.support(rotate(back, axis))), axis); };
    const float margin = shape.margin();
    return {.minimum = {.x = -reach({.x = -1}) - margin, .y = -reach({.y = -1}) - margin, .z = -reach({.z = -1}) - margin},
            .maximum = {.x = reach({.x = 1}) + margin, .y = reach({.y = 1}) + margin, .z = reach({.z = 1}) + margin}};
}

} // namespace gfx::physics
