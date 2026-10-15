/// @file backend_factory.cpp
/// @brief Implementation of the backend factory: the only unit that imports the backend modules.
module gfx.backend_factory;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;
#if GFX_WITH_VULKAN_BACKEND
import gfx.backends.vulkan;
#endif
#if GFX_WITH_NULL_BACKEND
import gfx.backends.null;
#endif
#if GFX_WITH_DIRECT3D12_BACKEND
import gfx.backends.direct3d12;
#endif
#if GFX_WITH_METAL_BACKEND
import gfx.backends.metal;
#endif

namespace gfx
{

namespace
{

/// @brief Name of the CMake option that builds @p backend.
constexpr auto
cmake_option(BackendKind backend) noexcept -> std::string_view
{
    switch (backend)
    {
        case BackendKind::NULL_BACKEND: return "GFX_BACKEND_NULL";
        case BackendKind::VULKAN: return "GFX_BACKEND_VULKAN";
        case BackendKind::DIRECT3D12: return "GFX_BACKEND_DIRECT3D12";
        case BackendKind::METAL: return "GFX_BACKEND_METAL";
    }
    return "GFX_BACKEND_<NAME>";
}

/// @brief Why @p backend cannot run here (empty when it can), or std::nullopt when it was not built.
auto
unavailable_reason(BackendKind backend) -> std::optional<std::string>
{
    switch (backend)
    {
#if GFX_WITH_NULL_BACKEND
        case BackendKind::NULL_BACKEND: return null::unavailable_reason();
#endif
#if GFX_WITH_VULKAN_BACKEND
        case BackendKind::VULKAN: return vulkan::unavailable_reason();
#endif
#if GFX_WITH_DIRECT3D12_BACKEND
        case BackendKind::DIRECT3D12: return direct3d12::unavailable_reason();
#endif
#if GFX_WITH_METAL_BACKEND
        case BackendKind::METAL: return metal::unavailable_reason();
#endif
    default: return std::nullopt;
    }
}

} // namespace

auto
backend_availability(BackendKind backend) -> BackendAvailability
{
    // Not built: the CMake option that builds it.
    const std::optional<std::string> reason = unavailable_reason(backend);
    if (!reason)
    {
        return {
            .backend = backend,
            .built = false,
            .available = false,
            .reason = std::format("the library was built without the {} backend (configure it with -D{}=ON)", backend,
                                  cmake_option(backend)),
        };
    }

    // Built: available unless the backend says why not.
    return {.backend = backend, .built = true, .available = reason->empty(), .reason = *reason};
}

auto
available_backends() -> std::vector<BackendKind>
{
    // Native APIs first, the null backend last.
    constexpr std::array<BackendKind, 4> preference{BackendKind::VULKAN, BackendKind::DIRECT3D12, BackendKind::METAL,
                                                    BackendKind::NULL_BACKEND};
    std::vector<BackendKind> backends;
    for (const BackendKind backend : preference)
    {
        if (backend_availability(backend).available)
        {
            backends.push_back(backend);
        }
    }
    return backends;
}

auto
create_instance(BackendKind backend,
                const InstanceDescription& description) -> std::unique_ptr<Instance>
{
    // Unavailable backends: say why, and what to do.
    const BackendAvailability availability = backend_availability(backend);
    if (!availability.available)
    {
        throw BackendCapabilityError({
            .backend = backend,
            .operation = "create_instance",
            .capability = std::format("{} backend", backend),
            .reason = availability.reason,
            .suggestion = availability.built ? "use another backend (gfx::available_backends() lists those that can run here)"
                                             : std::format("rebuild gfx with -D{}=ON, or use another backend", cmake_option(backend)),
        });
    }

    // Available backends create their instance.
    switch (backend)
    {
#if GFX_WITH_NULL_BACKEND
        case BackendKind::NULL_BACKEND: return null::create_instance(description);
#endif
#if GFX_WITH_VULKAN_BACKEND
        case BackendKind::VULKAN: return vulkan::create_instance(description);
#endif
        default: 
            break;
    }
    throw BackendCapabilityError({
        .backend = backend,
        .operation = "create_instance",
        .capability = std::format("{} backend", backend),
        .reason = "the backend reports itself available but the factory cannot create it",
        .suggestion = "report this as a defect of gfx",
    });
}

} // namespace gfx
