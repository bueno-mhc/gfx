/// @file sdl3_platform.cpp
/// @brief The SDL3 platform: SDL start-up and shutdown, features, the window, cursor and monitor
///        registries, monitors, clipboard and cursors. The event pump is in sdl3_events.cpp.
module;

#include "sdl3_headers.hpp"

module gfx.platform.backends.sdl3;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::sdl3
{

namespace
{

/// Whether an SDL3 platform exists: SDL has one event queue per process.
std::atomic<bool> g_platform_exists{false};

/// SDL's event watch: hands every event to the platform (any thread).
auto SDLCALL
watch_event(void* platform,
            SDL_Event* event) -> bool
{
    static_cast<Sdl3Platform*>(platform)->watch_native_event(*event);
    return true;
}

/// Frees memory SDL allocated, when the holder goes.
struct SdlFree
{
    void operator()(void* memory) const noexcept
    {
        SDL_free(memory);
    }
};

/// A surface over decoded RGBA pixels (the pixels must outlive it).
struct SurfaceDeleter
{
    void operator()(SDL_Surface* surface) const noexcept
    {
        SDL_DestroySurface(surface);
    }
};
using SurfacePointer = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

/// A surface of @p image (RGBA8, rows top to bottom), or a BackendApiError.
auto
surface_of(DecodedImage& image,
           std::string_view operation) -> SurfacePointer
{
    SurfacePointer surface{SDL_CreateSurfaceFrom(static_cast<int>(image.size.width), static_cast<int>(image.size.height),
                                                 SDL_PIXELFORMAT_RGBA32, image.rgba.data(),
                                                 static_cast<int>(image.size.width * 4))};
    if (!surface)
    {
        throw_sdl_error(operation, "SDL_CreateSurfaceFrom");
    }
    return surface;
}

/// Destroys an SDL cursor at the next pump.
struct DestroyCursor
{
    SDL_Cursor* cursor = nullptr;

    void operator()() const noexcept
    {
        SDL_DestroyCursor(cursor);
    }
};

/// The PNG bytes SDL hands to the clipboard, owned until SDL cleans up.
struct ClipboardImage
{
    std::vector<std::byte> png{};
};

/// SDL asks for the clipboard image in @p mime_type.
auto SDLCALL
clipboard_image_data(void* userdata,
                     const char* mime_type,
                     std::size_t* size) -> const void*
{
    const auto* image = static_cast<const ClipboardImage*>(userdata);
    if (std::string_view{mime_type} != "image/png")
    {
        *size = 0;
        return nullptr;
    }
    *size = image->png.size();
    return image->png.data();
}

/// SDL no longer needs the clipboard image.
void SDLCALL
clipboard_image_cleanup(void* userdata)
{
    delete static_cast<ClipboardImage*>(userdata);
}

/// A VideoMode of an SDL display mode (sizes in pixels).
auto
to_video_mode(const SDL_DisplayMode& mode) noexcept -> VideoMode
{
    return VideoMode{
        .size = {.width = static_cast<std::uint32_t>(std::lround(static_cast<float>(mode.w) * mode.pixel_density)),
                 .height = static_cast<std::uint32_t>(std::lround(static_cast<float>(mode.h) * mode.pixel_density)),
                 .depth = 1},
        .refresh_rate = mode.refresh_rate,
        .pixel_density = mode.pixel_density,
    };
}

/// A Rectangle of an SDL rectangle.
auto
to_rectangle(const SDL_Rect& rectangle) noexcept -> Rectangle
{
    return Rectangle{.x = rectangle.x,
                     .y = rectangle.y,
                     .width = static_cast<std::uint32_t>(std::max(rectangle.w, 0)),
                     .height = static_cast<std::uint32_t>(std::max(rectangle.h, 0))};
}

} // namespace

// ================================================================================================= Module functions

auto
unavailable_reason() -> std::string
{
    // SDL is linked into the library; whether its video subsystem starts is known only by starting it.
    return {};
}

auto
is_active() noexcept -> bool
{
    return g_platform_exists.load();
}

auto
create_platform(const PlatformDescription& description) -> std::unique_ptr<Platform>
{
    // One SDL3 platform at a time: SDL's event queue and subsystems are process-wide.
    if (g_platform_exists.exchange(true))
    {
        throw InvalidUsageError({
            .backend = PlatformBackendKind::SDL3,
            .operation = "create_platform",
            .problem = "an SDL3 platform exists already; SDL has one event queue per process",
            .suggestion = "share the existing platform, or destroy it before creating another",
        });
    }
    try
    {
        return std::make_unique<Sdl3Platform>(description);
    }
    catch (...)
    {
        g_platform_exists = false;
        throw;
    }
}

// ================================================================================================= Sdl3Platform

Sdl3Platform::Sdl3Platform(const PlatformDescription& description)
    : Platform{description}
    , m_alive{std::make_shared<Sdl3Platform* const>(this)}
{
    // The application's name, then the video and gamepad subsystems (events and joysticks come with them).
    const std::string name{application_name()};
    SDL_SetAppMetadata(name.c_str(), nullptr, nullptr);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        throw_sdl_error("create_platform", "SDL_Init");
    }
    try
    {
        // SDL timestamps count from SDL's start; events carry steady-clock times.
        const auto steady = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
        m_clock_offset = steady - std::chrono::nanoseconds{static_cast<std::int64_t>(SDL_GetTicksNS())};

        // An event type of our own wakes a waiting pump.
        m_wake_event = SDL_RegisterEvents(1);
        if (m_wake_event == 0)
        {
            throw_sdl_error("create_platform", "SDL_RegisterEvents");
        }
        const char* const driver = SDL_GetCurrentVideoDriver();
        m_wayland = driver != nullptr && std::string_view{driver} == "wayland";

        // Monitors and devices present now, then every event through the watch.
        int display_count = 0;
        const std::unique_ptr<SDL_DisplayID, SdlFree> displays{SDL_GetDisplays(&display_count)};
        for (int index = 0; displays && index < display_count; ++index)
        {
            static_cast<void>(register_display(displays.get()[index]));
        }
        m_devices = std::make_unique<Sdl3InputDevices>(*this);
        m_devices->open_connected_devices();
        if (!SDL_AddEventWatch(&watch_event, this))
        {
            throw_sdl_error("create_platform", "SDL_AddEventWatch");
        }
    }
    catch (...)
    {
        // The destructor does not run for a constructor that throws: shut SDL down here.
        if (m_devices)
        {
            m_devices->close_all();
        }
        SDL_Quit();
        throw;
    }
}

Sdl3Platform::~Sdl3Platform()
{
    // Pending releases first, then what the application still holds: its windows and cursors detach.
    release_deferred();
    SDL_RemoveEventWatch(&watch_event, this);
    m_alive.reset();
    m_cursors.for_each([](CursorHandle /*cursor*/, const CursorRecord& record)
    {
        SDL_DestroyCursor(record.cursor);
    });
    m_devices->close_all();
    SDL_Quit();
    g_platform_exists = false;
}

auto
Sdl3Platform::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::SDL3;
}

auto
Sdl3Platform::supports(PlatformFeature feature) const noexcept -> bool
{
    switch (feature)
    {
        case PlatformFeature::WINDOW_POSITION:
            // Wayland does not let clients position their windows.
            return !m_wayland;
        case PlatformFeature::GAMEPAD_SENSORS:
            // gfx has no sensor interface yet.
            return false;
        case PlatformFeature::WINDOW_ICON:
        case PlatformFeature::EXCLUSIVE_FULLSCREEN:
        case PlatformFeature::WINDOW_OCCLUSION:
        case PlatformFeature::WINDOW_HOVER:
        case PlatformFeature::HIGH_PIXEL_DENSITY:
        case PlatformFeature::CUSTOM_CURSORS:
        case PlatformFeature::CAPTURED_CURSOR:
        case PlatformFeature::RAW_MOUSE_MOTION:
        case PlatformFeature::TEXT_COMPOSITION:
        case PlatformFeature::CLIPBOARD_IMAGE:
        case PlatformFeature::DROP_TEXT:
        case PlatformFeature::TOUCH:
        case PlatformFeature::GAMEPAD_RUMBLE:
        case PlatformFeature::GAMEPAD_TRIGGER_RUMBLE:
        case PlatformFeature::GAMEPAD_LED:
        case PlatformFeature::GAMEPAD_TOUCHPAD:
            return true;
    }
    return false;
}

auto
Sdl3Platform::supports(CursorShape /*shape*/) const noexcept -> bool
{
    // SDL has every standard shape.
    return true;
}

auto
Sdl3Platform::supported_window_flags() const noexcept -> WindowFlag
{
    // SDL has no mouse pass-through window.
    return WindowFlag::RESIZABLE | WindowFlag::HIDDEN | WindowFlag::ALWAYS_ON_TOP | WindowFlag::TRANSPARENT_FRAMEBUFFER
           | WindowFlag::HIGH_PIXEL_DENSITY | WindowFlag::FOCUS_ON_SHOW | WindowFlag::UTILITY;
}

auto
Sdl3Platform::create_window(const WindowDescription& description) -> std::unique_ptr<Window>
{
    check_thread("Platform::create_window");
    if (validates(ValidationLevel::CREATION))
    {
        validate_window_description(description, supported_window_flags(), backend());
    }
    return std::make_unique<Sdl3Window>(*this, description);
}

auto
Sdl3Platform::create_cursor(CursorShape shape) -> std::unique_ptr<Cursor>
{
    check_thread("Platform::create_cursor");
    SDL_Cursor* const cursor = SDL_CreateSystemCursor(to_sdl(shape));
    if (cursor == nullptr)
    {
        throw_sdl_error("Platform::create_cursor", "SDL_CreateSystemCursor");
    }
    return std::make_unique<Sdl3Cursor>(*this, cursor, shape);
}

auto
Sdl3Platform::create_cursor(const CursorImageDescription& description) -> std::unique_ptr<Cursor>
{
    // The image, and a hot spot inside it.
    constexpr std::string_view OPERATION = "Platform::create_cursor";
    check_thread(OPERATION);
    DecodedImage image = decode_image(description.image, OPERATION);
    if (description.hot_spot.x < 0 || description.hot_spot.y < 0 || std::cmp_greater_equal(description.hot_spot.x, image.size.width)
        || std::cmp_greater_equal(description.hot_spot.y, image.size.height))
    {
        throw InvalidUsageError({
            .backend = backend(),
            .operation = std::string{OPERATION},
            .problem = std::format("the hot spot ({}, {}) is outside the {}x{} image", description.hot_spot.x, description.hot_spot.y,
                                   image.size.width, image.size.height),
            .suggestion = "give a hot spot inside the image (0, 0 is the top-left pixel)",
        });
    }
    const SurfacePointer surface = surface_of(image, OPERATION);
    SDL_Cursor* const cursor = SDL_CreateColorCursor(surface.get(), description.hot_spot.x, description.hot_spot.y);
    if (cursor == nullptr)
    {
        throw_sdl_error(OPERATION, "SDL_CreateColorCursor");
    }
    return std::make_unique<Sdl3Cursor>(*this, cursor, std::nullopt);
}

auto
Sdl3Platform::monitors() const -> std::vector<MonitorHandle>
{
    check_thread("Platform::monitors");
    std::vector<MonitorHandle> monitors;
    int count = 0;
    const std::unique_ptr<SDL_DisplayID, SdlFree> displays{SDL_GetDisplays(&count)};
    for (int index = 0; displays && index < count; ++index)
    {
        if (const MonitorHandle monitor = monitor_handle(displays.get()[index]); monitor.is_valid())
        {
            monitors.push_back(monitor);
        }
    }
    return monitors;
}

auto
Sdl3Platform::primary_monitor() const -> MonitorHandle
{
    check_thread("Platform::primary_monitor");
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display == 0)
    {
        throw_sdl_error("Platform::primary_monitor", "SDL_GetPrimaryDisplay");
    }
    return monitor_handle(display);
}

auto
Sdl3Platform::monitor_properties(MonitorHandle monitor) const -> MonitorProperties
{
    // Name, bounds, work area, scale and current mode, as SDL reports them.
    constexpr std::string_view OPERATION = "Platform::monitor_properties";
    check_thread(OPERATION);
    const SDL_DisplayID display = display_of(monitor, OPERATION);
    MonitorProperties properties{.handle = monitor};
    const char* const name = SDL_GetDisplayName(display);
    properties.name = name != nullptr ? name : "";
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(display, &bounds))
    {
        throw_sdl_error(OPERATION, "SDL_GetDisplayBounds");
    }
    properties.bounds = to_rectangle(bounds);
    SDL_Rect usable{};
    properties.work_area = SDL_GetDisplayUsableBounds(display, &usable) ? to_rectangle(usable) : properties.bounds;
    const float scale = SDL_GetDisplayContentScale(display);
    properties.content_scale = scale > 0.0F ? scale : 1.0F;
    if (const SDL_DisplayMode* const mode = SDL_GetCurrentDisplayMode(display))
    {
        properties.current_mode = to_video_mode(*mode);
    }
    properties.primary = display == SDL_GetPrimaryDisplay();
    return properties;
}

auto
Sdl3Platform::video_modes(MonitorHandle monitor) const -> std::vector<VideoMode>
{
    constexpr std::string_view OPERATION = "Platform::video_modes";
    check_thread(OPERATION);
    const SDL_DisplayID display = display_of(monitor, OPERATION);
    int count = 0;
    const std::unique_ptr<SDL_DisplayMode*, SdlFree> modes{SDL_GetFullscreenDisplayModes(display, &count)};
    if (!modes)
    {
        throw_sdl_error(OPERATION, "SDL_GetFullscreenDisplayModes");
    }
    std::vector<VideoMode> result;
    for (int index = 0; index < count; ++index)
    {
        result.push_back(to_video_mode(*modes.get()[index]));
    }
    return result;
}

auto
Sdl3Platform::clipboard_text() const -> std::string
{
    check_thread("Platform::clipboard_text");
    const std::unique_ptr<char, SdlFree> text{SDL_GetClipboardText()};
    return text ? std::string{text.get()} : std::string{};
}

void
Sdl3Platform::set_clipboard_text(std::string_view text)
{
    constexpr std::string_view OPERATION = "Platform::set_clipboard_text";
    check_thread(OPERATION);
    if (validates(ValidationLevel::CREATION) && !is_valid_utf8(text))
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION}, .problem = "the text is not valid UTF-8",
                                 .suggestion = "pass UTF-8 text"});
    }
    if (!SDL_SetClipboardText(std::string{text}.c_str()))
    {
        throw_sdl_error(OPERATION, "SDL_SetClipboardText");
    }
}

auto
Sdl3Platform::clipboard_image() const -> DecodedImage
{
    // PNG, decoded like any image given to the platform; else a BMP, which SDL reads.
    constexpr std::string_view OPERATION = "Platform::clipboard_image";
    check_thread(OPERATION);
    std::size_t size = 0;
    if (SDL_HasClipboardData("image/png"))
    {
        const std::unique_ptr<void, SdlFree> png{SDL_GetClipboardData("image/png", &size)};
        if (png && size != 0)
        {
            return decode_image(ImageData::from_png(std::span{static_cast<const std::byte*>(png.get()), size}), OPERATION);
        }
    }
    if (SDL_HasClipboardData("image/bmp"))
    {
        const std::unique_ptr<void, SdlFree> bmp{SDL_GetClipboardData("image/bmp", &size)};
        SDL_IOStream* const stream = bmp ? SDL_IOFromConstMem(bmp.get(), size) : nullptr;
        const SurfacePointer loaded{stream != nullptr ? SDL_LoadBMP_IO(stream, true) : nullptr};
        const SurfacePointer rgba{loaded ? SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32) : nullptr};
        if (!rgba)
        {
            throw_sdl_error(OPERATION, "SDL_LoadBMP_IO");
        }
        DecodedImage image{.size = {.width = static_cast<std::uint32_t>(rgba->w), .height = static_cast<std::uint32_t>(rgba->h), .depth = 1}};
        const std::size_t row = image.size.width * 4;
        image.rgba.resize(row * image.size.height);
        for (std::uint32_t y = 0; y < image.size.height; ++y)
        {
            std::memcpy(image.rgba.data() + (y * row), static_cast<const std::byte*>(rgba->pixels) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch)), row);
        }
        return image;
    }
    return {};
}

void
Sdl3Platform::set_clipboard_image(const ImageData& image)
{
    // Encode the pixels as PNG (SDL_SavePNG_IO) and offer them as image/png.
    constexpr std::string_view OPERATION = "Platform::set_clipboard_image";
    check_thread(OPERATION);
    DecodedImage pixels = decode_image(image, OPERATION);
    const SurfacePointer surface = surface_of(pixels, OPERATION);
    SDL_IOStream* const stream = SDL_IOFromDynamicMem();
    if (stream == nullptr || !SDL_SavePNG_IO(surface.get(), stream, false))
    {
        if (stream != nullptr)
        {
            SDL_CloseIO(stream);
        }
        throw_sdl_error(OPERATION, "SDL_SavePNG_IO");
    }
    auto owned = std::make_unique<ClipboardImage>();
    const auto* const data = static_cast<const std::byte*>(SDL_GetPointerProperty(SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    const auto size = static_cast<std::size_t>(SDL_TellIO(stream));
    owned->png.assign(data, data + size);
    SDL_CloseIO(stream);

    // SDL owns the PNG from here (clipboard_image_cleanup deletes it).
    const char* const mime_types[] = {"image/png"};
    ClipboardImage* const userdata = owned.release();
    if (!SDL_SetClipboardData(&clipboard_image_data, &clipboard_image_cleanup, userdata, mime_types, 1))
    {
        throw_sdl_error(OPERATION, "SDL_SetClipboardData");
    }
}

auto
Sdl3Platform::input_devices() noexcept -> InputDevices&
{
    return *m_devices;
}

// ------------------------------------------------------------------------------------------------- Registries

auto
Sdl3Platform::lifetime() const noexcept -> std::weak_ptr<Sdl3Platform* const>
{
    return m_alive;
}

void
Sdl3Platform::check_thread(std::string_view operation,
                           std::source_location location) const
{
    require_platform_thread(operation, location);
}

auto
Sdl3Platform::register_window(Sdl3Window& window,
                              SDL_WindowID identifier) -> WindowHandle
{
    const WindowHandle handle = m_windows.insert({.window = &window});
    m_window_identifiers[identifier] = handle;
    return handle;
}

void
Sdl3Platform::unregister_window(WindowHandle window,
                                SDL_WindowID identifier) noexcept
{
    static_cast<void>(m_windows.remove(window));
    m_window_identifiers.erase(identifier);
}

auto
Sdl3Platform::find_window(SDL_WindowID identifier) const noexcept -> Sdl3Window*
{
    const auto found = m_window_identifiers.find(identifier);
    if (found == m_window_identifiers.end())
    {
        return nullptr;
    }
    const WindowRecord* const record = m_windows.find(found->second);
    return record != nullptr ? record->window : nullptr;
}

auto
Sdl3Platform::is_window_alive(WindowHandle window) const noexcept -> bool
{
    return m_windows.find(window) != nullptr;
}

auto
Sdl3Platform::register_cursor(SDL_Cursor* cursor) -> CursorHandle
{
    return m_cursors.insert({.cursor = cursor});
}

void
Sdl3Platform::unregister_cursor(CursorHandle cursor) noexcept
{
    // The handle dies now; SDL forgets the cursor at the next pump (a window may show it until then).
    if (const std::optional<CursorRecord> record = m_cursors.remove(cursor))
    {
        try
        {
            defer_release(handle_key(), DestroyCursor{record->cursor});
        }
        catch (...)
        {
            SDL_DestroyCursor(record->cursor);
        }
    }
}

auto
Sdl3Platform::find_cursor(CursorHandle cursor) const noexcept -> SDL_Cursor*
{
    const CursorRecord* const record = m_cursors.find(cursor);
    return record != nullptr ? record->cursor : nullptr;
}

auto
Sdl3Platform::register_display(SDL_DisplayID display) -> MonitorHandle
{
    if (const auto found = m_displays.find(display); found != m_displays.end())
    {
        return found->second;
    }
    const MonitorHandle monitor = m_monitors.insert({.display = display});
    m_displays.emplace(display, monitor);
    return monitor;
}

auto
Sdl3Platform::monitor_handle(SDL_DisplayID display) const noexcept -> MonitorHandle
{
    const auto found = m_displays.find(display);
    return found != m_displays.end() ? found->second : MonitorHandle{};
}

auto
Sdl3Platform::display_of(MonitorHandle monitor,
                         std::string_view operation,
                         std::source_location location) const -> SDL_DisplayID
{
    return m_monitors.get(monitor, operation, location).display;
}

auto
Sdl3Platform::is_wayland() const noexcept -> bool
{
    return m_wayland;
}

} // namespace gfx::platform::sdl3
