/// @file scenes.cppm
/// @brief The physics benchmark's scenes (docs/profiler-performance-design.md, PF3): each built by one function for every
///        backend through PhysicsWorld, with a quality check — a step that is fast but wrong is no win. The checks hold at
///        any moment of a run (nothing passes through the floor, nothing flies off, joints keep their length, stacks
///        stand), so short runs and scaled-down scenes are checked too.
export module physics_benchmark.scenes;

import std;
import gfx.physics;

export namespace gfx::benchmark
{

/// @brief A benchmark scene.
enum class BenchmarkScene : std::uint8_t
{
    MANY_BOXES,         ///< 50 000 boxes resting on a floor, one contact manifold each.
    PYRAMID,            ///< A pyramid of 29 370 boxes: 44 square layers, a 44 × 44 base.
    CLOTH,              ///< A cloth of 316 × 316 = 99 856 particles falling onto a floor.
    CHAIN,              ///< 100 chains of 200 links (20 000 rods) swinging down from the world.
    MIXED,              ///< 10 000 boxes and a cloth of 224 × 224 = 50 176 particles draped over 16 of them.
    STRESS_CONSTRAINTS, ///< A lattice of 1 000 bodies and 100 000 rods (each body joined to the 100 after it, so to 200 in
                        ///< all) hanging from the world by its top layer.
    TWO_WORLDS,         ///< Two worlds of 25 000 resting boxes each, stepped one after the other (the GPU's on one device).
};

/// @brief A scene's name and what it is.
struct SceneInformation
{
    BenchmarkScene scene = BenchmarkScene::MANY_BOXES;
    std::string_view name{};
    std::string_view description{};
};

/// @brief Every scene, in the order the benchmark runs them.
inline constexpr std::array SCENES{
    SceneInformation{BenchmarkScene::MANY_BOXES, "many_boxes", "50 000 boxes resting on a floor (one contact manifold each)"},
    SceneInformation{BenchmarkScene::PYRAMID, "pyramid", "a pyramid of 29 370 boxes (44 square layers, a 44 x 44 base)"},
    SceneInformation{BenchmarkScene::CLOTH, "cloth", "a cloth of 316 x 316 particles (99 856) falling onto a floor"},
    SceneInformation{BenchmarkScene::CHAIN, "chain", "100 chains of 200 links held by rods (20 000 distance constraints)"},
    SceneInformation{BenchmarkScene::MIXED, "mixed", "10 000 boxes and a cloth of 50 176 particles draped over 16 of them"},
    SceneInformation{BenchmarkScene::STRESS_CONSTRAINTS, "stress_constraints",
                     "a lattice of 1 000 bodies and 100 000 rods (each body joined to 200) hanging from the world by 100 more"},
    SceneInformation{BenchmarkScene::TWO_WORLDS, "two_worlds", "two worlds of 25 000 resting boxes, stepped one after the other"},
};

/// @brief The scene named @p name, if any.
[[nodiscard]] auto find_scene(std::string_view name) -> std::optional<BenchmarkScene>;

/// @brief The information of @p scene.
[[nodiscard]] auto information(BenchmarkScene scene) -> const SceneInformation&;

/// @brief A rod of a scene: two bodies (index in the scene's bodies) or a body and a point of the world, and its length.
struct SceneRod
{
    std::uint32_t first = 0;
    std::optional<std::uint32_t> second{};
    Vector3 world_anchor{};
    float length = 0.0F;
};

/// @brief What a scene made in one world, and what its check needs.
struct SceneObjects
{
    std::vector<std::unique_ptr<physics::RigidBody>> bodies{};
    std::vector<std::unique_ptr<physics::Collider>> colliders{};
    std::vector<std::unique_ptr<physics::Joint>> joints{};
    std::vector<std::unique_ptr<physics::SoftBody>> soft_bodies{};
    std::vector<std::uint32_t> boxes_on_floor{};   ///< Boxes resting on the floor (their half height alongside).
    std::vector<float> box_half_heights{};
    std::vector<SceneRod> rods{};
    float rod_tolerance = 0.05F;                   ///< The relative error a rod may show.
    std::optional<std::uint32_t> top_box{};        ///< A stack's top box and its starting height.
    float top_height = 0.0F;
    float soft_start_height = 0.0F;                ///< The soft bodies' highest starting particle.
    std::uint32_t dynamic_bodies = 0;              ///< Counts, for the report.
    std::uint32_t particles = 0;

    SceneObjects() = default;
    SceneObjects(SceneObjects&&) = default;
    auto operator=(SceneObjects&&) -> SceneObjects& = default;
    SceneObjects(const SceneObjects&) = delete;
    auto operator=(const SceneObjects&) -> SceneObjects& = delete;

    /// The objects go before the world: soft bodies, joints, colliders, bodies.
    ~SceneObjects()
    {
        soft_bodies.clear();
        joints.clear();
        colliders.clear();
        bodies.clear();
    }
};

/// @brief Builds @p scene into @p world, its counts multiplied by @p scale (smaller runs; at least a few objects each).
///        TWO_WORLDS builds one of its two worlds.
[[nodiscard]] auto build_scene(BenchmarkScene scene,
                               physics::PhysicsWorld& world,
                               double scale) -> SceneObjects;

/// @brief What is wrong with @p objects now (empty: nothing), and the scene's measure ("max rod stretch 1.2 %").
struct SceneCheck
{
    std::string problem{};
    std::string measure{};
};

/// @brief Checks @p objects after the last completed step of their world.
[[nodiscard]] auto check_scene(const SceneObjects& objects) -> SceneCheck;

} // namespace gfx::benchmark

namespace gfx::benchmark
{

namespace
{

using namespace physics;

/// @p count scaled, at least @p minimum.
[[nodiscard]] auto
scaled(std::uint32_t count,
       double scale,
       std::uint32_t minimum) -> std::uint32_t
{
    return std::max(minimum, static_cast<std::uint32_t>(std::llround(static_cast<double>(count) * scale)));
}

/// A static floor, its top at y = 0, @p half meters from its center each way.
void
add_floor(PhysicsWorld& world,
          SceneObjects& objects,
          float half)
{
    objects.bodies.push_back(world.create_rigid_body({.kind = BodyKind::STATIC}));
    objects.colliders.push_back(objects.bodies.back()->add_collider(
        {.shape = make_shape(BoxShape{.half_extents = {.x = half, .y = 0.5F, .z = half}}), .pose = {.position = {.y = -0.5F}}}));
}

/// A dynamic body with one collider of @p shape at @p position; its index.
auto
add_body(PhysicsWorld& world,
         SceneObjects& objects,
         const std::shared_ptr<const Shape>& shape,
         Vector3 position,
         CollisionFilter filter = {}) -> std::uint32_t
{
    objects.bodies.push_back(world.create_rigid_body({.pose = {.position = position}}));
    objects.colliders.push_back(objects.bodies.back()->add_collider({.shape = shape, .filter = filter}));
    ++objects.dynamic_bodies;
    return static_cast<std::uint32_t>(objects.bodies.size() - 1);
}

/// Boxes of half size @p half resting on the floor on a grid spaced @p spacing, from (@p x, @p z) along +x and +z.
void
add_resting_boxes(PhysicsWorld& world,
                  SceneObjects& objects,
                  std::uint32_t count,
                  float half,
                  float spacing,
                  float x,
                  float z)
{
    const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const auto shape = make_shape(BoxShape{.half_extents = {.x = half, .y = half, .z = half}});
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const Vector3 position{.x = x + spacing * static_cast<float>(index % side), .y = half + 0.002F, .z = z + spacing * static_cast<float>(index / side)};
        objects.boxes_on_floor.push_back(add_body(world, objects, shape, position));
        objects.box_half_heights.push_back(half);
    }
}

/// A square cloth of @p side × @p side particles spaced @p spacing, centered at (@p x, @p z), at @p height.
void
add_cloth(PhysicsWorld& world,
          SceneObjects& objects,
          std::uint32_t side,
          float spacing,
          float x,
          float z,
          float height)
{
    const float half = static_cast<float>(side - 1) * spacing * 0.5F;
    objects.soft_bodies.push_back(world.create_soft_body(cloth_preset(
        {.columns = side, .rows = side, .spacing = spacing, .origin = {.x = x - half, .y = height, .z = z - half}, .row_direction = {.z = 1.0F}})));
    objects.particles += side * side;
    objects.soft_start_height = std::max(objects.soft_start_height, height);
}

void
many_boxes(PhysicsWorld& world,
           SceneObjects& objects,
           std::uint32_t count)
{
    const auto side = static_cast<float>(std::ceil(std::sqrt(static_cast<double>(count))));
    add_floor(world, objects, side * 0.75F + 2.0F);
    add_resting_boxes(world, objects, count, 0.4F, 1.5F, -side * 0.75F, -side * 0.75F);
}

void
pyramid(PhysicsWorld& world,
        SceneObjects& objects,
        std::uint32_t count)
{
    // Square layers of base², (base - 1)², ... boxes 1 cm apart, each box on the four below it: the largest base whose
    // pyramid holds at most `count` boxes (a one-box wall this tall would topple sideways in three dimensions).
    std::uint32_t base = 1;
    while ((base + 1) * (base + 2) * (2 * base + 3) / 6 <= count)
    {
        ++base;
    }
    base = std::max(base, 2U);
    add_floor(world, objects, static_cast<float>(base) + 2.0F);
    const auto cube = make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.5F, .z = 0.5F}});
    for (std::uint32_t layer = 0; layer < base; ++layer)
    {
        const std::uint32_t side = base - layer;
        const float corner = -static_cast<float>(side - 1) * 0.505F;
        for (std::uint32_t index = 0; index < side * side; ++index)
        {
            const float x = corner + static_cast<float>(index % side) * 1.01F;
            const float z = corner + static_cast<float>(index / side) * 1.01F;
            (void)add_body(world, objects, cube, {.x = x, .y = 0.5F + static_cast<float>(layer), .z = z});
        }
    }
    objects.top_box = static_cast<std::uint32_t>(objects.bodies.size() - 1);
    objects.top_height = 0.5F + static_cast<float>(base - 1);
}

void
cloth(PhysicsWorld& world,
      SceneObjects& objects,
      std::uint32_t count)
{
    const auto side = std::max(2U, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
    add_floor(world, objects, static_cast<float>(side) * 0.05F);
    add_cloth(world, objects, side, 0.05F, 0.0F, 0.0F, 1.0F);
}

void
chain(PhysicsWorld& world,
      SceneObjects& objects,
      std::uint32_t count)
{
    // Chains of 200 links joined by rods, the first link held by the world, starting horizontal (they swing down).
    constexpr std::uint32_t LINKS = 200;
    constexpr float SPACING = 0.25F;
    const std::uint32_t links = std::min(LINKS, count);
    const std::uint32_t chains = std::max(count / links, 1U);
    const auto ball = make_shape(SphereShape{.radius = 0.1F});
    for (std::uint32_t chain_index = 0; chain_index < chains; ++chain_index)
    {
        const float x = static_cast<float>(chain_index % 20) * 1.0F;
        const float z = static_cast<float>(chain_index / 20) * 1.0F;
        std::optional<std::uint32_t> previous{};
        for (std::uint32_t link = 0; link < links; ++link)
        {
            const Vector3 position{.x = x + SPACING * static_cast<float>(link + 1), .y = 60.0F, .z = z};
            const std::uint32_t body = add_body(world, objects, ball, position, {.group = -1});
            const Vector3 world_anchor{.x = x, .y = 60.0F, .z = z};
            objects.joints.push_back(world.create_joint(distance_joint({.first = objects.bodies[body]->handle(),
                                                                        .second = previous ? objects.bodies[*previous]->handle() : RigidBodyHandle{},
                                                                        .anchor_second = previous ? Vector3{} : world_anchor,
                                                                        .length = SPACING})));
            objects.rods.push_back({.first = body, .second = previous, .world_anchor = world_anchor, .length = SPACING});
            previous = body;
        }
    }
    // 200-link chains swinging down stretch at the bottom of the swing in iterative solvers (16 to 21 % measured on the
    // GPU backend and Box3D): the check catches chains coming apart, the report shows the stretch.
    objects.rod_tolerance = 0.30F;
}

void
mixed(PhysicsWorld& world,
      SceneObjects& objects,
      std::uint32_t boxes,
      std::uint32_t particles)
{
    // Sixteen boxes under the cloth (4 × 4, 2 m apart), the others on a grid beside it, out of the cloth's reach.
    const auto cloth_side = std::max(2U, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(particles)))));
    const float cloth_half = static_cast<float>(cloth_side) * 0.025F;
    const std::uint32_t under = std::min(16U, boxes);
    const auto others = boxes - under;
    const auto other_side = static_cast<float>(std::ceil(std::sqrt(static_cast<double>(others))));
    add_floor(world, objects, std::max(cloth_half, 10.0F) + other_side * 1.5F + 4.0F);
    for (std::uint32_t index = 0; index < under; ++index)
    {
        add_resting_boxes(world, objects, 1, 0.5F, 2.0F, -3.0F + 2.0F * static_cast<float>(index % 4), -3.0F + 2.0F * static_cast<float>(index / 4));
    }
    add_resting_boxes(world, objects, others, 0.4F, 1.5F, std::max(cloth_half, 6.0F) + 4.0F, -other_side * 0.75F);
    add_cloth(world, objects, cloth_side, 0.05F, 0.0F, 0.0F, 2.0F);
}

void
stress_constraints(PhysicsWorld& world,
                   SceneObjects& objects,
                   std::uint32_t requested,
                   std::uint32_t partners)
{
    // A lattice of small balls 0.5 m apart; each ball held by rods to the next `partners` balls (in creation order,
    // wrapping around) at their starting distances — a heavily over-constrained body — and its top layer hung from the
    // world by a rod each (no floor: a lattice landing pushed balls through it on every backend).
    // A whole cube (scaled-down scenes keep a full top layer to hang from).
    const auto side = std::max(2U, static_cast<std::uint32_t>(std::lround(std::cbrt(static_cast<double>(requested)))));
    const std::uint32_t count = side * side * side;
    const auto ball = make_shape(SphereShape{.radius = 0.1F});
    std::vector<Vector3> positions;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const Vector3 position{.x = 0.5F * static_cast<float>(index % side),
                               .y = 10.0F + 0.5F * static_cast<float>(index / (side * side)),
                               .z = 0.5F * static_cast<float>((index / side) % side)};
        (void)add_body(world, objects, ball, position);
        positions.push_back(position);
    }
    const std::uint32_t first_body = static_cast<std::uint32_t>(objects.bodies.size()) - count;
    const std::uint32_t joined = std::min(partners, (count - 1) / 2);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        for (std::uint32_t step = 1; step <= joined; ++step)
        {
            const std::uint32_t other = (index + step) % count;
            const float length = distance(positions[index], positions[other]);
            objects.joints.push_back(world.create_joint(distance_joint({.first = objects.bodies[first_body + index]->handle(),
                                                                        .second = objects.bodies[first_body + other]->handle(),
                                                                        .length = length})));
            objects.rods.push_back({.first = first_body + index, .second = first_body + other, .world_anchor = {}, .length = length});
        }
    }
    const float top = positions.back().y;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (positions[index].y == top)
        {
            const Vector3 anchor{.x = positions[index].x, .y = top + 0.5F, .z = positions[index].z};
            objects.joints.push_back(world.create_joint(distance_joint({.first = objects.bodies[first_body + index]->handle(), .anchor_second = anchor, .length = 0.5F})));
            objects.rods.push_back({.first = first_body + index, .second = std::nullopt, .world_anchor = anchor, .length = 0.5F});
        }
    }
    objects.rod_tolerance = 0.30F;
}

} // namespace

auto
find_scene(std::string_view name) -> std::optional<BenchmarkScene>
{
    const auto found = std::ranges::find(SCENES, name, &SceneInformation::name);
    return found != SCENES.end() ? std::optional{found->scene} : std::nullopt;
}

auto
information(BenchmarkScene scene) -> const SceneInformation&
{
    return SCENES[std::to_underlying(scene)];
}

auto
build_scene(BenchmarkScene scene,
            PhysicsWorld& world,
            double scale) -> SceneObjects
{
    SceneObjects objects{};
    switch (scene)
    {
        case BenchmarkScene::MANY_BOXES:
            many_boxes(world, objects, scaled(50'000, scale, 4));
            break;
        case BenchmarkScene::PYRAMID:
            pyramid(world, objects, scaled(29'890, scale, 3));
            break;
        case BenchmarkScene::CLOTH:
            cloth(world, objects, scaled(99'856, scale, 16));
            break;
        case BenchmarkScene::CHAIN:
            chain(world, objects, scaled(20'000, scale, 10));
            break;
        case BenchmarkScene::MIXED:
            mixed(world, objects, scaled(10'000, scale, 20), scaled(50'176, scale, 16));
            break;
        case BenchmarkScene::STRESS_CONSTRAINTS:
            stress_constraints(world, objects, scaled(1'000, scale, 8), 100);
            break;
        case BenchmarkScene::TWO_WORLDS:
            many_boxes(world, objects, scaled(25'000, scale, 4));
            break;
    }
    return objects;
}

auto
check_scene(const SceneObjects& objects) -> SceneCheck
{
    SceneCheck check{};
    const auto fail = [&check](std::string problem) {
        if (check.problem.empty())
        {
            check.problem = std::move(problem);
        }
    };
    const auto finite = [](Vector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };

    // Every body somewhere sensible: finite, not under the floor.
    std::vector<Vector3> positions(objects.bodies.size());
    for (std::size_t index = 0; index < objects.bodies.size(); ++index)
    {
        const RigidBodyState state = objects.bodies[index]->state();
        positions[index] = state.pose.position;
        if (!finite(state.pose.position) || !finite(state.linear_velocity))
        {
            fail(std::format("body {} is not finite", index));
        }
        else if (state.pose.position.y < -1.0F)
        {
            fail(std::format("body {} fell through the floor (y = {:.2f} m)", index, state.pose.position.y));
        }
    }

    // Resting boxes still on the floor, upright, nearly still.
    float worst_rest = 0.0F;
    for (std::size_t item = 0; item < objects.boxes_on_floor.size(); ++item)
    {
        const std::uint32_t index = objects.boxes_on_floor[item];
        const RigidBodyState state = objects.bodies[index]->state();
        const float half = objects.box_half_heights[item];
        const float up = rotate(state.pose.orientation, Vector3{.y = 1.0F}).y;
        worst_rest = std::max(worst_rest, std::abs(state.pose.position.y - half));
        if (std::abs(state.pose.position.y - half) > 0.25F * half + 0.05F || std::abs(up) < 0.95F)
        {
            fail(std::format("box {} left its place on the floor (y = {:.3f} m, up {:.3f})", index, state.pose.position.y, up));
        }
    }

    // A stack's top still up.
    if (objects.top_box)
    {
        const float height = positions[*objects.top_box].y;
        if (height < objects.top_height - 1.0F)
        {
            fail(std::format("the top box sank from {:.2f} to {:.2f} m: the pyramid fell", objects.top_height, height));
        }
        check.measure = std::format("top box {:+.3f} m", height - objects.top_height);
    }

    // Rods at their lengths.
    float stretch = 0.0F;
    const SceneRod* worst = nullptr;
    for (const SceneRod& rod : objects.rods)
    {
        const Vector3 other = rod.second ? positions[*rod.second] : rod.world_anchor;
        const float error = std::abs(distance(positions[rod.first], other) - rod.length) / rod.length;
        if (worst == nullptr || error > stretch)
        {
            stretch = error;
            worst = &rod;
        }
    }
    if (worst != nullptr)
    {
        check.measure = std::format("max rod error {:.2f} %", 100.0F * stretch);
        if (!(stretch <= objects.rod_tolerance))
        {
            const std::string ends = worst->second ? std::format("bodies {} and {}", worst->first, *worst->second)
                                                   : std::format("body {} and the world", worst->first);
            fail(std::format("the rod of {} ({:.2f} m) is {:.1f} % off its length (at most {:.0f} %); body {} at y = {:.2f} m", ends, worst->length,
                             100.0F * stretch, 100.0F * objects.rod_tolerance, worst->first, positions[worst->first].y));
        }
    }

    // Particles above the floor and below where they started (no explosion).
    for (const std::unique_ptr<SoftBody>& soft_body : objects.soft_bodies)
    {
        std::vector<Vector3> particles(soft_body->particle_count());
        (void)soft_body->positions(particles);
        float lowest = std::numeric_limits<float>::infinity();
        float highest = -std::numeric_limits<float>::infinity();
        for (const Vector3 particle : particles)
        {
            if (!finite(particle))
            {
                fail("a particle is not finite");
                break;
            }
            lowest = std::min(lowest, particle.y);
            highest = std::max(highest, particle.y);
        }
        if (lowest < -0.05F)
        {
            fail(std::format("a particle went through the floor (y = {:.3f} m)", lowest));
        }
        if (highest > objects.soft_start_height + 0.25F)
        {
            fail(std::format("a particle flew up to {:.2f} m (started at {:.2f} m at most)", highest, objects.soft_start_height));
        }
        check.measure = std::format("particles from {:.3f} to {:.3f} m", lowest, highest);
    }
    if (check.measure.empty() && !objects.boxes_on_floor.empty())
    {
        check.measure = std::format("boxes within {:.4f} m of rest", worst_rest);
    }
    return check;
}

} // namespace gfx::benchmark
