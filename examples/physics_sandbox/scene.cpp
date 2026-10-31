// The sandbox's scene: the world, fixed steps on the world's thread with interpolated poses, spawning, shooting and
// removing bodies, the showcase.
module physics_sandbox.scene;

import std;
import gfx.physics;
import physics_sandbox.renderer; // INSTANCE_GRID

namespace physics_sandbox
{

using namespace gfx;
using namespace gfx::physics;
namespace physics = gfx::physics;

namespace
{

/// A real-time pause longer than this (a breakpoint, a dragged window) is not caught up.
constexpr double LONGEST_FRAME = 0.25;

/// The corners of a frustum of a square pyramid (a plinth): a base of half side 0.5, a top of half side 0.25, 0.6 high.
consteval auto
plinth_corners() -> std::array<Vector3, 8>
{
    std::array<Vector3, 8> corners{};
    for (std::size_t index = 0; index < 4; ++index)
    {
        const float x = (index & 1U) != 0 ? 1.0F : -1.0F;
        const float z = (index & 2U) != 0 ? 1.0F : -1.0F;
        corners[index] = {.x = 0.5F * x, .y = -0.3F, .z = 0.5F * z};
        corners[index + 4] = {.x = 0.25F * x, .y = 0.3F, .z = 0.25F * z};
    }
    return corners;
}

/// A hull built at compile time (quickhull in a constant expression).
constexpr auto PLINTH = convex_hull<8, 6, 24>(plinth_corners());

/// The palette of spawned and showcase bodies.
constexpr std::array<DebugColor, 8> PALETTE{DebugColor{.red = 230, .green = 120, .blue = 60}, DebugColor{.red = 70, .green = 150, .blue = 220},
                                            DebugColor{.red = 110, .green = 190, .blue = 90}, DebugColor{.red = 225, .green = 190, .blue = 70},
                                            DebugColor{.red = 170, .green = 100, .blue = 200}, DebugColor{.red = 80, .green = 190, .blue = 180},
                                            DebugColor{.red = 220, .green = 90, .blue = 120}, DebugColor{.red = 200, .green = 200, .blue = 205}};

/// The color of shot balls.
constexpr DebugColor SHOT_COLOR{.red = 245, .green = 245, .blue = 240};

/// Densities by spawn kind (crates lighter, like the pyramid's).
constexpr std::array<float, SPAWN_KIND_COUNT> SPAWN_DENSITIES{1000.0F, 1000.0F, 1000.0F, 1000.0F, 400.0F, 1000.0F, 1000.0F};

[[nodiscard]] auto
make_library() -> ShapeLibrary
{
    ShapeLibrary library;
    library.spawned[std::to_underlying(SpawnKind::SPHERE)] = make_shape(SphereShape{.radius = 0.4F});
    library.spawned[std::to_underlying(SpawnKind::CAPSULE)] = make_shape(CapsuleShape{.half_height = 0.35F, .radius = 0.25F});
    library.spawned[std::to_underlying(SpawnKind::CYLINDER)] = make_shape(CylinderShape{.half_height = 0.3F, .radius = 0.35F});
    library.spawned[std::to_underlying(SpawnKind::CONE)] = make_shape(ConeShape{.half_height = 0.4F, .radius = 0.4F});
    library.spawned[std::to_underlying(SpawnKind::CRATE)] = make_shape(BoxShape{.half_extents = {.x = 0.4F, .y = 0.4F, .z = 0.4F}});
    library.spawned[std::to_underlying(SpawnKind::PLINTH)] = make_shape(HullShape(PLINTH));
    library.spawned[std::to_underlying(SpawnKind::DUMBBELL)] = make_shape(CompoundShape(
        {CompoundChild{.shape = make_shape(SphereShape{.radius = 0.3F}), .pose = {.position = {.x = -0.45F}}},
         CompoundChild{.shape = make_shape(SphereShape{.radius = 0.3F}), .pose = {.position = {.x = 0.45F}}},
         CompoundChild{.shape = make_shape(CapsuleShape{.half_height = 0.3F, .radius = 0.1F}),
                       .pose = {.orientation = Quaternion::from_axis_angle({.z = 1.0F}, std::numbers::pi_v<float> / 2)}}}));
    library.ball = make_shape(SphereShape{.radius = 0.25F});
    return library;
}

} // namespace

Scene::Scene(PhysicsBackendKind backend,
             physics::TaskScheduler& scheduler)
    : m_backend{backend}
    , m_shapes{make_library()}
    , m_world{create_physics_world({.asynchronous_stepping = true, .task_scheduler = &scheduler, .debug_name = "sandbox"}, backend)}
{
}

Scene::~Scene()
{
    // The step in flight ends before the bodies go.
    if (m_world != nullptr && m_world->is_stepping())
    {
        m_world->wait_step();
    }
}

auto
Scene::add_body(const RigidBodyDescription& description,
                std::span<const ColliderSpecification> colliders,
                DebugColor color,
                std::uint32_t flags) -> SceneBody&
{
    SceneBody& body = m_bodies.emplace_back();
    body.body = m_world->create_rigid_body(description);
    body.kind = description.kind;
    body.color = color;
    body.flags = flags;
    body.previous = body.current = description.pose;
    try
    {
        for (const ColliderSpecification& specification : colliders)
        {
            body.colliders.push_back({.collider = body.body->add_collider({.shape = specification.shape,
                                                                           .pose = specification.pose,
                                                                           .material = specification.material,
                                                                           .density = specification.density}),
                                      .shape = specification.shape,
                                      .pose = specification.pose});
        }
    }
    catch (...)
    {
        // A collider refused: no body.
        m_bodies.pop_back();
        throw;
    }
    return body;
}

auto
Scene::add_soft_body(const SoftBodyDescription& description,
                     std::vector<std::uint32_t> surface,
                     DebugColor color) -> SceneSoftBody&
{
    SceneSoftBody& soft_body = m_soft_bodies.emplace_back();
    try
    {
        soft_body.body = m_world->create_soft_body(description);
    }
    catch (...)
    {
        m_soft_bodies.pop_back();
        throw;
    }
    soft_body.surface = std::move(surface);
    if (soft_body.surface.empty())
    {
        soft_body.particle = make_shape(SphereShape{.radius = description.particle_radius});
    }
    soft_body.color = color;
    soft_body.current.resize(description.particles.size());
    std::ranges::transform(description.particles, soft_body.current.begin(), &SoftBodyParticle::position);
    soft_body.previous = soft_body.current;
    return soft_body;
}

auto
Scene::add_joint(const JointDescription& description) -> SceneJoint&
{
    std::unique_ptr<Joint> joint = m_world->create_joint(description);
    SceneJoint& made = m_joints.emplace_back();
    made.kind = joint_kind(description);
    std::tie(made.first, made.second) = joint_bodies(description);

    // The anchors and the axis in the bodies' frames, from their poses now (the world's frame for no second body).
    const SceneBody* first = find(made.first);
    const SceneBody* second = made.second.is_valid() ? find(made.second) : nullptr;
    const Transform first_pose = first != nullptr ? first->current : Transform{};
    const Transform second_pose = second != nullptr ? second->current : Transform{};
    const auto local = [&](Vector3 anchor) {
        made.anchor_first = transform_point(inverse(first_pose), anchor);
        made.anchor_second = transform_point(inverse(second_pose), anchor);
    };
    const auto local_axis = [&](Vector3 axis) { made.axis = rotate(conjugate(first_pose.orientation), axis); };
    std::visit(
        [&](const auto& joint_description) {
            using Description = std::remove_cvref_t<decltype(joint_description)>;
            if constexpr (std::same_as<Description, DistanceJointDescription>)
            {
                made.anchor_first = joint_description.anchor_first;
                made.anchor_second = joint_description.anchor_second;
                made.cable = true;
            }
            else if constexpr (std::same_as<Description, SixDegreesOfFreedomJointDescription>)
            {
                local(joint_description.frame.position);
                local_axis(rotate(joint_description.frame.orientation, Vector3{.x = 1.0F}));
            }
            else
            {
                local(joint_description.anchor);
                if constexpr (requires { joint_description.axis; })
                {
                    local_axis(joint_description.axis);
                }
            }
        },
        description);
    made.joint = std::move(joint);
    return made;
}

auto
Scene::add_trigger(std::string name,
                   const std::shared_ptr<const Shape>& shape,
                   const Transform& pose,
                   DebugColor color) -> SceneTrigger&
{
    std::unique_ptr<Trigger> trigger = m_world->create_trigger({.shape = shape, .pose = pose});
    return m_triggers.emplace_back(SceneTrigger{.trigger = std::move(trigger), .name = std::move(name), .shape = shape, .pose = pose, .color = color});
}

void
Scene::add_note(std::string note)
{
    m_notes.push_back(std::move(note));
}

auto
Scene::spawn(SpawnKind kind,
             const Transform& pose,
             Vector3 velocity) -> SceneBody&
{
    const std::size_t index = std::to_underlying(kind);
    const std::array collider{ColliderSpecification{.shape = m_shapes.spawned[index], .density = SPAWN_DENSITIES[index]}};
    return add_body({.pose = pose, .linear_velocity = velocity}, collider, PALETTE[m_spawned++ % PALETTE.size()]);
}

auto
Scene::shoot(Vector3 origin,
             Vector3 velocity) -> SceneBody&
{
    // A heavy ball that does not pass through thin bodies; the oldest goes when there are too many.
    if (m_shots.size() >= MAXIMUM_SHOTS)
    {
        static_cast<void>(remove_body(m_shots.front()));
    }
    const std::array collider{ColliderSpecification{.shape = m_shapes.ball, .material = {.restitution = 0.2F}, .density = 3000.0F}};
    SceneBody& ball = add_body({.pose = {.position = origin}, .linear_velocity = velocity, .motion_quality = MotionQuality::CONTINUOUS}, collider,
                               SHOT_COLOR);
    m_shots.push_back(ball.body->handle());
    return ball;
}

auto
Scene::remove_body(RigidBodyHandle handle) -> bool
{
    const auto found = std::ranges::find_if(m_bodies, [handle](const SceneBody& body) { return body.body->handle() == handle; });
    if (found == m_bodies.end())
    {
        return false;
    }
    // Its colliders leave the triggers (their EXIT events are not reported once they are gone).
    for (const SceneCollider& collider : found->colliders)
    {
        for (SceneTrigger& trigger : m_triggers)
        {
            std::erase(trigger.inside, collider.collider->handle());
        }
    }
    std::erase(m_shots, handle);
    m_bodies.erase(found);
    return true;
}

auto
Scene::find(RigidBodyHandle handle) noexcept -> SceneBody*
{
    const auto found = std::ranges::find_if(m_bodies, [handle](const SceneBody& body) { return body.body->handle() == handle; });
    return found == m_bodies.end() ? nullptr : &*found;
}

auto
Scene::find(RigidBodyHandle handle) const noexcept -> const SceneBody*
{
    const auto found = std::ranges::find_if(m_bodies, [handle](const SceneBody& body) { return body.body->handle() == handle; });
    return found == m_bodies.end() ? nullptr : &*found;
}

auto
Scene::body_of(ColliderHandle collider) const noexcept -> const SceneBody*
{
    for (const SceneBody& body : m_bodies)
    {
        if (std::ranges::any_of(body.colliders, [collider](const SceneCollider& item) { return item.collider->handle() == collider; }))
        {
            return &body;
        }
    }
    return nullptr;
}

void
Scene::advance(double seconds)
{
    // The step begun by the last call, then the steps due beyond one at once, then the next one on the world's thread.
    finish();
    m_accumulator += std::min(seconds, LONGEST_FRAME);
    while (m_accumulator >= 2.0 * STEP)
    {
        m_world->step(STEP);
        capture();
        m_accumulator -= STEP;
    }
    if (m_accumulator >= STEP)
    {
        m_world->begin_step(STEP);
        m_accumulator -= STEP;
    }
    m_alpha = static_cast<float>(m_accumulator / STEP);
}

void
Scene::finish()
{
    if (m_world->is_stepping())
    {
        m_world->wait_step();
        capture();
    }
}

void
Scene::hold()
{
    // No step begins; what is drawn is the last completed step.
    finish();
    m_accumulator = 0.0;
    m_alpha = 1.0F;
}

void
Scene::step_once()
{
    finish();
    m_world->begin_step(STEP);
    m_accumulator = 0.0;
    m_alpha = 1.0F;
}

auto
Scene::drawn_pose(const SceneBody& body) const noexcept -> Transform
{
    return interpolate(body.previous, body.current, m_alpha);
}

void
Scene::drawn_positions(const SceneSoftBody& soft_body,
                       std::vector<Vector3>& output) const
{
    output.resize(soft_body.current.size());
    for (std::size_t index = 0; index < output.size(); ++index)
    {
        output[index] = soft_body.previous[index] + (soft_body.current[index] - soft_body.previous[index]) * m_alpha;
    }
}

void
Scene::capture()
{
    // The bodies' poses and the soft bodies' particles, the last two of each.
    for (SceneBody& body : m_bodies)
    {
        const RigidBodyState state = body.body->state();
        body.previous = body.current;
        body.current = state.pose;
        body.awake = state.awake;
    }
    for (SceneSoftBody& soft_body : m_soft_bodies)
    {
        std::swap(soft_body.previous, soft_body.current);
        soft_body.current.resize(soft_body.body->particle_count());
        static_cast<void>(soft_body.body->positions(soft_body.current));
    }

    // The contacts kept until clear_events(); what entered and left the triggers.
    const PhysicsEvents events = m_world->events();
    m_contacts.insert(m_contacts.end(), events.contacts.begin(), events.contacts.end());
    for (const TriggerEvent& event : events.triggers)
    {
        const auto found = std::ranges::find_if(m_triggers, [&event](const SceneTrigger& trigger) { return trigger.trigger->handle() == event.trigger; });
        if (found == m_triggers.end())
        {
            continue;
        }
        if (event.kind == TriggerEventKind::ENTER && !std::ranges::contains(found->inside, event.collider))
        {
            found->inside.push_back(event.collider);
            ++found->entered;
        }
        else if (event.kind == TriggerEventKind::EXIT)
        {
            std::erase(found->inside, event.collider);
        }
    }
    for (const JointEvent& event : events.joints)
    {
        for (SceneJoint& joint : m_joints)
        {
            if (event.kind == JointEventKind::BROKEN && event.joint.is_valid() && joint.joint->handle() == event.joint)
            {
                joint.broken = true;
            }
        }
    }
}

auto
palette(std::size_t index) noexcept -> DebugColor
{
    return PALETTE[index % PALETTE.size()];
}

void
add_floor(Scene& scene)
{
    const auto floor = make_shape(BoxShape{.half_extents = {.x = 40.0F, .y = 0.5F, .z = 40.0F}});
    const std::array floor_collider{ColliderSpecification{.shape = floor, .material = {.friction = {.static_coefficient = 0.7F, .dynamic_coefficient = 0.7F}}}};
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, floor_collider,
                                     {.red = 150, .green = 152, .blue = 158}, INSTANCE_GRID));
}

auto
build_showcase(Scene& scene) -> Showcase
{
    Showcase showcase;
    const ShapeLibrary& shapes = scene.shapes();

    // The floor, then a pyramid of crates (one shared shape).
    add_floor(scene);
    const std::array crate_collider{ColliderSpecification{.shape = shapes.spawned[std::to_underlying(SpawnKind::CRATE)], .density = 400.0F}};
    constexpr int LEVELS = 6;
    for (int level = 0; level < LEVELS; ++level)
    {
        for (int column = 0; column < LEVELS - level; ++column)
        {
            const float x = -6.0F + static_cast<float>(column) * 0.82F + static_cast<float>(level) * 0.41F;
            const float y = 0.4F + static_cast<float>(level) * 0.8F;
            const SceneBody& crate = scene.add_body({.pose = {.position = {.x = x, .y = y}}}, crate_collider, PALETTE[static_cast<std::size_t>(level) % 4]);
            showcase.pyramid.push_back(crate.body->handle());
            if (level == 2 && column == 1)
            {
                showcase.middle_crate = crate.body->handle();
            }
        }
    }
    showcase.top_crate = showcase.pyramid.back();

    // Every convex shape kind but crates, three of each, falling from staggered heights.
    constexpr std::array FALLING{SpawnKind::SPHERE, SpawnKind::CAPSULE, SpawnKind::CYLINDER, SpawnKind::CONE, SpawnKind::PLINTH, SpawnKind::DUMBBELL};
    for (std::size_t kind = 0; kind < FALLING.size(); ++kind)
    {
        const std::array collider{ColliderSpecification{.shape = shapes.spawned[std::to_underlying(FALLING[kind])]}};
        for (int copy = 0; copy < 3; ++copy)
        {
            const Vector3 position{.x = 1.0F + static_cast<float>(kind) * 1.2F, .y = 2.0F + static_cast<float>(copy) * 1.5F + static_cast<float>(kind % 2) * 0.6F,
                                   .z = -1.5F + static_cast<float>(copy) * 1.5F};
            const Quaternion tilt = Quaternion::from_axis_angle(normalized(Vector3{.x = 1.0F, .y = 0.3F, .z = static_cast<float>(copy)}), 0.4F * static_cast<float>(copy + 1));
            const SceneBody& body = scene.add_body({.pose = {.position = position, .orientation = tilt}}, collider,
                                                   PALETTE[(kind + 4 + static_cast<std::size_t>(copy)) % PALETTE.size()]);
            if (FALLING[kind] == SpawnKind::SPHERE && copy == 0)
            {
                showcase.sphere = body.body->handle();
            }
        }
    }

    // A static ramp, and balls on its top end that roll down.
    const std::array ramp{ColliderSpecification{.shape = make_shape(BoxShape{.half_extents = {.x = 3.0F, .y = 0.15F, .z = 1.2F}})}};
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC,
                                      .pose = {.position = {.x = -2.0F, .y = 1.2F, .z = -5.0F},
                                               .orientation = Quaternion::from_axis_angle({.z = 1.0F}, 0.35F)}},
                                     ramp, {.red = 120, .green = 124, .blue = 135}));
    const std::array ball{ColliderSpecification{.shape = shapes.spawned[std::to_underlying(SpawnKind::SPHERE)], .material = {.restitution = 0.3F}}};
    for (int index = 0; index < 3; ++index)
    {
        static_cast<void>(scene.add_body({.pose = {.position = {.x = 0.2F, .y = 3.0F + static_cast<float>(index), .z = -5.6F + static_cast<float>(index) * 0.6F}}},
                                         ball, PALETTE[static_cast<std::size_t>(index) + 1]));
    }

    // A static mound: a height field of 12 × 12 samples, 0.5 m apart (in its body's frame).
    std::array<float, 144> heights{};
    for (std::size_t row = 0; row < 12; ++row)
    {
        for (std::size_t column = 0; column < 12; ++column)
        {
            const float u = (static_cast<float>(column) - 5.5F) / 5.5F;
            const float v = (static_cast<float>(row) - 5.5F) / 5.5F;
            heights[row * 12 + column] = 1.2F * std::max(0.0F, 1.0F - (u * u + v * v)) + 0.05F;
        }
    }
    const auto mound = make_shape(HeightFieldShape({.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 12, 12},
                                                    .scale = {.x = 0.5F, .y = 1.0F, .z = 0.5F}}));
    const std::array mound_collider{ColliderSpecification{.shape = mound}};
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = 5.0F, .z = -8.0F}}}, mound_collider,
                                     {.red = 140, .green = 160, .blue = 110}));
    return showcase;
}

} // namespace physics_sandbox
