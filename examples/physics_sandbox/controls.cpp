// The sandbox's live controls and debug views: settings applied between steps, the views' lines and islands, the
// scripted controls and their checks.
module physics_sandbox.controls;

import std;
import gfx.physics;
import physics_sandbox.interaction;
import physics_sandbox.renderer;

namespace physics_sandbox
{

using namespace gfx;
using namespace gfx::physics;

namespace
{

constexpr std::array<std::string_view, BROAD_PHASE_CHOICE_COUNT> BROAD_PHASE_NAMES{"BVH", "octree", "quadtree", "BSP tree", "uniform grid",
                                                                                  "sweep and prune (x)", "sweep and prune (x, y, z)"};

constexpr DebugColor AWAKE_BOX{.red = 235, .green = 205, .blue = 70};
constexpr DebugColor ASLEEP_BOX{.red = 120, .green = 115, .blue = 90};
constexpr DebugColor STATIC_BOX{.red = 110, .green = 112, .blue = 125};
constexpr DebugColor SOFT_BOX{.red = 70, .green = 215, .blue = 230};
constexpr DebugColor TRIGGER_BOX{.red = 220, .green = 90, .blue = 215};
constexpr DebugColor CONTACT{.red = 245, .green = 70, .blue = 50};
constexpr DebugColor JOINT{.red = 90, .green = 235, .blue = 130};
constexpr DebugColor BROKEN_JOINT{.red = 240, .green = 80, .blue = 70};
constexpr DebugColor JOINT_AXIS{.red = 120, .green = 170, .blue = 255};

/// Node colors by depth (the deeper, the further along).
constexpr std::array<DebugColor, 6> DEPTH_COLORS{DebugColor{.red = 80, .green = 160, .blue = 255}, DebugColor{.red = 90, .green = 220, .blue = 200},
                                                 DebugColor{.red = 140, .green = 230, .blue = 90}, DebugColor{.red = 230, .green = 220, .blue = 80},
                                                 DebugColor{.red = 245, .green = 150, .blue = 60}, DebugColor{.red = 240, .green = 80, .blue = 120}};

/// Island colors.
constexpr std::array<DebugColor, 10> ISLAND_COLORS{DebugColor{.red = 230, .green = 90, .blue = 80},   DebugColor{.red = 80, .green = 150, .blue = 230},
                                                   DebugColor{.red = 110, .green = 200, .blue = 90},  DebugColor{.red = 235, .green = 195, .blue = 60},
                                                   DebugColor{.red = 175, .green = 105, .blue = 215}, DebugColor{.red = 70, .green = 200, .blue = 190},
                                                   DebugColor{.red = 240, .green = 140, .blue = 60},  DebugColor{.red = 220, .green = 100, .blue = 160},
                                                   DebugColor{.red = 150, .green = 190, .blue = 230}, DebugColor{.red = 190, .green = 210, .blue = 110}};

/// @p color dimmed to @p fraction of itself.
[[nodiscard]] constexpr auto
dimmed(DebugColor color,
       float fraction) noexcept -> DebugColor
{
    const auto channel = [fraction](std::uint8_t value) { return static_cast<std::uint8_t>(static_cast<float>(value) * fraction); };
    return {.red = channel(color.red), .green = channel(color.green), .blue = channel(color.blue), .alpha = color.alpha};
}

/// The first line of what @p error refused, with its capability.
[[nodiscard]] auto
refusal(const BackendCapabilityError& error) -> std::string
{
    const std::string_view reason = error.reason().substr(0, error.reason().find('\n'));
    return std::format("{}: {}", error.capability(), reason);
}

/// The root of @p index in a union-find forest (paths halved on the way).
[[nodiscard]] auto
root(std::vector<std::uint32_t>& parents,
     std::uint32_t index) noexcept -> std::uint32_t
{
    while (parents[index] != index)
    {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

/// Joins the sets of @p first and @p second (the smaller root stays).
void
join(std::vector<std::uint32_t>& parents,
     std::uint32_t first,
     std::uint32_t second) noexcept
{
    const std::uint32_t a = root(parents, first);
    const std::uint32_t b = root(parents, second);
    parents[std::max(a, b)] = std::min(a, b);
}

} // namespace

// ===================================================================================================== simulation

auto
broad_phase_choice_name(BroadPhaseChoice choice) noexcept -> std::string_view
{
    return BROAD_PHASE_NAMES[std::to_underlying(choice)];
}

auto
make_broad_phase(BroadPhaseChoice choice) -> std::unique_ptr<BroadPhase>
{
    switch (choice)
    {
        case BroadPhaseChoice::BVH:
            return std::make_unique<SpatialBroadPhaseComponent<Bvh>>();
        case BroadPhaseChoice::OCTREE:
            return std::make_unique<SpatialBroadPhaseComponent<Octree<>>>();
        case BroadPhaseChoice::QUADTREE:
            return std::make_unique<SpatialBroadPhaseComponent<Quadtree<>>>();
        case BroadPhaseChoice::BSP:
            return std::make_unique<SpatialBroadPhaseComponent<Bsp>>();
        case BroadPhaseChoice::UNIFORM_GRID:
            return std::make_unique<SpatialBroadPhaseComponent<UniformGrid<>>>();
        case BroadPhaseChoice::SWEEP_AND_PRUNE:
            return std::make_unique<SpatialBroadPhaseComponent<SweepAndPrune<1>>>();
        case BroadPhaseChoice::SWEEP_AND_PRUNE_3_AXES:
            return std::make_unique<SpatialBroadPhaseComponent<SweepAndPrune<3>>>();
    }
    return std::make_unique<SpatialBroadPhaseComponent<Bvh>>();
}

void
SimulationControl::adopt(Scene& scene)
{
    // What the new world has: its description, its components (a backend's own broad phase counts as the default).
    PhysicsWorld& world = scene.world();
    const PhysicsWorldDescription description = world.description();
    m_applied.gravity = description.gravity;
    m_applied.solver = description.solver;
    m_applied.sleep = description.sleep.enabled;
    m_applied.broad_phase = BroadPhaseChoice::BVH;
    for (std::size_t index = 0; index < BROAD_PHASE_CHOICE_COUNT; ++index)
    {
        const auto choice = static_cast<BroadPhaseChoice>(index);
        if (world.broad_phase().is_external() && make_broad_phase(choice)->kind() == world.broad_phase().kind())
        {
            m_applied.broad_phase = choice;
            break;
        }
    }
    m_applied.integrator = world.integrator().kind();
    m_refusals.clear();
}

void
SimulationControl::apply(Scene& scene,
                         SimulationSettings& settings)
{
    PhysicsWorld& world = scene.world();

    // Gravity (every body woken: a sleeping one would not notice), the solver, sleeping.
    if (settings.gravity != m_applied.gravity)
    {
        world.set_gravity(settings.gravity);
        for (const SceneBody& body : scene.bodies())
        {
            if (body.kind == BodyKind::DYNAMIC)
            {
                body.body->wake_up();
            }
        }
        m_applied.gravity = settings.gravity;
    }
    if (settings.solver != m_applied.solver)
    {
        world.set_solver_description(settings.solver);
        m_applied.solver = settings.solver;
    }
    if (settings.sleep != m_applied.sleep)
    {
        SleepDescription sleep = world.description().sleep;
        sleep.enabled = settings.sleep;
        world.set_sleep_description(sleep);
        m_applied.sleep = settings.sleep;
    }

    // The components, which a backend running its own refuses (the setting goes back).
    const auto note = [this](const BackendCapabilityError& error) {
        std::string text = refusal(error);
        if (!std::ranges::contains(m_refusals, text))
        {
            m_refusals.push_back(std::move(text));
        }
    };
    if (settings.broad_phase != m_applied.broad_phase)
    {
        try
        {
            world.replace_broad_phase(make_broad_phase(settings.broad_phase));
            m_applied.broad_phase = settings.broad_phase;
        }
        catch (const BackendCapabilityError& error)
        {
            note(error);
            settings.broad_phase = m_applied.broad_phase;
        }
    }
    if (settings.integrator != m_applied.integrator)
    {
        try
        {
            world.replace_integrator(make_integrator(settings.integrator));
            m_applied.integrator = settings.integrator;
        }
        catch (const BackendCapabilityError& error)
        {
            note(error);
            settings.integrator = m_applied.integrator;
        }
    }
}

// ===================================================================================================== debug views

void
DebugViews::capture(Scene& scene,
                    const ViewSettings& views)
{
    m_lines.clear();
    m_statistics = {};
    m_islands.clear();
    if (views.bounding_boxes)
    {
        add_boxes(scene);
    }
    if (views.contacts)
    {
        add_contacts(scene);
    }
    if (views.broad_phase)
    {
        add_structure(scene);
    }
    if (views.joints)
    {
        add_joints(scene);
    }
    if (views.coloring == BodyColoring::ISLAND)
    {
        find_islands(scene);
    }
}

void
DebugViews::add_boxes(const Scene& scene)
{
    // Every collider's box (dynamic awake, asleep, static), every soft body's, every trigger's.
    for (const SceneBody& body : scene.bodies())
    {
        const DebugColor color = body.kind != BodyKind::DYNAMIC ? STATIC_BOX : body.awake ? AWAKE_BOX : ASLEEP_BOX;
        for (const SceneCollider& collider : body.colliders)
        {
            m_lines.add_box(collider.collider->world_bounds(), color);
            ++m_statistics.boxes;
        }
    }
    for (const SceneSoftBody& soft_body : scene.soft_bodies())
    {
        m_lines.add_box(soft_body.body->bounds(), SOFT_BOX);
        ++m_statistics.boxes;
    }
    for (const SceneTrigger& trigger : scene.triggers())
    {
        m_lines.add_box(trigger.trigger->world_bounds(), TRIGGER_BOX);
        ++m_statistics.boxes;
    }
}

void
DebugViews::add_contacts(const Scene& scene)
{
    // Each dynamic body's contacts; a pair of dynamic bodies drawn once (from the smaller collider).
    if (m_contacts.empty())
    {
        m_contacts.resize(64);
    }
    for (const SceneBody& body : scene.bodies())
    {
        if (body.kind != BodyKind::DYNAMIC)
        {
            continue;
        }
        QueryResult<std::span<ContactInfo>> found = scene.world().contacts_of(body.body->handle(), m_contacts);
        while (!found && found.error().kind == QueryFailureKind::CAPACITY_EXCEEDED)
        {
            m_contacts.resize(m_contacts.size() * 2);
            found = scene.world().contacts_of(body.body->handle(), m_contacts);
        }
        if (!found)
        {
            continue;
        }
        for (const ContactInfo& contact : *found)
        {
            const SceneBody* other = scene.find(contact.other_body);
            if (other != nullptr && other->kind == BodyKind::DYNAMIC && contact.other < contact.collider)
            {
                continue;
            }
            m_lines.add_contacts(contact.manifold, CONTACT, 0.06F, 0.35F);
            m_statistics.contact_points += contact.manifold.point_count;
        }
    }
}

void
DebugViews::add_structure(Scene& scene)
{
    // The broad phase's nodes or cells, colored by depth, those listing proxies brighter.
    try
    {
        scene.world().broad_phase().structure_nodes(m_nodes);
    }
    catch (const BackendCapabilityError& error)
    {
        m_statistics.structure_note = refusal(error);
        return;
    }
    for (const SpatialNode& node : m_nodes)
    {
        const DebugColor color = DEPTH_COLORS[node.depth % DEPTH_COLORS.size()];
        m_lines.add_box(node.box, node.proxies > 0 ? color : dimmed(color, 0.55F));
    }
    m_statistics.nodes = static_cast<std::uint32_t>(m_nodes.size());
}

void
DebugViews::add_joints(const Scene& scene)
{
    // Each joint whose bodies are there: its anchors on each body, the line between them, its axis.
    for (const SceneJoint& joint : scene.joints())
    {
        const SceneBody* first = scene.find(joint.first);
        const SceneBody* second = joint.second.is_valid() ? scene.find(joint.second) : nullptr;
        if (first == nullptr || (joint.second.is_valid() && second == nullptr))
        {
            continue;
        }
        const Vector3 on_first = transform_point(first->current, joint.anchor_first);
        const Vector3 on_second = second != nullptr ? transform_point(second->current, joint.anchor_second) : joint.anchor_second;
        const DebugColor color = joint.broken ? BROKEN_JOINT : JOINT;
        m_lines.add_cross(on_first, 0.12F, color);
        m_lines.add_cross(on_second, 0.12F, color);
        m_lines.add_line(on_first, on_second, color);
        if (joint.axis && !joint.broken)
        {
            const Vector3 axis = rotate(first->current.orientation, *joint.axis);
            m_lines.add_arrow(on_first, on_first + axis * 0.4F, JOINT_AXIS, 0.08F);
        }
        ++m_statistics.joints;
    }
}

void
DebugViews::find_islands(const Scene& scene)
{
    // Dynamic bodies joined by their contacts with each other and by joints (static bodies join nothing).
    const std::deque<SceneBody>& bodies = scene.bodies();
    m_body_indices.clear();
    m_parents.resize(bodies.size());
    for (std::uint32_t index = 0; index < bodies.size(); ++index)
    {
        m_parents[index] = index;
        if (bodies[index].kind == BodyKind::DYNAMIC)
        {
            m_body_indices.emplace(bodies[index].body->handle(), index);
        }
    }
    if (m_contacts.empty())
    {
        m_contacts.resize(64);
    }
    for (std::uint32_t index = 0; index < bodies.size(); ++index)
    {
        if (bodies[index].kind != BodyKind::DYNAMIC)
        {
            continue;
        }
        QueryResult<std::span<ContactInfo>> found = scene.world().contacts_of(bodies[index].body->handle(), m_contacts);
        while (!found && found.error().kind == QueryFailureKind::CAPACITY_EXCEEDED)
        {
            m_contacts.resize(m_contacts.size() * 2);
            found = scene.world().contacts_of(bodies[index].body->handle(), m_contacts);
        }
        if (!found)
        {
            continue;
        }
        for (const ContactInfo& contact : *found)
        {
            if (const auto other = m_body_indices.find(contact.other_body); other != m_body_indices.end())
            {
                join(m_parents, index, other->second);
            }
        }
    }
    for (const SceneJoint& joint : scene.joints())
    {
        const auto first = m_body_indices.find(joint.first);
        const auto second = m_body_indices.find(joint.second);
        if (first != m_body_indices.end() && second != m_body_indices.end())
        {
            join(m_parents, first->second, second->second);
        }
    }

    // Islands numbered by their first body.
    m_islands.assign(bodies.size(), NO_ISLAND);
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < bodies.size(); ++index)
    {
        if (bodies[index].kind != BodyKind::DYNAMIC)
        {
            continue;
        }
        const std::uint32_t top = root(m_parents, index);
        if (m_islands[top] == NO_ISLAND)
        {
            m_islands[top] = count++;
        }
        m_islands[index] = m_islands[top];
    }
    m_statistics.islands = count;
}

auto
body_color(const SceneBody& body,
           BodyColoring coloring,
           std::uint32_t island) noexcept -> DebugColor
{
    switch (coloring)
    {
        case BodyColoring::BODY:
            return body.kind == BodyKind::DYNAMIC && !body.awake ? dimmed(body.color, 0.72F) : body.color;
        case BodyColoring::SLEEP:
            return body.kind != BodyKind::DYNAMIC ? DebugColor{.red = 120, .green = 122, .blue = 130}
                   : body.awake                   ? DebugColor{.red = 235, .green = 120, .blue = 60}
                                                  : DebugColor{.red = 70, .green = 110, .blue = 190};
        case BodyColoring::ISLAND:
            return island == NO_ISLAND ? DebugColor{.red = 120, .green = 122, .blue = 130} : ISLAND_COLORS[island % ISLAND_COLORS.size()];
    }
    return body.color;
}

// ===================================================================================================== script

namespace
{

// The script's frames, counted from its first.
constexpr std::uint64_t PAUSE = 0;
constexpr std::uint64_t SINGLE_STEP = 30;
constexpr std::uint64_t SLOW_MOTION = 35;
constexpr std::uint64_t SLOW_FRAMES = 40;
constexpr std::uint64_t ZERO_GRAVITY = SLOW_MOTION + SLOW_FRAMES + 1;
constexpr std::uint64_t FLOATING_FRAMES = 30;
constexpr std::uint64_t FALLING_FRAMES = 30;
constexpr std::uint64_t SOLVER = ZERO_GRAVITY + FLOATING_FRAMES + FALLING_FRAMES + 1;
constexpr std::uint64_t COMPONENTS = SOLVER + 3;
constexpr std::uint64_t VIEWS = COMPONENTS + 10;
constexpr std::uint64_t VIEWS_SEEN = VIEWS + 5;
constexpr float SLOW_TIME_SCALE = 0.25F;
constexpr Vector3 BALL_START{.x = 6.0F, .y = 5.0F, .z = 6.0F};

} // namespace

void
ControlScript::control(std::uint64_t frame,
                       Scene& scene,
                       SimulationSettings& settings,
                       ViewSettings& views)
{
    if (frame < m_first)
    {
        return;
    }
    const std::uint64_t at = frame - m_first;
    switch (at)
    {
        case PAUSE:
            settings.paused = true;
            break;
        case SINGLE_STEP:
            settings.step_once = true;
            break;
        case SLOW_MOTION:
            settings.paused = false;
            settings.time_scale = SLOW_TIME_SCALE;
            break;
        case ZERO_GRAVITY:
            // A ball let go without gravity: it stays where it is.
            settings.time_scale = 1.0F;
            settings.gravity = {};
            m_ball = scene.spawn(SpawnKind::SPHERE, {.position = BALL_START}, {}).body->handle();
            break;
        case ZERO_GRAVITY + FLOATING_FRAMES:
            settings.gravity = {.y = -STANDARD_GRAVITY};
            break;
        case SOLVER:
            settings.solver.sub_steps = 8;
            settings.solver.velocity_iterations = 12;
            settings.solver.position_iterations = 4;
            break;
        case COMPONENTS:
            settings.broad_phase = BroadPhaseChoice::OCTREE;
            settings.integrator = IntegratorKind::VERLET;
            break;
        case VIEWS:
            // Every view, the ball held from a point above it by a ball-and-socket joint (a joint to show).
            views = {.wireframe = true, .bounding_boxes = true, .contacts = true, .broad_phase = true, .joints = true, .coloring = BodyColoring::ISLAND};
            if (const SceneBody* ball = scene.find(m_ball))
            {
                static_cast<void>(scene.add_joint(ball_and_socket_joint({.first = m_ball, .second = {}, .anchor = ball->current.position + Vector3{.y = 1.0F}})));
            }
            break;
        default:
            break;
    }
}

void
ControlScript::observe(std::uint64_t frame,
                       Scene& scene,
                       const SimulationSettings& settings,
                       const SimulationControl& control,
                       const DebugViews& debug,
                       const SceneRendererStatistics& drawn)
{
    if (frame < m_first)
    {
        return;
    }
    const std::uint64_t at = frame - m_first;
    if (at == PAUSE + 1)
    {
        // The step begun before the pause has completed: none after it until the single step.
        m_paused_steps = scene.step_count();
    }
    else if (at == SINGLE_STEP)
    {
        m_steps_while_paused = scene.step_count();
    }
    else if (at == SLOW_MOTION)
    {
        m_steps_after_single = scene.step_count();
        m_slow_start = scene.step_count();
    }
    else if (at == SLOW_MOTION + SLOW_FRAMES)
    {
        m_slow_steps = scene.step_count() - m_slow_start;
    }
    else if (at == ZERO_GRAVITY + FLOATING_FRAMES)
    {
        // Floating, before gravity comes back.
        if (const SceneBody* ball = scene.find(m_ball))
        {
            m_floating_height = ball->current.position.y;
            m_floating_speed = length(ball->body->state().linear_velocity);
        }
    }
    else if (at == ZERO_GRAVITY + FLOATING_FRAMES + FALLING_FRAMES)
    {
        if (const SceneBody* ball = scene.find(m_ball))
        {
            m_fallen_height = ball->current.position.y;
        }
    }
    else if (at == SOLVER + 2)
    {
        const SolverDescription solver = scene.world().description().solver;
        m_sub_steps = solver.sub_steps;
        m_velocity_iterations = solver.velocity_iterations;
    }
    else if (at == COMPONENTS + 2)
    {
        // Replaced on a backend that takes components; refused, and the settings back, on one that runs its own.
        PhysicsWorld& world = scene.world();
        if (world.broad_phase().is_external())
        {
            m_replaced = world.broad_phase().kind() == BroadPhaseKind::OCTREE && world.integrator().kind() == IntegratorKind::VERLET;
            m_replacement_details = std::format("broad phase {}, integrator {}", world.broad_phase().kind(), world.integrator().kind());
        }
        else
        {
            const auto refused = [&control](std::string_view capability) {
                return std::ranges::any_of(control.refusals(), [capability](const std::string& text) { return text.starts_with(capability); });
            };
            m_replaced = refused("CUSTOM_BROAD_PHASE") && refused("CUSTOM_INTEGRATOR") && settings.broad_phase == BroadPhaseChoice::BVH
                         && settings.integrator == IntegratorKind::SEMI_IMPLICIT_EULER;
            m_replacement_details = std::format("{} refusals, settings back to {} and {}", control.refusals().size(), broad_phase_choice_name(settings.broad_phase),
                                                settings.integrator);
        }
    }
    else if (at == VIEWS_SEEN)
    {
        m_views = debug.statistics();
        m_wireframe_draws = drawn.wireframe_draws;
    }
}

auto
ControlScript::checks() const -> std::vector<ScriptCheck>
{
    std::vector<ScriptCheck> checks;
    checks.push_back({.description = "pausing stopped the steps",
                      .passed = m_steps_while_paused == m_paused_steps,
                      .details = std::format("{} steps, then {} after {} paused frames", m_paused_steps, m_steps_while_paused, SINGLE_STEP - PAUSE - 1)});
    checks.push_back({.description = "a single step while paused took one step",
                      .passed = m_steps_after_single == m_paused_steps + 1,
                      .details = std::format("{} steps, {} expected", m_steps_after_single, m_paused_steps + 1)});
    const auto expected_slow = static_cast<std::uint64_t>(static_cast<float>(SLOW_FRAMES) * SLOW_TIME_SCALE);
    checks.push_back({.description = "slow motion at a quarter of the speed took a quarter of the steps",
                      .passed = m_slow_steps + 1 >= expected_slow && m_slow_steps <= expected_slow + 1,
                      .details = std::format("{} steps in {} frames, {} expected", m_slow_steps, SLOW_FRAMES, expected_slow)});
    checks.push_back({.description = "without gravity a ball let go stays where it is",
                      .passed = std::abs(m_floating_height - BALL_START.y) < 0.01F && m_floating_speed < 0.01F,
                      .details = std::format("at {:.4f} m (from {}), {:.4f} m/s", m_floating_height, BALL_START.y, m_floating_speed)});
    checks.push_back({.description = "with gravity back it falls",
                      .passed = m_fallen_height < BALL_START.y - 0.5F,
                      .details = std::format("at {:.2f} m after {} frames", m_fallen_height, FALLING_FRAMES)});
    checks.push_back({.description = "the solver's settings reached the world",
                      .passed = m_sub_steps == 8 && m_velocity_iterations == 12,
                      .details = std::format("{} sub-steps, {} velocity iterations", m_sub_steps, m_velocity_iterations)});
    checks.push_back({.description = "the broad phase and the integrator were replaced, or refused by a backend running its own",
                      .passed = m_replaced,
                      .details = m_replacement_details});
    const bool structure = m_views.nodes > 0 || !m_views.structure_note.empty();
    checks.push_back({.description = "every view drew something",
                      .passed = m_views.boxes > 0 && m_views.contact_points > 0 && structure && m_views.joints > 0 && m_wireframe_draws > 0,
                      .details = std::format("{} boxes, {} contact points, {} nodes{}, {} joints, {} wireframe draws", m_views.boxes, m_views.contact_points,
                                             m_views.nodes, m_views.structure_note.empty() ? "" : std::format(" ({})", m_views.structure_note), m_views.joints,
                                             m_wireframe_draws)});
    checks.push_back({.description = "the bodies fall into several islands",
                      .passed = m_views.islands >= 2,
                      .details = std::format("{} islands", m_views.islands)});
    return checks;
}

} // namespace physics_sandbox
