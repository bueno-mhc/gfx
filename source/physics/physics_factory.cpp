// The physics factory: which backends were built (GFX_WITH_PHYSICS_<NAME>_BACKEND), whether each can run, and worlds of
// the available ones. The backend modules are imported here only.
module gfx.physics.factory;

import std;
import gfx.core.errors;
import gfx.physics.interface;
import gfx.physics.backends.null;
import gfx.physics.backends.reference;
#if defined(GFX_WITH_PHYSICS_BOX3D_BACKEND)
import gfx.physics.backends.box3d;
#endif
#if defined(GFX_WITH_PHYSICS_CUSTOM_BACKEND)
import gfx.physics.backends.custom;
#endif

namespace gfx::physics
{

namespace
{

/// Throws the BackendCapabilityError of a backend that cannot make worlds.
[[noreturn]] void
reject_backend(const PhysicsBackendAvailability& availability)
{
    throw_unsupported(availability.backend, "create_physics_world", PhysicsCapability::WORLD_OBJECTS, availability.reason,
                      availability.built ? "use one of available_physics_backends()"
                                         : "configure the library with the backend (see the GFX_PHYSICS_* CMake options), or use one of "
                                           "available_physics_backends()");
}

} // namespace

auto
physics_backend_availability(PhysicsBackendKind backend) -> PhysicsBackendAvailability
{
    switch (backend)
    {
        case PhysicsBackendKind::BOX3D:
        {
#if defined(GFX_WITH_PHYSICS_BOX3D_BACKEND)
            std::string reason = box3d_backend::unavailable_reason();
            const bool available = reason.empty();
            return {.backend = backend, .built = true, .available = available, .reason = std::move(reason)};
#else
            return {.backend = backend, .built = false, .available = false, .reason = "the Box3D backend was not built (the CMake option GFX_PHYSICS_BACKEND_BOX3D is OFF)"};
#endif
        }
        case PhysicsBackendKind::REFERENCE:
        {
            std::string reason = reference_backend::unavailable_reason();
            const bool available = reason.empty();
            return {.backend = backend, .built = true, .available = available, .reason = std::move(reason)};
        }
        case PhysicsBackendKind::NULL_BACKEND:
        {
            std::string reason = null_backend::unavailable_reason();
            const bool available = reason.empty();
            return {.backend = backend, .built = true, .available = available, .reason = std::move(reason)};
        }
        case PhysicsBackendKind::CUSTOM:
        {
#if defined(GFX_WITH_PHYSICS_CUSTOM_BACKEND)
            std::string reason = custom_backend::unavailable_reason();
            const bool available = reason.empty();
            return {.backend = backend, .built = true, .available = available, .reason = std::move(reason)};
#else
            return {.backend = backend,
                    .built = false,
                    .available = false,
                    .reason = "no custom backend was built (the CMake option GFX_PHYSICS_CUSTOM_BACKEND_TARGET names none)"};
#endif
        }
    }
    return {.backend = backend, .built = false, .available = false, .reason = std::format("{} is not a physics backend", std::to_underlying(backend))};
}

auto
available_physics_backends() -> std::vector<PhysicsBackendKind>
{
    // Simulating backends first, the null backend last.
    std::vector<PhysicsBackendKind> backends;
    for (const PhysicsBackendKind backend : {PhysicsBackendKind::BOX3D, PhysicsBackendKind::REFERENCE, PhysicsBackendKind::CUSTOM, PhysicsBackendKind::NULL_BACKEND})
    {
        if (physics_backend_availability(backend).available)
        {
            backends.push_back(backend);
        }
    }
    return backends;
}

auto
create_physics_world(const PhysicsWorldDescription& description,
                     PhysicsBackendKind backend) -> std::unique_ptr<PhysicsWorld>
{
    const PhysicsBackendAvailability availability = physics_backend_availability(backend);
    if (!availability.available)
    {
        reject_backend(availability);
    }
    switch (backend)
    {
#if defined(GFX_WITH_PHYSICS_BOX3D_BACKEND)
        case PhysicsBackendKind::BOX3D:
            return box3d_backend::create_physics_world(description);
#endif
        case PhysicsBackendKind::REFERENCE:
            return reference_backend::create_physics_world(description);
        case PhysicsBackendKind::NULL_BACKEND:
            return null_backend::create_physics_world(description);
#if defined(GFX_WITH_PHYSICS_CUSTOM_BACKEND)
        case PhysicsBackendKind::CUSTOM:
            return custom_backend::create_physics_world(description);
#endif
        default:
            reject_backend(availability);
    }
}

} // namespace gfx::physics
