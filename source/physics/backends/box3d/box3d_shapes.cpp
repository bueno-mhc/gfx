// The Box3D backend's shapes: what Box3D can hold (checked when recorded), and colliders and triggers made of Box3D
// shapes — spheres and capsules as they are, boxes as box hulls, hulls, cylinders and cones as hulls (round shapes of
// ColliderDescription::round_shape_segments sides), meshes in the body's frame (two-sided ones with each triangle twice:
// Box3D culls back sides), height fields in the body's frame, compounds as one Box3D shape per part.
module;

#include <box3d/box3d.h>

module gfx.physics.backends.box3d;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.world;
import :backend;

namespace gfx::physics::box3d_backend
{

namespace
{

/// Sides of round shapes past which their hulls have more vertices than Box3D holds (cylinders: two rings).
constexpr std::uint32_t MAXIMUM_ROUND_SIDES = B3_MAX_HULL_VERTICES / 2;

/// Whether @p pose is the identity (within float precision).
[[nodiscard]] auto
is_identity(const Transform& pose) noexcept -> bool
{
    return length_squared(pose.position) <= 1e-12F && std::abs(std::abs(pose.orientation.w) - 1.0F) <= 1e-6F;
}

/// The hull Box3D makes of @p points (all of them its vertices).
[[nodiscard]] auto
make_hull(std::span<const b3Vec3> points) -> b3HullData*
{
    b3HullData* hull = b3CreateHull(points.data(), static_cast<int>(points.size()), static_cast<int>(points.size()));
    if (hull == nullptr)
    {
        throw PhysicsError({.backend = PhysicsBackendKind::BOX3D,
                            .operation = "RigidBody::add_collider",
                            .problem = std::format("Box3D made no hull of {} points", points.size()),
                            .suggestion = "give points that span a volume",
                            .context = {}});
    }
    return hull;
}

/// A ring of @p sides points of @p radius at height @p height, about +Y.
void
add_ring(std::vector<b3Vec3>& points,
         std::uint32_t sides,
         float radius,
         float height)
{
    for (std::uint32_t side = 0; side < sides; ++side)
    {
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(side) / static_cast<float>(sides);
        points.push_back({.x = radius * std::cos(angle), .y = height, .z = radius * std::sin(angle)});
    }
}

} // namespace

void
Box3dBackend::check_shape(const Shape& shape,
                          const Transform& pose,
                          BodyKind body_kind,
                          std::uint32_t round_sides,
                          std::string_view operation,
                          const PhysicsErrorContext& context) const
{
    // Each kind of geometry against what Box3D holds; compounds part by part, at their poses.
    std::visit(
        [&](const auto& geometry) {
            using Geometry = std::remove_cvref_t<decltype(geometry)>;
            if constexpr (std::same_as<Geometry, BoxShape>)
            {
                if (geometry.radius > 0.0F)
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::ROUNDED_HULLS, std::format("Box3D's hulls are not rounded (a box rounded by {})", geometry.radius),
                                      "use a box of radius 0 (or a capsule, or a compound of spheres and boxes)", context);
                }
            }
            else if constexpr (std::same_as<Geometry, HullShape>)
            {
                if (geometry.margin() > 0.0F)
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::ROUNDED_HULLS, std::format("Box3D's hulls are not rounded (a hull rounded by {})", geometry.margin()),
                                      "use a hull of radius 0", context);
                }
                if (geometry.view().vertices.size() > B3_MAX_HULL_VERTICES)
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::LARGE_HULLS,
                                      std::format("Box3D's hulls hold at most {} vertices ({} given)", B3_MAX_HULL_VERTICES, geometry.view().vertices.size()),
                                      "simplify the hull, or split it into a compound of hulls", context);
                }
            }
            else if constexpr (std::same_as<Geometry, CylinderShape> || std::same_as<Geometry, ConeShape>)
            {
                if (round_sides > MAXIMUM_ROUND_SIDES)
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::LARGE_HULLS,
                                      std::format("Box3D makes round shapes hulls of at most {} sides ({} asked)", MAXIMUM_ROUND_SIDES, round_sides),
                                      "ask fewer round_shape_segments", context);
                }
            }
            else if constexpr (std::same_as<Geometry, TriangleMeshShape>)
            {
                if (body_kind != BodyKind::STATIC)
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, std::format("Box3D's meshes go on static bodies only (this body is {})", body_kind),
                                      "make the moving body of convex shapes (hulls, or a compound of them: a convex decomposition)", context);
                }
            }
            else if constexpr (std::same_as<Geometry, HeightFieldShape>)
            {
                if (body_kind != BodyKind::STATIC)
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::DYNAMIC_HEIGHT_FIELDS,
                                      std::format("Box3D's height fields go on static bodies only (this body is {})", body_kind), "put the height field on a static body", context);
                }
                if (!is_identity(pose))
                {
                    throw_unsupported(identity(), operation, PhysicsCapability::POSED_HEIGHT_FIELDS, "Box3D's height fields lie in their body's frame",
                                      "give the height field a static body of its own at the pose wanted, and the collider no pose", context);
                }
            }
            else if constexpr (std::same_as<Geometry, CompoundShape>)
            {
                for (const CompoundChild& child : geometry.children())
                {
                    check_shape(*child.shape, pose * child.pose, body_kind, round_sides, operation, context);
                }
            }
        },
        shape.geometry());
}

auto
Box3dBackend::shape_definition(const ColliderDescription& description) const -> b3ShapeDef
{
    // Every shape can be seen by triggers and reports its contacts (the backend keeps the touching pairs); hits only when asked.
    b3ShapeDef definition = b3DefaultShapeDef();
    definition.baseMaterial.friction = description.material.friction.dynamic_coefficient;
    definition.baseMaterial.restitution = description.material.restitution;
    definition.baseMaterial.rollingResistance = description.material.friction.rolling;
    definition.density = description.density;
    definition.filter = to_box3d(description.filter);
    definition.enableSensorEvents = true;
    definition.enableContactEvents = true;
    definition.enableHitEvents = description.contact_events;
    return definition;
}

void
Box3dBackend::create_shapes(const Shape& shape,
                            const Transform& pose,
                            b3BodyId body,
                            const b3ShapeDef& definition,
                            std::uint32_t round_sides,
                            std::uint32_t child,
                            ShapeOwner owner,
                            std::vector<b3ShapeId>& shapes,
                            ColliderEntry* geometry_owner)
{
    owner.child = child;
    const auto add = [&](b3ShapeId id) {
        own_shape(id, owner);
        shapes.push_back(id);
    };
    const auto add_hull = [&](std::span<const b3Vec3> points) {
        b3HullData* hull = make_hull(points);
        add(b3CreateTransformedHullShape(body, &definition, hull, to_box3d(pose), b3Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}));
        b3DestroyHull(hull);
    };
    std::visit(
        [&](const auto& geometry) {
            using Geometry = std::remove_cvref_t<decltype(geometry)>;
            if constexpr (std::same_as<Geometry, SphereShape>)
            {
                const b3Sphere sphere{.center = to_box3d(pose.position), .radius = geometry.radius};
                add(b3CreateSphereShape(body, &definition, &sphere));
            }
            else if constexpr (std::same_as<Geometry, CapsuleShape>)
            {
                if (geometry.half_height <= 0.0F)
                {
                    const b3Sphere sphere{.center = to_box3d(pose.position), .radius = geometry.radius};
                    add(b3CreateSphereShape(body, &definition, &sphere));
                    return;
                }
                const b3Capsule capsule{.center1 = to_box3d(transform_point(pose, Vector3{.y = -geometry.half_height})),
                                        .center2 = to_box3d(transform_point(pose, Vector3{.y = geometry.half_height})),
                                        .radius = geometry.radius};
                add(b3CreateCapsuleShape(body, &definition, &capsule));
            }
            else if constexpr (std::same_as<Geometry, BoxShape>)
            {
                const b3BoxHull box = b3MakeBoxHull(geometry.half_extents.x, geometry.half_extents.y, geometry.half_extents.z);
                add(b3CreateTransformedHullShape(body, &definition, &box.base, to_box3d(pose), b3Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}));
            }
            else if constexpr (std::same_as<Geometry, HullShape>)
            {
                std::vector<b3Vec3> points;
                for (const Vector3 vertex : geometry.view().vertices)
                {
                    points.push_back(to_box3d(vertex));
                }
                add_hull(points);
            }
            else if constexpr (std::same_as<Geometry, CylinderShape>)
            {
                std::vector<b3Vec3> points;
                add_ring(points, round_sides, geometry.radius, -geometry.half_height);
                add_ring(points, round_sides, geometry.radius, geometry.half_height);
                add_hull(points);
            }
            else if constexpr (std::same_as<Geometry, ConeShape>)
            {
                std::vector<b3Vec3> points;
                add_ring(points, round_sides, geometry.radius, -geometry.half_height);
                points.push_back({.x = 0.0F, .y = geometry.half_height, .z = 0.0F});
                add_hull(points);
            }
            else if constexpr (std::same_as<Geometry, TriangleMeshShape>)
            {
                // In the body's frame; a two-sided mesh has each triangle twice, once reversed.
                std::vector<b3Vec3> vertices;
                for (const Vector3 vertex : geometry.vertices())
                {
                    vertices.push_back(to_box3d(transform_point(pose, vertex)));
                }
                std::vector<std::int32_t> indices;
                for (const std::array<std::uint32_t, 3>& triangle : geometry.triangles())
                {
                    indices.insert(indices.end(), {static_cast<std::int32_t>(triangle[0]), static_cast<std::int32_t>(triangle[1]), static_cast<std::int32_t>(triangle[2])});
                    if (geometry.two_sided())
                    {
                        indices.insert(indices.end(), {static_cast<std::int32_t>(triangle[0]), static_cast<std::int32_t>(triangle[2]), static_cast<std::int32_t>(triangle[1])});
                    }
                }
                b3MeshDef mesh_definition{};
                mesh_definition.vertices = vertices.data();
                mesh_definition.indices = indices.data();
                mesh_definition.vertexCount = static_cast<int>(vertices.size());
                mesh_definition.triangleCount = static_cast<int>(indices.size() / 3);
                mesh_definition.identifyEdges = !geometry.two_sided();
                b3MeshData* mesh = b3CreateMesh(&mesh_definition, nullptr, 0);
                geometry_owner->meshes.emplace_back(mesh);
                add(b3CreateMeshShape(body, &definition, mesh, b3Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}));
            }
            else if constexpr (std::same_as<Geometry, HeightFieldShape>)
            {
                // Heights row by row (rows along +Z, columns along +X), as Box3D stores them.
                const auto heights = geometry.heights();
                std::vector<float> samples;
                float lowest = std::numeric_limits<float>::max();
                float highest = std::numeric_limits<float>::lowest();
                for (std::size_t row = 0; row < heights.extent(0); ++row)
                {
                    for (std::size_t column = 0; column < heights.extent(1); ++column)
                    {
                        samples.push_back(heights[row, column]);
                        lowest = std::min(lowest, heights[row, column]);
                        highest = std::max(highest, heights[row, column]);
                    }
                }
                b3HeightFieldDef field_definition{};
                field_definition.heights = samples.data();
                field_definition.scale = to_box3d(geometry.scale());
                field_definition.countX = static_cast<int>(geometry.columns());
                field_definition.countZ = static_cast<int>(geometry.rows());
                field_definition.globalMinimumHeight = lowest;
                field_definition.globalMaximumHeight = highest > lowest ? highest : lowest + 1.0F;
                b3HeightFieldData* field = b3CreateHeightField(&field_definition);
                geometry_owner->height_fields.emplace_back(field);
                add(b3CreateHeightFieldShape(body, &definition, field));
            }
            else if constexpr (std::same_as<Geometry, CompoundShape>)
            {
                // One Box3D shape per part, each with the index of the compound's child it comes from.
                const auto children = geometry.children();
                for (std::size_t index = 0; index < children.size(); ++index)
                {
                    create_shapes(*children[index].shape, pose * children[index].pose, body, definition, round_sides,
                                  child == NO_SUB_SHAPE ? static_cast<std::uint32_t>(index) : child, owner, shapes, geometry_owner);
                }
            }
        },
        shape.geometry());
}

void
Box3dBackend::create_collider(const edits::CreateCollider& change,
                              const WorldSnapshot& /*pending*/)
{
    const ColliderDescription& description = change.description;
    if (m_colliders.size() <= slot(change.collider))
    {
        m_colliders.resize(slot(change.collider) + 1);
    }
    ColliderEntry& collider = m_colliders[slot(change.collider)];
    collider = {.handle = change.collider, .body = change.body, .filter = description.filter, .contact_events = description.contact_events};
    create_shapes(*description.shape, description.pose, m_bodies[slot(change.body)].id, shape_definition(description), description.round_shape_segments, NO_SUB_SHAPE,
                  ShapeOwner{.collider = change.collider}, collider.shapes, &collider);
}

void
Box3dBackend::destroy_collider(ColliderHandle collider,
                               WorldSnapshot& pending)
{
    // Its touches end and its overlaps exit now; its shapes go before the geometry they reference.
    end_touches_of(collider, pending);
    end_overlaps_of(collider, {}, pending);
    ColliderEntry& entry = m_colliders[slot(collider)];
    for (const b3ShapeId shape : entry.shapes)
    {
        disown_shape(shape);
        b3DestroyShape(shape, true);
    }
    entry.shapes.clear();
    entry = {};
}

void
Box3dBackend::create_trigger(const edits::CreateTrigger& change,
                             const WorldSnapshot& /*pending*/)
{
    // Sensor shapes of no mass: on the trigger's body, or on a static body of its own at its pose.
    const TriggerDescription& description = change.description;
    if (m_triggers.size() <= slot(change.trigger))
    {
        m_triggers.resize(slot(change.trigger) + 1);
    }
    TriggerEntry& entry = m_triggers[slot(change.trigger)];
    entry = {.handle = change.trigger};
    b3BodyId body{};
    Transform pose = description.pose;
    if (description.body)
    {
        body = m_bodies[slot(description.body)].id;
    }
    else
    {
        b3BodyDef own = b3DefaultBodyDef();
        own.position = to_box3d(description.pose.position);
        own.rotation = to_box3d(description.pose.orientation);
        entry.own_body = b3CreateBody(m_world, &own);
        entry.has_own_body = true;
        body = entry.own_body;
        pose = {};
    }
    b3ShapeDef definition = b3DefaultShapeDef();
    definition.isSensor = true;
    definition.enableSensorEvents = true;
    definition.density = 0.0F;
    definition.filter = to_box3d(description.filter);
    ColliderEntry geometry{};
    create_shapes(*description.shape, pose, body, definition, 16, NO_SUB_SHAPE, ShapeOwner{.trigger = change.trigger}, entry.shapes, &geometry);
    entry.meshes = std::move(geometry.meshes);
    entry.height_fields = std::move(geometry.height_fields);
}

void
Box3dBackend::destroy_trigger(TriggerHandle trigger,
                              WorldSnapshot& pending)
{
    end_overlaps_of({}, trigger, pending);
    TriggerEntry& entry = m_triggers[slot(trigger)];
    for (const b3ShapeId shape : entry.shapes)
    {
        disown_shape(shape);
        b3DestroyShape(shape, false);
    }
    entry.shapes.clear();
    if (entry.has_own_body)
    {
        b3DestroyBody(entry.own_body);
    }
    entry = {};
}

} // namespace gfx::physics::box3d_backend
