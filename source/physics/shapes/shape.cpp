// Shape: validation of the convex geometries, bounds (local and posed), mass properties; compounds.
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

constexpr std::string_view OPERATION = "Shape::Shape";

/// Throws the PhysicsError of a size that is not finite and positive.
void
require_positive(std::string_view kind,
                 std::string_view name,
                 float value)
{
    if (!std::isfinite(value) || !(value > 0.0F))
    {
        throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                            .operation = std::string{OPERATION},
                            .problem = std::format("the {} of a {} must be finite and greater than 0 (it is {})", name, kind, value),
                            .suggestion = "give sizes in the world's unit (half sizes for half extents and half heights)",
                            .context = {}});
    }
}

/// Throws the PhysicsError of a size that is not finite and at least 0.
void
require_non_negative(std::string_view kind,
                     std::string_view name,
                     float value)
{
    if (!std::isfinite(value) || !(value >= 0.0F))
    {
        throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                            .operation = std::string{OPERATION},
                            .problem = std::format("the {} of a {} must be finite and at least 0 (it is {})", name, kind, value),
                            .suggestion = "give 0 for none (a capsule of half height 0 is a sphere; a box of rounding 0 has sharp edges)",
                            .context = {}});
    }
}

/// The convex geometries' checks.
void
validate(const ShapeGeometry& geometry)
{
    if (const auto* sphere = std::get_if<SphereShape>(&geometry))
    {
        require_positive("sphere", "radius", sphere->radius);
    }
    else if (const auto* capsule = std::get_if<CapsuleShape>(&geometry))
    {
        require_positive("capsule", "radius", capsule->radius);
        require_non_negative("capsule", "half height", capsule->half_height);
    }
    else if (const auto* cylinder = std::get_if<CylinderShape>(&geometry))
    {
        require_positive("cylinder", "radius", cylinder->radius);
        require_positive("cylinder", "half height", cylinder->half_height);
    }
    else if (const auto* cone = std::get_if<ConeShape>(&geometry))
    {
        require_positive("cone", "radius", cone->radius);
        require_positive("cone", "half height", cone->half_height);
    }
    else if (const auto* box = std::get_if<BoxShape>(&geometry))
    {
        require_positive("box", "half extent x", box->half_extents.x);
        require_positive("box", "half extent y", box->half_extents.y);
        require_positive("box", "half extent z", box->half_extents.z);
        require_non_negative("box", "rounding radius", box->radius);
    }
}

/// The local bounds of @p geometry.
[[nodiscard]] auto
local_bounds(const ShapeGeometry& geometry) -> Aabb
{
    return std::visit([](const auto& shape) { return shape.bounds(); }, geometry);
}

} // namespace

// ===================================================================================================== compounds

CompoundShape::CompoundShape(std::vector<CompoundChild> children)
    : m_children{std::move(children)}
{
    if (m_children.empty())
    {
        throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                            .operation = "CompoundShape::CompoundShape",
                            .problem = "a compound needs at least one child",
                            .suggestion = "give the child shapes and their poses",
                            .context = {}});
    }
    std::vector<Aabb> boxes;
    boxes.reserve(m_children.size());
    for (std::size_t index = 0; index < m_children.size(); ++index)
    {
        if (!m_children[index].shape)
        {
            throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                                .operation = "CompoundShape::CompoundShape",
                                .problem = std::format("child {} has no shape", index),
                                .suggestion = "give every child a shape (make_shape())",
                                .context = {}});
        }
        boxes.push_back(m_children[index].shape->bounds(m_children[index].pose));
    }
    m_bvh = StaticBvh{boxes};
}

auto
CompoundShape::operator==(const CompoundShape& other) const noexcept -> bool
{
    return std::ranges::equal(m_children, other.m_children,
                              [](const CompoundChild& first, const CompoundChild& second) { return first.shape == second.shape && first.pose == second.pose; });
}

// ===================================================================================================== shapes

Shape::Shape(ShapeGeometry geometry)
    : m_geometry{std::move(geometry)}
{
    validate(m_geometry);
    m_bounds = local_bounds(m_geometry);
}

auto
Shape::bounds(const Transform& pose) const noexcept -> Aabb
{
    // Convex shapes from their support functions (tight); the others from their local box (conservative).
    return std::visit(
        [&pose, this](const auto& shape) {
            using Geometry = std::remove_cvref_t<decltype(shape)>;
            if constexpr (ConvexShape<Geometry>)
            {
                return convex_bounds(shape, pose);
            }
            else
            {
                return m_bounds.transformed(pose);
            }
        },
        m_geometry);
}

auto
Shape::mass_properties(float density) const -> MassProperties
{
    return std::visit(
        [density](const auto& shape) -> MassProperties {
            using Geometry = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Geometry, HullShape>)
            {
                return physics::mass_properties(shape.view(), density);
            }
            else if constexpr (std::same_as<Geometry, TriangleMeshShape>)
            {
                if (!shape.closed())
                {
                    return {};
                }
                auto triangles = std::views::iota(0U, shape.triangle_count()) | std::views::transform([&shape](std::uint32_t index) { return shape.triangle(index); });
                return surface_mass_properties(triangles, shape.bounds().center(), density);
            }
            else if constexpr (std::same_as<Geometry, HeightFieldShape>)
            {
                return {};
            }
            else if constexpr (std::same_as<Geometry, CompoundShape>)
            {
                MassProperties total{};
                for (const CompoundChild& child : shape.children())
                {
                    total = combined(total, transformed(child.shape->mass_properties(density), child.pose));
                }
                return total;
            }
            else
            {
                return physics::mass_properties(shape, density);
            }
        },
        m_geometry);
}

auto
make_shape(ShapeGeometry geometry) -> std::shared_ptr<const Shape>
{
    return std::make_shared<const Shape>(std::move(geometry));
}

} // namespace gfx::physics
