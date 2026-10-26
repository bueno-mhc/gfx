/// @file interaction.cppm
/// @brief How the sandbox is driven: a frame's commands (from the mouse and keyboard, or from a script — the same code
///        runs either way), the camera rig (orbit or free fly), and the interaction with the world — picking bodies under
///        the pointer, dragging them by a spring at the grabbed point, shooting balls, spawning and removing bodies.
///
/// The interaction queries the world and records edits between steps (after Scene::finish(), before Scene::advance()):
/// a query during a step returns STEP_IN_PROGRESS.
export module physics_sandbox.interaction;

import std;
export import gfx.camera;
export import physics_sandbox.scene;

export namespace physics_sandbox
{

/// What one frame asks of the camera and the world.
struct FrameCommands
{
    gfx::Vector2 pointer{};               ///< The pointer, in pixels of the framebuffer (origin top left).
    bool pointer_in_scene = true;         ///< Whether it is over the scene (not over the UI).
    bool grab_pressed = false;            ///< Grab the body under the pointer.
    bool grab_held = false;               ///< Keep dragging it.
    bool grab_released = false;           ///< Let it go.
    float push = 0.0F;                    ///< Meters to move a dragged body along the pointer's ray (away: positive).
    bool shoot = false;                   ///< Shoot a ball along the pointer's ray.
    std::optional<SpawnKind> spawn{};     ///< Spawn a body where the pointer points.
    bool remove = false;                  ///< Remove the body under the pointer.
    bool toggle_fly = false;              ///< Switch between orbiting and flying.
    gfx::Vector2 look{};                  ///< Turning, in radians: x to the right, y up.
    gfx::Vector2 pan{};                   ///< Orbit: the target's movement, in fractions of the distance (x right, y up).
    gfx::Vector3 fly{};                   ///< Fly: the movement, each component in [-1, 1] (x right, y up, z forward).
    float zoom = 0.0F;                    ///< Orbit: zoom steps (positive: closer).
};

/// How the camera moves.
enum class CameraMode : std::uint8_t
{
    ORBIT, ///< Around a target.
    FLY,   ///< Freely, along its own axes.
};

/// The camera's controllers: an orbit and a free fly one, the camera handed between them.
class CameraRig
{
public:
    /// @param orbit Where the orbit starts.
    explicit CameraRig(const gfx::camera::OrbitControllerDescription& orbit);

    /// @brief Moves and turns @p camera by @p commands over @p seconds (switching modes first when asked).
    void update(gfx::camera::Camera& camera,
                const FrameCommands& commands,
                float seconds);

    /// @brief Orbits again: around @p target, @p distance away, at @p yaw and @p pitch (radians); applied at the next
    ///        update().
    void aim(gfx::Vector3 target,
             float distance,
             float yaw,
             float pitch);

    /// @brief The current mode.
    [[nodiscard]] auto mode() const noexcept -> CameraMode
    {
        return m_mode;
    }

private:
    std::unique_ptr<gfx::camera::CameraController> m_orbit{};
    std::unique_ptr<gfx::camera::CameraController> m_fly{};
    CameraMode m_mode = CameraMode::ORBIT;
};

/// What the interaction did so far.
struct InteractionStatistics
{
    std::uint32_t grabs = 0;   ///< Bodies grabbed.
    std::uint32_t shots = 0;   ///< Balls shot.
    std::uint32_t spawned = 0; ///< Bodies spawned.
    std::uint32_t removed = 0; ///< Bodies removed.
};

/// Picking, dragging, shooting, spawning and removing.
class Interaction
{
public:
    explicit Interaction(Scene& scene) noexcept
        : m_scene{&scene}
    {
    }

    /// @brief Applies @p commands seen through @p camera on a framebuffer of @p framebuffer pixels; between steps.
    void update(const FrameCommands& commands,
                const gfx::camera::Camera& camera,
                gfx::Extent framebuffer);

    /// @brief The dynamic body under the pointer (null: none).
    [[nodiscard]] auto hovered() const noexcept -> gfx::physics::RigidBodyHandle
    {
        return m_hovered;
    }

    /// @brief The body being dragged (null: none).
    [[nodiscard]] auto dragged() const noexcept -> gfx::physics::RigidBodyHandle
    {
        return m_dragged;
    }

    /// @brief The last body shot, spawned and removed (null: none yet).
    [[nodiscard]] auto last_shot() const noexcept -> gfx::physics::RigidBodyHandle
    {
        return m_last_shot;
    }

    [[nodiscard]] auto last_spawned() const noexcept -> gfx::physics::RigidBodyHandle
    {
        return m_last_spawned;
    }

    [[nodiscard]] auto last_removed() const noexcept -> gfx::physics::RigidBodyHandle
    {
        return m_last_removed;
    }

    /// @brief Where the dragged body is pulled to (world space), while one is.
    [[nodiscard]] auto drag_target() const noexcept -> std::optional<gfx::Vector3>;

    /// @brief The drag's spring (from the grabbed point to its target) and the point under the pointer, as lines.
    void add_lines(gfx::physics::DebugLines& lines) const;

    /// @brief What it did so far.
    [[nodiscard]] auto statistics() const noexcept -> const InteractionStatistics&
    {
        return m_statistics;
    }

private:
    void release() noexcept;

    Scene* m_scene = nullptr;
    gfx::physics::RigidBodyHandle m_hovered{};
    std::optional<gfx::Vector3> m_hovered_point{};
    gfx::physics::RigidBodyHandle m_dragged{};
    gfx::Vector3 m_anchor{};   ///< The grabbed point, in the dragged body's frame.
    float m_depth = 0.0F;      ///< Its distance along the pointer's ray.
    gfx::Vector3 m_target{};   ///< Where it is pulled to.
    gfx::physics::RigidBodyHandle m_last_shot{};
    gfx::physics::RigidBodyHandle m_last_spawned{};
    gfx::physics::RigidBodyHandle m_last_removed{};
    InteractionStatistics m_statistics{};
};

/// A check of the scripted run.
struct ScriptCheck
{
    std::string description{};
    bool passed = false;
    std::string details{};
};

/// The scripted run (--script): over FRAMES frames of one step each, it grabs the pyramid's top crate and drags it up and
/// aside, shoots a ball at the pyramid, spawns a dumbbell on the floor, removes the body under the sphere's pixel, then
/// flies the camera forward for a second; then it checks what happened.
class Script
{
public:
    /// @brief Frames the script takes (run at least as many).
    static constexpr std::uint64_t FRAMES = 420;

    explicit Script(const Showcase& showcase) noexcept
        : m_showcase{showcase}
    {
    }

    /// @brief The commands of frame @p frame (counted from 0).
    [[nodiscard]] auto commands(std::uint64_t frame,
                                const Scene& scene,
                                const gfx::camera::Camera& camera,
                                gfx::Extent framebuffer) -> FrameCommands;

    /// @brief Notes what frame @p frame did (after the interaction's update and the scene's events).
    void observe(std::uint64_t frame,
                 const Scene& scene,
                 const Interaction& interaction,
                 const gfx::camera::Camera& camera,
                 const CameraRig& rig);

    /// @brief The checks, at the end of the run.
    [[nodiscard]] auto checks(const Scene& scene) const -> std::vector<ScriptCheck>;

private:
    Showcase m_showcase;
    gfx::Vector2 m_grab_pixel{};
    gfx::Vector2 m_drag_pixel{};
    gfx::Vector3 m_crate_start{};
    gfx::Vector3 m_crate_at_release{};
    gfx::Vector3 m_target_at_release{};
    bool m_grabbed_top_crate = false;
    gfx::physics::RigidBodyHandle m_ball{};
    bool m_ball_hit_pyramid = false;
    std::uint64_t m_ball_hit_frame = 0;
    gfx::Vector3 m_spawn_point{.x = -2.0F, .z = 3.0F};
    gfx::physics::RigidBodyHandle m_spawned{};
    std::size_t m_bodies_before_removal = 0;
    std::size_t m_bodies_after_removal = 0;
    gfx::physics::RigidBodyHandle m_removed{};
    gfx::Vector3 m_fly_start{};
    gfx::Vector3 m_fly_end{};
    bool m_flew = false;
    bool m_orbit_again = false;
};

} // namespace physics_sandbox
