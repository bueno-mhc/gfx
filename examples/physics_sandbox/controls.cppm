/// @file controls.cppm
/// @brief The sandbox's live controls and debug views: the simulation's settings applied to the world between steps
///        (pausing, single steps and slow motion are the frame loop's), what each debug view draws gathered between
///        steps (bounding boxes, contacts and their normals, the broad phase's structure, joints, islands), and the
///        scripted run that turns every control and view on and checks what each did.
///
/// A backend that refuses a setting (Box3D runs its own broad phase and integrator) keeps what it has: the setting goes
/// back to the applied one, and the refusal is kept for the panel.
export module physics_sandbox.controls;

import std;
export import physics_sandbox.interaction; // the scene, the script's checks
export import physics_sandbox.renderer;    // what the last frame drew

export namespace physics_sandbox
{

// ===================================================================================================== simulation

/// The broad phases the reference backend can be given.
enum class BroadPhaseChoice : std::uint8_t
{
    BVH,                    ///< A dynamic AABB tree (the default).
    OCTREE,                 ///< A loose octree over ±1024 m.
    QUADTREE,               ///< A loose quadtree over X and Z.
    BSP,                    ///< A BSP tree of axis-aligned planes.
    UNIFORM_GRID,           ///< A hashed grid of 1 m cells.
    SWEEP_AND_PRUNE,        ///< Sweep and prune on one axis.
    SWEEP_AND_PRUNE_3_AXES, ///< Sweep and prune on three axes.
};

/// How many broad phase choices there are.
inline constexpr std::size_t BROAD_PHASE_CHOICE_COUNT = 7;

/// @brief @p choice's name, as the panel shows it.
[[nodiscard]] auto broad_phase_choice_name(BroadPhaseChoice choice) noexcept -> std::string_view;

/// @brief A broad phase component of @p choice.
[[nodiscard]] auto make_broad_phase(BroadPhaseChoice choice) -> std::unique_ptr<gfx::physics::BroadPhase>;

/// The integrators rigid bodies can be given (XPBD is the soft bodies').
inline constexpr std::array<gfx::physics::IntegratorKind, 3> INTEGRATOR_CHOICES{
    gfx::physics::IntegratorKind::SEMI_IMPLICIT_EULER, gfx::physics::IntegratorKind::VERLET, gfx::physics::IntegratorKind::RUNGE_KUTTA_4};

/// What the simulation is asked to do.
struct SimulationSettings
{
    bool paused = false;                                                           ///< No step begins.
    bool step_once = false;                                                        ///< Paused: take one step (cleared when taken).
    float time_scale = 1.0F;                                                       ///< Simulated seconds per second (slow motion below 1).
    gfx::Vector3 gravity{.y = -gfx::physics::STANDARD_GRAVITY};                    ///< The world's gravity.
    gfx::physics::SolverDescription solver{};                                      ///< Sub-steps, iterations, warm starting.
    bool sleep = true;                                                             ///< Whether bodies may sleep.
    BroadPhaseChoice broad_phase = BroadPhaseChoice::BVH;                          ///< The reference backend's broad phase.
    gfx::physics::IntegratorKind integrator = gfx::physics::IntegratorKind::SEMI_IMPLICIT_EULER; ///< Its rigid bodies' integrator.
};

/// Applies SimulationSettings to a scene's world, between steps.
class SimulationControl
{
public:
    /// @brief Takes @p scene's world as it is (a new scene: what its world has counts as applied), forgetting the
    ///        refusals of the scene before.
    void adopt(Scene& scene);

    /// @brief Applies what changed in @p settings since the last call (a change of gravity wakes every body); what the
    ///        backend refuses goes back to what it has in @p settings, and is kept in refusals().
    void apply(Scene& scene,
               SimulationSettings& settings);

    /// @brief What the backend refused, and why.
    [[nodiscard]] auto refusals() const noexcept -> std::span<const std::string>
    {
        return m_refusals;
    }

private:
    SimulationSettings m_applied{};
    std::vector<std::string> m_refusals{};
};

// ===================================================================================================== debug views

/// How bodies are colored.
enum class BodyColoring : std::uint8_t
{
    BODY,   ///< Their own colors (darker while asleep).
    SLEEP,  ///< Awake, asleep, static.
    ISLAND, ///< One color per island: the dynamic bodies that touch or are joined.
};

/// What is drawn over the scene.
struct ViewSettings
{
    bool wireframe = false;                     ///< The meshes' edges.
    bool bounding_boxes = false;                ///< The colliders', soft bodies' and triggers' boxes.
    bool contacts = false;                      ///< The contact points and their normals.
    bool broad_phase = false;                   ///< The broad phase's nodes or cells.
    bool joints = false;                        ///< The joints' anchors and axes.
    BodyColoring coloring = BodyColoring::BODY; ///< How bodies are colored.
};

/// What the views gathered.
struct DebugStatistics
{
    std::uint32_t boxes = 0;          ///< Bounding boxes.
    std::uint32_t contact_points = 0; ///< Contact points.
    std::uint32_t nodes = 0;          ///< Broad phase nodes or cells.
    std::uint32_t joints = 0;         ///< Joints shown.
    std::uint32_t islands = 0;        ///< Islands found (ISLAND coloring).
    std::string structure_note{};     ///< Why the broad phase's structure is not shown, when it is not.
};

/// An island index of a body outside every island (static, kinematic).
inline constexpr std::uint32_t NO_ISLAND = 0xFFFFFFFFU;

/// The debug views of a scene: lines gathered between steps (at the last completed step's poses) and drawn over the
/// frame, and the bodies' islands.
class DebugViews
{
public:
    /// @brief Gathers what @p views show of @p scene (between steps).
    void capture(Scene& scene,
                 const ViewSettings& views);

    /// @brief The lines gathered.
    [[nodiscard]] auto lines() const noexcept -> const gfx::physics::DebugLines&
    {
        return m_lines;
    }

    /// @brief Each body's island, in the order of Scene::bodies() (NO_ISLAND for static and kinematic bodies); empty
    ///        unless the bodies are colored by island.
    [[nodiscard]] auto islands() const noexcept -> std::span<const std::uint32_t>
    {
        return m_islands;
    }

    /// @brief What was gathered.
    [[nodiscard]] auto statistics() const noexcept -> const DebugStatistics&
    {
        return m_statistics;
    }

private:
    void add_boxes(const Scene& scene);
    void add_contacts(const Scene& scene);
    void add_structure(Scene& scene);
    void add_joints(const Scene& scene);
    void find_islands(const Scene& scene);

    gfx::physics::DebugLines m_lines{};
    std::vector<std::uint32_t> m_islands{};
    DebugStatistics m_statistics{};
    std::vector<gfx::physics::ContactInfo> m_contacts{};
    std::vector<gfx::physics::SpatialNode> m_nodes{};
    std::vector<std::uint32_t> m_parents{};
    std::unordered_map<gfx::physics::RigidBodyHandle, std::uint32_t> m_body_indices{};
};

/// @brief The color of a body drawn with @p coloring: its own (darker while asleep), by sleep, or by island.
[[nodiscard]] auto body_color(const SceneBody& body,
                              BodyColoring coloring,
                              std::uint32_t island) noexcept -> gfx::physics::DebugColor;

// ===================================================================================================== script

/// The scripted controls and views (after the interaction's script, --script): pauses, steps once, runs at a quarter
/// of the speed, holds a ball without gravity then lets it fall, changes the solver, swaps the broad phase and the
/// integrator (refused on Box3D), turns every view on; then it checks what each did.
class ControlScript
{
public:
    /// @brief Frames it takes.
    static constexpr std::uint64_t FRAMES = 200;

    /// @param first_frame The frame it starts at (the interaction's script ends before it).
    explicit ControlScript(std::uint64_t first_frame) noexcept
        : m_first{first_frame}
    {
    }

    /// @brief Frame @p frame's settings and views (and what it adds to the scene), between steps, before the settings
    ///        are applied.
    void control(std::uint64_t frame,
                 Scene& scene,
                 SimulationSettings& settings,
                 ViewSettings& views);

    /// @brief Notes what frame @p frame did (the settings applied, the views gathered, the last frame drawn).
    void observe(std::uint64_t frame,
                 Scene& scene,
                 const SimulationSettings& settings,
                 const SimulationControl& control,
                 const DebugViews& debug,
                 const SceneRendererStatistics& drawn);

    /// @brief The checks, at the end of the run.
    [[nodiscard]] auto checks() const -> std::vector<ScriptCheck>;

private:
    std::uint64_t m_first = 0;
    std::uint64_t m_paused_steps = 0;
    std::uint64_t m_steps_while_paused = 0;
    std::uint64_t m_steps_after_single = 0;
    std::uint64_t m_slow_start = 0;
    std::uint64_t m_slow_steps = 0;
    gfx::physics::RigidBodyHandle m_ball{};
    float m_floating_height = 0.0F;
    float m_floating_speed = 0.0F;
    float m_fallen_height = 0.0F;
    std::uint32_t m_sub_steps = 0;
    std::uint32_t m_velocity_iterations = 0;
    bool m_replaced = false;
    std::string m_replacement_details{};
    DebugStatistics m_views{};
    std::uint32_t m_wireframe_draws = 0;
};

} // namespace physics_sandbox
