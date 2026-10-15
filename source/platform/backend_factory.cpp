/// @file backend_factory.cpp
/// @brief The platform backend factory: the only unit that imports the backend modules (each built
///        backend defines GFX_WITH_PLATFORM_<NAME>_BACKEND).
module gfx.platform.backend_factory;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.interface;

namespace gfx::platform
{

namespace
{

/// Why @p backend is not available, or an empty string when it is.
auto
unavailable_reason(PlatformBackendKind backend) -> std::string
{
    switch (backend)
    {
        case PlatformBackendKind::SDL3:
        case PlatformBackendKind::GLFW:
            return std::format("the {} backend is not part of this build of the library", backend);
        case PlatformBackendKind::WINDOWS:
        case PlatformBackendKind::XCB:
        case PlatformBackendKind::COCOA:
        case PlatformBackendKind::WEB:
            return std::format("there is no native {} backend: the supported backends are SDL3 and GLFW", backend);
    }
    return std::format("unknown platform backend {}", std::to_underlying(backend));
}

/// Whether @p backend is compiled into the library.
constexpr auto
is_built(PlatformBackendKind /*backend*/) noexcept -> bool
{
    return false;
}

} // namespace

auto
platform_backend_availability(PlatformBackendKind backend) -> PlatformBackendAvailability
{
    std::string reason = unavailable_reason(backend);
    return PlatformBackendAvailability{.backend = backend,
                                       .built = is_built(backend),
                                       .available = reason.empty(),
                                       .reason = std::move(reason)};
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
                const PlatformDescription& /*description*/) -> std::unique_ptr<Platform>
{
    // Every backend that cannot be created says why.
    const PlatformBackendAvailability availability = platform_backend_availability(backend);
    throw BackendCapabilityError({
        .backend = backend,
        .operation = "create_platform",
        .capability = std::format("{} platform", backend),
        .reason = availability.reason,
        .suggestion = "use a backend listed by available_platform_backends()",
    });
}

} // namespace gfx::platform
