// The sandbox's interaction: the camera rig, picking and dragging by a spring at the grabbed point, shooting, spawning,
// removing; the scripted run and its checks.
module physics_sandbox.interaction;

import std;
import gfx.camera;
import gfx.physics;
import physics_sandbox.scene;

namespace physics_sandbox
{

using namespace gfx;
using namespace gfx::physics;
namespace camera = gfx::camera;

namespace
{

constexpr float FLY_SPEED = 8.0F;                     ///< Meters per second.
constexpr float SHOT_SPEED = 60.0F;                   ///< Meters per second.
constexpr float DRAG_FREQUENCY = 4.0F;                ///< The drag spring's, in hertz.
constexpr float DRAG_DAMPING_RATIO = 1.0F;            ///< Critically damped.
constexpr float LARGEST_DRAG_ACCELERATION = 400.0F;   ///< Meters per second squared (about 40 g).
constexpr float SPIN_DAMPING = 5.0F;                  ///< Per second, on a dragged body's angular velocity.
constexpr float PICK_DISTANCE = 1000.0F;              ///< How far the pointer's ray reaches.

/// The pixel where @p camera shows @p point (the origin when it is behind the camera).
[[nodiscard]] auto
pixel_of(const camera::Camera& camera,
         Vector3 point,
         const camera::ScreenViewport& viewport) noexcept -> Vector2
{
    const std::optional<Vector3> screen = camera.world_to_screen(point, viewport);
    return screen ? Vector2{.x = screen->x, .y = screen->y} : Vector2{};
}

[[nodiscard]] auto
viewport_of(Extent framebuffer) noexcept -> camera::ScreenViewport
{
    return {.width = static_cast<float>(std::max(framebuffer.width, 1U)), .height = static_cast<float>(std::max(framebuffer.height, 1U))};
}

// The scripted run's frames.
constexpr std::uint64_t GRAB_FRAME = 60;
constexpr std::uint64_t DRAGGED_FRAMES = 60;   ///< The pointer moves over them, then stays.
constexpr std::uint64_t RELEASE_FRAME = 150;
constexpr std::uint64_t SHOT_FRAME = 180;
constexpr std::uint64_t SPAWN_FRAME = 270;
constexpr std::uint64_t REMOVE_FRAME = 300;
constexpr std::uint64_t FLY_START_FRAME = 330;
constexpr std::uint64_t FLY_END_FRAME = 390;

} // namespace

// ===================================================================================================== camera rig

CameraRig::CameraRig(const camera::OrbitControllerDescription& orbit)
    : m_orbit{camera::create_camera_controller(orbit)}
    , m_fly{camera::create_camera_controller(camera::FirstPersonControllerDescription{.speed = FLY_SPEED})}
{
}

void
CameraRig::update(camera::Camera& camera,
                  const FrameCommands& commands,
                  float seconds)
{
    // Switching: flying starts from where the camera is; orbiting again turns around the point ahead of it.
    if (commands.toggle_fly)
    {
        if (m_mode == CameraMode::ORBIT)
        {
            m_mode = CameraMode::FLY;
        }
        else
        {
            auto orbit = std::get<camera::OrbitControllerDescription>(m_orbit->description());
            const YawPitchRoll angles = camera.yaw_pitch_roll();
            orbit.yaw = angles.yaw;
            orbit.pitch = std::clamp(angles.pitch, orbit.minimum_pitch, orbit.maximum_pitch);
            orbit.target = camera.position() + camera.forward() * orbit.distance;
            m_orbit->set_description(orbit);
            m_mode = CameraMode::ORBIT;
        }
    }

    // The orbit pans by fractions of its distance (seconds 1: the commands are whole movements), flying over time.
    if (m_mode == CameraMode::ORBIT)
    {
        m_orbit->update(camera, {.movement = {.x = commands.pan.x, .y = commands.pan.y}, .look = commands.look, .zoom = commands.zoom, .seconds = 1.0F});
    }
    else
    {
        m_fly->update(camera, {.movement = commands.fly, .look = commands.look, .seconds = seconds});
    }
}

// ===================================================================================================== interaction

void
Interaction::update(const FrameCommands& commands,
                    const camera::Camera& camera,
                    Extent framebuffer)
{
    Scene& scene = *m_scene;
    PhysicsWorld& world = scene.world();
    if (m_dragged.is_valid() && scene.find(m_dragged) == nullptr)
    {
        release();
    }
    const Ray ray = camera.screen_to_world_ray(commands.pointer, viewport_of(framebuffer));

    // What the pointer points at (dynamic bodies can be grabbed and removed).
    m_hovered = {};
    m_hovered_point.reset();
    std::optional<RayHit> hit{};
    if (commands.pointer_in_scene)
    {
        const QueryResult<RayHit> result = world.ray_cast(ray, PICK_DISTANCE);
        if (result)
        {
            hit = *result;
            m_hovered_point = result->point;
            const SceneBody* body = scene.find(result->body);
            if (body != nullptr && body->kind == BodyKind::DYNAMIC)
            {
                m_hovered = result->body;
            }
        }
    }

    // Grabbing keeps the point grabbed (in the body's frame) and its distance along the ray.
    if (commands.grab_pressed && m_hovered.is_valid() && !m_dragged.is_valid())
    {
        const SceneBody& body = *scene.find(m_hovered);
        m_dragged = m_hovered;
        m_anchor = transform_point(inverse(body.current), hit->point);
        m_depth = hit->distance;
        ++m_statistics.grabs;
    }
    if (m_dragged.is_valid() && (commands.grab_released || !commands.grab_held))
    {
        release();
    }

    // Dragging: a critically damped spring of 4 Hz pulls the grabbed point toward the pointer's ray at its distance, as a
    // force at that point scaled by the body's mass (so every body follows alike) that also carries its weight; the spin
    // is damped.
    if (m_dragged.is_valid())
    {
        SceneBody& body = *scene.find(m_dragged);
        m_depth = std::max(0.5F, m_depth + commands.push);
        m_target = ray.origin + ray.direction * m_depth;
        const RigidBodyState state = body.body->state();
        const Vector3 anchor = transform_point(state.pose, m_anchor);
        const Vector3 point_velocity = state.linear_velocity + cross(state.angular_velocity, anchor - state.pose.position);
        const MassProperties mass = body.body->mass_properties();
        constexpr float ANGULAR_FREQUENCY = 2.0F * std::numbers::pi_v<float> * DRAG_FREQUENCY;
        Vector3 acceleration = (m_target - anchor) * (ANGULAR_FREQUENCY * ANGULAR_FREQUENCY) - point_velocity * (2.0F * DRAG_DAMPING_RATIO * ANGULAR_FREQUENCY)
                             - world.description().gravity;
        if (length(acceleration) > LARGEST_DRAG_ACCELERATION)
        {
            acceleration = normalized(acceleration) * LARGEST_DRAG_ACCELERATION;
        }
        body.body->apply_force_at(acceleration * mass.mass, anchor);
        const float moment = (mass.inertia.columns[0].x + mass.inertia.columns[1].y + mass.inertia.columns[2].z) / 3.0F;
        body.body->apply_torque(state.angular_velocity * (-SPIN_DAMPING * moment));
    }

    // Shooting along the ray, from just ahead of the camera.
    if (commands.shoot)
    {
        m_last_shot = scene.shoot(ray.origin + ray.direction * 0.6F, ray.direction * SHOT_SPEED).body->handle();
        ++m_statistics.shots;
    }

    // Spawning above the surface pointed at (or ahead along the ray when it points at nothing).
    if (commands.spawn)
    {
        const Vector3 position = hit ? hit->point + hit->normal * 0.9F : ray.origin + ray.direction * 10.0F;
        m_last_spawned = scene.spawn(*commands.spawn, {.position = position}, {}).body->handle();
        ++m_statistics.spawned;
    }

    // Removing the body pointed at.
    if (commands.remove && m_hovered.is_valid())
    {
        if (m_dragged == m_hovered)
        {
            release();
        }
        m_last_removed = m_hovered;
        static_cast<void>(scene.remove_body(m_hovered));
        m_hovered = {};
        m_hovered_point.reset();
        ++m_statistics.removed;
    }
}

auto
Interaction::drag_target() const noexcept -> std::optional<Vector3>
{
    return m_dragged.is_valid() ? std::optional{m_target} : std::nullopt;
}

void
Interaction::add_lines(DebugLines& lines) const
{
    // The spring from the grabbed point (where the body is drawn) to its target; else the point under the pointer.
    if (m_dragged.is_valid())
    {
        if (const SceneBody* body = m_scene->find(m_dragged); body != nullptr)
        {
            const Vector3 anchor = transform_point(m_scene->drawn_pose(*body), m_anchor);
            lines.add_line(anchor, m_target, {.red = 255, .green = 220, .blue = 60});
            lines.add_cross(m_target, 0.25F, {.red = 255, .green = 220, .blue = 60});
        }
    }
    else if (m_hovered_point)
    {
        lines.add_cross(*m_hovered_point, 0.2F, {.red = 255, .green = 255, .blue = 255});
    }
}

void
Interaction::release() noexcept
{
    m_dragged = {};
}

// ===================================================================================================== script

auto
Script::commands(std::uint64_t frame,
                 const Scene& scene,
                 const camera::Camera& camera,
                 Extent framebuffer) -> FrameCommands
{
    const camera::ScreenViewport viewport = viewport_of(framebuffer);
    FrameCommands commands{.pointer = m_drag_pixel};

    // Grab the top crate, drag it 2.5 m aside and 2 m up over a second, hold it there, let it go.
    if (frame == GRAB_FRAME)
    {
        if (const SceneBody* crate = scene.find(m_showcase.top_crate); crate != nullptr)
        {
            m_crate_start = crate->current.position;
        }
        m_grab_pixel = pixel_of(camera, m_crate_start, viewport);
        m_drag_pixel = pixel_of(camera, m_crate_start + Vector3{.x = 2.5F, .y = 2.0F}, viewport);
        commands.pointer = m_grab_pixel;
        commands.grab_pressed = true;
        commands.grab_held = true;
    }
    else if (frame > GRAB_FRAME && frame < RELEASE_FRAME)
    {
        const float fraction = std::min(1.0F, static_cast<float>(frame - GRAB_FRAME) / static_cast<float>(DRAGGED_FRAMES));
        commands.pointer = {.x = m_grab_pixel.x + (m_drag_pixel.x - m_grab_pixel.x) * fraction, .y = m_grab_pixel.y + (m_drag_pixel.y - m_grab_pixel.y) * fraction};
        commands.grab_held = true;
    }
    else if (frame == RELEASE_FRAME)
    {
        commands.grab_released = true;
    }

    // Shoot at the pyramid's middle, spawn a dumbbell on the floor, remove the body under the sphere's pixel.
    else if (frame == SHOT_FRAME)
    {
        if (const SceneBody* crate = scene.find(m_showcase.middle_crate); crate != nullptr)
        {
            commands.pointer = pixel_of(camera, crate->current.position, viewport);
        }
        commands.shoot = true;
    }
    else if (frame == SPAWN_FRAME)
    {
        commands.pointer = pixel_of(camera, m_spawn_point, viewport);
        commands.spawn = SpawnKind::DUMBBELL;
    }
    else if (frame == REMOVE_FRAME)
    {
        if (const SceneBody* sphere = scene.find(m_showcase.sphere); sphere != nullptr)
        {
            commands.pointer = pixel_of(camera, sphere->current.position, viewport);
        }
        commands.remove = true;
        m_bodies_before_removal = scene.bodies().size();
    }

    // Fly forward for a second, then orbit again.
    else if (frame == FLY_START_FRAME)
    {
        m_fly_start = camera.position();
        commands.toggle_fly = true;
        commands.fly = {.z = 1.0F};
    }
    else if (frame > FLY_START_FRAME && frame < FLY_END_FRAME)
    {
        commands.fly = {.z = 1.0F};
    }
    else if (frame == FLY_END_FRAME)
    {
        m_fly_end = camera.position();
        commands.toggle_fly = true;
    }
    return commands;
}

void
Script::observe(std::uint64_t frame,
                const Scene& scene,
                const Interaction& interaction,
                const camera::Camera&,
                const CameraRig& rig)
{
    if (frame == GRAB_FRAME)
    {
        m_grabbed_top_crate = interaction.dragged() == m_showcase.top_crate;
    }
    if (frame == RELEASE_FRAME - 1)
    {
        if (const SceneBody* crate = scene.find(m_showcase.top_crate); crate != nullptr)
        {
            m_crate_at_release = crate->current.position;
        }
        m_target_at_release = interaction.drag_target().value_or(Vector3{});
    }
    if (frame == SHOT_FRAME)
    {
        m_ball = interaction.last_shot();
    }
    if (frame > SHOT_FRAME && m_ball.is_valid() && !m_ball_hit_pyramid)
    {
        // A contact between the ball and any crate of the pyramid.
        for (const ContactEvent& event : scene.contact_events())
        {
            if (event.kind == ContactEventKind::END)
            {
                continue;
            }
            const SceneBody* first = scene.body_of(event.first);
            const SceneBody* second = scene.body_of(event.second);
            if (first == nullptr || second == nullptr)
            {
                continue;
            }
            const RigidBodyHandle a = first->body->handle();
            const RigidBodyHandle b = second->body->handle();
            const RigidBodyHandle other = a == m_ball ? b : b == m_ball ? a : RigidBodyHandle{};
            if (other.is_valid() && std::ranges::contains(m_showcase.pyramid, other))
            {
                m_ball_hit_pyramid = true;
                m_ball_hit_frame = frame;
            }
        }
    }
    if (frame == SPAWN_FRAME)
    {
        m_spawned = interaction.last_spawned();
    }
    if (frame == REMOVE_FRAME)
    {
        m_removed = interaction.last_removed();
        m_bodies_after_removal = scene.bodies().size();
    }
    if (frame == FLY_START_FRAME)
    {
        m_flew = rig.mode() == CameraMode::FLY;
    }
    if (frame == FLY_END_FRAME)
    {
        m_orbit_again = rig.mode() == CameraMode::ORBIT;
    }
}

auto
Script::checks(const Scene& scene) const -> std::vector<ScriptCheck>
{
    std::vector<ScriptCheck> checks;
    checks.push_back({.description = "the pointer grabbed the pyramid's top crate", .passed = m_grabbed_top_crate, .details = {}});

    const float moved = distance(m_crate_start, m_crate_at_release);
    const float lag = distance(m_crate_at_release, m_target_at_release);
    checks.push_back({.description = "dragging lifted it up and aside, following the pointer",
                      .passed = moved > 1.5F && m_crate_at_release.y > m_crate_start.y + 1.0F && lag < 1.2F,
                      .details = std::format("moved {:.2f} m (from y {:.2f} to {:.2f}), {:.2f} m from the pointer's target", moved, m_crate_start.y,
                                             m_crate_at_release.y, lag)});

    checks.push_back({.description = "the shot ball hit the pyramid",
                      .passed = m_ball.is_valid() && m_ball_hit_pyramid,
                      .details = !m_ball.is_valid()     ? std::string{"no ball was shot"}
                                 : m_ball_hit_pyramid ? std::format("a crate touched {} frames after the shot", m_ball_hit_frame - SHOT_FRAME)
                                                      : std::string{"no contact with a crate"}});

    const SceneBody* spawned = scene.find(m_spawned);
    const Vector3 rest = spawned != nullptr ? spawned->current.position : Vector3{};
    const float off = std::hypot(rest.x - m_spawn_point.x, rest.z - m_spawn_point.z);
    checks.push_back({.description = "a dumbbell spawned where the pointer pointed and came to rest there",
                      .passed = spawned != nullptr && off < 1.5F && rest.y < 1.0F,
                      .details = std::format("at ({:.2f}, {:.2f}, {:.2f}), {:.2f} m from the point", rest.x, rest.y, rest.z, off)});

    checks.push_back({.description = "the body under the pointer was removed",
                      .passed = m_removed.is_valid() && m_bodies_after_removal + 1 == m_bodies_before_removal && scene.find(m_removed) == nullptr,
                      .details = std::format("{} bodies before, {} after", m_bodies_before_removal, m_bodies_after_removal)});

    const float flown = distance(m_fly_start, m_fly_end);
    checks.push_back({.description = "the camera flew forward for a second, then orbited again",
                      .passed = m_flew && m_orbit_again && std::abs(flown - FLY_SPEED) < 0.6F,
                      .details = std::format("flew {:.2f} m ({} m expected)", flown, FLY_SPEED)});
    return checks;
}

} // namespace physics_sandbox
