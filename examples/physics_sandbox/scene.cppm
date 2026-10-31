/// @file scene.cppm
/// @brief The sandbox's scene: a physics world (Box3D or the reference backend) stepped at a fixed 60 Hz on its own
///        thread, the bodies the application made with their colliders and colors, and the poses rendering draws —
///        interpolated between the last two completed steps, so motion stays smooth whatever the frame rate.
///
/// Queries answer only between steps (a query made during one returns STEP_IN_PROGRESS), so a frame first finishes the
/// step begun by the frame before (finish()), then queries and edits the world, then begins the next step (advance()).
export module physics_sandbox.scene;

import std;
export import gfx.physics;

export namespace physics_sandbox
{

/// Seconds of simulated time per step.
inline constexpr float STEP = 1.0F / 60.0F;

/// Shot balls kept at most (the oldest goes).
inline constexpr std::size_t MAXIMUM_SHOTS = 40;

/// What can be spawned.
enum class SpawnKind : std::uint8_t
{
    SPHERE,
    CAPSULE,
    CYLINDER,
    CONE,
    CRATE,
    PLINTH,
    DUMBBELL,
};

/// How many spawn kinds there are.
inline constexpr std::size_t SPAWN_KIND_COUNT = 7;

/// A collider the scene made, with what drawing it needs.
struct SceneCollider
{
    std::unique_ptr<gfx::physics::Collider> collider{}; ///< The world's collider.
    std::shared_ptr<const gfx::physics::Shape> shape{}; ///< Its shape (the renderer's mesh key).
    gfx::Transform pose{};                              ///< In its body's frame.
};

/// A body the scene made.
struct SceneBody
{
    std::unique_ptr<gfx::physics::RigidBody> body{};              ///< The world's body.
    std::vector<SceneCollider> colliders{};                       ///< Its colliders.
    gfx::physics::BodyKind kind = gfx::physics::BodyKind::STATIC; ///< How it moves.
    gfx::physics::DebugColor color{};                             ///< Its color.
    std::uint32_t flags = 0;                                      ///< The renderer's instance flags (INSTANCE_GRID …).
    gfx::Transform previous{};                                    ///< Its pose after the step before the last.
    gfx::Transform current{};                                     ///< Its pose after the last completed step.
    bool awake = true;                                            ///< Whether it was simulated in the last step.
};

/// A soft body the scene made: drawn as a surface (cloth, volumes) or as a sphere at each particle (ropes).
struct SceneSoftBody
{
    std::unique_ptr<gfx::physics::SoftBody> body{};        ///< The world's soft body.
    std::vector<std::uint32_t> surface{};                  ///< Triangles (three particle indices each); empty: particles drawn.
    std::shared_ptr<const gfx::physics::Shape> particle{}; ///< The sphere drawn at each particle (no surface).
    gfx::physics::DebugColor color{};                      ///< Its color.
    std::vector<gfx::Vector3> previous{};                  ///< Its particles after the step before the last.
    std::vector<gfx::Vector3> current{};                   ///< After the last completed step.
};

/// A joint the scene made.
struct SceneJoint
{
    std::unique_ptr<gfx::physics::Joint> joint{};                    ///< The world's joint (gone with either body).
    gfx::physics::JointKind kind = gfx::physics::JointKind::DISTANCE; ///< What it is.
    gfx::physics::RigidBodyHandle first{};                           ///< Its first body.
    gfx::physics::RigidBodyHandle second{};                          ///< Its second (null: the world).
    gfx::Vector3 anchor_first{};                                     ///< Its anchor in the first body's frame.
    gfx::Vector3 anchor_second{};                                    ///< In the second body's frame (the world's when null).
    std::optional<gfx::Vector3> axis{};                              ///< Hinges, sliders, cone-twists, six-DOF: its axis in the first body's frame.
    bool cable = false;                                              ///< Distance joints: drawn as a line between the anchors.
    bool broken = false;                                             ///< Whether a JointEvent reported it broken.
};

/// A trigger the scene made, with what is in it.
struct SceneTrigger
{
    std::unique_ptr<gfx::physics::Trigger> trigger{};   ///< The world's trigger.
    std::string name{};                                 ///< Shown in the panel.
    std::shared_ptr<const gfx::physics::Shape> shape{}; ///< A sphere or a box (drawn as lines).
    gfx::Transform pose{};                              ///< In the world.
    gfx::physics::DebugColor color{};                   ///< Its lines' color.
    std::vector<gfx::physics::ColliderHandle> inside{}; ///< The colliders in it (entered, not left).
    std::uint32_t entered = 0;                          ///< ENTER events so far.
};

/// A collider to add with a body.
struct ColliderSpecification
{
    std::shared_ptr<const gfx::physics::Shape> shape{}; ///< Its shape (not null).
    gfx::Transform pose{};                              ///< In the body's frame.
    gfx::physics::SurfaceMaterial material{};           ///< Friction and restitution.
    float density = 1000.0F;                            ///< Kilograms per cubic meter.
};

/// The shapes bodies share (one mesh each in the renderer, whatever the number of bodies).
struct ShapeLibrary
{
    std::array<std::shared_ptr<const gfx::physics::Shape>, SPAWN_KIND_COUNT> spawned{}; ///< By SpawnKind.
    std::shared_ptr<const gfx::physics::Shape> ball{};                                  ///< What shooting throws.
};

/// A world and the bodies in it.
class Scene
{
public:
    /// @brief A world of @p backend whose steps run on its own thread and split over @p scheduler (outlives the scene).
    Scene(gfx::physics::PhysicsBackendKind backend,
          gfx::physics::TaskScheduler& scheduler);
    ~Scene();

    Scene(const Scene&) = delete;
    auto operator=(const Scene&) -> Scene& = delete;

    /// @brief The world.
    [[nodiscard]] auto world() noexcept -> gfx::physics::PhysicsWorld&
    {
        return *m_world;
    }

    [[nodiscard]] auto world() const noexcept -> const gfx::physics::PhysicsWorld&
    {
        return *m_world;
    }

    /// @brief The backend simulating it.
    [[nodiscard]] auto backend() const noexcept -> gfx::physics::PhysicsBackendKind
    {
        return m_backend;
    }

    /// @brief The shared shapes.
    [[nodiscard]] auto shapes() const noexcept -> const ShapeLibrary&
    {
        return m_shapes;
    }

    /// @brief A body of @p description with @p colliders, drawn in @p color.
    /// @throws gfx::BackendCapabilityError (and the body is not made) if the backend refuses a collider.
    auto add_body(const gfx::physics::RigidBodyDescription& description,
                  std::span<const ColliderSpecification> colliders,
                  gfx::physics::DebugColor color,
                  std::uint32_t flags = 0) -> SceneBody&;

    /// @brief A soft body of @p description drawn as @p surface (triangles of its particles; empty: a sphere of its
    ///        particle radius at each particle), in @p color.
    auto add_soft_body(const gfx::physics::SoftBodyDescription& description,
                       std::vector<std::uint32_t> surface,
                       gfx::physics::DebugColor color) -> SceneSoftBody&;

    /// @brief A joint of @p description (distance joints drawn as a line between their anchors).
    /// @throws gfx::BackendCapabilityError if the backend refuses it.
    auto add_joint(const gfx::physics::JointDescription& description) -> SceneJoint&;

    /// @brief A trigger named @p name: @p shape (a sphere or a box) at @p pose, drawn in @p color.
    auto add_trigger(std::string name,
                     const std::shared_ptr<const gfx::physics::Shape>& shape,
                     const gfx::Transform& pose,
                     gfx::physics::DebugColor color) -> SceneTrigger&;

    /// @brief Keeps @p note for the panel (what a backend refused, and what was made instead).
    void add_note(std::string note);

    /// @brief A dynamic body of @p kind at @p pose moving at @p velocity.
    auto spawn(SpawnKind kind,
               const gfx::Transform& pose,
               gfx::Vector3 velocity) -> SceneBody&;

    /// @brief A ball (continuous collision) from @p origin at @p velocity; the oldest goes beyond MAXIMUM_SHOTS.
    auto shoot(gfx::Vector3 origin,
               gfx::Vector3 velocity) -> SceneBody&;

    /// @brief Destroys @p body (with its colliders); false if the scene has no such body.
    auto remove_body(gfx::physics::RigidBodyHandle body) -> bool;

    /// @brief The body @p handle names, or null.
    [[nodiscard]] auto find(gfx::physics::RigidBodyHandle handle) noexcept -> SceneBody*;
    [[nodiscard]] auto find(gfx::physics::RigidBodyHandle handle) const noexcept -> const SceneBody*;

    /// @brief The body of @p collider, or null.
    [[nodiscard]] auto body_of(gfx::physics::ColliderHandle collider) const noexcept -> const SceneBody*;

    /// @brief The bodies, in the order they were made.
    [[nodiscard]] auto bodies() const noexcept -> const std::deque<SceneBody>&
    {
        return m_bodies;
    }

    /// @brief The soft bodies, joints and triggers, in the order they were made.
    [[nodiscard]] auto soft_bodies() const noexcept -> const std::deque<SceneSoftBody>&
    {
        return m_soft_bodies;
    }

    [[nodiscard]] auto joints() const noexcept -> const std::deque<SceneJoint>&
    {
        return m_joints;
    }

    [[nodiscard]] auto triggers() const noexcept -> const std::deque<SceneTrigger>&
    {
        return m_triggers;
    }

    /// @brief The notes added so far.
    [[nodiscard]] auto notes() const noexcept -> std::span<const std::string>
    {
        return m_notes;
    }

    /// @brief Advances by @p seconds of real time: completes the step begun by the last call, runs the steps due beyond
    ///        one at once, and begins the next one on the world's thread (it runs while the frame is drawn).
    void advance(double seconds);

    /// @brief Waits for a step in flight and takes its poses and events; the world then answers queries.
    void finish();

    /// @brief Paused: completes the step in flight and begins none; the latest poses are drawn.
    void hold();

    /// @brief Paused: completes the step in flight, then begins exactly one step (drawn once completed).
    void step_once();

    /// @brief Where @p body is drawn: between its last two completed poses.
    [[nodiscard]] auto drawn_pose(const SceneBody& body) const noexcept -> gfx::Transform;

    /// @brief Where the particles of @p soft_body are drawn (between their last two completed positions), into @p output.
    void drawn_positions(const SceneSoftBody& soft_body,
                         std::vector<gfx::Vector3>& output) const;

    /// @brief The contact events of the steps completed since clear_events().
    [[nodiscard]] auto contact_events() const noexcept -> std::span<const gfx::physics::ContactEvent>
    {
        return m_contacts;
    }

    /// @brief Forgets the contact events gathered so far.
    void clear_events() noexcept
    {
        m_contacts.clear();
    }

    /// @brief Steps completed.
    [[nodiscard]] auto step_count() const noexcept -> std::uint64_t
    {
        return m_world->step_count();
    }

    /// @brief How the last completed step went.
    [[nodiscard]] auto statistics() const -> gfx::physics::StepStatistics
    {
        return m_world->statistics();
    }

private:
    /// The poses and contact events of the step just completed.
    void capture();

    gfx::physics::PhysicsBackendKind m_backend = gfx::physics::PhysicsBackendKind::REFERENCE;
    ShapeLibrary m_shapes{};
    std::unique_ptr<gfx::physics::PhysicsWorld> m_world{};
    std::deque<SceneBody> m_bodies{}; // after the world: destroyed first
    std::deque<SceneSoftBody> m_soft_bodies{};
    std::deque<SceneJoint> m_joints{};
    std::deque<SceneTrigger> m_triggers{};
    std::vector<std::string> m_notes{};
    std::deque<gfx::physics::RigidBodyHandle> m_shots{};
    std::vector<gfx::physics::ContactEvent> m_contacts{};
    std::uint32_t m_spawned = 0;
    double m_accumulator = 0.0;
    float m_alpha = 0.0F;
};

/// The showcase's bodies a script points at.
struct Showcase
{
    std::vector<gfx::physics::RigidBodyHandle> pyramid{}; ///< Its crates, bottom row first.
    gfx::physics::RigidBodyHandle top_crate{};            ///< The pyramid's top crate.
    gfx::physics::RigidBodyHandle middle_crate{};         ///< A crate in the middle of the pyramid's front.
    gfx::physics::RigidBodyHandle sphere{};               ///< The first of the falling spheres.
};

/// @brief The first scene: a floor with a grid, a pyramid of crates, every convex shape kind falling (spheres, capsules,
///        cylinders, cones, a hull built at compile time, a compound), a static ramp and a static height-field mound.
auto build_showcase(Scene& scene) -> Showcase;

/// @brief The floor every scene stands on: a static slab whose top is at y = 0, drawn with the grid.
void add_floor(Scene& scene);

/// @brief The palette of bodies (@p index wraps).
[[nodiscard]] auto palette(std::size_t index) noexcept -> gfx::physics::DebugColor;

} // namespace physics_sandbox
