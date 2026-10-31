// The GPU backend (phase G0): capability probing on real and fake devices, the variants chosen, bodies moved on the GPU
// against closed forms (gravity, forces, impulses, damping, locks, kinematic motion, free rotation about the center of
// mass), the same edits giving the same bits, what it refuses until a later phase, the factory's own device. Every test
// of a real device runs on each GPU of the machine with the Vulkan validation layers (vulkan_test.hpp).
#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<gfx::Instance> instance =
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx GPU physics tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

/// A body's mass properties: @p mass, a diagonal inertia, a center of mass at @p center (body frame).
auto
mass_of(float mass,
        gfx::Vector3 inertia = {.x = 1, .y = 1, .z = 1},
        gfx::Vector3 center = {}) -> MassProperties
{
    return {.mass = mass,
            .center_of_mass = center,
            .inertia = gfx::Matrix3{.columns = {gfx::Vector3{.x = inertia.x}, gfx::Vector3{.y = inertia.y}, gfx::Vector3{.z = inertia.z}}}};
}

/// A world on each GPU (with 64-bit atomics when the GPU has them), with the validation layers.
class GpuXpbdTest : public gfx::tests::VulkanDeviceTest,
                    public testing::WithParamInterface<std::size_t>
{
protected:
    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam());
    }

    [[nodiscard]] auto required_features() const -> std::vector<gfx::DeviceFeature> override
    {
        if (m_physical_device->supports(gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS))
        {
            return {gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS};
        }
        return {};
    }

    /// A world on the test's device.
    [[nodiscard]] auto world(const PhysicsWorldDescription& description = {},
                             const gpu::GpuXpbdDescription& settings = {}) -> std::unique_ptr<PhysicsWorld>
    {
        return gpu::create_physics_world(description, *m_device, settings);
    }
};

TEST_P(GpuXpbdTest, CapabilitiesAreReportedAndVariantsChosen)
{
    const gpu::GpuXpbdCapabilities found = gpu::capabilities(*m_device);
    const gfx::DeviceLimits& limits = m_physical_device->properties().limits;
    EXPECT_EQ(found.device_name, m_physical_device->properties().name);
    EXPECT_EQ(found.queue_type, gfx::QueueType::COMPUTE) << "the test device has a compute queue";
    EXPECT_EQ(found.queue_family, m_device->queue(gfx::QueueType::COMPUTE).family_index());
    EXPECT_EQ(found.subgroup_operations, limits.subgroup_operations);
    EXPECT_GE(found.free_storage_buffers, GPU_XPBD_STORAGE_BUFFERS);
    EXPECT_EQ(found.subgroups, gfx::has_all(limits.subgroup_operations, gfx::SubgroupOperation::BASIC | gfx::SubgroupOperation::ARITHMETIC) &&
                                   limits.minimum_subgroup_size >= GPU_XPBD_MINIMUM_SUBGROUP_SIZE);
    EXPECT_EQ(found.integer64_atomics, m_physical_device->supports(gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS));

    // The portable variants whatever the device has; the summary says which run.
    const gpu::GpuXpbdCapabilities portable =
        gpu::capabilities(*m_device, {.subgroups = GpuVariantChoice::PORTABLE, .integer64_atomics = GpuVariantChoice::PORTABLE});
    EXPECT_FALSE(portable.subgroups);
    EXPECT_FALSE(portable.integer64_atomics);
    const std::string summary = gpu::to_string(portable);
    EXPECT_TRUE(contains(summary, found.device_name)) << summary;
    EXPECT_TRUE(contains(summary, "shared memory for scans and sorts")) << summary;
    EXPECT_TRUE(contains(summary, "two 32-bit passes for nearest hits")) << summary;
    std::cout << gpu::to_string(found) << '\n';
}

TEST_P(GpuXpbdTest, ThePipelineIsTheBackendsOwn)
{
    const std::unique_ptr<PhysicsWorld> simulated = world();
    EXPECT_TRUE(simulated->backend() == PhysicsBackendKind::GPU_XPBD);
    EXPECT_EQ(simulated->broad_phase().kind(), BroadPhaseKind::BACKEND);
    EXPECT_FALSE(simulated->broad_phase().is_external());
    EXPECT_EQ(simulated->narrow_phase().kind(), NarrowPhaseKind::BACKEND);
    EXPECT_EQ(simulated->solver().kind(), SolverKind::XPBD);
    EXPECT_EQ(simulated->integrator().kind(), IntegratorKind::XPBD);
    EXPECT_THROW(simulated->replace_solver(std::make_unique<SequentialImpulseSolver>()), gfx::BackendCapabilityError);
    EXPECT_THROW(simulated->replace_integrator(std::make_unique<SemiImplicitEulerIntegrator>()), gfx::BackendCapabilityError);
    EXPECT_THROW((void)simulated->soft_body_integrator(), gfx::BackendCapabilityError) << "the backend simulates its soft bodies itself";
}

TEST_P(GpuXpbdTest, BodiesFallAsSubsteppedEulerPredicts)
{
    // Substeps of h: v_n = v0 + n h g, x_n = x0 + n h v0 + h² g n (n + 1) / 2.
    constexpr std::uint32_t SUBSTEPS = 4;
    constexpr std::uint32_t STEPS = 60;
    constexpr float STEP = 1.0F / 60.0F;
    const std::unique_ptr<PhysicsWorld> simulated = world({.solver = {.sub_steps = SUBSTEPS}});
    const std::unique_ptr<RigidBody> body = simulated->create_rigid_body(
        {.pose = {.position = {.y = 10}}, .linear_velocity = {.x = 1, .z = -2}, .angular_damping = 0, .mass_properties = mass_of(2)});
    for (std::uint32_t step = 0; step < STEPS; ++step)
    {
        simulated->step(STEP);
    }
    const double h = static_cast<double>(STEP) / SUBSTEPS;
    const double n = static_cast<double>(STEPS) * SUBSTEPS;
    const double g = -static_cast<double>(STANDARD_GRAVITY);
    const RigidBodyState state = body->state();
    EXPECT_NEAR(state.pose.position.x, n * h * 1.0, 1e-4);
    EXPECT_NEAR(state.pose.position.y, 10.0 + h * h * g * n * (n + 1) / 2, 1e-4);
    EXPECT_NEAR(state.pose.position.z, n * h * -2.0, 1e-4);
    EXPECT_NEAR(state.linear_velocity.x, 1.0, 1e-5);
    EXPECT_NEAR(state.linear_velocity.y, n * h * g, 1e-4);
    EXPECT_NEAR(state.linear_velocity.z, -2.0, 1e-5);
    EXPECT_EQ(state.angular_velocity, gfx::Vector3{});
    EXPECT_TRUE(state.awake);
    EXPECT_EQ(simulated->step_count(), STEPS);
}

TEST_P(GpuXpbdTest, SpinningBodiesTurnAboutTheirCenterOfMass)
{
    // No gravity; the center of mass half a meter along x of the frame's origin, the spin about the principal z axis: the
    // center stays, the frame turns around it at the same rate (each substep turns by 2 atan(h ω / 2), a little less than
    // h ω; the velocity stays: it comes from the rotation vector, not from the two orientations).
    const std::unique_ptr<PhysicsWorld> simulated = world({.gravity = {}});
    const gfx::Vector3 center{.x = 0.5F};
    const std::unique_ptr<RigidBody> body = simulated->create_rigid_body({.pose = {},
                                                                          .linear_velocity = gfx::cross(gfx::Vector3{.z = 2}, -center),
                                                                          .angular_velocity = {.z = 2},
                                                                          .angular_damping = 0,
                                                                          .mass_properties = mass_of(3, {.x = 1, .y = 2, .z = 3}, center)});
    constexpr int STEPS = 60;
    for (int step = 0; step < STEPS; ++step)
    {
        simulated->step(1.0F / 60.0F);
    }
    const RigidBodyState state = body->state();
    const float angle = 2.0F;
    const gfx::Vector3 frame = center + gfx::rotate(gfx::Quaternion::from_axis_angle({.z = 1}, angle), -center);
    EXPECT_NEAR(state.angular_velocity.z, 2.0F, 1e-5F);
    EXPECT_NEAR(state.angular_velocity.x, 0.0F, 1e-4F);
    EXPECT_NEAR(state.pose.position.x, frame.x, 1e-4F);
    EXPECT_NEAR(state.pose.position.y, frame.y, 1e-4F);
    const gfx::Vector3 moved_center = state.pose.position + gfx::rotate(state.pose.orientation, center);
    EXPECT_NEAR(moved_center.x, center.x, 1e-4F) << "the center of mass does not move";
    EXPECT_NEAR(moved_center.y, center.y, 1e-4F);
}

TEST_P(GpuXpbdTest, ForcesImpulsesDampingAndLocksMoveBodiesAsTheySay)
{
    constexpr float STEP = 1.0F / 60.0F;
    constexpr std::uint32_t SUBSTEPS = 4;
    const std::unique_ptr<PhysicsWorld> simulated = world({.gravity = {.y = -10}, .solver = {.sub_steps = SUBSTEPS}});
    const auto body = [&](RigidBodyDescription description) {
        description.angular_damping = 0;
        if (description.kind == BodyKind::DYNAMIC)
        {
            description.mass_properties = mass_of(2);
        }
        return simulated->create_rigid_body(description);
    };
    const std::unique_ptr<RigidBody> struck = body({.gravity_scale = 0});
    const std::unique_ptr<RigidBody> pushed = body({.pose = {.position = {.x = 2}}, .gravity_scale = 0});
    const std::unique_ptr<RigidBody> damped = body({.pose = {.position = {.x = 4}}, .linear_velocity = {.x = 10}, .linear_damping = 1, .gravity_scale = 0});
    const std::unique_ptr<RigidBody> locked = body({.pose = {.position = {.x = 6}}, .locks = MotionLock::LINEAR_Y});
    const std::unique_ptr<RigidBody> light = body({.pose = {.position = {.x = 8}}, .gravity_scale = 0.5F});
    const std::unique_ptr<RigidBody> carried = body({.kind = BodyKind::KINEMATIC, .pose = {.position = {.x = 10}}, .linear_velocity = {.x = 1}});
    const std::unique_ptr<RigidBody> fixed = body({.kind = BodyKind::STATIC, .pose = {.position = {.x = 12}}});
    const std::unique_ptr<RigidBody> sleeper = body({.pose = {.position = {.x = 14}}, .awake = false});

    // An impulse once; a force every step.
    struck->apply_linear_impulse({.x = 4});
    constexpr int STEPS = 60;
    for (int step = 0; step < STEPS; ++step)
    {
        pushed->apply_force({.y = 20});
        simulated->step(STEP);
    }
    const double h = static_cast<double>(STEP) / SUBSTEPS;
    const double n = static_cast<double>(STEPS) * SUBSTEPS;
    EXPECT_NEAR(struck->state().linear_velocity.x, 2.0F, 1e-3F) << "4 N s on 2 kg";
    EXPECT_NEAR(struck->state().pose.position.x, 2.0F, 1e-3F);
    EXPECT_NEAR(pushed->state().linear_velocity.y, 10.0F, 1e-2F) << "20 N on 2 kg for a second";
    EXPECT_NEAR(damped->state().linear_velocity.x, 10.0 / std::pow(1.0 + h, n), 1e-2) << "v / (1 + h c) per substep";
    EXPECT_EQ(locked->state().pose.position.y, 0.0F) << "y locked: gravity does not move it";
    EXPECT_EQ(locked->state().linear_velocity.y, 0.0F);
    EXPECT_NEAR(light->state().linear_velocity.y, -5.0F, 2e-2F) << "half the gravity";
    EXPECT_NEAR(carried->state().pose.position.x, 11.0F, 1e-3F) << "a kinematic body moves by its velocity, without gravity";
    EXPECT_EQ(carried->state().pose.position.y, 0.0F);
    EXPECT_EQ(fixed->state().pose.position.x, 12.0F);
    EXPECT_EQ(fixed->state().pose.position.y, 0.0F);
    EXPECT_EQ(sleeper->state().pose.position.y, 0.0F) << "asleep: it does not fall";
    EXPECT_FALSE(sleeper->state().awake);
    EXPECT_TRUE(simulated->events().sleeps.empty());

    // Pushed, it wakes and falls; the step reports it (an explicit wake_up() is not reported, as on the reference backend).
    sleeper->apply_linear_impulse({.x = 1});
    simulated->step(STEP);
    EXPECT_TRUE(sleeper->state().awake);
    EXPECT_LT(sleeper->state().pose.position.y, 0.0F);
    ASSERT_EQ(simulated->events().sleeps.size(), 1U);
    EXPECT_EQ(simulated->events().sleeps[0].kind, SleepEventKind::WOKE_UP);
    EXPECT_EQ(simulated->events().sleeps[0].body, sleeper->handle());
}

TEST_P(GpuXpbdTest, IdenticalEditsGiveIdenticalBits)
{
    // Two worlds, the same edits (bodies made, pushed, turned and destroyed in the same order), the same states bit for
    // bit; then the portable variants, which change nothing in phase G0.
    const auto run = [&](const gpu::GpuXpbdDescription& settings) {
        const std::unique_ptr<PhysicsWorld> simulated = world({.solver = {.sub_steps = 3}}, settings);
        std::vector<std::unique_ptr<RigidBody>> bodies;
        std::vector<RigidBodyState> states;
        for (int step = 0; step < 90; ++step)
        {
            // A few bodies each step, every third one destroyed later (slots reused).
            for (int index = 0; index < 4; ++index)
            {
                const float value = static_cast<float>(step * 4 + index);
                bodies.push_back(simulated->create_rigid_body({.pose = {.position = {.x = value * 0.1F, .y = std::sin(value), .z = std::cos(value)},
                                                                        .orientation = gfx::Quaternion::from_axis_angle(gfx::normalized(gfx::Vector3{.x = 1, .y = value, .z = 2}), value)},
                                                               .linear_velocity = {.x = std::cos(value), .y = 3, .z = -1},
                                                               .angular_velocity = {.x = value * 0.01F, .y = 1, .z = -0.5F},
                                                               .linear_damping = 0.1F,
                                                               .mass_properties = mass_of(1 + value * 0.01F, {.x = 1, .y = 2, .z = 3}, {.y = 0.25F})}));
            }
            bodies[static_cast<std::size_t>(step) % bodies.size()]->apply_force({.x = 5, .z = 1});
            bodies[static_cast<std::size_t>(step * 7) % bodies.size()]->apply_angular_impulse({.y = 0.5F});
            if (step % 3 == 2)
            {
                bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(step) % bodies.size()));
            }
            simulated->step(1.0F / 60.0F);
        }
        for (const std::unique_ptr<RigidBody>& body : bodies)
        {
            states.push_back(body->state());
        }
        return states;
    };
    const std::vector<RigidBodyState> first = run({});
    const std::vector<RigidBodyState> second = run({});
    const std::vector<RigidBodyState> portable = run({.variants = {.subgroups = GpuVariantChoice::PORTABLE, .integer64_atomics = GpuVariantChoice::PORTABLE}});
    ASSERT_EQ(first.size(), second.size());
    ASSERT_EQ(first.size(), portable.size());
    const auto same_bits = [](const RigidBodyState& a, const RigidBodyState& b) {
        return std::bit_cast<std::array<std::uint32_t, 13>>(std::array{a.pose.position.x, a.pose.position.y, a.pose.position.z, a.pose.orientation.x, a.pose.orientation.y,
                                                                         a.pose.orientation.z, a.pose.orientation.w, a.linear_velocity.x, a.linear_velocity.y,
                                                                         a.linear_velocity.z, a.angular_velocity.x, a.angular_velocity.y, a.angular_velocity.z}) ==
               std::bit_cast<std::array<std::uint32_t, 13>>(std::array{b.pose.position.x, b.pose.position.y, b.pose.position.z, b.pose.orientation.x, b.pose.orientation.y,
                                                                         b.pose.orientation.z, b.pose.orientation.w, b.linear_velocity.x, b.linear_velocity.y,
                                                                         b.linear_velocity.z, b.angular_velocity.x, b.angular_velocity.y, b.angular_velocity.z});
    };
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        EXPECT_TRUE(same_bits(first[index], second[index])) << "body " << index;
        EXPECT_TRUE(same_bits(first[index], portable[index])) << "body " << index << " (portable variants)";
    }
}

TEST_P(GpuXpbdTest, WhatLaterPhasesBringIsRefusedNow)
{
    const std::unique_ptr<PhysicsWorld> simulated = world();
    const std::unique_ptr<RigidBody> body = simulated->create_rigid_body({.mass_properties = mass_of(1)});
    const auto refused = [](const auto& create, std::string_view part) {
        try
        {
            (void)create();
            ADD_FAILURE() << "accepted: " << part;
        }
        catch (const gfx::BackendCapabilityError& error)
        {
            EXPECT_TRUE(contains(error.what(), "[gfx::physics::GPU_XPBD]")) << error.what();
            EXPECT_TRUE(contains(error.what(), part)) << error.what();
        }
    };
    refused([&] { return body->add_collider({.shape = make_shape(SphereShape{.radius = 1})}); }, "phases G2 to G5");
    refused([&] { return simulated->create_joint(ball_and_socket_joint({.first = body->handle()})); }, "phase G6");
    refused([&] { return simulated->create_soft_body(rope_preset({})); }, "phase G7");
    refused([&] { return simulated->create_trigger({.shape = make_shape(SphereShape{.radius = 1})}); }, "phase G8");
}

TEST_P(GpuXpbdTest, DevicesWithAGraphicsQueueOnlyStepOnIt)
{
    // The default device: one graphics queue.
    std::unique_ptr<gfx::Device> device = m_physical_device->create_device({});
    {
        const std::unique_ptr<PhysicsWorld> simulated = gpu::create_physics_world({}, *device);
        EXPECT_EQ(gpu::capabilities(*device).queue_type, gfx::QueueType::GRAPHICS);
        const std::unique_ptr<RigidBody> body = simulated->create_rigid_body({.mass_properties = mass_of(1)});
        simulated->step(0.1F);
        EXPECT_LT(body->state().pose.position.y, 0.0F);
    }
    device.reset();
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdTest,
                         testing::ValuesIn(physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return std::format("Device{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdTest);

TEST(GpuXpbdCapabilityTest, DevicesLackingRequiredCapabilitiesAreRefusedWithEachOneNamed)
{
    // A device with no queue, no limits and no bindless table: every missing capability in one error, required and reported.
    gfx::tests::FakePhysicalDevice physical_device{gfx::PhysicalDeviceType::DISCRETE_GPU};
    const std::unique_ptr<gfx::Device> device = physical_device.create_device({});
    for (const auto& attempt : std::array<std::function<void()>, 2>{[&] { (void)gpu::capabilities(*device); },
                                                                   [&] { (void)gpu::create_physics_world({}, *device); }})
    {
        try
        {
            attempt();
            ADD_FAILURE() << "a device without compute was accepted";
        }
        catch (const gfx::BackendCapabilityError& error)
        {
            const std::string_view message = error.what();
            EXPECT_TRUE(contains(message, "[gfx::physics::GPU_XPBD]")) << message;
            EXPECT_TRUE(contains(message, "'fake DISCRETE_GPU' lacks what the GPU backend needs")) << message;
            EXPECT_TRUE(contains(message, "a COMPUTE queue, or a GRAPHICS queue with COMPUTE capability: required 1, reported 0 COMPUTE, 0 GRAPHICS")) << message;
            EXPECT_TRUE(contains(message, "maximum_compute_workgroup_invocations: required 64, reported 0")) << message;
            EXPECT_TRUE(contains(message, "maximum_compute_shared_memory_size: required 16384, reported 0")) << message;
            EXPECT_TRUE(contains(message, "maximum_storage_buffer_range: required 134217728, reported 0")) << message;
            EXPECT_TRUE(contains(message, "DeviceFeature::SHADER_MEMORY_MODEL")) << message;
            EXPECT_TRUE(contains(message, "free bindless storage buffers: required 64, reported 0")) << message;
            EXPECT_FALSE(contains(message, "TIMELINE_SEMAPHORE")) << "the fake device has the core features: " << message;
        }
    }
}

TEST(GpuXpbdFactoryTest, TheFactoryMakesWorldsOnADeviceOfTheirOwn)
{
    const PhysicsBackendAvailability availability = physics_backend_availability(PhysicsBackendKind::GPU_XPBD);
    EXPECT_TRUE(availability.built);
    EXPECT_EQ(availability.available, availability.reason.empty()) << availability.reason;
    if (!availability.available)
    {
        GTEST_SKIP() << availability.reason;
    }
    const std::vector<PhysicsBackendKind> backends = available_physics_backends();
    const auto reference = std::ranges::find(backends, PhysicsBackendKind::REFERENCE);
    ASSERT_NE(reference, backends.end());
    EXPECT_EQ(*(reference + 1), PhysicsBackendKind::GPU_XPBD) << "after the reference backend, before the null one";

    // Two worlds at once, each on its own device.
    const std::unique_ptr<PhysicsWorld> first = create_physics_world({.asynchronous_stepping = true}, PhysicsBackendKind::GPU_XPBD);
    const std::unique_ptr<PhysicsWorld> second = create_physics_world({}, PhysicsBackendKind::GPU_XPBD);
    const std::unique_ptr<RigidBody> falling = first->create_rigid_body({.mass_properties = mass_of(1)});
    const std::unique_ptr<RigidBody> rising = second->create_rigid_body({.linear_velocity = {.y = 20}, .mass_properties = mass_of(1)});
    for (int step = 0; step < 10; ++step)
    {
        first->begin_step(1.0F / 60.0F);
        second->step(1.0F / 60.0F);
        first->wait_step();
    }
    EXPECT_LT(falling->state().pose.position.y, 0.0F);
    EXPECT_GT(rising->state().pose.position.y, 0.0F);
    EXPECT_TRUE(first->backend() == PhysicsBackendKind::GPU_XPBD);
}

} // namespace
