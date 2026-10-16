/// @file backend_factory.cpp
/// @brief The platform backend factory: the only unit that imports the backend modules (each built
///        backend defines GFX_WITH_PLATFORM_<NAME>_BACKEND).
module gfx.platform.backend_factory;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.interface;
#if GFX_WITH_PLATFORM_SDL3_BACKEND
import gfx.platform.backends.sdl3;
#endif
#if GFX_WITH_PLATFORM_GLFW_BACKEND
import gfx.platform.backends.glfw;
#endif

namespace gfx::platform
{

namespace
{

/// Name of the CMake option that builds @p backend.
constexpr auto
cmake_option(PlatformBackendKind backend) noexcept -> std::string_view
{
    switch (backend)
    {
        case PlatformBackendKind::SDL3:
            return "GFX_PLATFORM_BACKEND_SDL3";
        case PlatformBackendKind::GLFW:
            return "GFX_PLATFORM_BACKEND_GLFW";
        case PlatformBackendKind::WINDOWS:
        case PlatformBackendKind::XCB:
        case PlatformBackendKind::COCOA:
        case PlatformBackendKind::WEB:
            break;
    }
    return "GFX_PLATFORM_BACKEND_<NAME>";
}

/// Why @p backend cannot run here (empty when it can), or std::nullopt when it was not built.
auto
unavailable_reason(PlatformBackendKind backend) -> std::optional<std::string>
{
    switch (backend)
    {
#if GFX_WITH_PLATFORM_SDL3_BACKEND
        case PlatformBackendKind::SDL3:
            return sdl3::unavailable_reason();
#endif
#if GFX_WITH_PLATFORM_GLFW_BACKEND
        case PlatformBackendKind::GLFW:
            return glfw::unavailable_reason();
#endif
        default:
            return std::nullopt;
    }
}

/// The backend whose platform exists now, if any: windowing backends share the thread's message
/// queue (each one would dispatch the other's window messages), so one is active at a time.
auto
active_backend() noexcept -> std::optional<PlatformBackendKind>
{
#if GFX_WITH_PLATFORM_SDL3_BACKEND
    if (sdl3::is_active())
    {
        return PlatformBackendKind::SDL3;
    }
#endif
#if GFX_WITH_PLATFORM_GLFW_BACKEND
    if (glfw::is_active())
    {
        return PlatformBackendKind::GLFW;
    }
#endif
    return std::nullopt;
}

/// Why @p backend, which was not built, is missing.
auto
missing_reason(PlatformBackendKind backend) -> std::string
{
    switch (backend)
    {
        case PlatformBackendKind::SDL3:
        case PlatformBackendKind::GLFW:
            return std::format("the library was built without the {} backend (configure it with -D{}=ON)", backend, cmake_option(backend));
        case PlatformBackendKind::WINDOWS:
        case PlatformBackendKind::XCB:
        case PlatformBackendKind::COCOA:
        case PlatformBackendKind::WEB:
            return std::format("there is no native {} backend: the supported backends are SDL3 and GLFW", backend);
    }
    return std::format("unknown platform backend {}", std::to_underlying(backend));
}

} // namespace

auto
platform_backend_availability(PlatformBackendKind backend) -> PlatformBackendAvailability
{
    // Not built, built but unable to run here, or available.
    const std::optional<std::string> reason = unavailable_reason(backend);
    if (!reason)
    {
        return PlatformBackendAvailability{.backend = backend, .built = false, .available = false, .reason = missing_reason(backend)};
    }
    return PlatformBackendAvailability{.backend = backend, .built = true, .available = reason->empty(), .reason = *reason};
}

auto
available_platform_backends() -> std::vector<PlatformBackendKind>
{
    // SDL3 first: it covers the most features (touch, IME composition, gamepad rumble and LEDs).
    std::vector<PlatformBackendKind> backends;
    for (const PlatformBackendKind backend : {PlatformBackendKind::SDL3, PlatformBackendKind::GLFW})
    {
        if (platform_backend_availability(backend).available)
        {
            backends.push_back(backend);
        }
    }
    return backends;
}

auto
create_platform(PlatformBackendKind backend,
                const PlatformDescription& description) -> std::unique_ptr<Platform>
{
    // A backend that cannot be created says why.
    const PlatformBackendAvailability availability = platform_backend_availability(backend);
    if (!availability.available)
    {
        throw BackendCapabilityError({
            .backend = backend,
            .operation = "create_platform",
            .capability = std::format("{} platform", backend),
            .reason = availability.reason,
            .suggestion = "use a backend listed by available_platform_backends()",
        });
    }
    if (const std::optional<PlatformBackendKind> active = active_backend(); active && *active != backend)
    {
        throw InvalidUsageError({
            .backend = backend,
            .operation = "create_platform",
            .problem = std::format("a {} platform exists already; platform backends share the thread's event queue, so one "
                                   "backend is active at a time",
                                   *active),
            .suggestion = std::format("destroy the {} platform first, or keep using it", *active),
        });
    }
    switch (backend)
    {
#if GFX_WITH_PLATFORM_SDL3_BACKEND
        case PlatformBackendKind::SDL3:
            return sdl3::create_platform(description);
#endif
#if GFX_WITH_PLATFORM_GLFW_BACKEND
        case PlatformBackendKind::GLFW:
            return glfw::create_platform(description);
#endif
        default:
            break;
    }
    static_cast<void>(description);
    throw BackendCapabilityError({
        .backend = backend,
        .operation = "create_platform",
        .capability = std::format("{} platform", backend),
        .reason = "the backend is available but the factory cannot create it",
        .suggestion = "report this inconsistency",
    });
}

} // namespace gfx::platform
