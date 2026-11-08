// What the physics conformance suite shares: the backends it runs on (Box3D when built, the reference backend, the GPU
// backend on each GPU of the machine in its two configurations when built), what each backend refuses, a fixture making
// worlds of the test's backend (the GPU's on a device with the Vulkan validation layers: any validation message fails
// the test), and the scenes' helpers.
#pragma once

#if GFX_TEST_GPU_XPBD
#include "gpu_test.hpp"
#else
#include <gtest/gtest.h>
#endif

import std;
import gfx.physics;
#if GFX_TEST_GPU_XPBD
import gfx.physics.gpu_xpbd;
#endif

namespace gfx::tests::conformance
{

using namespace gfx::physics;
using gfx::Quaternion;
using gfx::Transform;
using gfx::Vector3;

inline constexpr float STEP = 1.0F / 60.0F;
inline constexpr float PI = std::numbers::pi_v<float>;

/// @brief A backend the suite runs on: its kind; for the GPU backend, the physical device and the configuration.
struct ConformanceBackend
{
    PhysicsBackendKind kind = PhysicsBackendKind::REFERENCE;
    std::size_t device = 0; ///< The GPU backend's physical device.
    bool portable = false;  ///< The GPU backend without optional features: a device without 64-bit atomics, the
                            ///< portable variants (shared memory instead of subgroups), the Jacobi solver mode.

    [[nodiscard]] auto gpu() const noexcept -> bool
    {
        return kind == PhysicsBackendKind::GPU_XPBD;
    }

    /// @brief The test name's suffix: BOX3D, REFERENCE, GPU_XPBD_Device<n> or GPU_XPBD_Device<n>_PORTABLE.
    [[nodiscard]] auto name() const -> std::string
    {
        return gpu() ? std::format("{}_Device{}{}", kind, device, portable ? "_PORTABLE" : "") : std::format("{}", kind);
    }
};

/// @brief Every backend of this build the suite runs on.
inline auto
conformance_backends() -> std::vector<ConformanceBackend>
{
    std::vector<ConformanceBackend> backends;
    if (physics_backend_availability(PhysicsBackendKind::BOX3D).available)
    {
        backends.push_back({.kind = PhysicsBackendKind::BOX3D});
    }
    backends.push_back({.kind = PhysicsBackendKind::REFERENCE});
#if GFX_TEST_GPU_XPBD
    for (const std::size_t device : gpu_physical_devices())
    {
        backends.push_back({.kind = PhysicsBackendKind::GPU_XPBD, .device = device});
        backends.push_back({.kind = PhysicsBackendKind::GPU_XPBD, .device = device, .portable = true});
    }
#endif
    return backends;
}

/// @brief What a backend refuses when it is recorded (BackendCapabilityError naming the capability): Box3D's limits, the
///        GPU's user constraints (C++ code the GPU cannot run); the pipelines of Box3D and the GPU are their own.
inline auto
refused_capabilities(PhysicsBackendKind kind) -> std::span<const PhysicsCapability>
{
    static constexpr std::array BOX3D{PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS, PhysicsCapability::DYNAMIC_TRIANGLE_MESHES,
                                      PhysicsCapability::DYNAMIC_HEIGHT_FIELDS,         PhysicsCapability::SEPARATE_STATIC_FRICTION,
                                      PhysicsCapability::CUSTOM_BROAD_PHASE,            PhysicsCapability::CUSTOM_NARROW_PHASE,
                                      PhysicsCapability::CUSTOM_SOLVER,                 PhysicsCapability::CUSTOM_INTEGRATOR,
                                      PhysicsCapability::ROUNDED_HULLS,                 PhysicsCapability::LARGE_HULLS,
                                      PhysicsCapability::POSED_HEIGHT_FIELDS,           PhysicsCapability::WIDE_JOINT_LIMITS,
                                      PhysicsCapability::USER_CONSTRAINTS};
    static constexpr std::array GPU_XPBD{PhysicsCapability::CUSTOM_BROAD_PHASE, PhysicsCapability::CUSTOM_NARROW_PHASE, PhysicsCapability::CUSTOM_SOLVER,
                                         PhysicsCapability::CUSTOM_INTEGRATOR,  PhysicsCapability::USER_CONSTRAINTS};
    switch (kind)
    {
        case PhysicsBackendKind::BOX3D:
            return BOX3D;
        case PhysicsBackendKind::GPU_XPBD:
            return GPU_XPBD;
        default:
            return {};
    }
}

#if GFX_TEST_GPU_XPBD
using ConformanceBase = VulkanDeviceTest;
#else
using ConformanceBase = testing::Test;
#endif

/// @brief Worlds of the test's backend.
class ConformanceTest : public ConformanceBase,
                        public testing::WithParamInterface<ConformanceBackend>
{
protected:
    void SetUp() override
    {
        // A Vulkan instance (with validation) and a device for the GPU backend only.
#if GFX_TEST_GPU_XPBD
        if (GetParam().gpu())
        {
            ConformanceBase::SetUp();
        }
#endif
    }

#if GFX_TEST_GPU_XPBD
    [[nodiscard]] auto physical_device() -> PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam().device);
    }

    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        if (!GetParam().portable && m_physical_device->supports(DeviceFeature::SHADER_INTEGER64_ATOMICS))
        {
            return {DeviceFeature::SHADER_INTEGER64_ATOMICS};
        }
        return {};
    }
#endif

    [[nodiscard]] auto backend() const -> PhysicsBackendKind
    {
        return GetParam().kind;
    }

    /// @brief A world of the test's backend.
    [[nodiscard]] auto world(const PhysicsWorldDescription& description = {}) -> std::unique_ptr<PhysicsWorld>
    {
#if GFX_TEST_GPU_XPBD
        if (GetParam().gpu())
        {
            const bool portable = GetParam().portable;
            return gpu_xpbd::create_physics_world(
                description, *m_device,
                {.variants = portable ? gpu_xpbd::GpuXpbdVariants{.subgroups = GpuVariantChoice::PORTABLE, .integer64_atomics = GpuVariantChoice::PORTABLE}
                                      : gpu_xpbd::GpuXpbdVariants{},
                 .solver_mode = portable ? GpuSolverMode::JACOBI : GpuSolverMode::GRAPH_COLORING});
        }
#endif
        return create_physics_world(description, GetParam().kind);
    }

    /// @brief Whether the backend refuses @p capability.
    [[nodiscard]] auto refuses(PhysicsCapability capability) const -> bool
    {
        return std::ranges::contains(refused_capabilities(GetParam().kind), capability);
    }

    /// @brief Runs @p operation, which needs @p capability. When the backend refuses it, the operation throws the
    ///        BackendCapabilityError naming it and the backend, and false comes back (what follows does not apply);
    ///        otherwise it succeeds, and true comes back.
    template<class Operation>
    [[nodiscard]] auto supports(PhysicsCapability capability,
                                Operation&& operation) -> bool
    {
        if (!refuses(capability))
        {
            operation();
            return true;
        }
        try
        {
            operation();
            ADD_FAILURE() << GetParam().name() << " was expected to refuse " << to_string(capability);
        }
        catch (const BackendCapabilityError& error)
        {
            EXPECT_EQ(error.capability(), to_string(capability)) << error.what();
            EXPECT_NE(std::string_view{error.what()}.find(std::format("[gfx::physics::{}]", GetParam().kind)), std::string_view::npos) << error.what();
        }
        return false;
    }
};

/// @brief The suite's test names: the backend's.
inline auto
conformance_test_name(const testing::TestParamInfo<ConformanceBackend>& parameter) -> std::string
{
    return parameter.param.name();
}

// ===================================================================================================== scenes

[[nodiscard]] inline auto
sphere(float radius) -> std::shared_ptr<const Shape>
{
    return make_shape(SphereShape{.radius = radius});
}

[[nodiscard]] inline auto
box(Vector3 half_extents) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half_extents});
}

/// @brief A body with one collider.
struct Body
{
    std::unique_ptr<RigidBody> body;
    std::unique_ptr<Collider> collider;
};

[[nodiscard]] inline auto
make_body(PhysicsWorld& world,
          const RigidBodyDescription& description,
          const ColliderDescription& collider) -> Body
{
    Body made{.body = world.create_rigid_body(description), .collider = nullptr};
    made.collider = made.body->add_collider(collider);
    return made;
}

/// @brief A static floor: a box 100 m wide whose top is at y = 0.
[[nodiscard]] inline auto
make_floor(PhysicsWorld& world,
           const SurfaceMaterial& material = {}) -> Body
{
    return make_body(world, {.kind = BodyKind::STATIC}, {.shape = box({.x = 50, .y = 0.5F, .z = 50}), .pose = {.position = {.y = -0.5F}}, .material = material});
}

inline void
advance(PhysicsWorld& world,
    int steps)
{
    for (int step = 0; step < steps; ++step)
    {
        world.step(STEP);
    }
}

/// @brief The contacts of @p body (at most 16).
[[nodiscard]] inline auto
contacts_of(const PhysicsWorld& world,
            const RigidBody& body) -> std::vector<ContactInfo>
{
    std::array<ContactInfo, 16> storage{};
    const QueryResult<std::span<ContactInfo>> found = world.contacts_of(body.handle(), storage);
    return found ? std::vector<ContactInfo>(found->begin(), found->end()) : std::vector<ContactInfo>{};
}

/// @brief Whether @p event is between @p first and @p second (either order).
[[nodiscard]] inline auto
between(const ContactEvent& event,
        ColliderHandle first,
        ColliderHandle second) -> bool
{
    return std::minmax(event.first, event.second) == std::minmax(first, second);
}

/// @brief The bits of @p vector (to compare runs exactly).
[[nodiscard]] inline auto
bits(Vector3 vector) -> std::array<std::uint32_t, 3>
{
    return std::bit_cast<std::array<std::uint32_t, 3>>(vector);
}

} // namespace gfx::tests::conformance
