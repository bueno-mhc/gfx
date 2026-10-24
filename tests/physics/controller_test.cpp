// Camera controllers: free fly (turns about the camera's own axes, moves along them), first person (yaw about the up
// axis, clamped pitch, no roll, level movement, any up axis) and orbit (looks at its target from its distance, zooms,
// pans, clamps); descriptions validated, inputs validated, kinds kept.
#include <gtest/gtest.h>

import std;
import gfx.camera;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::camera;
using gfx::tests::describe;

constexpr float PI = std::numbers::pi_v<float>;

TEST(CameraControllerTest, FreeFlyTurnsAboutTheCamerasOwnAxes)
{
    const std::unique_ptr<Camera> camera = create_camera();
    const std::unique_ptr<CameraController> controller = create_camera_controller(FreeFlyControllerDescription{.speed = 4});
    EXPECT_EQ(controller->kind(), CameraControllerKind::FREE_FLY);
    EXPECT_TRUE(controller->backend() == CameraBackendKind::STANDARD);

    // A positive yaw turns right; a positive pitch looks up; a positive roll banks right.
    controller->update(*camera, {.look = {.x = PI / 2}});
    EXPECT_TRUE(approximately_equal(camera->forward(), {.x = 1}, 1e-6F)) << describe(camera->forward());
    controller->update(*camera, {.look = {.y = PI / 2}});
    EXPECT_TRUE(approximately_equal(camera->forward(), {.y = 1}, 1e-6F)) << describe(camera->forward());
    const std::unique_ptr<Camera> level = create_camera();
    controller->update(*level, {.roll = PI / 2});
    EXPECT_TRUE(approximately_equal(level->up(), {.x = 1}, 1e-6F)) << "banking right tilts the top to the right: " << describe(level->up());

    // Movement along the turned axes: speed × seconds.
    const std::unique_ptr<Camera> flier = create_camera({.orientation = Quaternion::from_axis_angle({.x = 1}, PI / 2)});
    controller->update(*flier, {.movement = {.z = 1}, .seconds = 0.5F});
    EXPECT_TRUE(approximately_equal(flier->position(), {.y = 2}, 1e-6F)) << "forward is up when the camera looks up: " << describe(flier->position());
    controller->update(*flier, {.movement = {.x = 1, .y = 1}, .seconds = 0.25F});
    EXPECT_TRUE(approximately_equal(flier->position(), {.x = 1, .y = 2, .z = 1}, 1e-5F)) << describe(flier->position());
}

TEST(CameraControllerTest, FirstPersonClampsPitchDropsRollAndWalksLevel)
{
    const std::unique_ptr<Camera> camera = create_camera({.orientation = Quaternion::from_yaw_pitch_roll({.yaw = 0.3F, .pitch = 0.1F, .roll = 0.5F})});
    const std::unique_ptr<CameraController> controller =
        create_camera_controller(FirstPersonControllerDescription{.speed = 2, .minimum_pitch = -1, .maximum_pitch = 1.2F});

    // The roll goes at the first update; pitch stops at its limit; yaw turns about the up axis.
    controller->update(*camera, {});
    EXPECT_NEAR(camera->yaw_pitch_roll().roll, 0, 1e-5F);
    EXPECT_NEAR(camera->yaw_pitch_roll().yaw, 0.3F, 1e-5F);
    EXPECT_NEAR(camera->yaw_pitch_roll().pitch, 0.1F, 1e-5F);
    controller->update(*camera, {.look = {.x = 0.3F, .y = 5}});
    EXPECT_NEAR(camera->yaw_pitch_roll().pitch, 1.2F, 1e-5F);
    EXPECT_NEAR(camera->yaw_pitch_roll().yaw, 0, 1e-5F);
    controller->update(*camera, {.look = {.y = -10}});
    EXPECT_NEAR(camera->yaw_pitch_roll().pitch, -1, 1e-5F);

    // Walking forward while looking down stays level; +Y movement goes up the axis.
    controller->update(*camera, {.movement = {.z = 1}, .seconds = 1});
    EXPECT_TRUE(approximately_equal(camera->position(), {.z = -2}, 1e-5F)) << describe(camera->position());
    controller->update(*camera, {.movement = {.x = 1, .y = 1}, .seconds = 0.5F});
    EXPECT_TRUE(approximately_equal(camera->position(), {.x = 1, .y = 1, .z = -2}, 1e-5F)) << describe(camera->position());

    // Another up axis (+Z, as in many modeling tools): yaw turns about it, movement stays perpendicular to it.
    const std::unique_ptr<Camera> modeler = create_camera();
    const std::unique_ptr<CameraController> z_up = create_camera_controller(FirstPersonControllerDescription{.speed = 1, .up = {.z = 3}});
    modeler->look_at({.y = 1}, {.z = 1});
    z_up->update(*modeler, {.look = {.x = PI / 2}});
    EXPECT_TRUE(approximately_equal(modeler->forward(), {.x = 1}, 1e-5F)) << describe(modeler->forward());
    EXPECT_TRUE(approximately_equal(modeler->up(), {.z = 1}, 1e-5F)) << describe(modeler->up());
    z_up->update(*modeler, {.movement = {.z = 1}, .look = {.y = 0.5F}, .seconds = 2});
    EXPECT_NEAR(modeler->position().z, 0, 1e-5F) << describe(modeler->position());
    EXPECT_NEAR(modeler->position().x, 2, 1e-5F) << describe(modeler->position());
}

TEST(CameraControllerTest, OrbitLooksAtItsTargetFromItsDistance)
{
    const OrbitControllerDescription initial{.target = {.x = 1, .y = 2, .z = 3}, .distance = 10, .yaw = 0, .pitch = -0.5F, .minimum_distance = 2,
                                             .maximum_distance = 20, .zoom_rate = 0.5F, .pan_speed = 0.1F};
    const std::unique_ptr<CameraController> controller = create_camera_controller(initial);
    const std::unique_ptr<Camera> camera = create_camera();

    // After any update the camera is at the distance from the target, looking at it.
    const auto check = [&camera, &controller](std::string_view label) {
        const OrbitControllerDescription state = std::get<OrbitControllerDescription>(controller->description());
        EXPECT_NEAR(distance(camera->position(), state.target), state.distance, 1e-4F) << label;
        EXPECT_TRUE(approximately_equal(camera->forward(), normalized(state.target - camera->position()), 1e-5F)) << label << ": " << describe(camera->forward());
        EXPECT_NEAR(dot(camera->right(), {.y = 1}), 0, 1e-5F) << label << ": no roll";
    };
    controller->update(*camera, {});
    check("initial");
    EXPECT_GT(camera->position().y, 2) << "a negative pitch looks down at the target from above";

    // Turning: yaw circles the target, pitch stops at its limit.
    controller->update(*camera, {.look = {.x = 1, .y = 0.2F}});
    check("turned");
    EXPECT_NEAR(std::get<OrbitControllerDescription>(controller->description()).yaw, -1, 1e-6F);
    controller->update(*camera, {.look = {.y = 9}});
    check("pitched");
    EXPECT_NEAR(std::get<OrbitControllerDescription>(controller->description()).pitch, 1.55F, 1e-6F);

    // Zoom: distance × exp(-zoom × rate), clamped.
    controller->update(*camera, {.zoom = 1});
    check("zoomed");
    EXPECT_NEAR(std::get<OrbitControllerDescription>(controller->description()).distance, 10 * std::exp(-0.5F), 1e-4F);
    controller->update(*camera, {.zoom = -100});
    EXPECT_EQ(std::get<OrbitControllerDescription>(controller->description()).distance, 20);
    controller->update(*camera, {.zoom = 100});
    EXPECT_EQ(std::get<OrbitControllerDescription>(controller->description()).distance, 2);

    // Panning moves the target along the camera's right axis: pan speed × distance × seconds.
    const Vector3 before = std::get<OrbitControllerDescription>(controller->description()).target;
    const Vector3 right = camera->right();
    controller->update(*camera, {.movement = {.x = 1}, .seconds = 2});
    check("panned");
    EXPECT_TRUE(approximately_equal(std::get<OrbitControllerDescription>(controller->description()).target, before + right * (0.1F * 2 * 2), 1e-5F));

    // The state can be replaced, with a description of the same kind only.
    controller->set_description(initial);
    controller->update(*camera, {});
    check("reset");
    EXPECT_EQ(std::get<OrbitControllerDescription>(controller->description()).distance, 10);
    EXPECT_THROW(controller->set_description(FreeFlyControllerDescription{}), InvalidUsageError);
}

TEST(CameraControllerTest, DescriptionsAndInputsAreValidated)
{
    // Descriptions: limits in order, pitch short of the poles, an up axis with a direction, distances in range.
    try
    {
        [[maybe_unused]] const std::unique_ptr<CameraController> refused =
            create_camera_controller(FirstPersonControllerDescription{.minimum_pitch = -2, .maximum_pitch = 1});
        FAIL() << "a pitch limit beyond -π/2 must be refused";
    }
    catch (const InvalidUsageError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::camera::STANDARD] create_camera_controller: FIRST_PERSON: the pitch limits must satisfy")) << message;
        EXPECT_NE(message.find("(they are -2 and 1)"), std::string_view::npos) << message;
    }
    EXPECT_THROW((void)create_camera_controller(FreeFlyControllerDescription{.speed = -1}), InvalidUsageError);
    EXPECT_THROW((void)create_camera_controller(FirstPersonControllerDescription{.up = {}}), InvalidUsageError);
    EXPECT_THROW((void)create_camera_controller(OrbitControllerDescription{.distance = 0.01F}), InvalidUsageError);
    EXPECT_THROW((void)create_camera_controller(OrbitControllerDescription{.minimum_distance = 10, .maximum_distance = 5}), InvalidUsageError);
    EXPECT_THROW((void)create_camera_controller(OrbitControllerDescription{.pitch = 1.6F}), InvalidUsageError);
    EXPECT_THROW((void)create_camera_controller(OrbitControllerDescription{.target = {.x = std::numeric_limits<float>::infinity()}}), InvalidUsageError);

    // Inputs: finite values and a time of at least 0; a refused input moves nothing.
    const std::unique_ptr<Camera> camera = create_camera();
    const std::unique_ptr<CameraController> controller = create_camera_controller(FreeFlyControllerDescription{});
    EXPECT_THROW(controller->update(*camera, {.look = {.x = std::numeric_limits<float>::quiet_NaN()}}), InvalidUsageError);
    EXPECT_THROW(controller->update(*camera, {.movement = {.z = 1}, .seconds = -1}), InvalidUsageError);
    EXPECT_EQ(camera->position(), Vector3{});
    EXPECT_EQ(camera->orientation(), Quaternion{});

    // Kinds follow the description's alternative.
    static_assert(controller_kind(OrbitControllerDescription{}) == CameraControllerKind::ORBIT);
    static_assert(controller_kind(FirstPersonControllerDescription{}) == CameraControllerKind::FIRST_PERSON);
    EXPECT_EQ(create_camera_controller(OrbitControllerDescription{})->kind(), CameraControllerKind::ORBIT);
}

} // namespace
