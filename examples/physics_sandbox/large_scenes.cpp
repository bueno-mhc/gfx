// The physics comparison's large scenes: thousands of bodies, particles or links in the same order in any world.
module physics_sandbox.large_scenes;

import std;
import gfx.physics;
import physics_sandbox.renderer; // INSTANCE_GRID

namespace physics_sandbox
{

using namespace gfx;
using namespace gfx::physics;

namespace
{

constexpr float PI = std::numbers::pi_v<float>;

constexpr std::array<LargeSceneInformation, LARGE_SCENE_COUNT> INFORMATION{
    LargeSceneInformation{.name = "pyramid", .description = "a square pyramid of boxes"},
    LargeSceneInformation{.name = "pile", .description = "boxes, balls and capsules poured into a walled bin"},
    LargeSceneInformation{.name = "wrecking", .description = "a brick wall hit by a heavy ball swinging on a rope"},
    LargeSceneInformation{.name = "chains", .description = "rows of chains released level, swinging down"},
    LargeSceneInformation{.name = "cloth", .description = "a large cloth falling onto a sphere and boxes"},
};

/// One collider of @p shape.
[[nodiscard]] auto
collider(const std::shared_ptr<const Shape>& shape,
         float friction,
         float density) -> std::array<ColliderSpecification, 1>
{
    return {ColliderSpecification{
        .shape = shape, .material = {.friction = {.static_coefficient = friction, .dynamic_coefficient = friction}}, .density = density}};
}

/// The floor: the sandbox's (a static box with a grid drawn on it, its top at y = 0), reaching at least @p reach meters
/// from the origin along x and z with 10 m to spare (the sandbox's 40 m at least): a wall 117 m long stands on it whole.
void
add_large_floor(Scene& scene,
                float reach)
{
    const float half = std::max(40.0F, reach + 10.0F);
    const auto floor = make_shape(BoxShape{.half_extents = {.x = half, .y = 0.5F, .z = half}});
    const std::array floor_collider{
        ColliderSpecification{.shape = floor, .material = {.friction = {.static_coefficient = 0.7F, .dynamic_coefficient = 0.7F}}}};
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, floor_collider,
                                     {.red = 150, .green = 152, .blue = 158}, INSTANCE_GRID));
}

/// A static body of @p shape at @p pose.
void
add_static(Scene& scene,
           const std::shared_ptr<const Shape>& shape,
           const Transform& pose)
{
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC, .pose = pose}, collider(shape, 0.6F, 1000.0F), {.red = 130, .green = 135, .blue = 145}));
}

/// @p count scaled by @p size (in proportion), at least @p minimum.
[[nodiscard]] auto
scaled(float count,
       float size,
       int minimum) -> int
{
    return std::max(minimum, static_cast<int>(std::lround(count * size)));
}

/// The triangles of a grid of @p columns × @p rows particles (cloth_particle() order), two per cell.
[[nodiscard]] auto
grid_surface(std::uint32_t columns,
             std::uint32_t rows) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> triangles;
    triangles.reserve(std::size_t{columns - 1} * (rows - 1) * 6);
    for (std::uint32_t row = 0; row + 1 < rows; ++row)
    {
        for (std::uint32_t column = 0; column + 1 < columns; ++column)
        {
            const std::uint32_t a = cloth_particle(columns, column, row);
            const std::uint32_t b = cloth_particle(columns, column + 1, row);
            const std::uint32_t c = cloth_particle(columns, column, row + 1);
            const std::uint32_t d = cloth_particle(columns, column + 1, row + 1);
            triangles.insert(triangles.end(), {a, c, b, b, c, d});
        }
    }
    return triangles;
}

auto
pyramid(Scene& scene,
        float size) -> LargeSceneResult
{
    // About 2 000 boxes at size 1: a base of B boxes a side holds B(B + 1)(2B + 1)/6 of them, about B³/3.
    const int base = std::max(2, static_cast<int>(std::lround(std::cbrt(6000.0F * size))));
    constexpr float HALF = 0.25F;
    constexpr float PITCH = 2.0F * HALF + 0.01F;
    add_large_floor(scene, 0.5F * static_cast<float>(base) * PITCH);
    const auto boxes = collider(make_shape(BoxShape{.half_extents = {.x = HALF, .y = HALF, .z = HALF}}), 0.6F, 500.0F);
    std::uint32_t made = 0;
    for (int level = 0; level < base; ++level)
    {
        const int side = base - level;
        const float offset = 0.5F * static_cast<float>(side - 1) * PITCH;
        for (int row = 0; row < side; ++row)
        {
            for (int column = 0; column < side; ++column)
            {
                const Vector3 position{.x = static_cast<float>(column) * PITCH - offset,
                                       .y = HALF + static_cast<float>(level) * (2.0F * HALF + 0.002F),
                                       .z = static_cast<float>(row) * PITCH - offset};
                static_cast<void>(scene.add_body({.pose = {.position = position}}, boxes, palette(static_cast<std::size_t>(level) % 8)));
                ++made;
            }
        }
    }
    const float width = static_cast<float>(base) * PITCH;
    return {.view = {.target = {.y = 0.3F * width}, .distance = 1.9F * width + 8.0F, .yaw = 0.6F, .pitch = -0.35F}, .bodies = made};
}

auto
pile(Scene& scene,
     float size) -> LargeSceneResult
{
    // About 2 000 bodies at size 1 in layers of a square grid, jittered, falling into a bin of four walls.
    const int count = scaled(2000.0F, size, 8);
    constexpr float SPACING = 0.7F;
    constexpr int LAYERS = 12;
    const int side = std::max(2, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count) / static_cast<float>(LAYERS)))));
    const float inner = 0.5F * static_cast<float>(side) * SPACING + 0.4F;
    add_large_floor(scene, inner + 0.4F);
    const auto along_x = make_shape(BoxShape{.half_extents = {.x = inner + 0.4F, .y = 1.0F, .z = 0.2F}});
    const auto along_z = make_shape(BoxShape{.half_extents = {.x = 0.2F, .y = 1.0F, .z = inner}});
    add_static(scene, along_x, {.position = {.y = 1.0F, .z = inner + 0.2F}});
    add_static(scene, along_x, {.position = {.y = 1.0F, .z = -inner - 0.2F}});
    add_static(scene, along_z, {.position = {.x = inner + 0.2F, .y = 1.0F}});
    add_static(scene, along_z, {.position = {.x = -inner - 0.2F, .y = 1.0F}});

    const std::array kinds{collider(make_shape(BoxShape{.half_extents = {.x = 0.22F, .y = 0.22F, .z = 0.22F}}), 0.6F, 500.0F),
                           collider(make_shape(SphereShape{.radius = 0.25F}), 0.6F, 500.0F),
                           collider(make_shape(CapsuleShape{.half_height = 0.18F, .radius = 0.16F}), 0.6F, 500.0F)};
    std::mt19937 generator{2026};
    std::uniform_real_distribution<float> jitter{-0.08F, 0.08F};
    std::uniform_real_distribution<float> angle{0.0F, 2.0F * PI};
    const int per_layer = side * side;
    for (int index = 0; index < count; ++index)
    {
        const int layer = index / per_layer;
        const int row = (index % per_layer) / side;
        const int column = index % side;
        const float offset = 0.5F * static_cast<float>(side - 1) * SPACING;
        const Vector3 position{.x = static_cast<float>(column) * SPACING - offset + jitter(generator),
                               .y = 1.0F + static_cast<float>(layer) * 0.8F,
                               .z = static_cast<float>(row) * SPACING - offset + jitter(generator)};
        const Vector3 axis = normalized(Vector3{.x = jitter(generator), .y = 0.05F, .z = jitter(generator)});
        const Quaternion orientation = Quaternion::from_axis_angle(axis, angle(generator));
        static_cast<void>(scene.add_body({.pose = {.position = position, .orientation = orientation}}, kinds[static_cast<std::size_t>(index) % kinds.size()],
                                         palette(static_cast<std::size_t>(index) % 8)));
    }
    return {.view = {.target = {.y = 1.5F}, .distance = 2.4F * inner + 6.0F, .yaw = 0.5F, .pitch = -0.6F}, .bodies = static_cast<std::uint32_t>(count)};
}

auto
wrecking(Scene& scene,
         float size) -> LargeSceneResult
{
    // A wall of about 1 500 bricks at size 1, in staggered rows along x.
    const float side = std::sqrt(size);
    const int columns = scaled(48.0F, side, 4);
    const int rows = scaled(32.0F, side, 3);
    constexpr Vector3 HALF{.x = 0.3F, .y = 0.15F, .z = 0.2F};
    constexpr float PITCH = 2.0F * HALF.x + 0.01F;
    const auto bricks = collider(make_shape(BoxShape{.half_extents = HALF}), 0.6F, 600.0F);
    const float length = static_cast<float>(columns) * PITCH;
    add_large_floor(scene, 0.5F * length);
    std::uint32_t made = 0;
    for (int row = 0; row < rows; ++row)
    {
        const bool odd = row % 2 == 1;
        for (int column = 0; column < (odd ? columns - 1 : columns); ++column)
        {
            const float x = -0.5F * length + (static_cast<float>(column) + (odd ? 1.0F : 0.5F)) * PITCH;
            const Vector3 position{.x = x, .y = HALF.y + static_cast<float>(row) * (2.0F * HALF.y + 0.002F)};
            const float shade = static_cast<float>((row * 7 + column * 3) % 5) / 4.0F;
            const auto channel = [shade](float from, float to) { return static_cast<std::uint8_t>(from + (to - from) * shade); };
            static_cast<void>(scene.add_body({.pose = {.position = position}}, bricks,
                                             {.red = channel(165.0F, 205.0F), .green = channel(75.0F, 125.0F), .blue = channel(55.0F, 90.0F)}));
            ++made;
        }
    }

    // A steel ball on a rope, let go 70 degrees out on the wall's near side: it reaches the wall at the bottom of its arc,
    // at mid-height.
    const float height = static_cast<float>(rows) * 2.0F * HALF.y;
    const float rope = std::max(6.0F, height);
    const float radius = 0.8F + 0.4F * side;
    const Vector3 pivot{.y = 0.45F * height + rope, .z = -radius - HALF.z};
    constexpr float RELEASE = 70.0F * PI / 180.0F;
    const RigidBodyHandle ball =
        scene.add_body({.pose = {.position = pivot + Vector3{.y = -rope * std::cos(RELEASE), .z = -rope * std::sin(RELEASE)}},
                        .motion_quality = MotionQuality::CONTINUOUS},
                       collider(make_shape(SphereShape{.radius = radius}), 0.4F, 7800.0F), {.red = 60, .green = 62, .blue = 70})
            .body->handle();
    static_cast<void>(scene.add_joint(distance_joint({.first = ball, .second = {}, .anchor_first = {}, .anchor_second = pivot, .length = rope})));
    return {.view = {.target = {.y = 0.4F * height}, .distance = 1.05F * length + 12.0F, .yaw = 0.9F, .pitch = -0.2F},
            .bodies = made + 1,
            .joints = 1};
}

auto
chains(Scene& scene,
       float size) -> LargeSceneResult
{
    // About 24 chains of 40 links at size 1 (960 links and as many joints), held at one end and starting level.
    const float side = std::sqrt(size);
    const int count = scaled(24.0F, side, 2);
    const int links = scaled(40.0F, side, 4);
    constexpr float PITCH = 0.36F;
    const float length = static_cast<float>(links) * PITCH;
    add_large_floor(scene, std::max(0.5F * length, 0.35F * static_cast<float>(count)));
    const float height = length + 1.5F;
    const auto link = collider(make_shape(CapsuleShape{.half_height = 0.15F, .radius = 0.06F}), 0.4F, 2000.0F);
    const Quaternion along_x = Quaternion::from_axis_angle({.z = 1.0F}, PI / 2.0F);
    for (int chain = 0; chain < count; ++chain)
    {
        const Vector3 anchor{.x = -0.5F * length, .y = height, .z = (static_cast<float>(chain) - 0.5F * static_cast<float>(count - 1)) * 0.7F};
        RigidBodyHandle previous{};
        for (int index = 0; index < links; ++index)
        {
            const float x = static_cast<float>(index) * PITCH;
            const RigidBodyHandle body = scene.add_body({.pose = {.position = anchor + Vector3{.x = x + 0.5F * PITCH}, .orientation = along_x}, .angular_damping = 0.1F},
                                                        link, palette(static_cast<std::size_t>(chain) % 8))
                                             .body->handle();
            static_cast<void>(scene.add_joint(ball_and_socket_joint({.first = body, .second = previous, .anchor = anchor + Vector3{.x = x}})));
            previous = body;
        }
    }
    const auto made = static_cast<std::uint32_t>(count * links);
    // The view of the chains as they hang (below their anchors, at x = -length / 2), seen across their rows.
    return {.view = {.target = {.x = -0.5F * length, .y = 0.55F * height}, .distance = 1.5F * length + 0.8F * static_cast<float>(count) + 8.0F,
                     .yaw = 0.75F, .pitch = -0.2F},
            .bodies = made,
            .joints = made};
}

auto
cloth(Scene& scene,
      float size) -> LargeSceneResult
{
    // A cloth of about 6 400 particles at size 1 (10 cm apart: larger sizes make a larger cloth), flat above a sphere and
    // two boxes.
    const auto columns = static_cast<std::uint32_t>(scaled(80.0F, std::sqrt(size), 8));
    constexpr float SPACING = 0.1F;
    const float width = static_cast<float>(columns - 1) * SPACING;
    add_large_floor(scene, 0.5F * width);
    add_static(scene, make_shape(SphereShape{.radius = 0.15F * width}), {.position = {.x = -0.2F * width, .y = 0.15F * width}});
    add_static(scene, make_shape(BoxShape{.half_extents = {.x = 0.1F * width, .y = 0.08F * width, .z = 0.1F * width}}),
               {.position = {.x = 0.25F * width, .y = 0.08F * width, .z = 0.1F * width}});
    add_static(scene, make_shape(BoxShape{.half_extents = {.x = 0.06F * width, .y = 0.05F * width, .z = 0.06F * width}}),
               {.position = {.x = 0.1F * width, .y = 0.05F * width, .z = -0.3F * width}});
    const ClothPresetDescription flat{.columns = columns,
                                      .rows = columns,
                                      .spacing = SPACING,
                                      .origin = {.x = -0.5F * width, .y = 0.35F * width + 1.0F, .z = -0.5F * width},
                                      .column_direction = {.x = 1.0F},
                                      .row_direction = {.z = 1.0F},
                                      .area_density = 0.3F,
                                      .bending_compliance = 1.0e-2F};
    static_cast<void>(scene.add_soft_body(cloth_preset(flat), grid_surface(columns, columns), {.red = 200, .green = 70, .blue = 60}));
    return {.view = {.target = {.y = 0.15F * width}, .distance = 1.1F * width + 4.0F, .yaw = 0.5F, .pitch = -0.5F},
            .particles = columns * columns};
}

} // namespace

auto
large_scene_information(LargeScene scene) noexcept -> LargeSceneInformation
{
    return INFORMATION[std::to_underlying(scene)];
}

auto
find_large_scene(std::string_view name) noexcept -> std::optional<LargeScene>
{
    for (std::size_t index = 0; index < INFORMATION.size(); ++index)
    {
        if (INFORMATION[index].name == name)
        {
            return static_cast<LargeScene>(index);
        }
    }
    return std::nullopt;
}

auto
build_large_scene(LargeScene kind,
                  Scene& scene,
                  float size) -> LargeSceneResult
{
    switch (kind)
    {
        case LargeScene::PYRAMID: return pyramid(scene, size);
        case LargeScene::PILE: return pile(scene, size);
        case LargeScene::WRECKING: return wrecking(scene, size);
        case LargeScene::CHAINS: return chains(scene, size);
        case LargeScene::CLOTH: return cloth(scene, size);
    }
    return {};
}

} // namespace physics_sandbox
