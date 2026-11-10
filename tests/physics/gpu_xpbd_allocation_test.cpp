// The GPU backend's steps allocate nothing once the storage they reuse has grown (docs/profiler-performance-design.md
// §A10, item 15; PF4): this executable counts every operator new, on every thread, while a world on each GPU of the
// machine steps after warm-up steps — boxes resting on a floor, a chain of ball joints hanging from the world, a cloth
// pinned at two corners lying on the boxes, a trigger zone around them, a force applied each step, the step's events,
// statistics and contacts read — with the backend's default optimizations (INCREMENTAL_LAYOUTS keeps the layouts the
// edits do not touch).
#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;

namespace
{

std::atomic<bool> g_counting{false};
std::atomic<std::uint64_t> g_allocations{0};

/// Never inlined: a breakpoint here (gdb) shows where a counted allocation comes from.
[[gnu::noinline]] void
counted_allocation() noexcept
{
    g_allocations.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

// Global allocation functions of this executable: counted while g_counting is set.
auto
operator new(std::size_t size) -> void*
{
    if (g_counting.load(std::memory_order_relaxed))
    {
        counted_allocation();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size); memory != nullptr)
    {
        return memory;
    }
    throw std::bad_alloc{};
}

auto
operator new[](std::size_t size) -> void*
{
    return operator new(size);
}

// Never inlined: an optimized caller would otherwise see free() release memory from operator new
// (-Wmismatched-new-delete in Release builds).
[[gnu::noinline]] void
operator delete(void* memory) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void
operator delete[](void* memory) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void
operator delete(void* memory,
                std::size_t) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void
operator delete[](void* memory,
                  std::size_t) noexcept
{
    std::free(memory);
}

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;
using gfx::Vector3;

constexpr float STEP = 1.0F / 60.0F;

/// @brief Steps before allocations are counted: the cloth lands, the chain swings out, every buffer reaches its size.
constexpr int WARM_UP_STEPS = 240;

/// @brief Steps whose allocations are counted.
constexpr int COUNTED_STEPS = 60;

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
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx GPU physics allocation tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// A world on each GPU (sleep off: every step does the same work), with a thread pool and the validation layers (their
/// allocations, the driver's, are their own DLLs' and not counted: this executable's operator new is its own).
class GpuXpbdAllocationTest : public gfx::tests::VulkanDeviceTest,
                              public testing::WithParamInterface<std::size_t>
{
protected:
    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam());
    }
};

TEST_P(GpuXpbdAllocationTest, StepsAllocateNothingAfterTheWarmUp)
{
    ThreadTaskScheduler scheduler{4};
    const std::unique_ptr<PhysicsWorld> world =
        gpu::create_physics_world({.task_scheduler = &scheduler, .sleep = {.enabled = false}, .debug_name = "allocation test"}, *m_device, {});
    std::vector<std::unique_ptr<RigidBody>> bodies;
    std::vector<std::unique_ptr<Collider>> colliders;
    const auto add = [&](const RigidBodyDescription& body, const ColliderDescription& collider) -> RigidBody& {
        bodies.push_back(world->create_rigid_body(body));
        colliders.push_back(bodies.back()->add_collider(collider));
        return *bodies.back();
    };

    // The floor, 8 x 8 boxes resting on it, a chain of 8 boxes hanging from the world beside them.
    (void)add({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, {.shape = make_shape(BoxShape{.half_extents = {.x = 20, .y = 0.5F, .z = 20}})});
    const std::shared_ptr<const Shape> cube = make_shape(BoxShape{.half_extents = {.x = 0.2F, .y = 0.2F, .z = 0.2F}});
    for (int row = 0; row < 8; ++row)
    {
        for (int column = 0; column < 8; ++column)
        {
            (void)add({.pose = {.position = {.x = 0.5F * static_cast<float>(column) - 1.75F, .y = 0.2F, .z = 0.5F * static_cast<float>(row) - 1.75F}}}, {.shape = cube});
        }
    }
    std::vector<std::unique_ptr<Joint>> joints;
    const std::shared_ptr<const Shape> link = make_shape(BoxShape{.half_extents = {.x = 0.05F, .y = 0.15F, .z = 0.05F}});
    RigidBodyHandle previous{};
    for (int index = 0; index < 8; ++index)
    {
        const float top = 6.0F - 0.3F * static_cast<float>(index);
        RigidBody& piece = add({.pose = {.position = {.x = 5.0F + 0.3F * static_cast<float>(index), .y = top - 0.15F}}}, {.shape = link});
        joints.push_back(world->create_joint(ball_and_socket_joint({.first = piece.handle(), .second = previous, .anchor = {.x = 5.0F + 0.3F * static_cast<float>(index), .y = top}})));
        previous = piece.handle();
    }

    // A cloth pinned at two corners, lying on the boxes; a trigger zone around them.
    SoftBodyDescription cloth = cloth_preset({.columns = 24, .rows = 24, .spacing = 0.15F, .origin = {.x = -1.75F, .y = 0.8F, .z = -1.75F}, .row_direction = {.z = 1}});
    cloth.pins.push_back(pin_to_world(cloth, 0));
    cloth.pins.push_back(pin_to_world(cloth, 23));
    const std::unique_ptr<SoftBody> sheet = world->create_soft_body(cloth);
    const std::unique_ptr<Trigger> zone = world->create_trigger({.shape = make_shape(BoxShape{.half_extents = {.x = 2.5F, .y = 1.0F, .z = 2.5F}}), .pose = {.position = {.y = 0.5F}}});

    // A step as an application does it: a force, the step, its events, statistics and a body's contacts read.
    std::uint64_t touching = 0;
    std::array<ContactInfo, 16> contacts{};
    const auto step = [&] {
        bodies[1]->apply_force({.y = 2.0F});
        world->step(STEP);
        const PhysicsEvents events = world->events();
        touching += events.contacts.size() + events.triggers.size() + events.joints.size();
        touching += world->statistics().contacts;
        if (const QueryResult<std::span<ContactInfo>> found = world->contacts_of(bodies[1]->handle(), contacts); found.has_value())
        {
            touching += found->size();
        }
    };
    for (int index = 0; index < WARM_UP_STEPS; ++index)
    {
        step();
    }

    // The count works: an allocation kept where the optimizer cannot remove it is counted.
    static std::unique_ptr<int> kept;
    g_allocations.store(0);
    g_counting.store(true);
    kept = std::make_unique<int>(WARM_UP_STEPS);
    g_counting.store(false);
    ASSERT_EQ(g_allocations.load(), 1U) << "this executable's operator new counts";

    g_allocations.store(0);
    g_counting.store(true);
    for (int index = 0; index < COUNTED_STEPS; ++index)
    {
        step();
    }
    g_counting.store(false);
    EXPECT_EQ(g_allocations.load(), 0U) << "allocations in " << COUNTED_STEPS << " steps after " << WARM_UP_STEPS << " (break on counted_allocation() to see where)";
    EXPECT_GT(touching, 0U) << "the scene touches";
    EXPECT_GT(world->statistics().contacts, 64U) << "the boxes rest on the floor";
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdAllocationTest,
                         testing::ValuesIn(physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return "Device" + std::to_string(parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdAllocationTest);

} // namespace
