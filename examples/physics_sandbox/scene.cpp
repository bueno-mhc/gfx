// The sandbox's scene: the world, fixed steps on the world's thread with interpolated poses, the showcase.
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

/// The showcase's palette.
constexpr std::array<DebugColor, 8> PALETTE{DebugColor{.red = 230, .green = 120, .blue = 60}, DebugColor{.red = 70, .green = 150, .blue = 220},
                                            DebugColor{.red = 110, .green = 190, .blue = 90}, DebugColor{.red = 225, .green = 190, .blue = 70},
                                            DebugColor{.red = 170, .green = 100, .blue = 200}, DebugColor{.red = 80, .green = 190, .blue = 180},
                                            DebugColor{.red = 220, .green = 90, .blue = 120}, DebugColor{.red = 200, .green = 200, .blue = 205}};

} // namespace

Scene::Scene(PhysicsBackendKind backend,
             physics::TaskScheduler& scheduler)
    : m_backend{backend}
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
    for (const ColliderSpecification& specification : colliders)
    {
        body.colliders.push_back({.collider = body.body->add_collider({.shape = specification.shape,
                                                                       .pose = specification.pose,
                                                                       .material = specification.material,
                                                                       .density = specification.density}),
                                  .shape = specification.shape,
                                  .pose = specification.pose});
    }
    return body;
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

auto
Scene::drawn_pose(const SceneBody& body) const noexcept -> Transform
{
    return interpolate(body.previous, body.current, m_alpha);
}

void
Scene::capture()
{
    for (SceneBody& body : m_bodies)
    {
        const RigidBodyState state = body.body->state();
        body.previous = body.current;
        body.current = state.pose;
        body.awake = state.awake;
    }
}

void
build_showcase(Scene& scene)
{
    // The floor: a static slab whose top is at y = 0, with the grid.
    const auto floor = make_shape(BoxShape{.half_extents = {.x = 40.0F, .y = 0.5F, .z = 40.0F}});
    const std::array floor_collider{ColliderSpecification{.shape = floor, .material = {.friction = {.static_coefficient = 0.7F, .dynamic_coefficient = 0.7F}}}};
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, floor_collider,
                                     {.red = 150, .green = 152, .blue = 158}, INSTANCE_GRID));

    // A pyramid of crates (one shared shape).
    const auto crate = make_shape(BoxShape{.half_extents = {.x = 0.4F, .y = 0.4F, .z = 0.4F}});
    const std::array crate_collider{ColliderSpecification{.shape = crate, .density = 400.0F}};
    constexpr int LEVELS = 6;
    for (int level = 0; level < LEVELS; ++level)
    {
        for (int column = 0; column < LEVELS - level; ++column)
        {
            const float x = -6.0F + static_cast<float>(column) * 0.82F + static_cast<float>(level) * 0.41F;
            const float y = 0.4F + static_cast<float>(level) * 0.8F;
            static_cast<void>(scene.add_body({.pose = {.position = {.x = x, .y = y}}}, crate_collider, PALETTE[static_cast<std::size_t>(level) % 4]));
        }
    }

    // Every convex shape kind, three of each, falling from staggered heights.
    const std::array<std::shared_ptr<const Shape>, 6> kinds{
        make_shape(SphereShape{.radius = 0.4F}),
        make_shape(CapsuleShape{.half_height = 0.35F, .radius = 0.25F}),
        make_shape(CylinderShape{.half_height = 0.3F, .radius = 0.35F}),
        make_shape(ConeShape{.half_height = 0.4F, .radius = 0.4F}),
        make_shape(HullShape(PLINTH)),
        make_shape(CompoundShape({CompoundChild{.shape = make_shape(SphereShape{.radius = 0.3F}), .pose = {.position = {.x = -0.45F}}},
                                  CompoundChild{.shape = make_shape(SphereShape{.radius = 0.3F}), .pose = {.position = {.x = 0.45F}}},
                                  CompoundChild{.shape = make_shape(CapsuleShape{.half_height = 0.3F, .radius = 0.1F}),
                                                .pose = {.orientation = Quaternion::from_axis_angle({.z = 1.0F}, std::numbers::pi_v<float> / 2)}}})),
    };
    for (std::size_t kind = 0; kind < kinds.size(); ++kind)
    {
        const std::array collider{ColliderSpecification{.shape = kinds[kind]}};
        for (int copy = 0; copy < 3; ++copy)
        {
            const Vector3 position{.x = 1.0F + static_cast<float>(kind) * 1.2F, .y = 2.0F + static_cast<float>(copy) * 1.5F + static_cast<float>(kind % 2) * 0.6F,
                                   .z = -1.5F + static_cast<float>(copy) * 1.5F};
            const Quaternion tilt = Quaternion::from_axis_angle(normalized(Vector3{.x = 1.0F, .y = 0.3F, .z = static_cast<float>(copy)}), 0.4F * static_cast<float>(copy + 1));
            static_cast<void>(scene.add_body({.pose = {.position = position, .orientation = tilt}}, collider, PALETTE[(kind + 4 + static_cast<std::size_t>(copy)) % PALETTE.size()]));
        }
    }

    // A static ramp, and balls on its top end that roll down.
    const std::array ramp{ColliderSpecification{.shape = make_shape(BoxShape{.half_extents = {.x = 3.0F, .y = 0.15F, .z = 1.2F}})}};
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC,
                                      .pose = {.position = {.x = -2.0F, .y = 1.2F, .z = -5.0F},
                                               .orientation = Quaternion::from_axis_angle({.z = 1.0F}, 0.35F)}},
                                     ramp, {.red = 120, .green = 124, .blue = 135}));
    const std::array ball{ColliderSpecification{.shape = kinds[0], .material = {.restitution = 0.3F}}};
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
}

} // namespace physics_sandbox
