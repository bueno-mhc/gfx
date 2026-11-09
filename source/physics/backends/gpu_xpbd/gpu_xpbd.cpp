// The GPU backend's entry points: whether the factory can run it here, worlds on a device of their own (the factory's)
// or on the application's device.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;
import gfx.backend_factory;
import gfx.physics.enums;
import gfx.physics.interface;
import gfx.physics.world;
import gfx.profiler;
import :settings;
import :probing;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

using profiler::StageCoverageKind::ABSENT;
using profiler::StageCoverageKind::FUSED_IN;
using profiler::StageCoverageKind::RECORDED;

/// The stages of section A5 (and the step's phases), in the order a step runs them.
constexpr std::array STAGES{
    profiler::StageCoverage{"step() entry and exit", RECORDED, "World: step", {}},
    profiler::StageCoverage{"deferred-edit apply (records)", RECORDED, "World: apply the edits to the records", {}},
    profiler::StageCoverage{"deferred-edit apply (backend)", RECORDED, "GPU XPBD: apply the edits", {}},
    profiler::StageCoverage{"layouts: soft bodies", RECORDED, "GPU XPBD: update the soft bodies", {}},
    profiler::StageCoverage{"layouts: colliders", RECORDED, "GPU XPBD: update the colliders", {}},
    profiler::StageCoverage{"layouts: joints", RECORDED, "GPU XPBD: update the joints", {}},
    profiler::StageCoverage{"command begin and end (recording)", RECORDED, "GPU XPBD: record the step", {}},
    profiler::StageCoverage{"buffer-pool acquire, staging drain", RECORDED, "uploads", {}},
    profiler::StageCoverage{"descriptor-arena reset", ABSENT, {}, "bindless: no descriptor set is written, bound or reset during a step"},
    profiler::StageCoverage{"body patches", RECORDED, "patch_bodies", {}},
    profiler::StageCoverage{"step start backed up (for replays)", RECORDED, "state backup", {}},
    profiler::StageCoverage{"phase: broad phase", RECORDED, "GPU XPBD: broad phase", {}},
    profiler::StageCoverage{"collider poses", RECORDED, "collider_poses", {}},
    profiler::StageCoverage{"instance boxes", RECORDED, "shape_bounds", {}},
    profiler::StageCoverage{"scene box (reduction)", RECORDED, "reduce_bounds", {}},
    profiler::StageCoverage{"BVH build", RECORDED, "BVH build", {}},
    profiler::StageCoverage{"Morton codes", RECORDED, "bvh_morton", {}},
    profiler::StageCoverage{"radix sort", RECORDED, "sort", {}},
    profiler::StageCoverage{"rank sort (sorts of GPU_XPBD_RANK_SORT_LIMIT keys at most)", RECORDED, "rank_sort", {}},
    profiler::StageCoverage{"radix histogram", RECORDED, "radix_histogram", {}},
    profiler::StageCoverage{"radix scan", RECORDED, "scan", {}},
    profiler::StageCoverage{"radix scatter", RECORDED, "radix_scatter", {}},
    profiler::StageCoverage{"longest common prefix and split", FUSED_IN, "bvh_hierarchy",
                            "the prefix search (Karras 2012) and the split are one pass, a node per invocation"},
    profiler::StageCoverage{"hierarchy", RECORDED, "bvh_hierarchy", {}},
    profiler::StageCoverage{"leaf boxes", FUSED_IN, "bvh_bounds", "the leaves and the internal nodes climb together in one pass"},
    profiler::StageCoverage{"internal boxes", RECORDED, "bvh_bounds", {}},
    profiler::StageCoverage{"treelet restructuring", ABSENT, {},
                            "0 treelet rounds for per-step trees by default (G2: a first round cost 75 ms at 100 000 boxes)"},
    profiler::StageCoverage{"four-wide collapse", RECORDED, "bvh_wide", {}},
    profiler::StageCoverage{"refit", ABSENT, {}, "the moving tree is rebuilt every step (refits by cadence: the BVH_REFIT optimization)"},
    profiler::StageCoverage{"pair enumeration", RECORDED, "BVH pairs", {}},
    profiler::StageCoverage{"pair counts", RECORDED, "bvh_pairs (count)", {}},
    profiler::StageCoverage{"pairs written", RECORDED, "bvh_pairs (write)", {}},
    profiler::StageCoverage{"BVH-assisted candidates (filtered, compacted)", RECORDED, "pairs_filter", {}},
    profiler::StageCoverage{"compaction", RECORDED, "compact", {}},
    profiler::StageCoverage{"phase: narrow phase", RECORDED, "GPU XPBD: narrow phase", {}},
    profiler::StageCoverage{"mid phase (sub-pairs counted)", RECORDED, "narrow_phase_pairs (count)", {}},
    profiler::StageCoverage{"mid phase (sub-pairs written)", RECORDED, "narrow_phase_pairs (write)", {}},
    profiler::StageCoverage{"GJK", FUSED_IN, "narrow_phase_collide", "GJK, EPA and clipping run in one kernel (one call site of the distance)"},
    profiler::StageCoverage{"EPA", FUSED_IN, "narrow_phase_collide", "GJK, EPA and clipping run in one kernel (one call site of the distance)"},
    profiler::StageCoverage{"manifold and feature identifiers", FUSED_IN, "narrow_phase_collide", "made by the kernel that collides each sub-pair"},
    profiler::StageCoverage{"sub-pair manifolds", RECORDED, "narrow_phase_collide", {}},
    profiler::StageCoverage{"manifolds merged and reduced", RECORDED, "narrow_phase_merge", {}},
    profiler::StageCoverage{"warm-start lookup (contact cache)", RECORDED, "contact cache", {}},
    profiler::StageCoverage{"contact cache keys", RECORDED, "contact_keys", {}},
    profiler::StageCoverage{"contact cache match", RECORDED, "contact_match", {}},
    profiler::StageCoverage{"contact constraints prepared", RECORDED, "contact_prepare", {}},
    profiler::StageCoverage{"phase: solver", RECORDED, "GPU XPBD: solver", {}},
    profiler::StageCoverage{"islands and waking", RECORDED, "islands", {}},
    profiler::StageCoverage{"adjacency (body-constraint graph)", RECORDED, "graph", {}},
    profiler::StageCoverage{"ranges of sorted keys", RECORDED, "ranges", {}},
    profiler::StageCoverage{"graph coloring", RECORDED, "coloring", {}},
    profiler::StageCoverage{"coloring rounds", RECORDED, "color", {}},
    profiler::StageCoverage{"overflow counts (mass splitting)", RECORDED, "jacobi_gather (counts)", {}},
    profiler::StageCoverage{"color dispatch arguments", RECORDED, "color dispatch arguments", {}},
    profiler::StageCoverage{"predict and integrate (bodies)", RECORDED, "xpbd_integrate", {}},
    profiler::StageCoverage{"predict (particles)", RECORDED, "particle_integrate", {}},
    profiler::StageCoverage{"phase: soft bodies", RECORDED, "GPU XPBD: soft bodies", {}},
    profiler::StageCoverage{"soft solve per iteration", RECORDED, "soft_solve iteration", {}},
    profiler::StageCoverage{"rigid solve per iteration", RECORDED, "position iteration", {}},
    profiler::StageCoverage{"joint projection by color", FUSED_IN, "position iteration", "a scope per iteration, not per color (hundreds a step otherwise)"},
    profiler::StageCoverage{"contact projection by color", FUSED_IN, "position iteration", "a scope per iteration, not per color (hundreds a step otherwise)"},
    profiler::StageCoverage{"Jacobi accumulation, deterministic reduction", RECORDED, "jacobi_gather", {}},
    profiler::StageCoverage{"compliance warm start", ABSENT, {}, "the multipliers start from zero each substep (GPU XPBD design, answer D8)"},
    profiler::StageCoverage{"velocity update (bodies)", RECORDED, "xpbd_velocity_update", {}},
    profiler::StageCoverage{"velocity update (particles)", RECORDED, "particle_velocity_update", {}},
    profiler::StageCoverage{"velocity iterations", RECORDED, "velocity iteration", {}},
    profiler::StageCoverage{"contact velocities by color", FUSED_IN, "velocity iteration", "a scope per iteration, not per color"},
    profiler::StageCoverage{"joint forces and breaking", RECORDED, "joint_forces", {}},
    profiler::StageCoverage{"sleep check", RECORDED, "sleep", {}},
    profiler::StageCoverage{"phase: events", RECORDED, "GPU XPBD: events", {}},
    profiler::StageCoverage{"touching contacts reported", RECORDED, "contacts_report", {}},
    profiler::StageCoverage{"triggers overlapped", RECORDED, "triggers", {}},
    profiler::StageCoverage{"trigger pairs filtered", RECORDED, "triggers_filter", {}},
    profiler::StageCoverage{"staging copies (readback)", RECORDED, "readback copies", {}},
    profiler::StageCoverage{"submit", RECORDED, "GPU XPBD: submit", {}},
    profiler::StageCoverage{"fence wait (timeline)", RECORDED, "GPU XPBD: wait for the GPU", {}},
    profiler::StageCoverage{"async readback (bodies, joints, particles)", RECORDED, "GPU XPBD: read the bodies back", {}},
    profiler::StageCoverage{"event drain (contacts)", RECORDED, "GPU XPBD: contacts and their events", {}},
    profiler::StageCoverage{"event drain (triggers)", RECORDED, "GPU XPBD: trigger events", {}},
    profiler::StageCoverage{"framework bounds", RECORDED, "World: update the bounds", {}},
    profiler::StageCoverage{"framework publish", RECORDED, "World: publish", {}},
    profiler::StageCoverage{"framework query tree", RECORDED, "World: update the query tree", {}},
};

/// The instance of a world made by the factory (no validation layers: the factory has no place to ask for them).
[[nodiscard]] auto
make_instance() -> std::unique_ptr<Instance>
{
    return create_instance(BackendKind::VULKAN, {.application_name = "gfx physics (GPU backend)"});
}

/// The physical devices of @p instance in the order the factory tries them: the one the graphics library selects first,
/// then the others.
[[nodiscard]] auto
candidates(Instance& instance) -> std::vector<PhysicalDevice*>
{
    std::vector<PhysicalDevice*> devices;
    try
    {
        devices.push_back(&instance.select_physical_device(PhysicalDevicePreference::HIGH_PERFORMANCE));
    }
    catch (const BackendCapabilityError&)
    {
        // No device with the core features: each is tried below and says what it lacks.
    }
    for (std::size_t index = 0; index < instance.physical_device_count(); ++index)
    {
        PhysicalDevice* device = &instance.physical_device(index);
        if (!std::ranges::contains(devices, device))
        {
            devices.push_back(device);
        }
    }
    return devices;
}

/// Why no physical device of @p devices can run the backend (empty when one can), and the first that can.
[[nodiscard]] auto
first_capable(std::span<PhysicalDevice* const> devices) -> std::pair<PhysicalDevice*, std::string>
{
    std::string lacking;
    for (PhysicalDevice* device : devices)
    {
        const std::vector<MissingCapability> missing = missing_capabilities(*device);
        if (missing.empty())
        {
            return {device, {}};
        }
        lacking += std::format("\n'{}' lacks:{}", device->properties().name, missing_lines(missing));
    }
    return {nullptr, devices.empty() ? std::string{"no physical device supports Vulkan"} : std::format("no physical device can run it:{}", lacking)};
}

/// An instance and a device on the most capable physical device that runs the backend.
[[nodiscard]] auto
make_owned_device(const GpuXpbdDescription& settings) -> OwnedDevice
{
    const BackendAvailability vulkan = backend_availability(BackendKind::VULKAN);
    if (!vulkan.available)
    {
        throw_unsupported(PhysicsBackendKind::GPU_XPBD, "create_physics_world", PhysicsCapability::WORLD_OBJECTS,
                          std::format("the graphics library's Vulkan backend is unavailable: {}", vulkan.reason),
                          "install a Vulkan driver, or use another backend (PhysicsBackendKind::REFERENCE or BOX3D)");
    }
    OwnedDevice owned{.instance = make_instance(), .device = {}};
    const std::vector<PhysicalDevice*> devices = candidates(*owned.instance);
    const auto [physical_device, reason] = first_capable(devices);
    if (physical_device == nullptr)
    {
        throw_unsupported(PhysicsBackendKind::GPU_XPBD, "create_physics_world", PhysicsCapability::WORLD_OBJECTS, reason,
                          "use another backend (PhysicsBackendKind::REFERENCE or BOX3D)");
    }

    // One compute queue; 64-bit atomics when the device has them and the settings allow them.
    static constexpr std::array QUEUES{QueueRequest{.type = QueueType::COMPUTE, .count = 1, .priority = 1.0F}};
    std::vector<DeviceFeature> features;
    if (settings.variants.integer64_atomics == GpuVariantChoice::AUTOMATIC && physical_device->supports(DeviceFeature::SHADER_INTEGER64_ATOMICS))
    {
        features.push_back(DeviceFeature::SHADER_INTEGER64_ATOMICS);
    }
    owned.device = physical_device->create_device({.queues = QUEUES, .required_features = features, .debug_name = "gfx physics GPU backend"});
    return owned;
}

} // namespace

auto
unavailable_reason() -> std::string
{
    const BackendAvailability vulkan = backend_availability(BackendKind::VULKAN);
    if (!vulkan.available)
    {
        return std::format("the graphics library's Vulkan backend is unavailable: {}", vulkan.reason);
    }
    try
    {
        const std::unique_ptr<Instance> instance = make_instance();
        const std::vector<PhysicalDevice*> devices = candidates(*instance);
        return first_capable(devices).second;
    }
    catch (const Error& error)
    {
        return error.what();
    }
}

auto
create_physics_world(const PhysicsWorldDescription& description,
                     const GpuXpbdDescription& settings) -> std::unique_ptr<PhysicsWorld>
{
    check_world_description(description, PhysicsBackendKind::GPU_XPBD);
    OwnedDevice owned = make_owned_device(settings);
    Device& device = *owned.device;
    return std::make_unique<SteppedWorld>(description, std::make_unique<GpuXpbdBackend>(std::move(owned), device, settings));
}

auto
create_physics_world(const PhysicsWorldDescription& description,
                     Device& device,
                     const GpuXpbdDescription& settings) -> std::unique_ptr<PhysicsWorld>
{
    check_world_description(description, PhysicsBackendKind::GPU_XPBD);
    return std::make_unique<SteppedWorld>(description, std::make_unique<GpuXpbdBackend>(OwnedDevice{}, device, settings));
}

auto
stage_coverage() noexcept -> std::span<const profiler::StageCoverage>
{
    return STAGES;
}

} // namespace gfx::physics::gpu_xpbd
