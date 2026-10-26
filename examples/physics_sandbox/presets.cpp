// The sandbox's presets: each scene's bodies, soft bodies, joints and triggers, with what a backend gets instead of what
// it refuses.
module physics_sandbox.presets;

import std;
import gfx.physics;

namespace physics_sandbox
{

using namespace gfx;
using namespace gfx::physics;

namespace
{

constexpr float PI = std::numbers::pi_v<float>;

constexpr std::array<PresetInformation, PRESET_COUNT> INFORMATION{
    PresetInformation{.name = "showcase", .description = "every shape kind: a pyramid, falling convex shapes, a ramp, a height field"},
    PresetInformation{.name = "stacks", .description = "a pyramid, a tower, a brick wall and a column of rounded crates"},
    PresetInformation{.name = "dominoes", .description = "a winding line of dominoes, the first one falling"},
    PresetInformation{.name = "chains", .description = "a hanging chain, a rope bridge, a ragdoll-like figure, a wrecking ball"},
    PresetInformation{.name = "cloth", .description = "cloth draped over a sphere and a box, a curtain on a bar"},
    PresetInformation{.name = "soft_volumes", .description = "jelly cubes from stiff to soft, a rope holding a crate"},
    PresetInformation{.name = "friction", .description = "crates of growing friction on a slope, one that sticks until pushed, rolling resistance"},
    PresetInformation{.name = "bouncing", .description = "balls from no bounce to almost elastic, bouncy crates, a tilted plate"},
    PresetInformation{.name = "triggers", .description = "three trigger zones, bodies swept through them by a motorized arm"},
};

// ------------------------------------------------------------------------------------------------- helpers

/// A box of @p half_extents (rounded by @p radius).
[[nodiscard]] auto
box(Vector3 half_extents,
    float radius = 0.0F) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half_extents, .radius = radius});
}

/// One collider of @p shape.
[[nodiscard]] auto
collider(const std::shared_ptr<const Shape>& shape,
         const SurfaceMaterial& material = {},
         float density = 1000.0F) -> std::array<ColliderSpecification, 1>
{
    return {ColliderSpecification{.shape = shape, .material = material, .density = density}};
}

/// Equal static and dynamic friction @p coefficient.
[[nodiscard]] constexpr auto
friction(float coefficient) noexcept -> Friction
{
    return {.static_coefficient = coefficient, .dynamic_coefficient = coefficient};
}

/// A static body with one collider.
void
add_static(Scene& scene,
           const std::shared_ptr<const Shape>& shape,
           const Transform& pose,
           DebugColor color,
           const SurfaceMaterial& material = {})
{
    static_cast<void>(scene.add_body({.kind = BodyKind::STATIC, .pose = pose}, collider(shape, material), color));
}

/// @p from to @p to by @p fraction.
[[nodiscard]] constexpr auto
mix(DebugColor from,
    DebugColor to,
    float fraction) noexcept -> DebugColor
{
    const auto channel = [fraction](std::uint8_t a, std::uint8_t b)
    {
        return static_cast<std::uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * fraction + 0.5F);
    };
    return {.red = channel(from.red, to.red), .green = channel(from.green, to.green), .blue = channel(from.blue, to.blue)};
}

/// Notes what @p error refused and what was made @p instead (once per distinct note).
void
note_refusal(Scene& scene,
             const BackendCapabilityError& error,
             std::string_view instead)
{
    // The reason's first line (the next ones name the object).
    const std::string_view reason = error.reason().substr(0, error.reason().find('\n'));
    std::string note = std::format("{}: {}; {}", error.capability(), reason, instead);
    if (!std::ranges::contains(scene.notes(), note))
    {
        scene.add_note(std::move(note));
    }
}

/// A body with @p wanted colliders, or with @p fallback ones when the backend refuses them.
auto
add_body_or(Scene& scene,
            const RigidBodyDescription& description,
            std::span<const ColliderSpecification> wanted,
            std::span<const ColliderSpecification> fallback,
            DebugColor color,
            std::string_view instead) -> SceneBody&
{
    try
    {
        return scene.add_body(description, wanted, color);
    }
    catch (const BackendCapabilityError& error)
    {
        note_refusal(scene, error, instead);
        return scene.add_body(description, fallback, color);
    }
}

/// The @p wanted joint, or the @p fallback one when the backend refuses it.
auto
add_joint_or(Scene& scene,
             const JointDescription& wanted,
             const JointDescription& fallback,
             std::string_view instead) -> SceneJoint&
{
    try
    {
        return scene.add_joint(wanted);
    }
    catch (const BackendCapabilityError& error)
    {
        note_refusal(scene, error, instead);
        return scene.add_joint(fallback);
    }
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

/// The boundary of @p mesh: the faces of one tetrahedron only, each turned away from its tetrahedron's fourth corner.
[[nodiscard]] auto
tetrahedral_surface(const TetrahedralMesh& mesh) -> std::vector<std::uint32_t>
{
    // Each face by its sorted corners: how many tetrahedra have it, and its outward winding.
    std::map<std::array<std::uint32_t, 3>, std::pair<std::uint32_t, std::array<std::uint32_t, 3>>> faces;
    for (const std::array<std::uint32_t, 4>& tetrahedron : mesh.tetrahedra)
    {
        for (std::size_t opposite = 0; opposite < 4; ++opposite)
        {
            std::array<std::uint32_t, 3> face{};
            std::size_t corner = 0;
            for (std::size_t index = 0; index < 4; ++index)
            {
                if (index != opposite)
                {
                    face[corner++] = tetrahedron[index];
                }
            }
            const Vector3 a = mesh.positions[face[0]];
            const Vector3 normal = cross(mesh.positions[face[1]] - a, mesh.positions[face[2]] - a);
            if (dot(normal, mesh.positions[tetrahedron[opposite]] - a) > 0.0F)
            {
                std::swap(face[1], face[2]);
            }
            std::array<std::uint32_t, 3> key = face;
            std::ranges::sort(key);
            auto& [count, winding] = faces[key];
            ++count;
            winding = face;
        }
    }

    // The faces seen once.
    std::vector<std::uint32_t> triangles;
    for (const auto& [key, face] : faces)
    {
        if (face.first == 1)
        {
            triangles.insert(triangles.end(), face.second.begin(), face.second.end());
        }
    }
    return triangles;
}

// ------------------------------------------------------------------------------------------------- the presets

auto
stacks(Scene& scene) -> PresetResult
{
    add_floor(scene);
    const auto crate = scene.shapes().spawned[std::to_underlying(SpawnKind::CRATE)];
    const auto crates = collider(crate, {}, 400.0F);

    // A pyramid of eight levels.
    constexpr int LEVELS = 8;
    for (int level = 0; level < LEVELS; ++level)
    {
        for (int column = 0; column < LEVELS - level; ++column)
        {
            const float x = -8.5F + static_cast<float>(column) * 0.82F + static_cast<float>(level) * 0.41F;
            static_cast<void>(scene.add_body({.pose = {.position = {.x = x, .y = 0.4F + static_cast<float>(level) * 0.8F}}}, crates,
                                             palette(static_cast<std::size_t>(level) % 4)));
        }
    }

    // A tower of ten crates, and a column of rounded ones (sharp ones where the backend has no rounded boxes).
    for (int level = 0; level < 10; ++level)
    {
        static_cast<void>(scene.add_body({.pose = {.position = {.x = 0.5F, .y = 0.4F + static_cast<float>(level) * 0.8F}}}, crates,
                                         palette(4 + static_cast<std::size_t>(level % 2))));
    }
    const auto rounded = collider(box({.x = 0.33F, .y = 0.33F, .z = 0.33F}, 0.07F), {}, 400.0F);
    for (int level = 0; level < 6; ++level)
    {
        static_cast<void>(add_body_or(scene, {.pose = {.position = {.x = 2.2F, .y = 0.4F + static_cast<float>(level) * 0.8F}}}, rounded, crates,
                                      palette(6 + static_cast<std::size_t>(level % 2)), "made sharp crates instead"));
    }

    // A wall of bricks in staggered rows.
    const auto brick = collider(box({.x = 0.4F, .y = 0.2F, .z = 0.25F}), {}, 600.0F);
    for (int row = 0; row < 6; ++row)
    {
        const bool odd = row % 2 == 1;
        for (int column = 0; column < (odd ? 7 : 8); ++column)
        {
            const float x = 4.0F + static_cast<float>(column) * 0.82F + (odd ? 0.41F : 0.0F);
            static_cast<void>(scene.add_body({.pose = {.position = {.x = x, .y = 0.2F + static_cast<float>(row) * 0.4F}}}, brick,
                                             mix({.red = 170, .green = 80, .blue = 60}, {.red = 205, .green = 130, .blue = 95}, static_cast<float>((row + column) % 3) / 2.0F)));
        }
    }
    return {.view = {.target = {.x = 0.5F, .y = 2.0F}, .distance = 18.0F, .yaw = 0.0F, .pitch = -0.25F}};
}

auto
dominoes(Scene& scene) -> PresetResult
{
    // Dominoes every half meter along a winding line, each turned along it; the first one tipped over.
    add_floor(scene);
    const auto domino = collider(box({.x = 0.06F, .y = 0.45F, .z = 0.22F}), {.friction = friction(0.5F)}, 600.0F);
    const auto point = [](float x) { return Vector3{.x = x, .y = 0.45F, .z = 2.5F * std::sin(0.45F * x)}; };
    const auto direction = [](float x) { return normalized(Vector3{.x = 1.0F, .z = 2.5F * 0.45F * std::cos(0.45F * x)}); };
    std::optional<Vector3> placed{};
    std::size_t count = 0;
    for (int sample = 0; sample <= 18000; ++sample)
    {
        const float x = -9.0F + static_cast<float>(sample) * 0.001F;
        const Vector3 position = point(x);
        if (placed && distance(position, *placed) < 0.5F)
        {
            continue;
        }
        const Vector3 along = direction(x);
        const Quaternion orientation = Quaternion::from_axis_angle({.y = 1.0F}, std::atan2(-along.z, along.x));
        const Vector3 tip = count == 0 ? cross(Vector3{.y = 1.0F}, along) * 3.0F : Vector3{};
        static_cast<void>(scene.add_body({.pose = {.position = position, .orientation = orientation}, .angular_velocity = tip}, domino,
                                         mix({.red = 235, .green = 225, .blue = 205}, {.red = 70, .green = 150, .blue = 220}, static_cast<float>(count % 12) / 11.0F)));
        placed = position;
        ++count;
    }
    return {.view = {.target = {.y = 0.5F, .z = 0.5F}, .distance = 15.0F, .yaw = 0.0F, .pitch = -0.75F}};
}

/// Arc points from @p first to @p last (same height) sagging below their chord: @p count + 1 points, equally spaced on a
/// circle whose arc is 7 % longer than the chord.
[[nodiscard]] auto
sagging_points(Vector3 first,
               Vector3 last,
               std::uint32_t count) -> std::vector<Vector3>
{
    // Half the arc's angle h, from h / sin h = 1.07 (an arc 7 % longer than its chord).
    constexpr float HALF_ANGLE = 0.64F;
    const float chord = distance(first, last);
    const float radius = chord / (2.0F * std::sin(HALF_ANGLE));
    const Vector3 along = normalized(last - first);
    const Vector3 center = (first + last) * 0.5F + Vector3{.y = radius * std::cos(HALF_ANGLE)};
    std::vector<Vector3> points(count + 1);
    for (std::uint32_t index = 0; index <= count; ++index)
    {
        const float angle = -HALF_ANGLE + 2.0F * HALF_ANGLE * static_cast<float>(index) / static_cast<float>(count);
        points[index] = center + along * (radius * std::sin(angle)) - Vector3{.y = radius * std::cos(angle)};
    }
    return points;
}

auto
chains(Scene& scene) -> PresetResult
{
    add_floor(scene);

    // A chain of capsules held at one end, starting level: it falls and swings.
    const auto link = collider(make_shape(CapsuleShape{.half_height = 0.15F, .radius = 0.06F}), {}, 2000.0F);
    const Quaternion along_x = Quaternion::from_axis_angle({.z = 1.0F}, PI / 2.0F);
    constexpr Vector3 CHAIN_ANCHOR{.x = -9.0F, .y = 6.5F};
    RigidBodyHandle previous{};
    for (int index = 0; index < 14; ++index)
    {
        const float x = static_cast<float>(index) * 0.36F;
        const RigidBodyHandle body = scene.add_body({.pose = {.position = CHAIN_ANCHOR + Vector3{.x = x + 0.18F}, .orientation = along_x}, .angular_damping = 0.1F},
                                                    link, palette(7 - static_cast<std::size_t>(index % 2)))
                                         .body->handle();
        static_cast<void>(scene.add_joint(ball_and_socket_joint({.first = body, .second = previous, .anchor = CHAIN_ANCHOR + Vector3{.x = x}})));
        previous = body;
    }

    // A rope bridge: planks on hinges between two posts, sagging, three crates dropped on it.
    const auto post = box({.x = 0.3F, .y = 1.25F, .z = 0.8F});
    add_static(scene, post, {.position = {.x = -3.3F, .y = 1.25F}}, {.red = 120, .green = 110, .blue = 100});
    add_static(scene, post, {.position = {.x = 3.3F, .y = 1.25F}}, {.red = 120, .green = 110, .blue = 100});
    const std::vector<Vector3> points = sagging_points({.x = -3.0F, .y = 2.45F}, {.x = 3.0F, .y = 2.45F}, 16);
    previous = {};
    for (std::size_t index = 0; index + 1 < points.size(); ++index)
    {
        const Vector3 from = points[index];
        const Vector3 to = points[index + 1];
        const auto plank = collider(box({.x = distance(from, to) * 0.5F - 0.01F, .y = 0.04F, .z = 0.5F}), {}, 500.0F);
        const Quaternion slope = Quaternion::from_axis_angle({.z = 1.0F}, std::atan2(to.y - from.y, to.x - from.x));
        const RigidBodyHandle body = scene.add_body({.pose = {.position = (from + to) * 0.5F, .orientation = slope}}, plank,
                                                    index % 2 == 0 ? DebugColor{.red = 170, .green = 120, .blue = 70} : DebugColor{.red = 150, .green = 105, .blue = 60})
                                         .body->handle();
        static_cast<void>(scene.add_joint(hinge_joint({.first = body, .second = previous, .anchor = from, .axis = {.z = 1.0F}})));
        if (index + 2 == points.size())
        {
            static_cast<void>(scene.add_joint(hinge_joint({.first = body, .second = {}, .anchor = to, .axis = {.z = 1.0F}})));
        }
        previous = body;
    }
    const auto small_crate = collider(box({.x = 0.3F, .y = 0.3F, .z = 0.3F}), {}, 300.0F);
    for (int index = 0; index < 3; ++index)
    {
        static_cast<void>(scene.add_body({.pose = {.position = {.x = -1.2F + static_cast<float>(index) * 1.3F, .y = 4.0F + static_cast<float>(index)}}}, small_crate,
                                         palette(static_cast<std::size_t>(index))));
    }

    // A figure of ten parts: cone-twist neck, shoulders and hips, hinged elbows and knees, a six-degree-of-freedom waist
    // (a cone-twist one where the backend has none).
    constexpr float X = 7.0F;
    const DebugColor skin{.red = 225, .green = 185, .blue = 150};
    const DebugColor shirt{.red = 70, .green = 120, .blue = 190};
    const DebugColor trousers{.red = 60, .green = 60, .blue = 75};
    const auto part = [&scene](const std::shared_ptr<const Shape>& shape, const Transform& pose, DebugColor color)
    {
        return scene.add_body({.pose = pose, .angular_damping = 0.2F}, collider(shape), color).body->handle();
    };
    const auto upper_arm = make_shape(CapsuleShape{.half_height = 0.12F, .radius = 0.05F});
    const auto forearm = make_shape(CapsuleShape{.half_height = 0.11F, .radius = 0.045F});
    const auto thigh = make_shape(CapsuleShape{.half_height = 0.16F, .radius = 0.07F});
    const auto shin = make_shape(CapsuleShape{.half_height = 0.16F, .radius = 0.06F});
    const RigidBodyHandle pelvis = part(box({.x = 0.16F, .y = 0.1F, .z = 0.1F}), {.position = {.x = X, .y = 1.15F}}, trousers);
    const RigidBodyHandle torso = part(box({.x = 0.18F, .y = 0.24F, .z = 0.11F}), {.position = {.x = X, .y = 1.51F}}, shirt);
    const RigidBodyHandle head = part(make_shape(SphereShape{.radius = 0.12F}), {.position = {.x = X, .y = 1.89F}}, skin);
    SixDegreesOfFreedomJointDescription waist{.first = pelvis, .second = torso, .frame = {.position = {.x = X, .y = 1.26F}}};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        waist.motion[axis] = AxisMotion::LOCKED;
        waist.motion[axis + 3] = AxisMotion::LIMITED;
    }
    waist.limits[std::to_underlying(JointAxis::ANGULAR_X)] = {.lower = -0.5F, .upper = 0.5F};
    waist.limits[std::to_underlying(JointAxis::ANGULAR_Y)] = {.lower = -0.4F, .upper = 0.4F};
    waist.limits[std::to_underlying(JointAxis::ANGULAR_Z)] = {.lower = -0.3F, .upper = 0.3F};
    static_cast<void>(add_joint_or(scene, six_degrees_of_freedom_joint(waist),
                                   cone_twist_joint({.first = pelvis, .second = torso, .anchor = {.x = X, .y = 1.26F}, .axis = {.y = 1.0F}, .swing_limit = 0.5F,
                                                     .twist_limits = JointLimits{.lower = -0.4F, .upper = 0.4F}}),
                                   "made a cone-twist waist instead"));
    static_cast<void>(scene.add_joint(cone_twist_joint({.first = torso, .second = head, .anchor = {.x = X, .y = 1.76F}, .axis = {.y = 1.0F}, .swing_limit = 0.6F,
                                                        .twist_limits = JointLimits{.lower = -0.6F, .upper = 0.6F}})));
    for (const float side : {1.0F, -1.0F})
    {
        // An arm along x (side), a leg below the pelvis.
        const Quaternion sideways = Quaternion::from_axis_angle({.z = 1.0F}, PI / 2.0F);
        const RigidBodyHandle arm = part(upper_arm, {.position = {.x = X + side * 0.37F, .y = 1.69F}, .orientation = sideways}, shirt);
        const RigidBodyHandle hand = part(forearm, {.position = {.x = X + side * 0.715F, .y = 1.69F}, .orientation = sideways}, skin);
        const RigidBodyHandle upper_leg = part(thigh, {.position = {.x = X + side * 0.09F, .y = 0.8F}}, trousers);
        const RigidBodyHandle lower_leg = part(shin, {.position = {.x = X + side * 0.09F, .y = 0.33F}}, trousers);
        static_cast<void>(scene.add_joint(cone_twist_joint({.first = torso, .second = arm, .anchor = {.x = X + side * 0.2F, .y = 1.69F}, .axis = {.x = side},
                                                            .swing_limit = 1.2F, .twist_limits = JointLimits{.lower = -0.5F, .upper = 0.5F}})));
        static_cast<void>(scene.add_joint(hinge_joint({.first = arm,
                                                       .second = hand,
                                                       .anchor = {.x = X + side * 0.54F, .y = 1.69F},
                                                       .axis = {.y = 1.0F},
                                                       .limits = side > 0.0F ? JointLimits{.lower = -2.3F, .upper = 0.0F} : JointLimits{.lower = 0.0F, .upper = 2.3F}})));
        static_cast<void>(scene.add_joint(cone_twist_joint({.first = pelvis, .second = upper_leg, .anchor = {.x = X + side * 0.09F, .y = 1.04F}, .axis = {.y = -1.0F},
                                                            .swing_limit = 0.9F, .twist_limits = JointLimits{.lower = -0.3F, .upper = 0.3F}})));
        static_cast<void>(scene.add_joint(hinge_joint({.first = upper_leg,
                                                       .second = lower_leg,
                                                       .anchor = {.x = X + side * 0.09F, .y = 0.56F},
                                                       .axis = {.x = 1.0F},
                                                       .limits = JointLimits{.lower = 0.0F, .upper = 2.3F}})));
    }

    // A wrecking ball on a rod from a fixed point, let go at 60 degrees, swinging into a stack of crates.
    constexpr Vector3 PIVOT{.y = 9.0F, .z = -5.0F};
    constexpr float ROD = 5.5F;
    const RigidBodyHandle ball =
        scene.add_body({.pose = {.position = PIVOT + Vector3{.x = -ROD * 0.8660254F, .y = -ROD * 0.5F}}, .motion_quality = MotionQuality::CONTINUOUS},
                       collider(make_shape(SphereShape{.radius = 0.5F}), {}, 3000.0F), {.red = 60, .green = 62, .blue = 70})
            .body->handle();
    static_cast<void>(scene.add_joint(distance_joint({.first = ball, .second = {}, .anchor_first = {}, .anchor_second = PIVOT, .length = ROD})));
    const auto crates = collider(scene.shapes().spawned[std::to_underlying(SpawnKind::CRATE)], {}, 400.0F);
    for (int level = 0; level < 5; ++level)
    {
        for (const float z : {-5.41F, -4.59F})
        {
            static_cast<void>(scene.add_body({.pose = {.position = {.x = 1.2F, .y = 0.4F + static_cast<float>(level) * 0.8F, .z = z}}}, crates,
                                             palette(static_cast<std::size_t>(level) % 4)));
        }
    }
    return {.view = {.target = {.x = -1.0F, .y = 3.0F, .z = -1.0F}, .distance = 25.0F, .yaw = 0.0F, .pitch = -0.25F}};
}

auto
cloth(Scene& scene) -> PresetResult
{
    add_floor(scene);

    // Two cloths falling flat onto a sphere and a box.
    add_static(scene, make_shape(SphereShape{.radius = 1.0F}), {.position = {.x = -3.0F, .y = 1.0F}}, {.red = 130, .green = 135, .blue = 145});
    add_static(scene, box({.x = 1.0F, .y = 0.6F, .z = 0.8F}), {.position = {.x = 2.5F, .y = 0.6F}}, {.red = 130, .green = 135, .blue = 145});
    constexpr std::uint32_t SIDE = 22;
    constexpr float SPACING = 0.12F;
    constexpr float HALF = 0.5F * SPACING * static_cast<float>(SIDE - 1);
    for (const auto& [center, color] : {std::pair{Vector3{.x = -3.0F, .y = 2.6F}, DebugColor{.red = 200, .green = 60, .blue = 60}},
                                        std::pair{Vector3{.x = 2.5F, .y = 2.0F}, DebugColor{.red = 60, .green = 120, .blue = 200}}})
    {
        const ClothPresetDescription flat{.columns = SIDE,
                                          .rows = SIDE,
                                          .spacing = SPACING,
                                          .origin = center - Vector3{.x = HALF, .z = HALF},
                                          .column_direction = {.x = 1.0F},
                                          .row_direction = {.z = 1.0F},
                                          .area_density = 0.3F,
                                          .bending_compliance = 1.0e-2F};
        static_cast<void>(scene.add_soft_body(cloth_preset(flat), grid_surface(SIDE, SIDE), color));
    }

    // A curtain hanging from a bar by its top row, held from stretching by long-range attachments.
    add_static(scene, make_shape(CapsuleShape{.half_height = 1.7F, .radius = 0.05F}),
               {.position = {.x = 6.6F, .y = 4.0F, .z = -1.0F}, .orientation = Quaternion::from_axis_angle({.z = 1.0F}, PI / 2.0F)}, {.red = 90, .green = 90, .blue = 95});
    constexpr ClothPresetDescription CURTAIN{.columns = 26, .rows = 20, .spacing = 0.11F, .origin = {.x = 5.2F, .y = 3.88F, .z = -1.0F}, .area_density = 0.3F};
    SoftBodyDescription curtain = cloth_preset(CURTAIN);
    for (std::uint32_t column = 0; column < CURTAIN.columns; ++column)
    {
        curtain.pins.push_back(pin_to_world(curtain, cloth_particle(CURTAIN.columns, column, 0)));
    }
    static_cast<void>(add_long_range_attachments(curtain));
    static_cast<void>(scene.add_soft_body(curtain, grid_surface(CURTAIN.columns, CURTAIN.rows), {.red = 230, .green = 200, .blue = 90}));
    return {.view = {.target = {.x = 2.0F, .y = 1.5F}, .distance = 14.0F, .yaw = 0.0F, .pitch = -0.45F}};
}

auto
soft_volumes(Scene& scene) -> PresetResult
{
    add_floor(scene);

    // Four jelly cubes, stiff to soft, two dropped on a ramp, two on the floor (soft bodies do not collide with each
    // other: they fall apart).
    add_static(scene, box({.x = 2.5F, .y = 0.15F, .z = 1.5F}), {.position = {.x = -3.0F, .y = 1.0F}, .orientation = Quaternion::from_axis_angle({.z = 1.0F}, -0.35F)},
               {.red = 120, .green = 124, .blue = 135});
    constexpr std::array<std::pair<Vector3, float>, 4> CUBES{std::pair{Vector3{.x = -4.2F, .y = 3.0F, .z = -0.6F}, 0.0F},
                                                             std::pair{Vector3{.x = -3.4F, .y = 4.2F, .z = 0.6F}, 2.0e-5F},
                                                             std::pair{Vector3{.x = 0.5F, .y = 1.5F, .z = -1.0F}, 2.0e-4F},
                                                             std::pair{Vector3{.x = 1.8F, .y = 2.5F, .z = 0.7F}, 1.0e-3F}};
    for (std::size_t index = 0; index < CUBES.size(); ++index)
    {
        const TetrahedralMesh mesh = tetrahedral_box({.half_extents = {.x = 0.4F, .y = 0.4F, .z = 0.4F}, .cells = {3, 3, 3}, .pose = {.position = CUBES[index].first}});
        static_cast<void>(scene.add_soft_body(volume_preset(mesh, {.density = 200.0F, .edge_compliance = CUBES[index].second}), tetrahedral_surface(mesh),
                                              mix({.red = 60, .green = 200, .blue = 90}, {.red = 200, .green = 230, .blue = 60}, static_cast<float>(index) / 3.0F)));
    }

    // A rope from a fixed point holding a crate (pinned to its top), swinging.
    constexpr Vector3 TOP{.x = 4.5F, .y = 4.5F};
    constexpr float LENGTH = 2.4F;
    const RigidBodyHandle crate = scene.add_body({.pose = {.position = TOP - Vector3{.y = LENGTH + 0.2F}}, .linear_velocity = {.z = 2.5F}},
                                                 collider(box({.x = 0.2F, .y = 0.2F, .z = 0.2F}), {}, 250.0F), palette(3))
                                      .body->handle();
    constexpr std::uint32_t SEGMENTS = 24;
    SoftBodyDescription rope = rope_preset({.segments = SEGMENTS, .length = LENGTH, .origin = TOP, .mass = 0.4F});
    rope.pins = {pin_to_world(rope, 0), pin_to_body(SEGMENTS, crate, {.y = 0.2F})};
    static_cast<void>(scene.add_soft_body(rope, {}, {.red = 210, .green = 180, .blue = 140}));
    return {.view = {.target = {.y = 1.5F}, .distance = 12.0F, .yaw = 0.15F, .pitch = -0.35F}};
}

auto
friction_slope(Scene& scene) -> PresetResult
{
    add_floor(scene);

    // A slope of 25 degrees (friction 1: a crate's coefficient mixes to its square root); crates from slippery to grippy.
    const Quaternion tilt = Quaternion::from_axis_angle({.z = 1.0F}, 0.44F);
    constexpr Vector3 SLOPE_CENTER{.y = 2.0F, .z = -1.5F};
    add_static(scene, box({.x = 4.0F, .y = 0.15F, .z = 3.5F}), {.position = SLOPE_CENTER, .orientation = tilt}, {.red = 120, .green = 124, .blue = 135},
               {.friction = friction(1.0F)});
    const Vector3 up = rotate(tilt, Vector3{.y = 1.0F});
    const Vector3 uphill = rotate(tilt, Vector3{.x = 1.0F});
    const auto crate_shape = box({.x = 0.3F, .y = 0.3F, .z = 0.3F});
    const auto on_slope = [&](float z) { return Transform{.position = SLOPE_CENTER + up * 0.455F + uphill * 2.0F + Vector3{.z = z}, .orientation = tilt}; };
    constexpr std::array COEFFICIENTS{0.0F, 0.05F, 0.12F, 0.3F, 0.6F};
    for (std::size_t index = 0; index < COEFFICIENTS.size(); ++index)
    {
        static_cast<void>(scene.add_body({.pose = on_slope(-4.2F + static_cast<float>(index) * 1.2F)}, collider(crate_shape, {.friction = friction(COEFFICIENTS[index])}, 500.0F),
                                         mix({.red = 220, .green = 70, .blue = 60}, {.red = 80, .green = 200, .blue = 90}, static_cast<float>(index) / 4.0F)));
    }

    // A crate that sticks (static 0.6) but slides easily once moving (dynamic 0.05): drag it.
    const auto sticky = collider(crate_shape, {.friction = {.static_coefficient = 0.6F, .dynamic_coefficient = 0.05F}}, 500.0F);
    const auto slippery = collider(crate_shape, {.friction = friction(0.05F)}, 500.0F);
    static_cast<void>(add_body_or(scene, {.pose = on_slope(1.6F)}, sticky, slippery, {.red = 225, .green = 190, .blue = 70}, "made it 0.05 for both"));

    // Balls rolling on the floor with growing rolling resistance.
    const auto ball = make_shape(SphereShape{.radius = 0.3F});
    constexpr std::array ROLLING{0.0F, 0.03F, 0.15F};
    for (std::size_t index = 0; index < ROLLING.size(); ++index)
    {
        static_cast<void>(scene.add_body({.pose = {.position = {.x = -7.0F, .y = 0.3F, .z = 3.5F + static_cast<float>(index) * 0.8F}},
                                          .linear_velocity = {.x = 4.0F},
                                          .angular_velocity = {.z = -4.0F / 0.3F}},
                                         collider(ball, {.friction = {.static_coefficient = 0.6F, .dynamic_coefficient = 0.6F, .rolling = ROLLING[index]}}),
                                         palette(index + 1)));
    }
    return {.view = {.target = {.y = 1.5F, .z = 0.5F}, .distance = 14.0F, .yaw = 0.35F, .pitch = -0.3F}};
}

auto
bouncing(Scene& scene) -> PresetResult
{
    add_floor(scene);

    // Balls from no bounce to almost elastic (the larger restitution of a pair wins).
    const auto ball = make_shape(SphereShape{.radius = 0.3F});
    for (int index = 0; index < 8; ++index)
    {
        const float fraction = static_cast<float>(index) / 7.0F;
        static_cast<void>(scene.add_body({.pose = {.position = {.x = -5.25F + static_cast<float>(index) * 1.5F, .y = 4.0F}}},
                                         collider(ball, {.restitution = 0.95F * fraction}),
                                         mix({.red = 120, .green = 120, .blue = 130}, {.red = 250, .green = 140, .blue = 40}, fraction)));
    }

    // Bouncy crates dropped tilted; balls bouncing off a tilted plate.
    const auto crate = box({.x = 0.35F, .y = 0.35F, .z = 0.35F});
    for (int index = 0; index < 4; ++index)
    {
        const Quaternion tilt = Quaternion::from_axis_angle(normalized(Vector3{.x = 1.0F, .y = 1.0F}), 0.5F + 0.3F * static_cast<float>(index));
        static_cast<void>(scene.add_body({.pose = {.position = {.x = -3.0F + 2.0F * static_cast<float>(index), .y = 3.0F + 0.5F * static_cast<float>(index), .z = -3.0F},
                                                   .orientation = tilt}},
                                         collider(crate, {.restitution = 0.8F}, 500.0F), palette(4 + static_cast<std::size_t>(index))));
    }
    add_static(scene, box({.x = 1.5F, .y = 0.1F, .z = 1.5F}), {.position = {.x = 8.0F, .y = 1.2F, .z = -1.0F}, .orientation = Quaternion::from_axis_angle({.z = 1.0F}, 0.4F)},
               {.red = 120, .green = 124, .blue = 135});
    const auto small_ball = make_shape(SphereShape{.radius = 0.25F});
    for (int index = 0; index < 3; ++index)
    {
        static_cast<void>(scene.add_body({.pose = {.position = {.x = 8.0F, .y = 4.0F + static_cast<float>(index) * 0.7F, .z = -2.0F + static_cast<float>(index)}}},
                                         collider(small_ball, {.restitution = 0.7F}), palette(static_cast<std::size_t>(index))));
    }
    return {.view = {.target = {.x = 1.5F, .y = 2.0F, .z = -0.5F}, .distance = 16.0F, .yaw = 0.0F, .pitch = -0.2F}};
}

auto
triggers(Scene& scene) -> PresetResult
{
    add_floor(scene);

    // Three zones.
    const auto zone_box = box({.x = 1.5F, .y = 1.0F, .z = 1.5F});
    static_cast<void>(scene.add_trigger("left box", zone_box, {.position = {.x = -4.5F, .y = 1.0F}}, {.red = 60, .green = 200, .blue = 230}));
    static_cast<void>(scene.add_trigger("center sphere", make_shape(SphereShape{.radius = 1.6F}), {.position = {.y = 1.6F}}, {.red = 240, .green = 210, .blue = 60}));
    static_cast<void>(scene.add_trigger("right box", zone_box, {.position = {.x = 4.5F, .y = 1.0F}}, {.red = 230, .green = 90, .blue = 210}));

    // An arm turning about the center on a motorized hinge, sweeping the bodies around it.
    const RigidBodyHandle arm =
        scene.add_body({.pose = {.position = {.y = 0.26F}}, .allow_sleep = false}, collider(box({.x = 3.2F, .y = 0.25F, .z = 0.12F}), {}, 800.0F), {.red = 200, .green = 200, .blue = 205})
            .body->handle();
    static_cast<void>(scene.add_joint(hinge_joint({.first = arm, .second = {}, .anchor = {.y = 0.26F}, .axis = {.y = 1.0F}, .motor = JointMotor{.target_speed = 0.9F, .maximum_force = 20000.0F}})));

    // Bodies on a ring around the arm's middle, and balls falling into each zone.
    const auto ball = make_shape(SphereShape{.radius = 0.3F});
    const auto crate = box({.x = 0.25F, .y = 0.25F, .z = 0.25F});
    for (int index = 0; index < 16; ++index)
    {
        const float angle = 2.0F * PI * static_cast<float>(index) / 16.0F;
        const Vector3 position{.x = 2.2F * std::cos(angle), .y = 0.8F, .z = 2.2F * std::sin(angle)};
        static_cast<void>(scene.add_body({.pose = {.position = position}}, collider(index % 2 == 0 ? ball : crate, {}, 300.0F), palette(static_cast<std::size_t>(index))));
    }
    for (int index = 0; index < 6; ++index)
    {
        const float x = -4.5F + 4.5F * static_cast<float>(index % 3);
        static_cast<void>(scene.add_body({.pose = {.position = {.x = x + 0.3F * static_cast<float>(index / 3), .y = 6.0F + 2.0F * static_cast<float>(index / 3)}}},
                                         collider(ball, {.restitution = 0.3F}, 300.0F), palette(static_cast<std::size_t>(index) + 3)));
    }
    return {.view = {.target = {.y = 1.0F}, .distance = 15.0F, .yaw = 0.0F, .pitch = -0.6F}};
}

} // namespace

auto
preset_information(Preset preset) noexcept -> PresetInformation
{
    return INFORMATION[std::to_underlying(preset)];
}

auto
find_preset(std::string_view name) noexcept -> std::optional<Preset>
{
    for (std::size_t index = 0; index < PRESET_COUNT; ++index)
    {
        if (INFORMATION[index].name == name)
        {
            return static_cast<Preset>(index);
        }
    }
    return std::nullopt;
}

auto
build_preset(Preset preset,
             Scene& scene) -> PresetResult
{
    switch (preset)
    {
        case Preset::SHOWCASE:
        {
            PresetResult result{.view = {.target = {.x = -0.44F, .y = 0.8F, .z = -3.11F}, .distance = 17.0F, .yaw = 0.12F, .pitch = -0.36F}};
            result.showcase = build_showcase(scene);
            return result;
        }
        case Preset::STACKS:
            return stacks(scene);
        case Preset::DOMINOES:
            return dominoes(scene);
        case Preset::CHAINS:
            return chains(scene);
        case Preset::CLOTH:
            return cloth(scene);
        case Preset::SOFT_VOLUMES:
            return soft_volumes(scene);
        case Preset::FRICTION:
            return friction_slope(scene);
        case Preset::BOUNCING:
            return bouncing(scene);
        case Preset::TRIGGERS:
            return triggers(scene);
    }
    return {};
}

} // namespace physics_sandbox
