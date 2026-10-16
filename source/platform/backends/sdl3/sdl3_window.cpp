/// @file sdl3_window.cpp
/// @brief SDL3 windows and cursors.
module;

#include "sdl3_headers.hpp"

module gfx.platform.backends.sdl3;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::sdl3
{

namespace
{

/// Destroys an SDL window at the next pump.
struct DestroyWindow
{
    SDL_Window* window = nullptr;

    void operator()() const noexcept
    {
        SDL_DestroyWindow(window);
    }
};

/// Destroys SDL properties when the holder goes.
struct PropertiesHolder
{
    SDL_PropertiesID properties = 0;

    ~PropertiesHolder()
    {
        SDL_DestroyProperties(properties);
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

/// Sets an SDL hint for the lifetime of the holder, then restores it.
class ScopedHint
{
public:
    ScopedHint(const char* name,
               const char* value)
        : m_name{name}
    {
        const char* const previous = SDL_GetHint(name);
        m_previous = previous != nullptr ? std::optional<std::string>{previous} : std::nullopt;
        SDL_SetHint(name, value);
    }

    ~ScopedHint()
    {
        SDL_SetHint(m_name, m_previous ? m_previous->c_str() : nullptr);
    }

    ScopedHint(const ScopedHint&) = delete;
    auto operator=(const ScopedHint&) -> ScopedHint& = delete;

private:
    const char* m_name = nullptr;
    std::optional<std::string> m_previous{};
};

/// The SDL creation flags of @p description (the window is created hidden and shown once set up).
auto
sdl_flags(const WindowDescription& description) noexcept -> SDL_WindowFlags
{
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
    constexpr std::array<std::pair<WindowFlag, SDL_WindowFlags>, 5> FLAGS{{
        {WindowFlag::RESIZABLE, SDL_WINDOW_RESIZABLE},
        {WindowFlag::ALWAYS_ON_TOP, SDL_WINDOW_ALWAYS_ON_TOP},
        {WindowFlag::TRANSPARENT_FRAMEBUFFER, SDL_WINDOW_TRANSPARENT},
        {WindowFlag::HIGH_PIXEL_DENSITY, SDL_WINDOW_HIGH_PIXEL_DENSITY},
        {WindowFlag::UTILITY, SDL_WINDOW_UTILITY},
    }};
    for (const auto& [flag, sdl_flag] : FLAGS)
    {
        if (has_any(description.flags, flag))
        {
            flags |= sdl_flag;
        }
    }
    if (description.mode == WindowMode::BORDERLESS)
    {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (description.surface_api == SurfaceApi::VULKAN)
    {
        flags |= SDL_WINDOW_VULKAN;
    }
    if (description.surface_api == SurfaceApi::METAL)
    {
        flags |= SDL_WINDOW_METAL;
    }
    return flags;
}

/// The operating-system handles of @p window.
auto
native_handle_of(SDL_Window* window) noexcept -> NativeWindowHandle
{
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    const auto pointer = [properties](const char* name)
    {
        return reinterpret_cast<std::uintptr_t>(SDL_GetPointerProperty(properties, name, nullptr));
    };
    if (const std::uintptr_t hwnd = pointer(SDL_PROP_WINDOW_WIN32_HWND_POINTER); hwnd != 0)
    {
        return NativeWindowHandle{Win32WindowHandle{.instance = pointer(SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER), .window = hwnd}};
    }
    if (const std::uintptr_t surface = pointer(SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER); surface != 0)
    {
        return NativeWindowHandle{WaylandWindowHandle{.display = pointer(SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER), .surface = surface}};
    }
    if (const std::uintptr_t display = pointer(SDL_PROP_WINDOW_X11_DISPLAY_POINTER); display != 0)
    {
        const auto x11_window = static_cast<std::uintptr_t>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        return NativeWindowHandle{XlibWindowHandle{.display = display, .window = x11_window}};
    }
#if defined(__APPLE__)
    if (const std::uintptr_t cocoa = pointer(SDL_PROP_WINDOW_COCOA_WINDOW_POINTER); cocoa != 0)
    {
        SDL_MetalView view = SDL_Metal_CreateView(window);
        return NativeWindowHandle{CocoaWindowHandle{.window = cocoa,
                                                    .metal_layer = reinterpret_cast<std::uintptr_t>(SDL_Metal_GetLayer(view))}};
    }
#endif
    return NativeWindowHandle{};
}

/// SDL size of @p extent.
auto
to_int(std::uint32_t value) noexcept -> int
{
    return static_cast<int>(std::min<std::uint32_t>(value, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

} // namespace

// ================================================================================================= Sdl3Window

Sdl3Window::Sdl3Window(Sdl3Platform& platform,
                       const WindowDescription& description)
    : Window{platform.validation_level()}
    , m_platform{platform}
    , m_alive{platform.lifetime()}
    , m_mode{description.mode}
    , m_flags{description.flags}
{
    // Created hidden at its place: centered on the fullscreen monitor (or the primary one) by default.
    constexpr std::string_view OPERATION = "Platform::create_window";
    const SDL_DisplayID display = description.fullscreen.monitor.is_valid()
                                      ? platform.display_of(description.fullscreen.monitor, OPERATION)
                                      : SDL_GetPrimaryDisplay();
    const std::string title{description.title};
    const PropertiesHolder properties{SDL_CreateProperties()};
    SDL_SetStringProperty(properties.properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
    SDL_SetNumberProperty(properties.properties, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                          description.position ? description.position->x : static_cast<Sint64>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
    SDL_SetNumberProperty(properties.properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                          description.position ? description.position->y : static_cast<Sint64>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
    SDL_SetNumberProperty(properties.properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, std::max(1, to_int(description.size.width)));
    SDL_SetNumberProperty(properties.properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, std::max(1, to_int(description.size.height)));
    SDL_SetNumberProperty(properties.properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(sdl_flags(description)));
    m_window = SDL_CreateWindowWithProperties(properties.properties);
    if (m_window == nullptr)
    {
        throw_sdl_error(OPERATION, "SDL_CreateWindowWithProperties");
    }

    // Set up while hidden; a failure destroys the window at once (nothing references it yet).
    try
    {
        set_size_limits(description.minimum_size, description.maximum_size);
        set_aspect_ratio(description.aspect_ratio);
        if (!description.icon.empty())
        {
            set_icon(description.icon);
        }
        if (description.mode == WindowMode::BORDERLESS_FULLSCREEN || description.mode == WindowMode::EXCLUSIVE_FULLSCREEN)
        {
            set_mode(description.mode, description.fullscreen);
        }
    }
    catch (...)
    {
        SDL_DestroyWindow(m_window);
        throw;
    }

    // Registered: its events are delivered from now on.
    m_handle = platform.register_window(*this, SDL_GetWindowID(m_window));
    set_native_handle(native_handle_of(m_window));
    const WindowEvent initial = refresh();
    m_reported_size = initial.size;
    m_reported_framebuffer_size = initial.framebuffer_size;
    if (!has_any(description.flags, WindowFlag::HIDDEN))
    {
        show_native();
    }
}

Sdl3Window::~Sdl3Window()
{
    // The handle dies now (later events are dropped); SDL destroys the window at the next pump, never
    // inside its own event processing. Without the platform, SDL is shut down and the window gone.
    if (m_alive.expired())
    {
        return;
    }
    m_platform.unregister_window(m_handle, SDL_GetWindowID(m_window));
    SDL_HideWindow(m_window);
    try
    {
        m_platform.defer_release(handle_key(), DestroyWindow{m_window});
    }
    catch (...)
    {
        SDL_DestroyWindow(m_window);
    }
}

auto
Sdl3Window::require(std::string_view operation,
                    std::source_location location) const -> SDL_Window*
{
    if (m_alive.expired())
    {
        throw InvalidUsageError(
            {
                .backend = backend(),
                .operation = std::string{operation},
                .problem = "the window's platform was destroyed",
                .suggestion = "destroy windows before their platform",
            },
            location);
    }
    m_platform.check_thread(operation, location);
    return m_window;
}

auto
Sdl3Window::native() const noexcept -> SDL_Window*
{
    return m_window;
}

auto
Sdl3Window::report_size(Extent size) noexcept -> bool
{
    return std::exchange(m_reported_size, size) != size;
}

auto
Sdl3Window::report_framebuffer_size(Extent size) noexcept -> bool
{
    return std::exchange(m_reported_framebuffer_size, size) != size;
}

auto
Sdl3Window::refresh() noexcept -> WindowEvent
{
    // Window coordinates, pixels, the monitor's UI scale and the minimized state.
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_window, &width, &height);
    int pixel_width = 0;
    int pixel_height = 0;
    SDL_GetWindowSizeInPixels(m_window, &pixel_width, &pixel_height);
    int x = 0;
    int y = 0;
    SDL_GetWindowPosition(m_window, &x, &y);
    const float display_scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(m_window));
    const WindowEvent state{
        .size = {.width = static_cast<std::uint32_t>(std::max(width, 0)), .height = static_cast<std::uint32_t>(std::max(height, 0)), .depth = 1},
        .framebuffer_size = {.width = static_cast<std::uint32_t>(std::max(pixel_width, 0)),
                             .height = static_cast<std::uint32_t>(std::max(pixel_height, 0)),
                             .depth = 1},
        .position = {.x = x, .y = y, .z = 0},
        .content_scale = display_scale > 0.0F ? display_scale : 1.0F,
    };
    update_size(state.size, state.framebuffer_size);
    update_content_scale(state.content_scale);
    update_minimized((SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0);
    return state;
}

auto
Sdl3Window::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::SDL3;
}

auto
Sdl3Window::handle() const noexcept -> WindowHandle
{
    return m_handle;
}

auto
Sdl3Window::title() const -> std::string
{
    const char* const title = SDL_GetWindowTitle(require("Window::title"));
    return title != nullptr ? std::string{title} : std::string{};
}

void
Sdl3Window::set_title(std::string_view title)
{
    constexpr std::string_view OPERATION = "Window::set_title";
    SDL_Window* const window = require(OPERATION);
    if (validates(ValidationLevel::CREATION) && !is_valid_utf8(title))
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION}, .problem = "the title is not valid UTF-8",
                                 .suggestion = "pass UTF-8 text"});
    }
    if (!SDL_SetWindowTitle(window, std::string{title}.c_str()))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowTitle");
    }
}

void
Sdl3Window::set_icon(std::span<const ImageData> images)
{
    // Every image decoded; the first is the icon, the others alternate resolutions SDL picks from.
    constexpr std::string_view OPERATION = "Window::set_icon";
    SDL_Window* const window = require(OPERATION);
    if (images.empty())
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION}, .problem = "no image was given",
                                 .suggestion = "pass at least one image"});
    }
    std::vector<DecodedImage> decoded;
    for (const ImageData& image : images)
    {
        decoded.push_back(decode_image(image, OPERATION));
    }
    std::vector<std::unique_ptr<SDL_Surface, SurfaceDeleter>> surfaces;
    for (DecodedImage& image : decoded)
    {
        surfaces.emplace_back(SDL_CreateSurfaceFrom(to_int(image.size.width), to_int(image.size.height), SDL_PIXELFORMAT_RGBA32,
                                                    image.rgba.data(), to_int(image.size.width * 4)));
        if (!surfaces.back())
        {
            throw_sdl_error(OPERATION, "SDL_CreateSurfaceFrom");
        }
    }
    for (std::size_t index = 1; index < surfaces.size(); ++index)
    {
        if (!SDL_AddSurfaceAlternateImage(surfaces.front().get(), surfaces[index].get()))
        {
            throw_sdl_error(OPERATION, "SDL_AddSurfaceAlternateImage");
        }
    }
    if (!SDL_SetWindowIcon(window, surfaces.front().get()))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowIcon");
    }
}

void
Sdl3Window::set_size(Extent size)
{
    constexpr std::string_view OPERATION = "Window::set_size";
    SDL_Window* const window = require(OPERATION);
    if (size.width == 0 || size.height == 0)
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION},
                                 .problem = std::format("the size {}x{} is empty", size.width, size.height),
                                 .suggestion = "give the window a width and a height"});
    }
    if (!SDL_SetWindowSize(window, to_int(size.width), to_int(size.height)))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowSize");
    }
    static_cast<void>(refresh());
}

auto
Sdl3Window::position() const -> Offset
{
    constexpr std::string_view OPERATION = "Window::position";
    SDL_Window* const window = require(OPERATION);
    if (m_platform.is_wayland())
    {
        throw_unsupported(OPERATION, "window positions", "Wayland does not let applications know or choose window positions",
                          "check supports(PlatformFeature::WINDOW_POSITION) first");
    }
    int x = 0;
    int y = 0;
    if (!SDL_GetWindowPosition(window, &x, &y))
    {
        throw_sdl_error(OPERATION, "SDL_GetWindowPosition");
    }
    return Offset{.x = x, .y = y, .z = 0};
}

void
Sdl3Window::set_position(Offset position)
{
    constexpr std::string_view OPERATION = "Window::set_position";
    SDL_Window* const window = require(OPERATION);
    if (m_platform.is_wayland())
    {
        throw_unsupported(OPERATION, "window positions", "Wayland does not let applications know or choose window positions",
                          "check supports(PlatformFeature::WINDOW_POSITION) first");
    }
    if (!SDL_SetWindowPosition(window, position.x, position.y))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowPosition");
    }
}

void
Sdl3Window::set_size_limits(std::optional<Extent> minimum_size,
                            std::optional<Extent> maximum_size)
{
    // 0 x 0 removes a limit.
    constexpr std::string_view OPERATION = "Window::set_size_limits";
    SDL_Window* const window = require(OPERATION);
    const Extent minimum = minimum_size.value_or(Extent{.width = 0, .height = 0, .depth = 1});
    const Extent maximum = maximum_size.value_or(Extent{.width = 0, .height = 0, .depth = 1});
    if (!SDL_SetWindowMinimumSize(window, to_int(minimum.width), to_int(minimum.height)))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowMinimumSize");
    }
    if (!SDL_SetWindowMaximumSize(window, to_int(maximum.width), to_int(maximum.height)))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowMaximumSize");
    }
}

void
Sdl3Window::set_aspect_ratio(std::optional<AspectRatio> aspect_ratio)
{
    // The same minimum and maximum ratio locks it; 0 frees it.
    constexpr std::string_view OPERATION = "Window::set_aspect_ratio";
    SDL_Window* const window = require(OPERATION);
    if (aspect_ratio && (aspect_ratio->numerator == 0 || aspect_ratio->denominator == 0))
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION},
                                 .problem = std::format("the aspect ratio {}:{} has a zero term", aspect_ratio->numerator, aspect_ratio->denominator),
                                 .suggestion = "use positive terms, e.g. 16:9, or no aspect ratio"});
    }
    const float ratio = aspect_ratio ? static_cast<float>(aspect_ratio->numerator) / static_cast<float>(aspect_ratio->denominator) : 0.0F;
    if (!SDL_SetWindowAspectRatio(window, ratio, ratio))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowAspectRatio");
    }
}

void
Sdl3Window::set_resizable(bool resizable)
{
    if (!SDL_SetWindowResizable(require("Window::set_resizable"), resizable))
    {
        throw_sdl_error("Window::set_resizable", "SDL_SetWindowResizable");
    }
}

auto
Sdl3Window::mode() const -> WindowMode
{
    static_cast<void>(require("Window::mode"));
    return m_mode;
}

void
Sdl3Window::set_mode(WindowMode mode,
                     const FullscreenDescription& fullscreen)
{
    constexpr std::string_view OPERATION = "Window::set_mode";
    SDL_Window* const window = require(OPERATION);

    // Windowed modes: decorations or not, no fullscreen.
    if (mode == WindowMode::WINDOWED || mode == WindowMode::BORDERLESS)
    {
        if (!SDL_SetWindowFullscreen(window, false) || !SDL_SetWindowBordered(window, mode == WindowMode::WINDOWED))
        {
            throw_sdl_error(OPERATION, "SDL_SetWindowFullscreen");
        }
        m_mode = mode;
        return;
    }

    // Fullscreen modes: on the requested monitor, at the desktop's mode or at the closest video mode.
    const SDL_DisplayID display = fullscreen.monitor.is_valid() ? m_platform.display_of(fullscreen.monitor, OPERATION)
                                                                : SDL_GetDisplayForWindow(window);
    if (fullscreen.monitor.is_valid() && !SDL_SetWindowPosition(window, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
                                                                static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display))))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowPosition");
    }
    if (mode == WindowMode::BORDERLESS_FULLSCREEN)
    {
        if (!SDL_SetWindowFullscreenMode(window, nullptr))
        {
            throw_sdl_error(OPERATION, "SDL_SetWindowFullscreenMode");
        }
    }
    else
    {
        VideoMode requested{};
        if (fullscreen.video_mode)
        {
            requested = *fullscreen.video_mode;
        }
        else if (const SDL_DisplayMode* const desktop = SDL_GetDesktopDisplayMode(display))
        {
            requested = VideoMode{.size = {.width = static_cast<std::uint32_t>(desktop->w), .height = static_cast<std::uint32_t>(desktop->h), .depth = 1},
                                  .refresh_rate = desktop->refresh_rate,
                                  .pixel_density = desktop->pixel_density};
        }
        SDL_DisplayMode closest{};
        if (!SDL_GetClosestFullscreenDisplayMode(display, to_int(requested.size.width), to_int(requested.size.height), requested.refresh_rate,
                                                 requested.pixel_density > 1.0F, &closest))
        {
            throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION},
                                     .problem = std::format("the monitor has no video mode close to {}x{} at {} Hz ({})", requested.size.width,
                                                            requested.size.height, requested.refresh_rate, SDL_GetError()),
                                     .suggestion = "pick one of Platform::video_modes(monitor)"});
        }
        if (!SDL_SetWindowFullscreenMode(window, &closest))
        {
            throw_sdl_error(OPERATION, "SDL_SetWindowFullscreenMode");
        }
    }
    if (!SDL_SetWindowFullscreen(window, true))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowFullscreen");
    }
    m_mode = mode;
}

void
Sdl3Window::show_native() const
{
    // SDL activates shown windows unless told otherwise.
    const ScopedHint activate{SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, has_any(m_flags, WindowFlag::FOCUS_ON_SHOW) ? "1" : "0"};
    if (!SDL_ShowWindow(m_window))
    {
        throw_sdl_error("Window::show", "SDL_ShowWindow");
    }
}

void
Sdl3Window::show()
{
    static_cast<void>(require("Window::show"));
    show_native();
}

void
Sdl3Window::hide()
{
    if (!SDL_HideWindow(require("Window::hide")))
    {
        throw_sdl_error("Window::hide", "SDL_HideWindow");
    }
}

void
Sdl3Window::minimize()
{
    if (!SDL_MinimizeWindow(require("Window::minimize")))
    {
        throw_sdl_error("Window::minimize", "SDL_MinimizeWindow");
    }
}

void
Sdl3Window::maximize()
{
    if (!SDL_MaximizeWindow(require("Window::maximize")))
    {
        throw_sdl_error("Window::maximize", "SDL_MaximizeWindow");
    }
}

void
Sdl3Window::restore()
{
    if (!SDL_RestoreWindow(require("Window::restore")))
    {
        throw_sdl_error("Window::restore", "SDL_RestoreWindow");
    }
}

void
Sdl3Window::focus()
{
    if (!SDL_RaiseWindow(require("Window::focus")))
    {
        throw_sdl_error("Window::focus", "SDL_RaiseWindow");
    }
}

auto
Sdl3Window::is_visible() const -> bool
{
    return (SDL_GetWindowFlags(require("Window::is_visible")) & SDL_WINDOW_HIDDEN) == 0;
}

auto
Sdl3Window::is_focused() const -> bool
{
    return (SDL_GetWindowFlags(require("Window::is_focused")) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

auto
Sdl3Window::is_hovered() const -> bool
{
    return (SDL_GetWindowFlags(require("Window::is_hovered")) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

auto
Sdl3Window::is_maximized() const -> bool
{
    return (SDL_GetWindowFlags(require("Window::is_maximized")) & SDL_WINDOW_MAXIMIZED) != 0;
}

auto
Sdl3Window::is_occluded() const -> bool
{
    return (SDL_GetWindowFlags(require("Window::is_occluded")) & SDL_WINDOW_OCCLUDED) != 0;
}

auto
Sdl3Window::monitor() const -> MonitorHandle
{
    const SDL_DisplayID display = SDL_GetDisplayForWindow(require("Window::monitor"));
    if (display == 0)
    {
        throw_sdl_error("Window::monitor", "SDL_GetDisplayForWindow");
    }
    return m_platform.monitor_handle(display);
}

void
Sdl3Window::set_cursor(CursorHandle cursor)
{
    // SDL has one cursor: the window's is shown while the mouse is over it.
    constexpr std::string_view OPERATION = "Window::set_cursor";
    SDL_Window* const window = require(OPERATION);
    if (cursor.is_valid() && m_platform.find_cursor(cursor) == nullptr)
    {
        throw InvalidHandleError({.backend = backend(), .operation = std::string{OPERATION}, .handle_type = "Cursor",
                                  .problem = std::format("{} was destroyed", cursor)});
    }
    m_cursor = cursor;
    if (SDL_GetMouseFocus() == window)
    {
        mouse_entered();
    }
}

void
Sdl3Window::mouse_entered() const noexcept
{
    SDL_Cursor* const cursor = m_platform.find_cursor(m_cursor);
    SDL_SetCursor(cursor != nullptr ? cursor : SDL_GetDefaultCursor());
    if (m_cursor_mode == CursorMode::HIDDEN)
    {
        SDL_HideCursor();
    }
}

void
Sdl3Window::mouse_left() const noexcept
{
    if (m_cursor_mode == CursorMode::HIDDEN)
    {
        SDL_ShowCursor();
    }
}

void
Sdl3Window::set_cursor_mode(CursorMode mode)
{
    // Captured: relative mode (hidden, locked, raw motion); confined: grabbed; hidden: hidden over the window.
    constexpr std::string_view OPERATION = "Window::set_cursor_mode";
    SDL_Window* const window = require(OPERATION);
    if (!SDL_SetWindowRelativeMouseMode(window, mode == CursorMode::CAPTURED))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowRelativeMouseMode");
    }
    if (!SDL_SetWindowMouseGrab(window, mode == CursorMode::CONFINED))
    {
        throw_sdl_error(OPERATION, "SDL_SetWindowMouseGrab");
    }
    m_cursor_mode = mode;
    if (SDL_GetMouseFocus() == window)
    {
        if (mode == CursorMode::HIDDEN)
        {
            SDL_HideCursor();
        }
        else if (mode != CursorMode::CAPTURED)
        {
            SDL_ShowCursor();
        }
    }
}

auto
Sdl3Window::cursor_mode() const -> CursorMode
{
    static_cast<void>(require("Window::cursor_mode"));
    return m_cursor_mode;
}

auto
Sdl3Window::cursor_position() const -> Vector2
{
    // The desktop position, relative to the window's.
    SDL_Window* const window = require("Window::cursor_position");
    float x = 0.0F;
    float y = 0.0F;
    static_cast<void>(SDL_GetGlobalMouseState(&x, &y));
    int left = 0;
    int top = 0;
    SDL_GetWindowPosition(window, &left, &top);
    return Vector2{.x = x - static_cast<float>(left), .y = y - static_cast<float>(top)};
}

void
Sdl3Window::set_cursor_position(Vector2 position)
{
    SDL_WarpMouseInWindow(require("Window::set_cursor_position"), position.x, position.y);
}

void
Sdl3Window::start_text_input(const TextInputDescription& description)
{
    // Where the IME shows its candidates, and whether the application draws compositions itself.
    constexpr std::string_view OPERATION = "Window::start_text_input";
    SDL_Window* const window = require(OPERATION);
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, description.composition_events ? "composition" : "none");
    const SDL_Rect area{.x = description.area.x, .y = description.area.y, .w = to_int(description.area.width), .h = to_int(description.area.height)};
    if (!SDL_SetTextInputArea(window, &area, description.cursor))
    {
        throw_sdl_error(OPERATION, "SDL_SetTextInputArea");
    }
    if (!SDL_StartTextInput(window))
    {
        throw_sdl_error(OPERATION, "SDL_StartTextInput");
    }
}

void
Sdl3Window::stop_text_input()
{
    if (!SDL_StopTextInput(require("Window::stop_text_input")))
    {
        throw_sdl_error("Window::stop_text_input", "SDL_StopTextInput");
    }
}

auto
Sdl3Window::is_text_input_active() const -> bool
{
    return SDL_TextInputActive(require("Window::is_text_input_active"));
}

// ================================================================================================= Sdl3Cursor

Sdl3Cursor::Sdl3Cursor(Sdl3Platform& platform,
                       SDL_Cursor* cursor,
                       std::optional<CursorShape> shape)
    : Cursor{platform.validation_level()}
    , m_platform{platform}
    , m_alive{platform.lifetime()}
    , m_shape{shape}
{
    m_handle = platform.register_cursor(cursor);
}

Sdl3Cursor::~Sdl3Cursor()
{
    // Without the platform, SDL has destroyed the cursor already.
    if (!m_alive.expired())
    {
        m_platform.unregister_cursor(m_handle);
    }
}

auto
Sdl3Cursor::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::SDL3;
}

auto
Sdl3Cursor::handle() const noexcept -> CursorHandle
{
    return m_handle;
}

auto
Sdl3Cursor::shape() const noexcept -> std::optional<CursorShape>
{
    return m_shape;
}

} // namespace gfx::platform::sdl3
