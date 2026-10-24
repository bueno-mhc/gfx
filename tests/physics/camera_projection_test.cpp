// The camera's constexpr functions, without camera objects: projection matrices against values computed by hand for
// every clip convention, view matrices and look-at orientations, frustums that do not depend on the conventions,
// screen rays and their round trips through world_to_screen(), the validity of descriptions — and cameras evaluated
// entirely at compile time.
#include <gtest/gtest.h>

import std;
import gfx.camera;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::camera;
using gfx::tests::describe;
using gfx::tests::Generator;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

// A 90° perspective (focal length 1), aspect 2, from 1 to 3: x scale 1/2, y scale 1; clip depth (A z + B) / -z with
// the A and B worked out below for each convention.
constexpr PerspectiveProjection LENS{.vertical_field_of_view = PI / 2, .aspect_ratio = 2, .near_plane = 1, .far_plane = 3};
constexpr PerspectiveProjection ENDLESS{.vertical_field_of_view = PI / 2, .aspect_ratio = 2, .near_plane = 1, .far_plane = INFINITE_FAR_PLANE};
constexpr ClipConventions REVERSED{.reversed_depth = true};
constexpr ClipConventions OPEN_GL{.depth_range = DepthRange::NEGATIVE_ONE_TO_ONE};
constexpr ClipConventions OPEN_GL_REVERSED{.depth_range = DepthRange::NEGATIVE_ONE_TO_ONE, .reversed_depth = true};
constexpr ClipConventions Y_DOWN{.y_axis = NormalizedDeviceYAxis::DOWN};

/// The perspective matrix with depth terms @p scale (A) and @p offset (B), y scaled by @p y.
constexpr auto
lens_matrix(float scale,
            float offset,
            float y = 1) -> Matrix4
{
    return {.columns = {Vector4{.x = 0.5F}, Vector4{.y = y}, Vector4{.z = scale, .w = -1}, Vector4{.z = offset}}};
}

// Finite far plane n = 1, f = 3:
//   [0, 1]:            A = f / (n - f) = -1.5,         B = n f / (n - f) = -1.5
//   [0, 1] reversed:   A = n / (f - n) = 0.5,          B = n f / (f - n) = 1.5
//   [-1, 1]:           A = (f + n) / (n - f) = -2,     B = 2 n f / (n - f) = -3
//   [-1, 1] reversed:  A = (f + n) / (f - n) = 2,      B = 2 n f / (f - n) = 3
static_assert(approximately_equal(perspective_matrix(LENS), lens_matrix(-1.5F, -1.5F), 1e-5F));
static_assert(approximately_equal(perspective_matrix(LENS, REVERSED), lens_matrix(0.5F, 1.5F), 1e-5F));
static_assert(approximately_equal(perspective_matrix(LENS, OPEN_GL), lens_matrix(-2, -3), 1e-5F));
static_assert(approximately_equal(perspective_matrix(LENS, OPEN_GL_REVERSED), lens_matrix(2, 3), 1e-5F));
// Infinite far plane: the limits as f grows.
//   [0, 1]: A = -1, B = -n    reversed: A = 0, B = n    [-1, 1]: A = -1, B = -2 n    reversed: A = 1, B = 2 n
static_assert(approximately_equal(perspective_matrix(ENDLESS), lens_matrix(-1, -1), 1e-5F));
static_assert(approximately_equal(perspective_matrix(ENDLESS, REVERSED), lens_matrix(0, 1), 1e-5F));
static_assert(approximately_equal(perspective_matrix(ENDLESS, OPEN_GL), lens_matrix(-1, -2), 1e-5F));
static_assert(approximately_equal(perspective_matrix(ENDLESS, OPEN_GL_REVERSED), lens_matrix(1, 2), 1e-5F));
// +Y down in normalized device coordinates: the y scale negated.
static_assert(approximately_equal(perspective_matrix(LENS, Y_DOWN), lens_matrix(-1.5F, -1.5F, -1), 1e-5F));

// An orthographic box [-2, 4] × [-1, 3] × [-5, -1]: x scale 2 / 6, offset -(4 - 2) / 6; y scale 2 / 4, offset
// -(3 - 1) / 4; depth z' = A z + B, A = (d_near - d_far) / (f - n), B = d_near + A n.
constexpr OrthographicProjection BOX{.left = -2, .right = 4, .bottom = -1, .top = 3, .near_plane = 1, .far_plane = 5};

constexpr auto
box_matrix(float scale,
           float offset,
           float y = 1) -> Matrix4
{
    return {.columns = {Vector4{.x = 1.0F / 3}, Vector4{.y = 0.5F * y}, Vector4{.z = scale}, Vector4{.x = -1.0F / 3, .y = -0.5F * y, .z = offset, .w = 1}}};
}

static_assert(approximately_equal(orthographic_matrix(BOX), box_matrix(-0.25F, -0.25F), 1e-6F));
static_assert(approximately_equal(orthographic_matrix(BOX, REVERSED), box_matrix(0.25F, 1.25F), 1e-6F));
static_assert(approximately_equal(orthographic_matrix(BOX, OPEN_GL), box_matrix(-0.5F, -1.5F), 1e-6F));
static_assert(approximately_equal(orthographic_matrix(BOX, OPEN_GL_REVERSED), box_matrix(0.5F, 1.5F), 1e-6F));
static_assert(approximately_equal(orthographic_matrix(BOX, Y_DOWN), box_matrix(-0.25F, -0.25F, -1), 1e-6F));
static_assert(orthographic({.width = 6, .height = 4, .near_plane = 1, .far_plane = 5})
              == OrthographicProjection{.left = -3, .right = 3, .bottom = -2, .top = 2, .near_plane = 1, .far_plane = 5});
static_assert(projection_kind(CameraProjection{BOX}) == CameraProjectionKind::ORTHOGRAPHIC && projection_kind(CameraProjection{LENS}) == CameraProjectionKind::PERSPECTIVE);

// View matrices: the inverse of the camera's pose. A quarter turn about +Y looks along -X.
static_assert(approximately_equal(view_matrix({.x = 1, .y = 2, .z = 3}, {}), Matrix4::translation({.x = -1, .y = -2, .z = -3}), 1e-6F));
constexpr Quaternion LOOKING_WEST = Quaternion::from_axis_angle({.y = 1}, PI / 2);
static_assert(approximately_equal(transform_point(view_matrix({}, LOOKING_WEST), {.x = -5}), {.z = -5}, 1e-5F));
static_assert(approximately_equal(transform_point(view_matrix({.y = 1}, LOOKING_WEST), {.x = -5, .y = 1, .z = 2}), {.x = -2, .z = -5}, 1e-5F),
              "looking west, +Z is on the left");

// Look-at orientations: looking along +X with +Y up has +Z on the right.
constexpr std::optional<Quaternion> TO_EAST = look_at_orientation({}, {.x = 4});
static_assert(TO_EAST.has_value() && approximately_equal(rotate(*TO_EAST, {.z = -1}), {.x = 1}, 1e-5F)
              && approximately_equal(rotate(*TO_EAST, {.y = 1}), {.y = 1}, 1e-5F) && approximately_equal(rotate(*TO_EAST, {.x = 1}), {.z = 1}, 1e-5F));
constexpr std::optional<Quaternion> TO_SKY = look_at_orientation({}, {.y = 10});
static_assert(TO_SKY.has_value() && approximately_equal(rotate(*TO_SKY, {.z = -1}), {.y = 1}, 1e-5F), "looking along the up axis still works");
static_assert(!look_at_orientation({.x = 1}, {.x = 1}).has_value() && !look_at_orientation({}, {.x = 1}, {}).has_value());

// A camera evaluated at compile time: a tower 40 units ahead is in view; a point behind is not; the tower's top is
// in the upper half of the screen.
constexpr CameraDescription WATCHER{.projection = PerspectiveProjection{.vertical_field_of_view = 1.0F, .aspect_ratio = 16.0F / 9.0F, .near_plane = 0.5F, .far_plane = 200},
                                    .clip = REVERSED,
                                    .position = {.y = 2},
                                    .orientation = Quaternion::from_axis_angle({.y = 1}, PI)};
static_assert(!camera_problem(WATCHER).has_value() && !camera_problem(CameraDescription{}).has_value());
static_assert(camera_frustum(WATCHER).contains({.y = 2, .z = 40}) && !camera_frustum(WATCHER).contains({.y = 2, .z = -1}));
static_assert(world_to_screen(view_projection_matrix(WATCHER), {.y = 10, .z = 40}, {.width = 1920, .height = 1080}, WATCHER.clip)->y < 540);
static_assert(!world_to_screen(view_projection_matrix(WATCHER), {.y = 2, .z = -10}, {.width = 1920, .height = 1080}, WATCHER.clip).has_value());

// Problems found at compile time.
static_assert(camera_problem({.projection = PerspectiveProjection{.vertical_field_of_view = 4}})->value == 4);
static_assert(camera_problem({.projection = PerspectiveProjection{.near_plane = 0}}).has_value());
static_assert(camera_problem({.projection = OrthographicProjection{.far_plane = INFINITE_FAR_PLANE}}).has_value());
static_assert(camera_problem({.orientation = Quaternion{.w = 0}}).has_value());

// ===================================================================================================== run time

/// Every combination of depth range, reversal and y axis.
auto
every_convention() -> std::vector<ClipConventions>
{
    std::vector<ClipConventions> conventions;
    for (const DepthRange range : {DepthRange::ZERO_TO_ONE, DepthRange::NEGATIVE_ONE_TO_ONE})
    {
        for (const bool reversed : {false, true})
        {
            for (const NormalizedDeviceYAxis y : {NormalizedDeviceYAxis::UP, NormalizedDeviceYAxis::DOWN})
            {
                conventions.push_back({.depth_range = range, .reversed_depth = reversed, .y_axis = y});
            }
        }
    }
    return conventions;
}

/// "[0, 1] reversed y-down", for messages.
auto
describe(const ClipConventions& clip) -> std::string
{
    return std::format("{}{}{}", clip.depth_range == DepthRange::ZERO_TO_ONE ? "[0, 1]" : "[-1, 1]", clip.reversed_depth ? " reversed" : "",
                       clip.y_axis == NormalizedDeviceYAxis::DOWN ? " y-down" : "");
}

/// A turned and moved camera of @p projection under @p clip.
auto
sample_camera(const CameraProjection& projection,
              const ClipConventions& clip = {}) -> CameraDescription
{
    return {.projection = projection,
            .clip = clip,
            .position = {.x = 1, .y = 2, .z = 3},
            .orientation = Quaternion::from_yaw_pitch_roll({.yaw = 0.7F, .pitch = -0.3F, .roll = 0.2F})};
}

TEST(CameraProjectionTest, NearAndFarPlanesMapToTheConventionsDepths)
{
    for (const ClipConventions& clip : every_convention())
    {
        // The depth range's ends, in the order of the convention.
        const float low = clip.depth_range == DepthRange::ZERO_TO_ONE ? 0.0F : -1.0F;
        const float near_depth = clip.reversed_depth ? 1.0F : low;
        const float far_depth = clip.reversed_depth ? low : 1.0F;
        for (const CameraProjection& projection : {CameraProjection{LENS}, CameraProjection{BOX}})
        {
            const Matrix4 matrix = projection_matrix(projection, clip);
            const float near_plane = projection_kind(projection) == CameraProjectionKind::PERSPECTIVE ? LENS.near_plane : BOX.near_plane;
            const float far_plane = projection_kind(projection) == CameraProjectionKind::PERSPECTIVE ? LENS.far_plane : BOX.far_plane;
            EXPECT_NEAR(project_point(matrix, {.z = -near_plane}).z, near_depth, 1e-6F) << describe(clip);
            EXPECT_NEAR(project_point(matrix, {.z = -far_plane}).z, far_depth, 1e-6F) << describe(clip);

            // Depth moves monotonically from the near depth to the far one.
            const float between = project_point(matrix, {.z = -(near_plane + far_plane) * 0.5F}).z;
            EXPECT_GT((between - near_depth) * (far_depth - near_depth), 0.0F) << describe(clip);
            EXPECT_GT((far_depth - between) * (far_depth - near_depth), 0.0F) << describe(clip);
        }

        // An infinite far plane: depth approaches the far depth without reaching it.
        const Matrix4 endless = perspective_matrix(ENDLESS, clip);
        EXPECT_NEAR(project_point(endless, {.z = -1}).z, near_depth, 1e-6F) << describe(clip);
        EXPECT_NEAR(project_point(endless, {.z = -1e6F}).z, far_depth, 1e-5F) << describe(clip);
    }
}

TEST(CameraProjectionTest, FrustumsDoNotDependOnTheConventions)
{
    // The same camera under every convention bounds the same region of the world, plane for plane.
    for (const CameraProjection& projection : {CameraProjection{LENS}, CameraProjection{BOX}, CameraProjection{ENDLESS}})
    {
        const Frustum reference = camera_frustum(sample_camera(projection));
        for (const ClipConventions& clip : every_convention())
        {
            const Frustum frustum = camera_frustum(sample_camera(projection, clip));
            for (std::size_t plane = 0; plane < 6; ++plane)
            {
                EXPECT_TRUE(approximately_equal(frustum.planes[plane].normal, reference.planes[plane].normal, 1e-4F))
                    << describe(clip) << ", plane " << plane << ": " << describe(frustum.planes[plane].normal) << " vs "
                    << describe(reference.planes[plane].normal);
                if (reference.planes[plane].normal != Vector3{})
                {
                    EXPECT_NEAR(frustum.planes[plane].distance, reference.planes[plane].distance, 1e-3F) << describe(clip) << ", plane " << plane;
                }
            }
        }
    }

    // The planes are where the projection puts them: near and far along the view direction, sides at the half angles.
    const CameraDescription camera = sample_camera(LENS);
    const Frustum frustum = camera_frustum(camera);
    const Vector3 forward = rotate(camera.orientation, {.z = -1});
    const Vector3 up = rotate(camera.orientation, {.y = 1});
    EXPECT_TRUE(approximately_equal(frustum.plane(FrustumPlane::NEAR_PLANE).normal, forward, 1e-5F));
    EXPECT_NEAR(frustum.plane(FrustumPlane::NEAR_PLANE).signed_distance(camera.position + forward * LENS.near_plane), 0, 1e-5F);
    EXPECT_NEAR(frustum.plane(FrustumPlane::FAR_PLANE).signed_distance(camera.position + forward * LENS.far_plane), 0, 1e-4F);
    EXPECT_NEAR(frustum.plane(FrustumPlane::TOP).signed_distance(camera.position + forward + up * 0.999F), 0.001F / std::numbers::sqrt2_v<float>, 1e-5F);
    EXPECT_LT(frustum.plane(FrustumPlane::TOP).signed_distance(camera.position + forward + up * 1.1F), 0);
    EXPECT_GT(frustum.plane(FrustumPlane::BOTTOM).signed_distance(camera.position + forward + up * 0.9F), 0);
    EXPECT_EQ(camera_frustum(sample_camera(ENDLESS)).plane(FrustumPlane::FAR_PLANE).normal, Vector3{});
}

TEST(CameraProjectionTest, ScreenRaysStartOnTheNearPlaneAndDoNotDependOnTheConventions)
{
    const ScreenViewport viewport{.x = 100, .y = 50, .width = 800, .height = 400};
    const std::array<Vector2, 5> points{Vector2{.x = 500, .y = 250}, Vector2{.x = 100, .y = 50}, Vector2{.x = 900, .y = 450}, Vector2{.x = 137.5F, .y = 400},
                                        Vector2{.x = 820, .y = 77}};

    // Perspective: the center pixel looks along the view direction from the near plane; the top-left corner along
    // the frustum's top-left edge (view direction (-aspect, 1, -1) at a 90° field of view).
    const CameraDescription camera = sample_camera(LENS);
    const Matrix4 inverse_view_projection = inverse(view_projection_matrix(camera));
    const Vector3 forward = rotate(camera.orientation, {.z = -1});
    const Ray center = screen_to_world_ray(inverse_view_projection, points[0], viewport);
    EXPECT_TRUE(approximately_equal(center.direction, forward, 1e-5F)) << describe(center.direction);
    EXPECT_TRUE(approximately_equal(center.origin, camera.position + forward * LENS.near_plane, 1e-5F)) << describe(center.origin);
    const Ray corner = screen_to_world_ray(inverse_view_projection, points[1], viewport);
    EXPECT_TRUE(approximately_equal(corner.direction, rotate(camera.orientation, normalized(Vector3{.x = -2, .y = 1, .z = -1})), 1e-5F))
        << describe(corner.direction);

    // Orthographic: parallel rays from the near face of the box.
    const CameraDescription flat = sample_camera(BOX);
    const Ray flat_corner = screen_to_world_ray(inverse(view_projection_matrix(flat)), points[1], viewport);
    EXPECT_TRUE(approximately_equal(flat_corner.direction, rotate(flat.orientation, {.z = -1}), 1e-5F));
    EXPECT_TRUE(approximately_equal(flat_corner.origin, transform_point(camera_transform(flat), {.x = BOX.left, .y = BOX.top, .z = -BOX.near_plane}), 1e-4F))
        << describe(flat_corner.origin);

    // Every convention gives the same rays, an infinite far plane too.
    for (const CameraProjection& projection : {CameraProjection{LENS}, CameraProjection{BOX}, CameraProjection{ENDLESS}})
    {
        const Matrix4 reference_inverse = inverse(view_projection_matrix(sample_camera(projection)));
        for (const ClipConventions& clip : every_convention())
        {
            const Matrix4 inverse_matrix = inverse(view_projection_matrix(sample_camera(projection, clip)));
            for (const Vector2 point : points)
            {
                const Ray expected = screen_to_world_ray(reference_inverse, point, viewport);
                const Ray ray = screen_to_world_ray(inverse_matrix, point, viewport, clip);
                EXPECT_TRUE(approximately_equal(ray.origin, expected.origin, 1e-4F)) << describe(clip) << ": " << describe(ray.origin);
                EXPECT_TRUE(approximately_equal(ray.direction, expected.direction, 1e-4F)) << describe(clip) << ": " << describe(ray.direction);
            }
        }
    }
}

TEST(CameraProjectionTest, PointsOnScreenRaysProjectBackToTheirPixels)
{
    const ScreenViewport viewport{.x = 16, .y = 8, .width = 1280, .height = 720};
    Generator generator{1701};
    for (const CameraProjection& projection : {CameraProjection{LENS}, CameraProjection{BOX}, CameraProjection{ENDLESS}})
    {
        for (const ClipConventions& clip : every_convention())
        {
            const CameraDescription camera = sample_camera(projection, clip);
            const Matrix4 view_projection = view_projection_matrix(camera);
            const Matrix4 inverse_view_projection = inverse(view_projection);
            for (std::size_t sample = 0; sample < 50; ++sample)
            {
                // A pixel, a point along its ray between the planes, and back.
                const Vector2 pixel{.x = generator.between(viewport.x, viewport.x + viewport.width), .y = generator.between(viewport.y, viewport.y + viewport.height)};
                const Ray ray = screen_to_world_ray(inverse_view_projection, pixel, viewport, clip);
                const Vector3 point = ray.at(generator.between(0.0F, 1.5F));
                const std::optional<Vector3> projected = world_to_screen(view_projection, point, viewport, clip);
                ASSERT_TRUE(projected.has_value()) << describe(clip);
                EXPECT_NEAR(projected->x, pixel.x, 2e-3F) << describe(clip) << ", sample " << sample;
                EXPECT_NEAR(projected->y, pixel.y, 2e-3F) << describe(clip) << ", sample " << sample;
            }

            // Behind a perspective camera nothing projects; the near plane's center has the near depth.
            const Vector3 forward = rotate(camera.orientation, {.z = -1});
            if (projection_kind(projection) == CameraProjectionKind::PERSPECTIVE)
            {
                EXPECT_FALSE(world_to_screen(view_projection, camera.position - forward, viewport, clip).has_value()) << describe(clip);
            }
            const float low = clip.depth_range == DepthRange::ZERO_TO_ONE ? 0.0F : -1.0F;
            const float near_plane = projection_kind(projection) == CameraProjectionKind::PERSPECTIVE ? LENS.near_plane : BOX.near_plane;
            const std::optional<Vector3> near_center = world_to_screen(view_projection, camera.position + forward * near_plane, viewport, clip);
            ASSERT_TRUE(near_center.has_value());
            EXPECT_NEAR(near_center->z, clip.reversed_depth ? 1.0F : low, 1e-5F) << describe(clip);

            // The view axis is at the viewport's center, except for the off-center box: there at its device coordinates
            // (-(r + l) / (r - l), -(t + b) / (t - b)), which every y convention puts at the same pixel.
            const bool centered = projection_kind(projection) == CameraProjectionKind::PERSPECTIVE;
            const Vector2 device = centered ? Vector2{} : Vector2{.x = -(BOX.right + BOX.left) / (BOX.right - BOX.left), .y = -(BOX.top + BOX.bottom) / (BOX.top - BOX.bottom)};
            EXPECT_NEAR(near_center->x, viewport.x + (device.x + 1) * 0.5F * viewport.width, 1e-3F) << describe(clip);
            EXPECT_NEAR(near_center->y, viewport.y + (1 - device.y) * 0.5F * viewport.height, 1e-3F) << describe(clip);
        }
    }
}

TEST(CameraProjectionTest, LookAtOrientationsKeepTheirUpAxis)
{
    Generator generator{97};
    for (std::size_t sample = 0; sample < 500; ++sample)
    {
        const Vector3 eye = generator.vector(-10, 10);
        const Vector3 target = generator.vector(-10, 10);
        const Vector3 world_up = generator.direction();
        const std::optional<Quaternion> orientation = look_at_orientation(eye, target, world_up);
        ASSERT_TRUE(orientation.has_value());
        const Vector3 forward = rotate(*orientation, {.z = -1});
        const Vector3 up = rotate(*orientation, {.y = 1});
        const Vector3 right = rotate(*orientation, {.x = 1});
        EXPECT_TRUE(approximately_equal(forward, normalized(target - eye), 1e-4F)) << describe(forward);
        EXPECT_NEAR(dot(up, forward), 0, 1e-4F);
        EXPECT_GE(dot(up, world_up), -1e-4F) << "the camera's up leans toward the world's";
        EXPECT_NEAR(dot(right, world_up), 0, 1e-3F) << "no roll: the right axis is level";
        EXPECT_NEAR(length(*orientation), 1, 1e-5F);
    }
}

TEST(CameraProjectionTest, ProblemsNameTheirValue)
{
    // Each problem, with the value at fault; valid descriptions have none.
    const auto problem = [](const CameraDescription& description) { return std::string{camera_problem(description).value_or(CameraProblem{}).problem}; };
    EXPECT_EQ(problem({.projection = PerspectiveProjection{.vertical_field_of_view = 0}}), "the vertical field of view must be in (0, π) radians");
    EXPECT_EQ(problem({.projection = PerspectiveProjection{.vertical_field_of_view = std::nanf("")}}), "the vertical field of view must be in (0, π) radians");
    EXPECT_EQ(problem({.projection = PerspectiveProjection{.aspect_ratio = -1}}), "the aspect ratio must be finite and greater than 0");
    EXPECT_EQ(problem({.projection = PerspectiveProjection{.near_plane = -0.1F}}), "the near plane of a perspective projection must be finite and greater than 0");
    EXPECT_EQ(problem({.projection = PerspectiveProjection{.near_plane = 2, .far_plane = 2}}), "the far plane must be beyond the near plane (or INFINITE_FAR_PLANE)");
    EXPECT_EQ(problem({.projection = OrthographicProjection{.left = 1, .right = 1}}), "the right bound must be finite and greater than the left one");
    EXPECT_EQ(problem({.projection = OrthographicProjection{.bottom = 2, .top = 1}}), "the top bound must be finite and greater than the bottom one");
    EXPECT_EQ(problem({.projection = OrthographicProjection{.near_plane = 3, .far_plane = 1}}),
              "the far plane of an orthographic projection must be finite and beyond the near plane");
    EXPECT_EQ(problem({.position = {.y = std::numeric_limits<float>::infinity()}}), "the position must be finite");
    EXPECT_EQ(problem({.orientation = Quaternion{.w = 0}}), "the orientation must be a quaternion of finite, non-zero length");
    EXPECT_EQ(problem({.projection = PerspectiveProjection{.far_plane = INFINITE_FAR_PLANE}}), "");
    EXPECT_EQ(problem({.projection = OrthographicProjection{.near_plane = -10, .far_plane = 10}}), "") << "an orthographic near plane may be behind the camera";
}

} // namespace
