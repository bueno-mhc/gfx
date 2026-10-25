/// @file scene.cppm
/// @brief The sandbox's scene: a physics world (Box3D or the reference backend) stepped at a fixed 60 Hz on its own
///        thread, the bodies the application made with their colliders and colors, and the poses rendering draws —
///        interpolated between the last two completed steps, so motion stays smooth whatever the frame rate.
export module physics_sandbox.scene;

import std;
export import gfx.physics;

export namespace physics_sandbox
{

/// Seconds of simulated time per step.
inline constexpr float STEP = 1.0F / 60.0F;

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
    std::unique_ptr<gfx::physics::RigidBody> body{};         ///< The world's body.
    std::vector<SceneCollider> colliders{};                  ///< Its colliders.
    gfx::physics::BodyKind kind = gfx::physics::BodyKind::STATIC; ///< How it moves.
    gfx::physics::DebugColor color{};                        ///< Its color.
    std::uint32_t flags = 0;                                 ///< The renderer's instance flags (INSTANCE_GRID …).
    gfx::Transform previous{};                               ///< Its pose after the step before the last.
    gfx::Transform current{};                                ///< Its pose after the last completed step.
    bool awake = true;                                       ///< Whether it was simulated in the last step.
};

/// A collider to add with a body.
struct ColliderSpecification
{
    std::shared_ptr<const gfx::physics::Shape> shape{}; ///< Its shape (not null).
    gfx::Transform pose{};                              ///< In the body's frame.
    gfx::physics::SurfaceMaterial material{};           ///< Friction and restitution.
    float density = 1000.0F;                            ///< Kilograms per cubic meter.
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

    /// @brief The backend simulating it.
    [[nodiscard]] auto backend() const noexcept -> gfx::physics::PhysicsBackendKind
    {
        return m_backend;
    }

    /// @brief A body of @p description with @p colliders, drawn in @p color.
    auto add_body(const gfx::physics::RigidBodyDescription& description,
                  std::span<const ColliderSpecification> colliders,
                  gfx::physics::DebugColor color,
                  std::uint32_t flags = 0) -> SceneBody&;

    /// @brief The bodies, in the order they were made.
    [[nodiscard]] auto bodies() const noexcept -> const std::deque<SceneBody>&
    {
        return m_bodies;
    }

    /// @brief Advances by @p seconds of real time: completes the step begun by the last call, runs the steps due beyond
    ///        one at once, and begins the next one on the world's thread (it runs while the frame is drawn).
    void advance(double seconds);

    /// @brief Waits for a step in flight and takes its poses.
    void finish();

    /// @brief Where @p body is drawn: between its last two completed poses.
    [[nodiscard]] auto drawn_pose(const SceneBody& body) const noexcept -> gfx::Transform;

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
    /// The poses of the step just completed.
    void capture();

    gfx::physics::PhysicsBackendKind m_backend = gfx::physics::PhysicsBackendKind::REFERENCE;
    std::unique_ptr<gfx::physics::PhysicsWorld> m_world{};
    std::deque<SceneBody> m_bodies{}; // after the world: destroyed first
    double m_accumulator = 0.0;
    float m_alpha = 0.0F;
};

/// @brief The first scene: a floor with a grid, a pyramid of crates, every convex shape kind falling (spheres, capsules,
///        cylinders, cones, a hull built at compile time, a compound), a static ramp and a static height-field mound.
void build_showcase(Scene& scene);

} // namespace physics_sandbox
