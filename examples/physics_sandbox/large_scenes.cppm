/// @file large_scenes.cppm
/// @brief The physics comparison's scenes (gfx_physics_comparison): thousands of bodies, particles or links, built the
///        same way into a Scene of any backend, so two backends step the same scene side by side. A size scales every
///        count (1: about two thousand bodies; the counts grow in proportion to it).
export module physics_sandbox.large_scenes;

import std;
export import physics_sandbox.scene;
export import physics_sandbox.presets;

export namespace physics_sandbox
{

/// The large scenes.
enum class LargeScene : std::uint8_t
{
    PYRAMID,  ///< A square pyramid of boxes.
    PILE,     ///< Boxes, balls and capsules poured into a walled bin.
    WRECKING, ///< A brick wall hit by a heavy ball swinging on a rope.
    CHAINS,   ///< Rows of chains released level, swinging down.
    CLOTH,    ///< A large cloth falling onto a sphere and boxes.
};

/// How many large scenes there are.
inline constexpr std::size_t LARGE_SCENE_COUNT = 5;

/// The sizes the comparison's panel offers (a factor of every count).
inline constexpr std::array<float, 7> LARGE_SCENE_SIZES{0.25F, 0.5F, 1.0F, 2.0F, 4.0F, 8.0F, 16.0F};

/// A large scene's name and what it shows.
struct LargeSceneInformation
{
    std::string_view name{};        ///< Shown in the panel and on the command line (lower case).
    std::string_view description{}; ///< One line.
};

/// @brief @p scene's name and description.
[[nodiscard]] auto large_scene_information(LargeScene scene) noexcept -> LargeSceneInformation;

/// @brief The large scene named @p name (as large_scene_information() spells it), if any.
[[nodiscard]] auto find_large_scene(std::string_view name) noexcept -> std::optional<LargeScene>;

/// What a large scene built.
struct LargeSceneResult
{
    PresetView view{};           ///< Where the camera starts.
    std::uint32_t bodies = 0;    ///< Dynamic rigid bodies made.
    std::uint32_t particles = 0; ///< Soft bodies' particles made.
    std::uint32_t joints = 0;    ///< Joints made.
};

/// @brief Builds @p kind at @p size (greater than 0) into @p scene (empty; any backend). The same arguments build the
///        same bodies, in the same order, into any world (no randomness but a seeded one).
auto build_large_scene(LargeScene kind,
                       Scene& scene,
                       float size) -> LargeSceneResult;

} // namespace physics_sandbox
