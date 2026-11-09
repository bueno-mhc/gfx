// The scene the profiling tests trace on every backend: something of each kind a step works on — a floor, a column of
// boxes resting on it, a chain of boxes hanging from the world by ball-and-socket joints, a cloth falling onto the floor
// and a trigger around the column — stepped in profiler frames (docs/profiler-performance-design.md, PF2).
#pragma once

import std;
import gfx.profiler;
import gfx.physics;

namespace gfx::tests
{

/// @brief The profiled scene's objects (destroyed before their world).
struct ProfiledScene
{
    std::unique_ptr<physics::PhysicsWorld> world{};
    std::vector<std::unique_ptr<physics::RigidBody>> bodies{};
    std::vector<std::unique_ptr<physics::Collider>> colliders{};
    std::vector<std::unique_ptr<physics::Joint>> joints{};
    std::unique_ptr<physics::SoftBody> cloth{};
    std::unique_ptr<physics::Trigger> zone{};

    ProfiledScene() = default;
    ProfiledScene(ProfiledScene&&) = default;
    auto operator=(ProfiledScene&&) -> ProfiledScene& = default;
    ProfiledScene(const ProfiledScene&) = delete;
    auto operator=(const ProfiledScene&) -> ProfiledScene& = delete;

    ~ProfiledScene()
    {
        zone.reset();
        cloth.reset();
        joints.clear();
        colliders.clear();
        bodies.clear();
        world.reset();
    }
};

/// @brief Bodies of the profiled scene: the floor, the column, the chain.
inline constexpr std::uint32_t PROFILED_SCENE_BODIES = 1 + 8 + 3;

/// @brief Fills @p world with the profiled scene, its cloth of @p cloth_columns squared particles (66: more than 4 096,
///        the GPU backend's sorts of them take radix passes).
inline auto
make_profiled_scene(std::unique_ptr<physics::PhysicsWorld> world,
                    std::uint32_t cloth_columns = 6) -> ProfiledScene
{
    using namespace physics;
    ProfiledScene scene{};
    scene.world = std::move(world);
    const auto add = [&scene](const RigidBodyDescription& body,
                              Vector3 half) -> RigidBody& {
        scene.bodies.push_back(scene.world->create_rigid_body(body));
        scene.colliders.push_back(scene.bodies.back()->add_collider({.shape = make_shape(BoxShape{.half_extents = half})}));
        return *scene.bodies.back();
    };

    // The floor, its top at y = 0; a column of eight boxes resting on it.
    (void)add({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, {.x = 20, .y = 0.5F, .z = 20});
    for (int level = 0; level < 8; ++level)
    {
        (void)add({.kind = BodyKind::DYNAMIC, .pose = {.position = {.x = -2.0F, .y = 0.25F + 0.5F * static_cast<float>(level)}}},
                  {.x = 0.25F, .y = 0.25F, .z = 0.25F});
    }

    // A chain of three boxes from a point of the world, each held by the one before.
    RigidBodyHandle previous{};
    for (int link = 0; link < 3; ++link)
    {
        const Vector3 center{.x = 2.0F, .y = 4.0F - 0.5F * static_cast<float>(link), .z = 0.0F};
        const RigidBody& body = add({.kind = BodyKind::DYNAMIC, .pose = {.position = center}}, {.x = 0.1F, .y = 0.2F, .z = 0.1F});
        const Vector3 anchor{.x = center.x, .y = center.y + 0.25F, .z = center.z};
        scene.joints.push_back(scene.world->create_joint(BallAndSocketJointDescription{.first = body.handle(), .second = previous, .anchor = anchor}));
        previous = body.handle();
    }

    // A cloth falling onto the floor beside them; a zone around the column.
    const float spacing = 0.6F / static_cast<float>(cloth_columns);
    scene.cloth = scene.world->create_soft_body(cloth_preset(
        {.columns = cloth_columns, .rows = cloth_columns, .spacing = spacing, .origin = {.x = -0.3F, .y = 1.0F, .z = 1.0F}, .row_direction = {.z = 1}}));
    scene.zone = scene.world->create_trigger({.shape = make_shape(BoxShape{.half_extents = {.x = 0.6F, .y = 2.5F, .z = 0.6F}}), .pose = {.position = {.x = -2.0F, .y = 2.0F}}});
    return scene;
}

/// @brief Steps @p scene @p steps times, a profiler frame each, recorded by a profiler made current meanwhile.
inline auto
trace_steps(ProfiledScene& scene,
            std::uint32_t steps,
            std::string_view name) -> profiler::Trace
{
    const std::unique_ptr<profiler::Profiler> recorder = profiler::create_profiler();
    profiler::set_current_profiler(recorder.get());
    const profiler::SessionHandle session = recorder->create_session({.name = std::string{name}});
    for (std::uint32_t step = 0; step < steps; ++step)
    {
        recorder->begin_frame(session);
        scene.world->step(1.0F / 60.0F);
        (void)recorder->end_frame(session);
    }
    profiler::Trace trace = recorder->finalize_session(session);
    profiler::set_current_profiler(nullptr);
    return trace;
}

/// @brief The last value of the counter named @p name in @p trace (nullopt: none).
inline auto
last_counter(const profiler::Trace& trace,
             std::string_view name) -> std::optional<double>
{
    std::optional<double> value;
    for (const profiler::TraceFrame& frame : trace.frames())
    {
        for (const profiler::TraceSample& sample : frame.samples)
        {
            if (trace.sites()[sample.site].name == name)
            {
                value = sample.number();
            }
        }
    }
    return value;
}

/// @brief The stages of a step every backend's trace shows: the framework's.
inline constexpr std::array FRAMEWORK_STAGES{
    profiler::StageCoverage{"step() entry and exit", profiler::StageCoverageKind::RECORDED, "World: step", {}},
    profiler::StageCoverage{"run the step", profiler::StageCoverageKind::RECORDED, "World: run the step", {}},
    profiler::StageCoverage{"edits applied to the records", profiler::StageCoverageKind::RECORDED, "World: apply the edits to the records", {}},
    profiler::StageCoverage{"edits applied by the backend", profiler::StageCoverageKind::RECORDED, "World: backend apply", {}},
    profiler::StageCoverage{"the backend's simulation", profiler::StageCoverageKind::RECORDED, "World: backend simulate", {}},
    profiler::StageCoverage{"the bounds", profiler::StageCoverageKind::RECORDED, "World: update the bounds", {}},
    profiler::StageCoverage{"the wait for the step", profiler::StageCoverageKind::RECORDED, "World: wait for the step", {}},
    profiler::StageCoverage{"publish", profiler::StageCoverageKind::RECORDED, "World: publish", {}},
    profiler::StageCoverage{"records copied", profiler::StageCoverageKind::RECORDED, "World: copy the records", {}},
    profiler::StageCoverage{"events and contacts copied", profiler::StageCoverageKind::RECORDED, "World: copy the events and contacts", {}},
    profiler::StageCoverage{"the query tree", profiler::StageCoverageKind::RECORDED, "World: update the query tree", {}},
};

} // namespace gfx::tests
