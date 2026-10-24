// Camera objects: the factory, matrices that agree with the constexpr functions and invert each other, setters that
// validate (and leave the camera as it was when they refuse), axes, turning, moving and looking at points, culling of
// volumes on both sides of every frustum plane under every convention, and screen rays.
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

TEST(CameraTest, TheFactoryBuildsTheStandardCamerasOnly)
{
    const CameraBackendAvailability standard = camera_backend_availability(CameraBackendKind::STANDARD);
    EXPECT_TRUE(standard.built && standard.available && standard.reason.empty());
    const CameraBackendAvailability custom = camera_backend_availability(CameraBackendKind::CUSTOM);
    EXPECT_FALSE(custom.built || custom.available);
    EXPECT_NE(custom.reason.find("derived from gfx::camera::Camera"), std::string::npos) << custom.reason;
    EXPECT_EQ(available_camera_backends(), std::vector{CameraBackendKind::STANDARD});

    // A camera of the default description; CUSTOM through the factory is a capability error naming the backend.
    const std::unique_ptr<Camera> camera = create_camera();
    EXPECT_TRUE(camera->backend() == CameraBackendKind::STANDARD);
    EXPECT_EQ(std::format("{}", camera->backend()), "gfx::camera::STANDARD");
    try
    {
        [[maybe_unused]] const std::unique_ptr<Camera> refused = create_camera({}, CameraBackendKind::CUSTOM);
        FAIL() << "CUSTOM cameras are not built by the factory";
    }
    catch (const BackendCapabilityError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::camera::CUSTOM] create_camera")) << message;
        EXPECT_NE(message.find("construct the derived class directly"), std::string_view::npos) << message;
    }
    EXPECT_THROW((void)create_camera_controller(FreeFlyControllerDescription{}, CameraBackendKind::CUSTOM), BackendCapabilityError);
}

TEST(CameraTest, MatricesAgreeWithTheFunctionsAndInvertEachOther)
{
    for (const CameraProjection& projection : {CameraProjection{PerspectiveProjection{.vertical_field_of_view = 1.2F, .aspect_ratio = 1.5F, .near_plane = 0.25F, .far_plane = 80}},
                                               CameraProjection{PerspectiveProjection{.near_plane = 0.05F, .far_plane = INFINITE_FAR_PLANE}},
                                               CameraProjection{orthographic({.width = 30, .height = 20, .near_plane = -5, .far_plane = 50})}})
    {
        for (const ClipConventions& clip : every_convention())
        {
            const CameraDescription description{.projection = projection,
                                                .clip = clip,
                                                .position = {.x = -4, .y = 7, .z = 2},
                                                .orientation = Quaternion::from_yaw_pitch_roll({.yaw = -2.1F, .pitch = 0.4F, .roll = -0.3F})};
            const std::unique_ptr<Camera> camera = create_camera(description);

            // The camera's matrices are the functions' (up to the order of float operations).
            EXPECT_TRUE(approximately_equal(camera->view(), view_matrix(description), 1e-5F));
            EXPECT_TRUE(approximately_equal(camera->projection_matrix(), projection_matrix(description), 1e-6F));
            EXPECT_TRUE(approximately_equal(camera->view_projection(), view_projection_matrix(description), 1e-4F));

            // Each inverse undoes its matrix.
            EXPECT_TRUE(approximately_equal(camera->view() * camera->inverse_view(), Matrix4::identity(), 1e-5F));
            EXPECT_TRUE(approximately_equal(camera->projection_matrix() * camera->inverse_projection(), Matrix4::identity(), 1e-4F));
            EXPECT_TRUE(approximately_equal(camera->view_projection() * camera->inverse_view_projection(), Matrix4::identity(), 1e-4F));
            const Frustum expected = camera_frustum(description);
            for (std::size_t plane = 0; plane < 6; ++plane)
            {
                EXPECT_TRUE(approximately_equal(camera->frustum().planes[plane].normal, expected.planes[plane].normal, 1e-4F)) << "plane " << plane;
            }

            // The description comes back as given (the orientation was already a unit quaternion).
            EXPECT_EQ(camera->clip_conventions(), clip);
            EXPECT_EQ(camera->projection(), projection);
            EXPECT_EQ(camera->position(), description.position);
            EXPECT_TRUE(same_rotation(camera->orientation(), description.orientation, 1e-6F));
        }
    }
}

TEST(CameraTest, SettersValidateAndKeepTheCameraOnRefusal)
{
    const std::unique_ptr<Camera> camera = create_camera({.projection = PerspectiveProjection{.vertical_field_of_view = 1}, .position = {.x = 1}});
    const Matrix4 before = camera->view_projection();

    // A refused projection names the problem, the value and a suggestion; the camera is unchanged.
    try
    {
        camera->set_projection(PerspectiveProjection{.vertical_field_of_view = 4});
        FAIL() << "a field of view of 4 radians must be refused";
    }
    catch (const InvalidUsageError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::camera::STANDARD] Camera::set_projection: the vertical field of view must be in (0, π) radians (it is 4)"))
            << message;
        EXPECT_NE(message.find("degrees × π / 180"), std::string_view::npos) << message;
    }
    EXPECT_EQ(camera->view_projection(), before);
    EXPECT_THROW(camera->set_position({.x = std::numeric_limits<float>::quiet_NaN()}), InvalidUsageError);
    EXPECT_THROW(camera->set_orientation(Quaternion{.w = 0}), InvalidUsageError);
    EXPECT_THROW(camera->set_aspect_ratio(0), InvalidUsageError);
    EXPECT_THROW(camera->set_projection(OrthographicProjection{.near_plane = 0, .far_plane = INFINITE_FAR_PLANE}), InvalidUsageError);
    EXPECT_THROW((void)create_camera({.projection = PerspectiveProjection{.aspect_ratio = -1}}), InvalidUsageError);
    EXPECT_EQ(camera->view_projection(), before);

    // Orientations are normalized; the pose changes at once.
    camera->set_orientation(Quaternion{.y = 2, .w = 2});
    EXPECT_NEAR(length(camera->orientation()), 1, 1e-6F);
    EXPECT_TRUE(same_rotation(camera->orientation(), Quaternion::from_axis_angle({.y = 1}, PI / 2), 1e-6F));
    camera->set_pose({.z = 5}, {});
    EXPECT_EQ(camera->position(), (Vector3{.z = 5}));
    EXPECT_EQ(camera->orientation(), Quaternion{});

    // The aspect ratio: a perspective keeps its field of view, an orthographic box its height and center.
    camera->set_aspect_ratio(2.5F);
    EXPECT_EQ(std::get<PerspectiveProjection>(camera->projection()).aspect_ratio, 2.5F);
    EXPECT_EQ(std::get<PerspectiveProjection>(camera->projection()).vertical_field_of_view, 1.0F);
    camera->set_projection(OrthographicProjection{.left = 0, .right = 4, .bottom = 1, .top = 3});
    camera->set_aspect_ratio(3);
    EXPECT_EQ(std::get<OrthographicProjection>(camera->projection()), (OrthographicProjection{.left = -1, .right = 5, .bottom = 1, .top = 3}));

    // Clip conventions change the matrices, not what the camera sees.
    const Frustum seen = camera->frustum();
    camera->set_clip_conventions({.depth_range = DepthRange::NEGATIVE_ONE_TO_ONE, .reversed_depth = true, .y_axis = NormalizedDeviceYAxis::DOWN});
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        EXPECT_TRUE(approximately_equal(camera->frustum().planes[plane].normal, seen.planes[plane].normal, 1e-5F)) << "plane " << plane;
        EXPECT_NEAR(camera->frustum().planes[plane].distance, seen.planes[plane].distance, 1e-4F) << "plane " << plane;
    }
}

TEST(CameraTest, CamerasTurnMoveAndLookAtPoints)
{
    const std::unique_ptr<Camera> camera = create_camera();
    EXPECT_EQ(camera->forward(), (Vector3{.z = -1}));
    EXPECT_EQ(camera->right(), (Vector3{.x = 1}));
    EXPECT_EQ(camera->up(), (Vector3{.y = 1}));

    // A quarter turn about the world's up axis looks west; then a local pitch up tilts the view, not the world.
    camera->rotate({.y = 1}, PI / 2, ReferenceFrame::WORLD);
    EXPECT_TRUE(approximately_equal(camera->forward(), {.x = -1}, 1e-6F)) << describe(camera->forward());
    camera->rotate({.x = 1}, PI / 4, ReferenceFrame::LOCAL);
    const float diagonal = 1 / std::numbers::sqrt2_v<float>;
    EXPECT_TRUE(approximately_equal(camera->forward(), {.x = -diagonal, .y = diagonal}, 1e-6F)) << describe(camera->forward());
    EXPECT_TRUE(approximately_equal(camera->right(), {.z = -1}, 1e-6F)) << describe(camera->right());
    EXPECT_THROW(camera->rotate({}, 1, ReferenceFrame::WORLD), InvalidUsageError);

    // A world rotation about +X now differs from the local one.
    const Quaternion before = camera->orientation();
    camera->rotate({.x = 1}, 0.3F, ReferenceFrame::WORLD);
    EXPECT_TRUE(same_rotation(camera->orientation(), Quaternion::from_axis_angle({.x = 1}, 0.3F) * before, 1e-6F));

    // Moving in world space and along the camera's own axes.
    camera->set_pose({}, Quaternion::from_axis_angle({.y = 1}, PI / 2));
    camera->translate({.y = 3}, ReferenceFrame::WORLD);
    camera->translate({.z = -2}, ReferenceFrame::LOCAL);
    EXPECT_TRUE(approximately_equal(camera->position(), {.x = -2, .y = 3}, 1e-6F)) << describe(camera->position());

    // Yaw, pitch and roll round trip through the camera.
    camera->set_orientation_from({.yaw = 0.5F, .pitch = -0.25F, .roll = 0.75F});
    const YawPitchRoll angles = camera->yaw_pitch_roll();
    EXPECT_NEAR(angles.yaw, 0.5F, 1e-5F);
    EXPECT_NEAR(angles.pitch, -0.25F, 1e-5F);
    EXPECT_NEAR(angles.roll, 0.75F, 1e-5F);

    // Looking at a point: it is at the screen's center, level with +Y up.
    camera->set_position({.x = 3, .y = 1, .z = 4});
    camera->look_at({.x = -2, .y = 6, .z = -1});
    EXPECT_TRUE(approximately_equal(camera->forward(), normalized(Vector3{.x = -5, .y = 5, .z = -5}), 1e-5F)) << describe(camera->forward());
    EXPECT_NEAR(dot(camera->right(), {.y = 1}), 0, 1e-5F);
    const ScreenViewport viewport{.width = 640, .height = 480};
    const std::optional<Vector3> target = camera->world_to_screen({.x = -2, .y = 6, .z = -1}, viewport);
    ASSERT_TRUE(target.has_value());
    EXPECT_NEAR(target->x, 320, 1e-3F);
    EXPECT_NEAR(target->y, 240, 1e-3F);
    const Ray ray = camera->screen_to_world_ray({.x = 320, .y = 240}, viewport);
    EXPECT_TRUE(approximately_equal(ray.direction, camera->forward(), 1e-5F));
    EXPECT_THROW(camera->look_at(camera->position()), InvalidUsageError);
    EXPECT_THROW(camera->look_at({}, {}), InvalidUsageError);
}

/// Where a volume is placed to test one plane: just inside, straddling it, just outside.
struct PlaneCase
{
    FrustumPlane plane{};
    Vector3 inside{};
    Vector3 straddling{};
    Vector3 outside{};
};

TEST(CameraTest, VolumesOnEitherSideOfEveryPlaneAreClassified)
{
    // A 90° perspective from 1 to 100 (the sides are the planes |x| = -z and |y| = -z of view space) and a box seen
    // orthographically; volumes of half size 0.25 placed in view space at least 1 unit from the plane (1.5 / √2 for the
    // slanted sides: a world-aligned box around a turned one reaches 0.75 at most), then moved with the camera.
    struct Shape
    {
        CameraProjection projection;
        std::array<PlaneCase, 6> cases;
    };
    const std::array shapes{
        Shape{.projection = PerspectiveProjection{.vertical_field_of_view = PI / 2, .aspect_ratio = 1, .near_plane = 1, .far_plane = 100},
              .cases = {PlaneCase{FrustumPlane::LEFT, {.x = -8.5F, .z = -10}, {.x = -10, .z = -10}, {.x = -11.5F, .z = -10}},
                        PlaneCase{FrustumPlane::RIGHT, {.x = 8.5F, .z = -10}, {.x = 10, .z = -10}, {.x = 11.5F, .z = -10}},
                        PlaneCase{FrustumPlane::BOTTOM, {.y = -8.5F, .z = -10}, {.y = -10, .z = -10}, {.y = -11.5F, .z = -10}},
                        PlaneCase{FrustumPlane::TOP, {.y = 8.5F, .z = -10}, {.y = 10, .z = -10}, {.y = 11.5F, .z = -10}},
                        PlaneCase{FrustumPlane::NEAR_PLANE, {.z = -2}, {.z = -1}, {.z = 0}},
                        PlaneCase{FrustumPlane::FAR_PLANE, {.z = -99}, {.z = -100}, {.z = -101}}}},
        Shape{.projection = OrthographicProjection{.left = -4, .right = 6, .bottom = -2, .top = 3, .near_plane = -1, .far_plane = 20},
              .cases = {PlaneCase{FrustumPlane::LEFT, {.x = -3, .z = -5}, {.x = -4, .z = -5}, {.x = -5, .z = -5}},
                        PlaneCase{FrustumPlane::RIGHT, {.x = 5, .z = -5}, {.x = 6, .z = -5}, {.x = 7, .z = -5}},
                        PlaneCase{FrustumPlane::BOTTOM, {.y = -1, .z = -5}, {.y = -2, .z = -5}, {.y = -3, .z = -5}},
                        PlaneCase{FrustumPlane::TOP, {.y = 2, .z = -5}, {.y = 3, .z = -5}, {.y = 4, .z = -5}},
                        PlaneCase{FrustumPlane::NEAR_PLANE, {.z = 0}, {.z = 1}, {.z = 2}},
                        PlaneCase{FrustumPlane::FAR_PLANE, {.z = -19}, {.z = -20}, {.z = -21}}}},
    };    const Transform pose{.position = {.x = 5, .y = -3, .z = 12}, .orientation = Quaternion::from_yaw_pitch_roll({.yaw = 2.5F, .pitch = 0.6F, .roll = -1.1F})};
    for (const Shape& shape : shapes)
    {
        for (const ClipConventions& clip : every_convention())
        {
            const std::unique_ptr<Camera> camera = create_camera({.projection = shape.projection, .clip = clip, .position = pose.position, .orientation = pose.orientation});
            for (const PlaneCase& test : shape.cases)
            {
                // Boxes, spheres and oriented boxes at each place, in world space; then the batches agree.
                const std::array<Vector3, 3> places{transform_point(pose, test.inside), transform_point(pose, test.straddling), transform_point(pose, test.outside)};
                const std::array<FrustumIntersection, 3> expected{FrustumIntersection::INSIDE, FrustumIntersection::INTERSECTING, FrustumIntersection::OUTSIDE};
                std::array<Aabb, 3> boxes{};
                std::array<Sphere, 3> spheres{};
                for (std::size_t index = 0; index < 3; ++index)
                {
                    const std::string label = std::format("{} plane {} place {}", std::to_underlying(projection_kind(shape.projection)), std::to_underlying(test.plane), index);
                    const Obb turned{.center = places[index], .half_extents = {.x = 0.25F, .y = 0.25F, .z = 0.25F}, .orientation = pose.orientation};
                    boxes[index] = turned.bounds();
                    spheres[index] = {.center = places[index], .radius = 0.25F};
                    EXPECT_EQ(camera->classify(turned), expected[index]) << label;
                    EXPECT_EQ(camera->classify(spheres[index]), expected[index]) << label;
                    if (index != 0)
                    {
                        // A box aligned with the world grows when the camera is turned: it may reach another plane
                        // from inside, never this one from outside.
                        EXPECT_EQ(camera->classify(boxes[index]), expected[index]) << label;
                    }
                    if (index != 1)
                    {
                        EXPECT_EQ(camera->frustum().plane(test.plane).signed_distance(places[index]) > 0, index == 0) << label << ": the plane is the one named";
                    }
                }
                std::array<FrustumIntersection, 3> box_classes{};
                std::array<FrustumIntersection, 3> sphere_classes{};
                camera->classify(boxes, box_classes);
                camera->classify(spheres, sphere_classes);
                for (std::size_t index = 0; index < 3; ++index)
                {
                    EXPECT_EQ(box_classes[index], camera->classify(boxes[index]));
                    EXPECT_EQ(sphere_classes[index], camera->classify(spheres[index]));
                }
            }
        }
    }
}

TEST(CameraTest, ScreenRaysGoThroughWhatThePixelsShow)
{
    // Random points in view: the ray through the pixel each projects to passes through it.
    Generator generator{555};
    for (const ClipConventions& clip : every_convention())
    {
        const std::unique_ptr<Camera> camera = create_camera({.projection = PerspectiveProjection{.vertical_field_of_view = 0.9F, .aspect_ratio = 16.0F / 9, .near_plane = 0.1F, .far_plane = 500},
                                                              .clip = clip,
                                                              .position = {.x = 10, .y = 2, .z = -3},
                                                              .orientation = generator.rotation()});
        const ScreenViewport viewport{.x = 0, .y = 0, .width = 1920, .height = 1080};
        for (std::size_t sample = 0; sample < 100; ++sample)
        {
            const Vector3 view_point{.x = generator.between(-5, 5), .y = generator.between(-3, 3), .z = generator.between(-100, -10)};
            const Vector3 point = transform_point(camera->transform(), view_point);
            const std::optional<Vector3> pixel = camera->world_to_screen(point, viewport);
            ASSERT_TRUE(pixel.has_value());
            const Ray ray = camera->screen_to_world_ray({.x = pixel->x, .y = pixel->y}, viewport);
            const float along = dot(point - ray.origin, ray.direction);
            EXPECT_LT(distance(ray.at(along), point), 1e-3F * along) << "sample " << sample;
        }
    }
}

} // namespace
