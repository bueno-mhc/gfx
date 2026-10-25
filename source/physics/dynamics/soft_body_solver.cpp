module gfx.physics.dynamics;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.tasks;
import gfx.physics.interface;
import :xpbd_math;

namespace gfx::physics
{

namespace
{

using xpbd::TINY;

/// No proxy, no body.
constexpr std::uint32_t NONE = 0xFFFFFFFFU;

/// The smallest color whose constraints are projected on the scheduler's threads, and the range each task takes.
constexpr std::uint32_t PARALLEL_COLOR_SIZE = 512;
constexpr std::uint32_t PARALLEL_RANGE = 128;

/// Throws the PhysicsError of a step the solver cannot take.
[[noreturn]] void
reject_step(std::string problem,
            std::string suggestion)
{
    throw PhysicsError({.backend = PhysicsSubsystem::SOFT_BODIES, .operation = "XpbdSoftBodySolver::step", .problem = std::move(problem), .suggestion = std::move(suggestion)});
}

/// @p rotation turned further by the small rotation vector @p angle (radians, world axes).
[[nodiscard]] auto
turned(const Quaternion& rotation,
       Vector3 angle) noexcept -> Quaternion
{
    const Quaternion change = Quaternion{.x = angle.x * 0.5F, .y = angle.y * 0.5F, .z = angle.z * 0.5F, .w = 0.0F} * rotation;
    return normalized(Quaternion{.x = rotation.x + change.x, .y = rotation.y + change.y, .z = rotation.z + change.z, .w = rotation.w + change.w});
}

/// A rigid body as one soft body sees it during a step: the backend's motion from its start pose to its end pose, plus
/// what the soft body's corrections moved it by (dynamic bodies only).
struct Proxy
{
    std::uint32_t body = NONE;           ///< Its index in the bodies.
    Transform start{};                   ///< The backend's pose before the rigid step.
    Transform end{};                     ///< After it.
    float inverse_mass = 0.0F;           ///< 0: the corrections do not move it (static, kinematic, without mass).
    Vector3 linear_mask{.x = 1.0F, .y = 1.0F, .z = 1.0F};  ///< 0 on the axes it may not move along.
    Vector3 angular_mask{.x = 1.0F, .y = 1.0F, .z = 1.0F}; ///< 0 on the axes it may not turn about.
    Matrix3 local_inverse_inertia{.columns = {Vector3{}, Vector3{}, Vector3{}}}; ///< About its center of mass, in its frame.
    Vector3 local_center{};              ///< Its center of mass in its frame.

    Transform base{};                    ///< The backend's motion at the current sub-step.
    Transform pose{};                    ///< Its frame as the soft body sees it now.
    Transform previous_pose{};           ///< At the start of the sub-step.
    Vector3 center{};                    ///< Its center of mass now, world space.
    Matrix3 inverse_inertia{.columns = {Vector3{}, Vector3{}, Vector3{}}}; ///< World space, with its locks.

    Vector3 offset{};                    ///< Of its center beyond the backend's motion.
    Quaternion turn{};                   ///< Its rotation beyond the backend's motion.
    Vector3 linear_change{};             ///< The velocity the corrections gave it.
    Vector3 angular_change{};            ///< The angular velocity they gave it.
    Vector3 linear_impulse{};            ///< Their impulse.
    Vector3 angular_impulse{};           ///< Their angular impulse about its center.
};

/// The frame of @p proxy from its base, its center and its turn.
void
refresh(Proxy& proxy) noexcept
{
    const Quaternion orientation = normalized(proxy.turn * proxy.base.orientation);
    proxy.pose = {.position = proxy.center - rotate(orientation, proxy.local_center), .orientation = orientation};
}

/// @p proxy at @p fraction of the step: the backend's motion there, moved by the corrections so far.
void
place(Proxy& proxy,
      float fraction) noexcept
{
    proxy.base = interpolate(proxy.start, proxy.end, fraction);
    proxy.center = transform_point(proxy.base, proxy.local_center) + proxy.offset;
    refresh(proxy);
    if (proxy.inverse_mass > 0.0F)
    {
        const Matrix3 rotation = Matrix3::from_rotation(proxy.pose.orientation);
        const Matrix3 world = rotation * proxy.local_inverse_inertia * transpose(rotation);
        const Vector3 mask = proxy.angular_mask;
        proxy.inverse_inertia = Matrix3{.columns = {component_multiply(world.columns[0], mask) * mask.x, component_multiply(world.columns[1], mask) * mask.y,
                                                    component_multiply(world.columns[2], mask) * mask.z}};
    }
}

/// The inverse mass @p proxy opposes to a correction along @p direction at @p point (0 when it does not move).
[[nodiscard]] auto
inverse_mass_at(const Proxy& proxy,
                Vector3 point,
                Vector3 direction) noexcept -> float
{
    if (!(proxy.inverse_mass > 0.0F))
    {
        return 0.0F;
    }
    const Vector3 arm = cross(point - proxy.center, direction);
    return proxy.inverse_mass * dot(direction, component_multiply(direction, proxy.linear_mask)) + dot(arm, proxy.inverse_inertia * arm);
}

/// Moves @p proxy by the positional correction @p correction at @p point (a sub-step of @p seconds), of which the part
/// @p moving becomes velocity and impulse (the rest undoes an overlap the sub-step started with).
void
push(Proxy& proxy,
     Vector3 correction,
     Vector3 point,
     float seconds,
     float moving = 1.0F) noexcept
{
    if (!(proxy.inverse_mass > 0.0F))
    {
        return;
    }
    const Vector3 arm = point - proxy.center;
    const Vector3 moment = cross(arm, correction);
    const Vector3 translation = component_multiply(correction, proxy.linear_mask) * proxy.inverse_mass;
    const Vector3 rotation = proxy.inverse_inertia * moment;
    proxy.offset += translation;
    proxy.center += translation;
    proxy.turn = turned(proxy.turn, rotation);
    refresh(proxy);
    const float rate = moving / seconds;
    proxy.linear_change += translation * rate;
    proxy.angular_change += rotation * rate;
    proxy.linear_impulse += correction * rate;
    proxy.angular_impulse += moment * rate;
}

/// How far the point of @p proxy now at @p point moved during the sub-step.
[[nodiscard]] auto
body_displacement(const Proxy& proxy,
                  Vector3 point) noexcept -> Vector3
{
    const Vector3 local = transform_point(inverse(proxy.pose), point);
    return point - transform_point(proxy.previous_pose, local);
}

/// A collider part (a compound's leaves are parts of their own) a soft body may touch in a step.
struct Candidate
{
    const Shape* shape = nullptr; ///< Never a compound.
    Transform pose{};             ///< In its body's frame.
    std::uint32_t proxy = NONE;   ///< Its body's proxy.
    float static_friction = 0.0F; ///< Combined with the soft body's.
    float dynamic_friction = 0.0F;
};

/// A pin as the step uses it.
struct Pin
{
    std::uint32_t particle = 0;
    std::uint32_t proxy = NONE; ///< NONE: the world.
    Vector3 point{};            ///< In the world, or in the body's frame.
    float compliance = 0.0F;
    bool kinematic = false;     ///< The particle follows the point (rigid pin to the world or to a body corrections do not move).
};

/// Where a particle touches a shape.
struct Touch
{
    float depth = 0.0F; ///< How far the particle's sphere is inside.
    Vector3 normal{};   ///< Out of the shape, toward the particle.
    Vector3 point{};    ///< On the shape's surface.
};

/// The touch of a sphere of @p radius at @p point with @p shape at @p pose (not a compound), if any.
[[nodiscard]] auto
touch(const Shape& shape,
      const Transform& pose,
      Vector3 point,
      float radius) noexcept -> std::optional<Touch>
{
    // A touch of a sphere around a core point.
    const auto around = [&](Vector3 core, float core_radius) -> std::optional<Touch> {
        const Vector3 offset = point - core;
        const float size = length(offset);
        const float gap = size - core_radius;
        if (gap >= radius)
        {
            return std::nullopt;
        }
        const Vector3 normal = size > TINY ? offset / size : Vector3{.y = 1.0F};
        return Touch{.depth = radius - gap, .normal = normal, .point = core + normal * core_radius};
    };
    switch (shape.kind())
    {
        case ShapeKind::SPHERE:
            return around(pose.position, shape.get_if<SphereShape>()->radius);
        case ShapeKind::CAPSULE:
        {
            const CapsuleShape& capsule = *shape.get_if<CapsuleShape>();
            const Vector3 local = transform_point(inverse(pose), point);
            return around(transform_point(pose, {.y = std::clamp(local.y, -capsule.half_height, capsule.half_height)}), capsule.radius);
        }
        case ShapeKind::BOX:
        {
            // Outside the core: the nearest core point; inside: out through the nearest face.
            const BoxShape& box = *shape.get_if<BoxShape>();
            const Vector3 local = transform_point(inverse(pose), point);
            const Vector3 half = box.half_extents;
            const Vector3 clamped{.x = std::clamp(local.x, -half.x, half.x), .y = std::clamp(local.y, -half.y, half.y), .z = std::clamp(local.z, -half.z, half.z)};
            const Vector3 outside = local - clamped;
            const float outside_size = length(outside);
            Vector3 normal{};
            Vector3 surface{};
            float gap = 0.0F;
            if (outside_size > TINY)
            {
                normal = outside / outside_size;
                gap = outside_size - box.radius;
                surface = clamped + normal * box.radius;
            }
            else
            {
                const std::array<float, 3> coordinates{local.x, local.y, local.z};
                const std::array<float, 3> halves{half.x, half.y, half.z};
                std::size_t axis = 0;
                for (std::size_t candidate = 1; candidate < 3; ++candidate)
                {
                    if (halves[candidate] - std::abs(coordinates[candidate]) < halves[axis] - std::abs(coordinates[axis]))
                    {
                        axis = candidate;
                    }
                }
                const float side = coordinates[axis] >= 0.0F ? 1.0F : -1.0F;
                std::array<float, 3> face = coordinates;
                face[axis] = side * halves[axis];
                std::array<float, 3> direction{};
                direction[axis] = side;
                normal = {.x = direction[0], .y = direction[1], .z = direction[2]};
                gap = -(halves[axis] - std::abs(coordinates[axis])) - box.radius;
                surface = Vector3{.x = face[0], .y = face[1], .z = face[2]} + normal * box.radius;
            }
            if (gap >= radius)
            {
                return std::nullopt;
            }
            return Touch{.depth = radius - gap, .normal = rotate(pose.orientation, normal), .point = transform_point(pose, surface)};
        }
        case ShapeKind::CYLINDER:
        case ShapeKind::CONE:
        case ShapeKind::CONVEX_HULL:
        {
            // The narrow phase's closest point (a negative distance inside).
            const std::optional<ShapeClosestPoint> nearest = closest_point_on_shape(shape, pose, point, radius);
            if (!nearest)
            {
                return std::nullopt;
            }
            const Vector3 offset = point - nearest->point;
            const float size = length(offset);
            if (!(size > TINY) || nearest->distance >= radius)
            {
                return std::nullopt;
            }
            return Touch{.depth = radius - nearest->distance, .normal = nearest->distance >= 0.0F ? offset / size : -offset / size, .point = nearest->point};
        }
        case ShapeKind::TRIANGLE_MESH:
        case ShapeKind::HEIGHT_FIELD:
        {
            // The nearest triangle: behind a one-sided one, the particle is inside.
            const std::optional<ShapeClosestPoint> nearest = closest_point_on_shape(shape, pose, point, 2.0F * radius);
            if (!nearest || nearest->sub_shape == NO_SUB_SHAPE)
            {
                return std::nullopt;
            }
            const TriangleMeshShape* mesh = shape.get_if<TriangleMeshShape>();
            const TriangleShape triangle = mesh != nullptr ? mesh->triangle(nearest->sub_shape) : shape.get_if<HeightFieldShape>()->triangle(nearest->sub_shape);
            const Vector3 face = rotate(pose.orientation, triangle.normal());
            const Vector3 offset = point - nearest->point;
            const float size = length(offset);
            const bool behind = (mesh == nullptr || !mesh->two_sided()) && dot(offset, face) < 0.0F;
            const float gap = behind ? -size : size;
            if (gap >= radius)
            {
                return std::nullopt;
            }
            const Vector3 normal = behind || !(size > TINY) ? face : offset / size;
            return Touch{.depth = radius - gap, .normal = normal, .point = nearest->point};
        }
        case ShapeKind::COMPOUND:
            break;
    }
    return std::nullopt;
}

/// Projects one constraint (not a tether) of a sub-step whose duration squared is @p squared_step.
void
project(const SoftBodyModel::Constraint& constraint,
        Vector3* positions,
        const float* inverse_masses,
        float squared_step) noexcept
{
    const float compliance = constraint.compliance / squared_step;
    const auto& [a, b, c, d] = constraint.particles;
    switch (constraint.kind)
    {
        case SoftBodyConstraintKind::STRETCH:
        case SoftBodyConstraintKind::SHEAR:
        case SoftBodyConstraintKind::BENDING:
        {
            const float weights = inverse_masses[a] + inverse_masses[b];
            const Vector3 offset = positions[a] - positions[b];
            const float size = length(offset);
            if (!(weights > 0.0F) || !(size > TINY))
            {
                return;
            }
            const Vector3 normal = offset / size;
            const float multiplier = -(size - constraint.rest) / (weights + compliance);
            positions[a] += normal * (inverse_masses[a] * multiplier);
            positions[b] -= normal * (inverse_masses[b] * multiplier);
            return;
        }
        case SoftBodyConstraintKind::VOLUME:
        {
            const Vector3 origin = positions[a];
            const std::array<Vector3, 4> gradients{Vector3{}, cross(positions[c] - origin, positions[d] - origin), cross(positions[d] - origin, positions[b] - origin),
                                                   cross(positions[b] - origin, positions[c] - origin)};
            std::array<Vector3, 4> all = gradients;
            all[0] = -(gradients[1] + gradients[2] + gradients[3]);
            float weights = compliance;
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                weights += inverse_masses[constraint.particles[corner]] * length_squared(all[corner]);
            }
            if (!(weights > TINY))
            {
                return;
            }
            const float multiplier = -(xpbd::six_volume(origin, positions[b], positions[c], positions[d]) - constraint.rest) / weights;
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                positions[constraint.particles[corner]] += all[corner] * (inverse_masses[constraint.particles[corner]] * multiplier);
            }
            return;
        }
        case SoftBodyConstraintKind::DIHEDRAL:
        {
            const xpbd::Dihedral angle = xpbd::dihedral(positions[a], positions[b], positions[c], positions[d]);
            if (!angle.valid)
            {
                return;
            }
            float weights = compliance;
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                weights += inverse_masses[constraint.particles[corner]] * length_squared(angle.gradients[corner]);
            }
            if (!(weights > TINY))
            {
                return;
            }
            const float multiplier = -xpbd::wrapped_angle(angle.angle - constraint.rest) / weights;
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                positions[constraint.particles[corner]] += angle.gradients[corner] * (inverse_masses[constraint.particles[corner]] * multiplier);
            }
            return;
        }
        case SoftBodyConstraintKind::TETHER:
            return;
    }
}

/// The colored constraints of a sub-step, for the scheduler's tasks.
struct ColorProjection
{
    const SoftBodyModel::Constraint* constraints = nullptr;
    Vector3* positions = nullptr;
    const float* inverse_masses = nullptr;
    float squared_step = 0.0F;
};

} // namespace

// ===================================================================================================== the solver's state

struct XpbdSoftBodySolver::State
{
    // The bodies by handle (pins name them so), the proxies of the soft body being stepped.
    std::vector<std::pair<RigidBodyHandle, std::uint32_t>> body_lookup{};
    std::vector<std::uint32_t> proxy_of_body{};
    std::vector<Proxy> proxies{};

    // The soft body being stepped.
    std::vector<Candidate> candidates{};
    std::vector<Pin> pins{};
    std::vector<float> inverse_masses{};
    std::vector<Vector3> start{};
    std::vector<Vector3> previous{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> self_pairs{};
    std::vector<std::uint32_t> cell_starts{};
    std::vector<std::uint32_t> cell_entries{};

    // What the step did to the bodies (by body index), then of those it touched.
    std::vector<BodyCorrection> body_corrections{};
    std::vector<std::uint8_t> body_touched{};
    std::vector<BodyCorrection> corrections{};
    std::uint32_t contacts = 0;

    /// The proxy of body @p index, made for this soft body's step when needed.
    auto proxy_for(std::span<const CoupledBody> bodies,
                   std::uint32_t index) -> std::uint32_t;

    /// The index of the body @p handle names, or NONE.
    [[nodiscard]] auto find_body(RigidBodyHandle handle) const noexcept -> std::uint32_t;

    /// Steps one soft body.
    void step_soft_body(float seconds,
                        Vector3 gravity,
                        const SimulatedSoftBody& soft_body,
                        std::span<const CoupledBody> bodies,
                        std::span<const CoupledCollider> colliders,
                        Integrator& integrator,
                        TaskScheduler* scheduler);

    /// The colliders' parts the soft body may touch (a compound's leaves each), into candidates.
    void gather_candidates(const SimulatedSoftBody& soft_body,
                           const Aabb& reach,
                           std::span<const CoupledBody> bodies,
                           std::span<const CoupledCollider> colliders);

    /// The pairs of its particles that may touch during the step (not those that start closer than two radii).
    void gather_self_pairs(const SimulatedSoftBody& soft_body,
                           float distance);
};

auto
XpbdSoftBodySolver::State::proxy_for(std::span<const CoupledBody> bodies,
                                     std::uint32_t index) -> std::uint32_t
{
    if (proxy_of_body[index] != NONE)
    {
        return proxy_of_body[index];
    }

    // Dynamic bodies with mass take corrections; the others are followed as they move.
    const CoupledBody& body = bodies[index];
    Proxy proxy{.body = index, .start = body.start_pose, .end = body.end_pose};
    if (body.kind == BodyKind::DYNAMIC && body.mass.mass > 0.0F)
    {
        proxy.inverse_mass = 1.0F / body.mass.mass;
        proxy.local_center = body.mass.center_of_mass;
        if (std::abs(determinant(body.mass.inertia)) > std::numeric_limits<float>::min())
        {
            proxy.local_inverse_inertia = inverse(body.mass.inertia);
        }
        const auto free = [&](MotionLock lock) { return has_any(body.locks, lock) ? 0.0F : 1.0F; };
        proxy.linear_mask = {.x = free(MotionLock::LINEAR_X), .y = free(MotionLock::LINEAR_Y), .z = free(MotionLock::LINEAR_Z)};
        proxy.angular_mask = {.x = free(MotionLock::ANGULAR_X), .y = free(MotionLock::ANGULAR_Y), .z = free(MotionLock::ANGULAR_Z)};
    }
    place(proxy, 0.0F);
    proxy.previous_pose = proxy.pose;
    proxy_of_body[index] = static_cast<std::uint32_t>(proxies.size());
    proxies.push_back(proxy);
    return proxy_of_body[index];
}

auto
XpbdSoftBodySolver::State::find_body(RigidBodyHandle handle) const noexcept -> std::uint32_t
{
    const auto found = std::ranges::lower_bound(body_lookup, handle, {}, &std::pair<RigidBodyHandle, std::uint32_t>::first);
    return found != body_lookup.end() && found->first == handle ? found->second : NONE;
}

void
XpbdSoftBodySolver::State::gather_candidates(const SimulatedSoftBody& soft_body,
                                             const Aabb& reach,
                                             std::span<const CoupledBody> bodies,
                                             std::span<const CoupledCollider> colliders)
{
    const SoftBodyDescription& settings = *soft_body.settings;
    const auto add = [&](this const auto& self, const Shape& shape, const Transform& pose, const CoupledCollider& collider) -> void {
        if (const CompoundShape* compound = shape.get_if<CompoundShape>())
        {
            for (const CompoundChild& child : compound->children())
            {
                self(*child.shape, pose * child.pose, collider);
            }
            return;
        }
        const CoupledBody& body = bodies[collider.body];
        const Aabb swept = shape.bounds(body.start_pose * pose).merged(shape.bounds(body.end_pose * pose));
        if (!intersects(swept, reach))
        {
            return;
        }
        candidates.push_back({.shape = &shape,
                              .pose = pose,
                              .proxy = proxy_for(bodies, collider.body),
                              .static_friction = square_root(settings.friction * collider.friction.static_coefficient),
                              .dynamic_friction = square_root(settings.friction * collider.friction.dynamic_coefficient)});
    };
    for (const CoupledCollider& collider : colliders)
    {
        if (collider.handle && collider.shape != nullptr && collider.body < bodies.size() && bodies[collider.body].handle && should_collide(settings.filter, collider.filter))
        {
            add(*collider.shape, collider.pose, collider);
        }
    }
}

void
XpbdSoftBodySolver::State::gather_self_pairs(const SimulatedSoftBody& soft_body,
                                             float distance)
{
    // A hashed grid of cells of the query distance over the particles (Teschner et al.'s hash).
    const std::span<const Vector3> positions = soft_body.positions;
    const std::span<const Vector3> rest = soft_body.model->rest_positions();
    const auto count = static_cast<std::uint32_t>(positions.size());
    const std::uint32_t table = std::max(1U, 2 * count);
    const float inverse_cell = 1.0F / distance;
    const auto cell_of = [&](Vector3 position) {
        return std::array<std::int32_t, 3>{static_cast<std::int32_t>(std::floor(position.x * inverse_cell)), static_cast<std::int32_t>(std::floor(position.y * inverse_cell)),
                                           static_cast<std::int32_t>(std::floor(position.z * inverse_cell))};
    };
    const auto hash = [table](std::int32_t x, std::int32_t y, std::int32_t z) {
        const std::uint32_t mixed = (static_cast<std::uint32_t>(x) * 92837111U) ^ (static_cast<std::uint32_t>(y) * 689287499U) ^ (static_cast<std::uint32_t>(z) * 283923481U);
        return mixed % table;
    };
    cell_starts.assign(table + 1, 0);
    cell_entries.resize(count);
    for (const Vector3 position : positions)
    {
        const auto [x, y, z] = cell_of(position);
        ++cell_starts[hash(x, y, z) + 1];
    }
    std::partial_sum(cell_starts.begin(), cell_starts.end(), cell_starts.begin());
    for (std::uint32_t particle = 0; particle < count; ++particle)
    {
        const auto [x, y, z] = cell_of(positions[particle]);
        const std::uint32_t bucket = hash(x, y, z);
        cell_entries[cell_starts[bucket]++] = particle;
    }
    for (std::uint32_t bucket = table; bucket > 0; --bucket)
    {
        cell_starts[bucket] = cell_starts[bucket - 1];
    }
    cell_starts[0] = 0;

    // Pairs closer than the distance in the 27 cells around each particle, but those that start within two radii.
    self_pairs.clear();
    const float squared = distance * distance;
    const float radius = soft_body.settings->particle_radius;
    const float rest_squared = 4.0F * radius * radius * 1.0001F;
    for (std::uint32_t particle = 0; particle < count; ++particle)
    {
        const auto [x, y, z] = cell_of(positions[particle]);
        for (std::int32_t dz = -1; dz <= 1; ++dz)
        {
            for (std::int32_t dy = -1; dy <= 1; ++dy)
            {
                for (std::int32_t dx = -1; dx <= 1; ++dx)
                {
                    const std::uint32_t bucket = hash(x + dx, y + dy, z + dz);
                    for (std::uint32_t entry = cell_starts[bucket]; entry < cell_starts[bucket + 1]; ++entry)
                    {
                        const std::uint32_t other = cell_entries[entry];
                        if (other > particle && distance_squared(positions[particle], positions[other]) < squared
                            && distance_squared(rest[particle], rest[other]) > rest_squared)
                        {
                            self_pairs.emplace_back(particle, other);
                        }
                    }
                }
            }
        }
    }
    std::ranges::sort(self_pairs);
    const auto [last, end] = std::ranges::unique(self_pairs);
    self_pairs.erase(last, end);
}

void
XpbdSoftBodySolver::State::step_soft_body(float seconds,
                                          Vector3 gravity,
                                          const SimulatedSoftBody& soft_body,
                                          std::span<const CoupledBody> bodies,
                                          std::span<const CoupledCollider> colliders,
                                          Integrator& integrator,
                                          TaskScheduler* scheduler)
{
    const SoftBodyModel& model = *soft_body.model;
    const SoftBodyDescription& settings = *soft_body.settings;
    const std::span<Vector3> positions = soft_body.positions;
    const std::span<Vector3> velocities = soft_body.velocities;
    const auto count = static_cast<std::uint32_t>(positions.size());
    const std::uint32_t sub_steps = std::max(1U, settings.sub_steps);
    const float step = seconds / static_cast<float>(sub_steps);
    const float squared_step = step * step;
    const float radius = settings.particle_radius;
    const Vector3 acceleration = gravity * settings.gravity_scale;

    // Fresh proxies; the particles' inverse masses, rigid pins making theirs 0.
    for (const Proxy& proxy : proxies)
    {
        proxy_of_body[proxy.body] = NONE;
    }
    proxies.clear();
    candidates.clear();
    pins.clear();
    inverse_masses.assign(model.inverse_masses().begin(), model.inverse_masses().end());
    for (const ParticlePin& pin : settings.pins)
    {
        if (pin.particle >= count)
        {
            continue;
        }
        Pin used{.particle = pin.particle, .proxy = NONE, .point = pin.point, .compliance = pin.compliance, .kinematic = pin.compliance == 0.0F};
        if (pin.body)
        {
            const std::uint32_t body = find_body(pin.body);
            if (body == NONE)
            {
                continue;
            }
            used.proxy = proxy_for(bodies, body);
            used.kinematic = used.kinematic && !(proxies[used.proxy].inverse_mass > 0.0F);
        }
        if (used.kinematic)
        {
            inverse_masses[pin.particle] = 0.0F;
        }
        pins.push_back(used);
    }

    // How far particles may go in the step: what they and the pins' points may reach, for the colliders and the pairs.
    float fastest = 0.0F;
    Aabb reach = Aabb::empty();
    for (std::uint32_t particle = 0; particle < count; ++particle)
    {
        fastest = std::max(fastest, length(velocities[particle]));
        reach = reach.merged(positions[particle]);
    }
    for (const Pin& pin : pins)
    {
        if (pin.proxy != NONE)
        {
            reach = reach.merged(transform_point(proxies[pin.proxy].start, pin.point)).merged(transform_point(proxies[pin.proxy].end, pin.point));
        }
    }
    const float travel = fastest * seconds + length(acceleration) * seconds * seconds;
    reach = reach.expanded(travel + 2.0F * radius);
    gather_candidates(soft_body, reach, bodies, colliders);
    self_pairs.clear();
    if (settings.self_collision)
    {
        gather_self_pairs(soft_body, 2.0F * radius + 2.0F * travel);
    }

    // The sub-steps.
    start.resize(count);
    previous.resize(count);
    const std::span<const std::uint32_t> color_starts = model.color_starts();
    const bool parallel = scheduler != nullptr && scheduler->concurrency() > 1;
    const float damping = settings.damping >= 1.0F ? 0.0F : std::pow(1.0F - settings.damping, step);
    for (std::uint32_t sub_step = 0; sub_step < sub_steps; ++sub_step)
    {
        // The bodies where the backend's motion and the corrections so far take them at the sub-step's end.
        const float fraction = static_cast<float>(sub_step + 1) / static_cast<float>(sub_steps);
        for (Proxy& proxy : proxies)
        {
            proxy.previous_pose = proxy.pose;
            proxy.offset += proxy.linear_change * step;
            proxy.turn = turned(proxy.turn, proxy.angular_change * step);
            place(proxy, fraction);
        }

        // Prediction (the previous positions as the velocities give them, for Verlet); rigid pins hold their particles.
        std::ranges::copy(positions, start.begin());
        std::ranges::copy(positions, previous.begin());
        multiply_add(previous, velocities, -step);
        integrator.predict({.positions = positions,
                            .velocities = velocities,
                            .inverse_masses = inverse_masses,
                            .forces = {},
                            .previous_positions = previous,
                            .gravity = acceleration,
                            .force_field = nullptr},
                           step);
        for (const Pin& pin : pins)
        {
            if (pin.kinematic)
            {
                positions[pin.particle] = pin.proxy == NONE ? pin.point : transform_point(proxies[pin.proxy].pose, pin.point);
            }
        }

        // The colored constraints, a large color on the scheduler's threads.
        ColorProjection projection{.constraints = model.constraints().data(), .positions = positions.data(), .inverse_masses = inverse_masses.data(), .squared_step = squared_step};
        for (std::size_t color = 0; color + 1 < color_starts.size(); ++color)
        {
            const std::uint32_t first = color_starts[color];
            const std::uint32_t size = color_starts[color + 1] - first;
            if (parallel && size >= PARALLEL_COLOR_SIZE)
            {
                const ColorProjection* shared = &projection;
                scheduler->parallel_for(size, PARALLEL_RANGE, [shared, first](std::uint32_t begin, std::uint32_t end) {
                    for (std::uint32_t index = begin; index < end; ++index)
                    {
                        project(shared->constraints[first + index], shared->positions, shared->inverse_masses, shared->squared_step);
                    }
                });
                continue;
            }
            for (std::uint32_t index = first; index < first + size; ++index)
            {
                project(projection.constraints[index], projection.positions, projection.inverse_masses, squared_step);
            }
        }

        // Tethers move their particle back within reach of the anchor.
        for (const SoftBodyModel::Constraint& tether : model.tethers())
        {
            const std::uint32_t held = tether.particles[0];
            const Vector3 offset = positions[held] - positions[tether.particles[1]];
            const float size = length(offset);
            if (!(inverse_masses[held] > 0.0F) || !(size > tether.rest) || !(size > TINY))
            {
                continue;
            }
            const float multiplier = -(size - tether.rest) / (inverse_masses[held] + tether.compliance / squared_step);
            positions[held] += offset * (inverse_masses[held] * multiplier / size);
        }

        // Pins that are not rigid (compliant, or to a body the corrections move): shared with the body.
        for (const Pin& pin : pins)
        {
            if (pin.kinematic)
            {
                continue;
            }
            const Vector3 target = pin.proxy == NONE ? pin.point : transform_point(proxies[pin.proxy].pose, pin.point);
            const Vector3 offset = positions[pin.particle] - target;
            const float size = length(offset);
            if (!(size > TINY))
            {
                continue;
            }
            const Vector3 normal = offset / size;
            const float body_weight = pin.proxy == NONE ? 0.0F : inverse_mass_at(proxies[pin.proxy], target, normal);
            const float weights = inverse_masses[pin.particle] + body_weight + pin.compliance / squared_step;
            if (!(weights > TINY))
            {
                continue;
            }
            const float multiplier = -size / weights;
            positions[pin.particle] += normal * (inverse_masses[pin.particle] * multiplier);
            if (pin.proxy != NONE)
            {
                push(proxies[pin.proxy], normal * -multiplier, target, step);
            }
        }

        // Contacts with the colliders, with friction: each correction shared with the collider's body.
        std::uint32_t contacts_now = 0;
        for (const Candidate& candidate : candidates)
        {
            Proxy& proxy = proxies[candidate.proxy];
            Transform pose = proxy.pose * candidate.pose;
            Aabb box = candidate.shape->bounds(pose).expanded(radius);
            for (std::uint32_t particle = 0; particle < count; ++particle)
            {
                const float particle_weight = inverse_masses[particle];
                if (!(particle_weight > 0.0F) || !box.contains(positions[particle]))
                {
                    continue;
                }
                const std::optional<Touch> found = touch(*candidate.shape, pose, positions[particle], radius);
                if (!found)
                {
                    continue;
                }
                ++contacts_now;

                // Out along the normal. What was already inside where the body was at the sub-step's start moves the
                // particle's start too: an overlap the sub-step began with is undone without becoming velocity, for the
                // particle as for the body (pre-stabilization, Macklin et al. 2014).
                const float normal_weight = inverse_mass_at(proxy, found->point, found->normal);
                const float pushed = found->depth / (particle_weight + normal_weight);
                const float moved = particle_weight * pushed;
                const Transform back = proxy.previous_pose * inverse(proxy.pose);
                const float initial = radius - dot(start[particle] - transform_point(back, found->point), rotate(back.orientation, found->normal));
                const float settled = initial > 0.0F ? std::min(initial, moved) : 0.0F;
                positions[particle] += found->normal * moved;
                start[particle] += found->normal * settled;
                push(proxy, found->normal * -pushed, found->point, step, moved > 0.0F ? 1.0F - settled / moved : 1.0F);

                // Friction against the sliding relative to the body's surface: all of it while under the static
                // limit, else as much as the dynamic one allows.
                const Vector3 relative = (positions[particle] - start[particle]) - body_displacement(proxy, found->point);
                const Vector3 sliding = relative - found->normal * dot(relative, found->normal);
                const float slide = length(sliding);
                if (slide > TINY)
                {
                    const float removed = slide < candidate.static_friction * found->depth ? slide : std::min(candidate.dynamic_friction * found->depth, slide);
                    const Vector3 tangent = sliding / slide;
                    const float tangent_weight = inverse_mass_at(proxy, found->point, tangent);
                    const float held = removed / (particle_weight + tangent_weight);
                    positions[particle] -= tangent * (particle_weight * held);
                    push(proxy, tangent * held, found->point, step);
                }
                if (proxy.inverse_mass > 0.0F)
                {
                    pose = proxy.pose * candidate.pose;
                    box = candidate.shape->bounds(pose).expanded(radius);
                }
            }
        }
        if (sub_step + 1 == sub_steps)
        {
            contacts += contacts_now;
        }

        // The body's own particles apart by two radii.
        const float contact_distance = 2.0F * radius;
        for (const auto& [first, second] : self_pairs)
        {
            const float weights = inverse_masses[first] + inverse_masses[second];
            const Vector3 offset = positions[first] - positions[second];
            const float size = length(offset);
            if (!(weights > 0.0F) || !(size < contact_distance) || !(size > TINY))
            {
                continue;
            }
            const Vector3 normal = offset / size;
            const float multiplier = (contact_distance - size) / weights;
            positions[first] += normal * (inverse_masses[first] * multiplier);
            positions[second] -= normal * (inverse_masses[second] * multiplier);
        }

        // The velocities from the positions' change, damped.
        const float rate = 1.0F / step;
        for (std::uint32_t particle = 0; particle < count; ++particle)
        {
            velocities[particle] = (positions[particle] - start[particle]) * (rate * damping);
        }
    }

    // What the corrections did to each dynamic body (several soft bodies' add up).
    for (const Proxy& proxy : proxies)
    {
        if (proxy.inverse_mass > 0.0F)
        {
            BodyCorrection& correction = body_corrections[proxy.body];
            correction.linear_impulse += proxy.linear_impulse;
            correction.angular_impulse += proxy.angular_impulse;
            correction.translation += proxy.offset;
            correction.rotation = normalized(proxy.turn * correction.rotation);
            body_touched[proxy.body] = 1;
        }
    }
}

// ===================================================================================================== XpbdSoftBodySolver

XpbdSoftBodySolver::XpbdSoftBodySolver()
    : m_state{std::make_unique<State>()}
{
}

XpbdSoftBodySolver::~XpbdSoftBodySolver() = default;

void
XpbdSoftBodySolver::step(float seconds,
                         Vector3 gravity,
                         std::span<const SimulatedSoftBody> soft_bodies,
                         std::span<const CoupledBody> bodies,
                         std::span<const CoupledCollider> colliders,
                         Integrator& integrator,
                         TaskScheduler* scheduler)
{
    State& state = *m_state;

    // The inputs checked.
    if (!(seconds > 0.0F) || !std::isfinite(seconds))
    {
        reject_step(std::format("the step of {} seconds is not finite and positive", seconds), "step by a positive time");
    }
    for (std::size_t index = 0; index < soft_bodies.size(); ++index)
    {
        const SimulatedSoftBody& soft_body = soft_bodies[index];
        if (soft_body.model == nullptr || soft_body.settings == nullptr)
        {
            reject_step(std::format("soft body {} has no model or no settings", index), "give each soft body its model and its description");
        }
        const std::uint32_t count = soft_body.model->particle_count();
        if (soft_body.positions.size() != count || soft_body.velocities.size() != count)
        {
            reject_step(std::format("soft body {} has {} positions and {} velocities for {} particles", index, soft_body.positions.size(), soft_body.velocities.size(), count),
                        "give one position and one velocity per particle");
        }
    }

    // The bodies by handle; nothing given to any yet.
    state.body_lookup.clear();
    for (std::uint32_t index = 0; index < bodies.size(); ++index)
    {
        if (bodies[index].handle)
        {
            state.body_lookup.emplace_back(bodies[index].handle, index);
        }
    }
    std::ranges::sort(state.body_lookup);
    for (const Proxy& proxy : state.proxies)
    {
        if (proxy.body < state.proxy_of_body.size())
        {
            state.proxy_of_body[proxy.body] = NONE;
        }
    }
    state.proxies.clear();
    state.proxy_of_body.assign(bodies.size(), NONE);
    state.body_corrections.assign(bodies.size(), BodyCorrection{});
    state.body_touched.assign(bodies.size(), 0);
    state.corrections.clear();
    state.contacts = 0;

    // Each soft body over the whole step, one after the other; then the corrections, in the bodies' order.
    for (const SimulatedSoftBody& soft_body : soft_bodies)
    {
        state.step_soft_body(seconds, gravity, soft_body, bodies, colliders, integrator, scheduler);
    }
    for (std::uint32_t index = 0; index < bodies.size(); ++index)
    {
        if (state.body_touched[index] != 0)
        {
            BodyCorrection correction = state.body_corrections[index];
            correction.body = bodies[index].handle;
            state.corrections.push_back(correction);
        }
    }
}

auto
XpbdSoftBodySolver::corrections() const noexcept -> std::span<const BodyCorrection>
{
    return m_state->corrections;
}

auto
XpbdSoftBodySolver::contact_count() const noexcept -> std::uint32_t
{
    return m_state->contacts;
}

} // namespace gfx::physics
