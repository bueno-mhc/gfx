// The physics example of gfx: the physics library's usage example as a headless program, with the camera library.
//
// A static floor; a crate whose hull is built at compile time; a lid on a hinge that breaks above 800 N; a cloth (an
// XPBD soft body) pinned at two corners; a trigger zone the crate falls into. The world steps asynchronously: while its
// first step runs on the world's thread, two cameras are set up (a perspective view and an orthographic map). After the
// steps, the program checks what happened: the crate fell into the zone and rests on the floor, the hinge broke when the
// crate landed, the cloth hangs from its pins, the ray through the screen's center hits the crate (through the
// abstraction's BVH and GJK, and through the backend's own queries), screen and world points go back and forth, the
// view computed at compile time is the one the camera object computes at run time, and the frustums see what they should.
//
// Usage: gfx_physics_example [--backend box3d|reference] [--steps <count>] [--threads <count>]
//   --backend    the physics backend (default: Box3D when it is built, else the reference backend)
//   --steps N    steps of 1/60 s (default 240: four seconds; the crate lands after about 0.9 s)
//   --threads N  threads of the task scheduler the world splits its steps over (default: the hardware's)
// The exit code is 0 when every check passes, 1 when one fails (each check is printed), 2 for a bad command line.
import std;
import gfx.physics; // worlds, bodies, colliders, joints, soft bodies, triggers, queries (and gfx.core.math)
import gfx.camera;  // cameras: projections, frustums, rays through screen points

namespace
{

using namespace gfx;
using namespace gfx::physics;
namespace camera = gfx::camera;

constexpr float STEP = 1.0F / 60.0F;

// ------------------------------------------------------------------------------------------------- the scene

/// Half the crate's height: at rest, its center is this high above the floor (whose top is at y = 0).
constexpr float CRATE_HALF_HEIGHT = 0.4F;

/// The corners of a box of 1 × 0.8 × 1 m whose four vertical edges are cut off by 10 cm: an octagonal prism.
consteval auto
crate_corners() -> std::array<Vector3, 16>
{
    constexpr float HALF = 0.5F;
    constexpr float CUT = 0.4F; // HALF minus the 10 cm cut
    constexpr std::array<Vector2, 8> OUTLINE{Vector2{.x = -CUT, .y = -HALF}, Vector2{.x = CUT, .y = -HALF}, Vector2{.x = HALF, .y = -CUT},
                                             Vector2{.x = HALF, .y = CUT},   Vector2{.x = CUT, .y = HALF},   Vector2{.x = -CUT, .y = HALF},
                                             Vector2{.x = -HALF, .y = CUT},  Vector2{.x = -HALF, .y = -CUT}};
    std::array<Vector3, 16> corners{};
    for (std::size_t index = 0; index < OUTLINE.size(); ++index)
    {
        corners[index] = {.x = OUTLINE[index].x, .y = -CRATE_HALF_HEIGHT, .z = OUTLINE[index].y};
        corners[index + OUTLINE.size()] = {.x = OUTLINE[index].x, .y = CRATE_HALF_HEIGHT, .z = OUTLINE[index].y};
    }
    return corners;
}

/// The crate's hull, built at compile time (quickhull in a constant expression). Points that make no hull, or a hull
/// larger than the capacities (16 vertices, 10 faces, 48 polygon indices), do not compile.
constexpr auto CRATE_HULL = convex_hull<16, 10, 48>(crate_corners());
static_assert(CRATE_HULL.vertex_count == 16 && CRATE_HULL.face_count == 10, "the coplanar triangles merge into 8 sides, a top and a bottom");

constexpr Vector3 CRATE_START{.y = 4.0F};                                           ///< Where the crate falls from.
constexpr float CRATE_DENSITY = 500.0F;                                             ///< 0.78 m³: 390 kg.
constexpr Vector3 LID_HALF_EXTENTS{.x = 0.5F, .y = 0.05F, .z = 0.5F};               ///< A board on the crate's top.
constexpr float LID_DENSITY = 300.0F;                                               ///< 30 kg: it weighs 294 N.
constexpr Vector3 HINGE_ANCHOR{.y = CRATE_START.y + CRATE_HALF_HEIGHT, .z = -0.5F}; ///< The back edge of the crate's top.
constexpr float HINGE_BREAKING_FORCE = 800.0F;                                      ///< More than the lid's weight, far less than stopping it.

constexpr Sphere ZONE{.center = {.y = 1.0F}, .radius = 2.0F}; ///< The trigger zone around the place the crate lands.

/// The cloth: 24 × 24 particles 5 cm apart, a curtain hanging from its first row, behind the crate.
constexpr ClothPresetDescription CLOTH{.columns = 24, .rows = 24, .spacing = 0.05F, .origin = {.x = -0.6F, .y = 3.0F, .z = -2.0F}};

// ------------------------------------------------------------------------------------------------- the cameras

constexpr Vector3 EYE{.y = 3.0F, .z = 8.0F};                             ///< Where the view camera is.
constexpr Vector3 LOOKED_AT{.y = CRATE_HALF_HEIGHT};                     ///< Where the crate comes to rest.
constexpr camera::ScreenViewport SCREEN{.width = 1920, .height = 1080};  ///< The view's screen.
constexpr Vector2 SCREEN_CENTER{.x = 960, .y = 540};                     ///< Its center, in pixels.
constexpr camera::ScreenViewport MAP_SCREEN{.width = 1024, .height = 1024}; ///< The map's.

/// The view camera, known at compile time: its matrices, its frustum and its screen points are constant expressions too.
constexpr camera::CameraDescription VIEW{
    .projection = camera::PerspectiveProjection{.vertical_field_of_view = 1.0F, .aspect_ratio = 16.0F / 9.0F, .near_plane = 0.1F, .far_plane = 500},
    .position = EYE,
    .orientation = camera::look_at_orientation(EYE, LOOKED_AT).value()};

static_assert(
    []
    {
        const std::optional<Vector3> pixel = camera::world_to_screen(camera::view_projection_matrix(VIEW), LOOKED_AT, SCREEN);
        return pixel && std::abs(pixel->x - SCREEN_CENTER.x) < 0.01F && std::abs(pixel->y - SCREEN_CENTER.y) < 0.01F;
    }(),
    "the point looked at appears at the screen's center");

// ------------------------------------------------------------------------------------------------- the command line

/// What the command line asks for.
struct Options
{
    std::optional<PhysicsBackendKind> backend{};
    std::uint32_t steps = 240;
    std::uint32_t threads = std::max(1U, std::thread::hardware_concurrency());
};

/// The command line, or std::nullopt after printing the usage.
auto
parse_options(std::span<char* const> arguments) -> std::optional<Options>
{
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1 < arguments.size();
        if (argument == "--backend" && has_value)
        {
            std::string name{arguments[++index]};
            std::ranges::transform(name, name.begin(), [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
            options.backend = gfx::from_string<PhysicsBackendKind>(name);
            if (!options.backend)
            {
                std::println(std::cerr, "unknown physics backend '{}'", arguments[index]);
                return std::nullopt;
            }
        }
        else if (argument == "--steps" && has_value)
        {
            options.steps = static_cast<std::uint32_t>(std::stoul(arguments[++index]));
        }
        else if (argument == "--threads" && has_value)
        {
            options.threads = std::max(1U, static_cast<std::uint32_t>(std::stoul(arguments[++index])));
        }
        else
        {
            std::println(std::cerr, "usage: gfx_physics_example [--backend box3d|reference] [--steps <count>] [--threads <count>]");
            return std::nullopt;
        }
    }
    return options;
}

// ------------------------------------------------------------------------------------------------- what the steps report

/// What the events of every step said about the crate and the hinge.
struct Observations
{
    std::uint32_t crate_entered_zone = 0;            ///< ENTER events of the crate in the zone.
    std::uint32_t crate_left_zone = 0;               ///< EXIT events.
    bool crate_stays_in_zone = false;                ///< Whether the last step reported STAY.
    std::optional<std::uint64_t> landing_step{};     ///< The step the crate first touched the floor.
    std::optional<std::uint64_t> hinge_broken_step{}; ///< The step the hinge broke.
    float hinge_breaking_force = 0.0F;               ///< Its force then.
};

/// Adds what the step just completed reported.
void
observe(const PhysicsWorld& world,
        ColliderHandle crate,
        ColliderHandle floor,
        JointHandle hinge,
        Observations& observations)
{
    const PhysicsEvents events = world.events();
    observations.crate_stays_in_zone = false;
    for (const TriggerEvent& event : events.triggers)
    {
        if (event.collider == crate)
        {
            observations.crate_entered_zone += event.kind == TriggerEventKind::ENTER ? 1U : 0U;
            observations.crate_left_zone += event.kind == TriggerEventKind::EXIT ? 1U : 0U;
            observations.crate_stays_in_zone = observations.crate_stays_in_zone || event.kind == TriggerEventKind::STAY;
        }
    }
    for (const ContactEvent& event : events.contacts)
    {
        const bool crate_and_floor = (event.first == crate && event.second == floor) || (event.first == floor && event.second == crate);
        if (crate_and_floor && event.kind == ContactEventKind::BEGIN && !observations.landing_step)
        {
            observations.landing_step = world.step_count();
        }
    }
    for (const JointEvent& event : events.joints)
    {
        if (event.joint == hinge && event.kind == JointEventKind::BROKEN)
        {
            observations.hinge_broken_step = world.step_count();
            observations.hinge_breaking_force = event.force;
        }
    }
}

/// Counts and prints checks.
class Checks
{
public:
    /// Prints @p description as passed, or as failed with @p details.
    void expect(bool passed,
                std::string_view description,
                std::string_view details = {})
    {
        if (passed)
        {
            std::println("  ok      {}", description);
        }
        else
        {
            ++m_failures;
            std::println("  FAILED  {}{}{}", description, details.empty() ? "" : ": ", details);
        }
    }

    [[nodiscard]] auto failures() const noexcept -> std::uint32_t
    {
        return m_failures;
    }

private:
    std::uint32_t m_failures = 0;
};

/// The largest distance of @p point (in the crate's frame) above the planes of the crate's faces (0 on its surface),
/// with the normal of the face it is farthest above.
[[nodiscard]] auto
crate_surface_distance(Vector3 point) -> std::pair<float, Vector3>
{
    std::pair<float, Vector3> farthest{-std::numeric_limits<float>::infinity(), Vector3{}};
    for (const Plane& plane : CRATE_HULL.view().planes)
    {
        const float above = plane.signed_distance(point);
        if (above > farthest.first)
        {
            farthest = {above, plane.normal};
        }
    }
    return farthest;
}

// ------------------------------------------------------------------------------------------------- the program

auto
run(const Options& options) -> int
{
    // The backend: the one asked for, else Box3D when it is built, else the reference backend.
    const PhysicsBackendKind backend = options.backend.value_or(physics_backend_availability(PhysicsBackendKind::BOX3D).available ? PhysicsBackendKind::BOX3D
                                                                                                                                   : PhysicsBackendKind::REFERENCE);
    const PhysicsBackendAvailability availability = physics_backend_availability(backend);
    if (!availability.available)
    {
        std::println(std::cerr, "the {} physics backend is unavailable: {}", backend, availability.reason);
        return 1;
    }

    // The world splits its steps over the scheduler's threads and runs them on its own thread (begin_step()).
    ThreadTaskScheduler scheduler{options.threads};
    const std::unique_ptr<PhysicsWorld> world = create_physics_world({.gravity = {.y = -9.81F}, .asynchronous_stepping = true, .task_scheduler = &scheduler, .debug_name = "physics example"},
                                                                     backend);
    std::println("{} physics world on {} threads, {} steps of 1/60 s", backend, scheduler.concurrency(), options.steps);

    // A static floor (its top at y = 0) and the crate, falling from 4 m, with the hull built at compile time.
    const std::unique_ptr<RigidBody> ground = world->create_rigid_body({.kind = BodyKind::STATIC});
    const std::unique_ptr<Collider> floor = ground->add_collider({.shape = make_shape(BoxShape{.half_extents = {.x = 50, .y = 0.5F, .z = 50}}),
                                                                  .pose = {.position = {.y = -0.5F}},
                                                                  .material = {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.8F}}});
    const std::unique_ptr<RigidBody> crate = world->create_rigid_body({.kind = BodyKind::DYNAMIC, .pose = {.position = CRATE_START}});
    const std::unique_ptr<Collider> crate_collider = crate->add_collider({.shape = make_shape(HullShape(CRATE_HULL)), .density = CRATE_DENSITY});

    // A cloth pinned at its two top corners where they start (the abstraction's XPBD soft bodies, on any backend).
    SoftBodyDescription cloth_description = cloth_preset(CLOTH);
    const std::uint32_t last_column = cloth_particle(CLOTH.columns, CLOTH.columns - 1, 0);
    cloth_description.pins = {pin_to_world(cloth_description, 0), pin_to_world(cloth_description, last_column)};
    const std::unique_ptr<SoftBody> cloth = world->create_soft_body(cloth_description);

    // A lid on the crate, on a hinge along the crate's back edge that lets it open by up to 1.2 radians and breaks above
    // 800 N; a trigger zone around the place the crate lands.
    const std::unique_ptr<RigidBody> lid = world->create_rigid_body(
        {.kind = BodyKind::DYNAMIC, .pose = {.position = {.y = CRATE_START.y + CRATE_HALF_HEIGHT + LID_HALF_EXTENTS.y}}});
    const std::unique_ptr<Collider> lid_collider = lid->add_collider({.shape = make_shape(BoxShape{.half_extents = LID_HALF_EXTENTS}), .density = LID_DENSITY});
    const std::unique_ptr<Joint> hinge = world->create_joint(hinge_joint({.first = crate->handle(),
                                                                          .second = lid->handle(),
                                                                          .anchor = HINGE_ANCHOR,
                                                                          .axis = {.x = 1},
                                                                          .limits = JointLimits{.lower = -1.2F, .upper = 0},
                                                                          .breaking = {.force = HINGE_BREAKING_FORCE}}));
    const std::unique_ptr<Trigger> zone = world->create_trigger({.shape = make_shape(SphereShape{.radius = ZONE.radius}), .pose = {.position = ZONE.center}});

    // The first step runs on the world's thread while the cameras are set up: the view looks at the crate's resting
    // place, the map looks straight down at the scene (north up).
    Observations observations;
    world->begin_step(STEP);
    const std::unique_ptr<camera::Camera> view = camera::create_camera({.projection = VIEW.projection, .position = EYE}, camera::CameraBackendKind::STANDARD);
    view->look_at(LOOKED_AT, {.y = 1});
    const std::unique_ptr<camera::Camera> map = camera::create_camera(
        {.projection = camera::orthographic({.width = 100, .height = 100, .near_plane = 0, .far_plane = 200}), .position = {.y = 100}}, camera::CameraBackendKind::STANDARD);
    map->look_at({}, {.z = -1});
    world->wait_step();
    observe(*world, crate_collider->handle(), floor->handle(), hinge->handle(), observations);

    // The other steps (asynchronous too: a game would prepare its frame meanwhile).
    for (std::uint32_t step = 1; step < options.steps; ++step)
    {
        world->begin_step(STEP);
        world->wait_step();
        observe(*world, crate_collider->handle(), floor->handle(), hinge->handle(), observations);
    }
    const StepStatistics statistics = world->statistics();
    std::println("after step {}: {} rigid bodies ({} awake), {} soft body, {} joint, {} contact points",
                 statistics.step, statistics.rigid_bodies, statistics.awake_bodies, statistics.soft_bodies, statistics.joints, statistics.contacts);

    Checks checks;

    // The crate fell into the trigger zone once and stays there.
    checks.expect(observations.crate_entered_zone == 1 && observations.crate_left_zone == 0 && observations.crate_stays_in_zone,
                  "the crate entered the trigger zone and stays in it",
                  std::format("{} ENTER, {} EXIT, STAY in the last step: {}", observations.crate_entered_zone, observations.crate_left_zone, observations.crate_stays_in_zone));

    // The crate rests upright on the floor.
    const RigidBodyState crate_state = crate->state();
    const float crate_speed = length(crate_state.linear_velocity);
    const float uprightness = dot(rotate(crate_state.pose.orientation, Vector3{.y = 1}), Vector3{.y = 1});
    checks.expect(std::abs(crate_state.pose.position.y - CRATE_HALF_HEIGHT) < 0.01F && crate_speed < 0.05F && uprightness > 0.999F,
                  "the crate rests upright on the floor",
                  std::format("height {:.4f} m (expected {}), speed {:.4f} m/s, up axis at cos {:.5f}", crate_state.pose.position.y, CRATE_HALF_HEIGHT, crate_speed,
                              uprightness));
    if (observations.landing_step)
    {
        std::println("  (the crate touched the floor in step {}, {}asleep now)", *observations.landing_step, crate_state.awake ? "not " : "");
    }

    // The hinge held the lid while the crate fell, and broke when the crate landed.
    const JointState hinge_state = hinge->state();
    checks.expect(hinge_state.broken && !hinge_state.enabled && observations.hinge_broken_step && observations.landing_step
                      && *observations.hinge_broken_step >= *observations.landing_step,
                  "the hinge broke when the crate landed",
                  std::format("broken {}, enabled {}, broke in step {}, landed in step {}", hinge_state.broken, hinge_state.enabled,
                              observations.hinge_broken_step.value_or(0), observations.landing_step.value_or(0)));
    if (observations.hinge_broken_step)
    {
        std::println("  (the hinge broke in step {} at {:.0f} N)", *observations.hinge_broken_step, observations.hinge_breaking_force);
    }

    // The cloth hangs from its pins: the pinned corners where they started, the rest below them, near its plane.
    std::vector<Vector3> positions(cloth->particle_count());
    (void)cloth->positions(positions);
    const float pin_error = std::max(distance(positions.front(), cloth_description.particles.front().position),
                                     distance(positions[last_column], cloth_description.particles[last_column].position));
    const bool hangs = std::ranges::all_of(positions,
                                           [](Vector3 position)
                                           {
                                               return position.y <= CLOTH.origin.y + 1.0e-3F && position.y > 0.5F && std::abs(position.z - CLOTH.origin.z) < 0.6F
                                                   && position.x > CLOTH.origin.x - 0.1F && position.x < CLOTH.origin.x + CLOTH.spacing * CLOTH.columns + 0.1F;
                                           });
    const Aabb cloth_bounds = cloth->bounds();
    checks.expect(pin_error < 1.0e-3F && hangs, "the cloth hangs from its two pinned corners",
                  std::format("pins {:.5f} m away, between y = {:.3f} and {:.3f} m", pin_error, cloth_bounds.minimum.y, cloth_bounds.maximum.y));

    // The ray through the screen's center, through the abstraction's BVH and GJK: it hits the crate's front face.
    const Ray ray = view->screen_to_world_ray(SCREEN_CENTER, SCREEN);
    const QueryResult<RayHit> hit = world->ray_cast(ray, 100, {.engine = QueryEngine::ABSTRACTION});
    bool hit_on_crate = false;
    if (hit && hit->collider == crate_collider->handle())
    {
        const Transform& pose = crate_state.pose;
        const auto [surface_distance, face_normal] = crate_surface_distance(transform_point(inverse(pose), hit->point));
        hit_on_crate = std::abs(surface_distance) < 1.0e-3F && dot(hit->normal, rotate(pose.orientation, face_normal)) > 0.999F;
        std::println("  (the ray hits the crate {:.3f} m away at ({:.3f}, {:.3f}, {:.3f}))", hit->distance, hit->point.x, hit->point.y, hit->point.z);
    }
    checks.expect(hit_on_crate, "the ray through the screen's center hits the crate's surface (the abstraction's BVH and GJK)",
                  hit ? std::format("hit collider {} at distance {}", hit->collider, hit->distance) : std::format("no hit: {}", hit.error().kind));

    // The backend's own queries agree (backends without them answer through the abstraction).
    const QueryResult<RayHit> backend_hit = world->ray_cast(ray, 100, {.engine = QueryEngine::BACKEND});
    checks.expect(hit && backend_hit && backend_hit->collider == hit->collider && std::abs(backend_hit->distance - hit->distance) < 1.0e-3F,
                  "the backend's own ray cast finds the same point",
                  backend_hit ? std::format("distance {} (the abstraction's: {})", backend_hit->distance, hit ? hit->distance : 0.0F)
                              : std::format("no hit: {}", backend_hit.error().kind));

    // Back to the screen: the hit point appears at the center; its depth is in [0, 1].
    const std::optional<Vector3> pixel = hit ? view->world_to_screen(hit->point, SCREEN) : std::nullopt;
    checks.expect(pixel && std::abs(pixel->x - SCREEN_CENTER.x) < 0.5F && std::abs(pixel->y - SCREEN_CENTER.y) < 0.5F && pixel->z > 0 && pixel->z < 1,
                  "the hit point projects back to the screen's center",
                  pixel ? std::format("pixel ({:.3f}, {:.3f}), depth {:.6f}", pixel->x, pixel->y, pixel->z) : std::string{"not in front of the camera"});

    // The camera object's matrices at run time (cglm) are the ones computed at compile time.
    checks.expect(approximately_equal(view->view_projection(), camera::view_projection_matrix(VIEW), 1.0e-5F),
                  "the camera built at run time matches the one computed at compile time");

    // The frustums: the view sees the crate and not what is behind it; the map sees the crate, not 80 m away.
    const Aabb crate_bounds = crate_collider->world_bounds();
    const Aabb behind_the_view{.minimum = {.x = -1, .y = 2, .z = 11}, .maximum = {.x = 1, .y = 4, .z = 13}};
    const Aabb off_the_map{.minimum = {.x = 79, .y = 0, .z = -1}, .maximum = {.x = 81, .y = 1, .z = 1}};
    checks.expect(view->classify(crate_bounds) == FrustumIntersection::INSIDE && view->classify(behind_the_view) == FrustumIntersection::OUTSIDE
                      && map->classify(crate_bounds) == FrustumIntersection::INSIDE && map->classify(off_the_map) == FrustumIntersection::OUTSIDE
                      && view->classify(Sphere{.center = ZONE.center, .radius = ZONE.radius}) != FrustumIntersection::OUTSIDE,
                  "the frustums see the crate, the zone, and not what is behind the view or off the map",
                  std::format("view: crate {}, behind {}; map: crate {}, off {}", view->classify(crate_bounds), view->classify(behind_the_view),
                              map->classify(crate_bounds), map->classify(off_the_map)));

    // The map's rays are parallel: the one through the crate's pixel falls straight down onto it.
    const std::optional<Vector3> map_pixel = map->world_to_screen(crate_state.pose.position, MAP_SCREEN);
    const Ray map_ray = map->screen_to_world_ray(map_pixel ? Vector2{.x = map_pixel->x, .y = map_pixel->y} : Vector2{}, MAP_SCREEN);
    checks.expect(map_pixel && approximately_equal(map_ray.direction, Vector3{.y = -1}, 1.0e-5F)
                      && std::abs(map_ray.origin.x - crate_state.pose.position.x) < 1.0e-3F && std::abs(map_ray.origin.z - crate_state.pose.position.z) < 1.0e-3F,
                  "the map's ray through the crate's pixel falls straight down onto it",
                  std::format("origin ({:.4f}, {:.4f}, {:.4f}), direction ({:.6f}, {:.6f}, {:.6f})", map_ray.origin.x, map_ray.origin.y, map_ray.origin.z,
                              map_ray.direction.x, map_ray.direction.y, map_ray.direction.z));

    std::println("{} checks failed", checks.failures());
    return checks.failures() == 0 ? 0 : 1;
}

} // namespace

int
main(int argument_count,
     char** arguments)
{
    // Every gfx error says which backend failed, in which operation, why, and what to do instead.
    try
    {
        const std::optional<Options> options = parse_options(std::span{arguments, static_cast<std::size_t>(argument_count)});
        return options ? run(*options) : 2;
    }
    catch (const gfx::Error& error)
    {
        std::println(std::cerr, "{}", error.what());
        return 1;
    }
}
