/// @file presets.cppm
/// @brief The sandbox's scenes: each preset builds its bodies, soft bodies, joints and triggers into a Scene of either
///        backend, and says where the camera starts. A backend that refuses something (Box3D: rounded boxes, separate
///        static friction, six-degree-of-freedom joints) gets the nearest thing it has instead, and the scene keeps a
///        note of what it refused and why (its BackendCapabilityError).
export module physics_sandbox.presets;

import std;
export import physics_sandbox.scene;

export namespace physics_sandbox
{

/// The scenes.
enum class Preset : std::uint8_t
{
    SHOWCASE,     ///< Every shape kind: a pyramid, falling convex shapes, a ramp, a height field.
    STACKS,       ///< A pyramid, a tower, a brick wall, a column of rounded crates.
    DOMINOES,     ///< A winding line of dominoes, the first one falling.
    CHAINS,       ///< A hanging chain, a rope bridge, a ragdoll-like figure, a wrecking ball.
    CLOTH,        ///< Cloth draped over a sphere and a box, a curtain.
    SOFT_VOLUMES, ///< Jelly cubes of growing softness, a rope holding a crate.
    FRICTION,     ///< Crates on a slope with growing friction, a crate that sticks then slides, rolling resistance.
    BOUNCING,     ///< Balls of growing restitution, bouncy crates.
    TRIGGERS,     ///< Three trigger zones, bodies swept through them by a motorized arm.
};

/// How many presets there are.
inline constexpr std::size_t PRESET_COUNT = 9;

/// A preset's name and what it shows.
struct PresetInformation
{
    std::string_view name{};        ///< Shown in the panel and on the command line (lower case).
    std::string_view description{}; ///< One line.
};

/// @brief @p preset's name and description.
[[nodiscard]] auto preset_information(Preset preset) noexcept -> PresetInformation;

/// @brief The preset named @p name (as preset_information() spells it), if any.
[[nodiscard]] auto find_preset(std::string_view name) noexcept -> std::optional<Preset>;

/// Where the orbit camera starts looking.
struct PresetView
{
    gfx::Vector3 target{}; ///< The point it turns around.
    float distance = 15.0F; ///< From the target.
    float yaw = 0.0F;       ///< Radians (0: looking along -z).
    float pitch = -0.35F;   ///< Radians (negative: looking down).
};

/// What a preset built.
struct PresetResult
{
    PresetView view{};                 ///< Where the camera starts.
    std::optional<Showcase> showcase{}; ///< SHOWCASE: the bodies its script points at.
};

/// @brief Builds @p preset into @p scene (empty; any backend).
auto build_preset(Preset preset,
                  Scene& scene) -> PresetResult;

} // namespace physics_sandbox
