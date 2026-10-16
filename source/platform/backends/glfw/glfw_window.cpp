/// @file glfw_window.cpp
/// @brief GLFW windows (without graphics context: GLFW_NO_API) and cursors.
module;

#include "glfw_headers.hpp"
#include "glfw_native.hpp"

module gfx.platform.backends.glfw;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::glfw
{

namespace
{

/// Destroys a GLFW window at the next pump.
struct DestroyWindow
{
    GLFWwindow* window = nullptr;

    void operator()() const noexcept
    {
        glfwDestroyWindow(window);
    }
};

/// GLFW's boolean of @p value.
constexpr auto
glfw_bool(bool value) noexcept -> int
{
    return value ? GLFW_TRUE : GLFW_FALSE;
}

/// An int of a size (GLFW takes ints).
auto
to_int(std::uint32_t value) noexcept -> int
{
    return static_cast<int>(std::min<std::uint32_t>(value, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

/// The operating-system handles of @p window, from the native-access bridge.
auto
native_handle_of(GLFWwindow* window) noexcept -> NativeWindowHandle
{
    unsigned long long first = 0;
    unsigned long long second = 0;
    if (win32_handles(window, first, second))
    {
        return NativeWindowHandle{Win32WindowHandle{.instance = static_cast<std::uintptr_t>(first), .window = static_cast<std::uintptr_t>(second)}};
    }
    if (x11_handles(window, first, second))
    {
        return NativeWindowHandle{XlibWindowHandle{.display = static_cast<std::uintptr_t>(first), .window = static_cast<std::uintptr_t>(second)}};
    }
    if (wayland_handles(window, first, second))
    {
        return NativeWindowHandle{WaylandWindowHandle{.display = static_cast<std::uintptr_t>(first), .surface = static_cast<std::uintptr_t>(second)}};
    }
    if (cocoa_handles(window, first))
    {
        // No Metal layer yet: GLFW does not create one (P5 attaches it on macOS).
        return NativeWindowHandle{CocoaWindowHandle{.window = static_cast<std::uintptr_t>(first), .metal_layer = 0}};
    }
    return NativeWindowHandle{};
}

} // namespace

// ================================================================================================= GlfwWindow

GlfwWindow::GlfwWindow(GlfwPlatform& platform,
                       const WindowDescription& description)
    : Window{platform.validation_level()}
    , m_platform{platform}
    , m_alive{platform.lifetime()}
    , m_mode{WindowMode::WINDOWED}
    , m_flags{description.flags}
    , m_windowed_size{description.size}
{
    // Created hidden, without graphics context, with its options as hints.
    constexpr std::string_view OPERATION = "Platform::create_window";
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, glfw_bool(has_any(description.flags, WindowFlag::RESIZABLE)));
    glfwWindowHint(GLFW_DECORATED, glfw_bool(description.mode != WindowMode::BORDERLESS));
    glfwWindowHint(GLFW_FLOATING, glfw_bool(has_any(description.flags, WindowFlag::ALWAYS_ON_TOP)));
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, glfw_bool(has_any(description.flags, WindowFlag::TRANSPARENT_FRAMEBUFFER)));
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, glfw_bool(has_any(description.flags, WindowFlag::HIGH_PIXEL_DENSITY)));
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, glfw_bool(has_any(description.flags, WindowFlag::FOCUS_ON_SHOW)));
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, glfw_bool(has_any(description.flags, WindowFlag::MOUSE_PASSTHROUGH)));
    if (description.position)
    {
        glfwWindowHint(GLFW_POSITION_X, description.position->x);
        glfwWindowHint(GLFW_POSITION_Y, description.position->y);
    }
    const std::string title{description.title};
    clear_glfw_error();
    m_window = glfwCreateWindow(std::max(1, to_int(description.size.width)), std::max(1, to_int(description.size.height)), title.c_str(),
                                nullptr, nullptr);
    if (m_window == nullptr)
    {
        check_glfw(OPERATION, "glfwCreateWindow");
        throw BackendApiError({.backend = PlatformBackendKind::GLFW, .operation = std::string{OPERATION}, .api_function = "glfwCreateWindow",
                               .result_code = 0, .result_name = "failure without an error code"});
    }

    // Registered first (GLFW may report events while the window is set up), then set up while hidden.
    m_handle = platform.register_window(*this);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetInputMode(m_window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    try
    {
        set_size_limits(description.minimum_size, description.maximum_size);
        set_aspect_ratio(description.aspect_ratio);
        if (!description.icon.empty())
        {
            set_icon(description.icon);
        }

        // Centered on the fullscreen monitor (or the primary one) unless placed.
        if (!description.position && !platform.runs_on(GLFW_PLATFORM_WAYLAND))
        {
            GLFWmonitor* const monitor = description.fullscreen.monitor.is_valid()
                                             ? platform.native_monitor(description.fullscreen.monitor, OPERATION)
                                             : glfwGetPrimaryMonitor();
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
            glfwSetWindowPos(m_window, x + ((width - to_int(description.size.width)) / 2), y + ((height - to_int(description.size.height)) / 2));
        }
        if (description.mode == WindowMode::BORDERLESS)
        {
            m_mode = WindowMode::BORDERLESS;
        }
        if (description.mode == WindowMode::BORDERLESS_FULLSCREEN || description.mode == WindowMode::EXCLUSIVE_FULLSCREEN)
        {
            set_mode(description.mode, description.fullscreen);
        }
        check_glfw(OPERATION, "glfwSetWindowPos");
    }
    catch (...)
    {
        platform.unregister_window(m_handle);
        glfwDestroyWindow(m_window);
        throw;
    }

    // Native handles and sizes, then the callbacks; shown unless created hidden.
    set_native_handle(native_handle_of(m_window));
    static_cast<void>(refresh());
    install_window_callbacks(m_window);
    if (!has_any(description.flags, WindowFlag::HIDDEN))
    {
        show_native();
    }
}

GlfwWindow::~GlfwWindow()
{
    // The handle dies now (later events are dropped); GLFW destroys the window at the next pump, never
    // inside its own event processing. Without the platform, glfwTerminate destroyed it already.
    if (m_alive.expired())
    {
        return;
    }
    m_platform.unregister_window(m_handle);
    glfwSetWindowUserPointer(m_window, nullptr);
    glfwHideWindow(m_window);
    try
    {
        m_platform.defer_release(handle_key(), DestroyWindow{m_window});
    }
    catch (...)
    {
        glfwDestroyWindow(m_window);
    }
    clear_glfw_error();
}

auto
GlfwWindow::require(std::string_view operation,
                    std::source_location location) const -> GLFWwindow*
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
    clear_glfw_error();
    return m_window;
}

auto
GlfwWindow::platform() const noexcept -> GlfwPlatform&
{
    return m_platform;
}

auto
GlfwWindow::refresh() noexcept -> WindowEvent
{
    // Window coordinates, pixels, the monitor's UI scale and the minimized state.
    int width = 0;
    int height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    int pixel_width = 0;
    int pixel_height = 0;
    glfwGetFramebufferSize(m_window, &pixel_width, &pixel_height);
    int x = 0;
    int y = 0;
    if (!m_platform.runs_on(GLFW_PLATFORM_WAYLAND))
    {
        glfwGetWindowPos(m_window, &x, &y);
    }
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    glfwGetWindowContentScale(m_window, &scale_x, &scale_y);
    const WindowEvent state{
        .size = {.width = static_cast<std::uint32_t>(std::max(width, 0)), .height = static_cast<std::uint32_t>(std::max(height, 0)), .depth = 1},
        .framebuffer_size = {.width = static_cast<std::uint32_t>(std::max(pixel_width, 0)),
                             .height = static_cast<std::uint32_t>(std::max(pixel_height, 0)),
                             .depth = 1},
        .position = {.x = x, .y = y, .z = 0},
        .content_scale = scale_x > 0.0F ? scale_x : 1.0F,
    };
    update_size(state.size, state.framebuffer_size);
    update_content_scale(state.content_scale);
    update_minimized(glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE);
    return state;
}

auto
GlfwWindow::last_cursor_position() const noexcept -> std::optional<Vector2>
{
    return m_last_cursor;
}

void
GlfwWindow::set_last_cursor_position(std::optional<Vector2> position) noexcept
{
    m_last_cursor = position;
}

auto
GlfwWindow::held_buttons() const noexcept -> MouseButtonMask
{
    return m_buttons;
}

void
GlfwWindow::set_button(MouseButton button,
                       bool down) noexcept
{
    const auto bit = static_cast<MouseButtonMask>(1U << std::to_underlying(button));
    m_buttons = down ? (m_buttons | bit) : (m_buttons & ~bit);
}

auto
GlfwWindow::wheel_steps(double x,
                        double y) noexcept -> std::pair<std::int32_t, std::int32_t>
{
    // Fractions accumulate until they complete a notch.
    m_wheel_x += x;
    m_wheel_y += y;
    const double whole_x = std::trunc(m_wheel_x);
    const double whole_y = std::trunc(m_wheel_y);
    m_wheel_x -= whole_x;
    m_wheel_y -= whole_y;
    return {static_cast<std::int32_t>(whole_x), static_cast<std::int32_t>(whole_y)};
}

auto
GlfwWindow::reports_text() const noexcept -> bool
{
    return m_text_input;
}

auto
GlfwWindow::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::GLFW;
}

auto
GlfwWindow::handle() const noexcept -> WindowHandle
{
    return m_handle;
}

auto
GlfwWindow::title() const -> std::string
{
    const char* const title = glfwGetWindowTitle(require("Window::title"));
    return title != nullptr ? std::string{title} : std::string{};
}

void
GlfwWindow::set_title(std::string_view title)
{
    constexpr std::string_view OPERATION = "Window::set_title";
    GLFWwindow* const window = require(OPERATION);
    if (validates(ValidationLevel::CREATION) && !is_valid_utf8(title))
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION}, .problem = "the title is not valid UTF-8",
                                 .suggestion = "pass UTF-8 text"});
    }
    glfwSetWindowTitle(window, std::string{title}.c_str());
    check_glfw(OPERATION, "glfwSetWindowTitle");
}

void
GlfwWindow::set_icon(std::span<const ImageData> images)
{
    // Every image decoded; GLFW picks the resolutions closest to what the system wants.
    constexpr std::string_view OPERATION = "Window::set_icon";
    GLFWwindow* const window = require(OPERATION);
    if (!m_platform.supports(PlatformFeature::WINDOW_ICON))
    {
        throw_unsupported(OPERATION, "window icons", "GLFW cannot set window icons on Wayland or macOS",
                          "check supports(PlatformFeature::WINDOW_ICON) first (on macOS the bundle's icon is used)");
    }
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
    std::vector<GLFWimage> icons;
    for (DecodedImage& image : decoded)
    {
        icons.push_back(GLFWimage{.width = to_int(image.size.width),
                                  .height = to_int(image.size.height),
                                  .pixels = reinterpret_cast<unsigned char*>(image.rgba.data())});
    }
    glfwSetWindowIcon(window, static_cast<int>(icons.size()), icons.data());
    check_glfw(OPERATION, "glfwSetWindowIcon");
}

void
GlfwWindow::set_size(Extent size)
{
    constexpr std::string_view OPERATION = "Window::set_size";
    GLFWwindow* const window = require(OPERATION);
    if (size.width == 0 || size.height == 0)
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION},
                                 .problem = std::format("the size {}x{} is empty", size.width, size.height),
                                 .suggestion = "give the window a width and a height"});
    }
    glfwSetWindowSize(window, to_int(size.width), to_int(size.height));
    check_glfw(OPERATION, "glfwSetWindowSize");
    static_cast<void>(refresh());
}

auto
GlfwWindow::position() const -> Offset
{
    constexpr std::string_view OPERATION = "Window::position";
    GLFWwindow* const window = require(OPERATION);
    if (m_platform.runs_on(GLFW_PLATFORM_WAYLAND))
    {
        throw_unsupported(OPERATION, "window positions", "Wayland does not let applications know or choose window positions",
                          "check supports(PlatformFeature::WINDOW_POSITION) first");
    }
    int x = 0;
    int y = 0;
    glfwGetWindowPos(window, &x, &y);
    check_glfw(OPERATION, "glfwGetWindowPos");
    return Offset{.x = x, .y = y, .z = 0};
}

void
GlfwWindow::set_position(Offset position)
{
    constexpr std::string_view OPERATION = "Window::set_position";
    GLFWwindow* const window = require(OPERATION);
    if (m_platform.runs_on(GLFW_PLATFORM_WAYLAND))
    {
        throw_unsupported(OPERATION, "window positions", "Wayland does not let applications know or choose window positions",
                          "check supports(PlatformFeature::WINDOW_POSITION) first");
    }
    glfwSetWindowPos(window, position.x, position.y);
    check_glfw(OPERATION, "glfwSetWindowPos");
}

void
GlfwWindow::set_size_limits(std::optional<Extent> minimum_size,
                            std::optional<Extent> maximum_size)
{
    // GLFW_DONT_CARE removes a limit.
    constexpr std::string_view OPERATION = "Window::set_size_limits";
    GLFWwindow* const window = require(OPERATION);
    glfwSetWindowSizeLimits(window, minimum_size ? to_int(minimum_size->width) : GLFW_DONT_CARE,
                            minimum_size ? to_int(minimum_size->height) : GLFW_DONT_CARE,
                            maximum_size ? to_int(maximum_size->width) : GLFW_DONT_CARE,
                            maximum_size ? to_int(maximum_size->height) : GLFW_DONT_CARE);
    check_glfw(OPERATION, "glfwSetWindowSizeLimits");
}

void
GlfwWindow::set_aspect_ratio(std::optional<AspectRatio> aspect_ratio)
{
    constexpr std::string_view OPERATION = "Window::set_aspect_ratio";
    GLFWwindow* const window = require(OPERATION);
    if (aspect_ratio && (aspect_ratio->numerator == 0 || aspect_ratio->denominator == 0))
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION},
                                 .problem = std::format("the aspect ratio {}:{} has a zero term", aspect_ratio->numerator, aspect_ratio->denominator),
                                 .suggestion = "use positive terms, e.g. 16:9, or no aspect ratio"});
    }
    glfwSetWindowAspectRatio(window, aspect_ratio ? to_int(aspect_ratio->numerator) : GLFW_DONT_CARE,
                             aspect_ratio ? to_int(aspect_ratio->denominator) : GLFW_DONT_CARE);
    check_glfw(OPERATION, "glfwSetWindowAspectRatio");
}

void
GlfwWindow::set_resizable(bool resizable)
{
    glfwSetWindowAttrib(require("Window::set_resizable"), GLFW_RESIZABLE, glfw_bool(resizable));
    check_glfw("Window::set_resizable", "glfwSetWindowAttrib");
}

auto
GlfwWindow::mode() const -> WindowMode
{
    static_cast<void>(require("Window::mode"));
    return m_mode;
}

void
GlfwWindow::set_mode(WindowMode mode,
                     const FullscreenDescription& fullscreen)
{
    constexpr std::string_view OPERATION = "Window::set_mode";
    GLFWwindow* const window = require(OPERATION);

    // Windowed modes: back from fullscreen to the remembered place, with or without decorations.
    if (mode == WindowMode::WINDOWED || mode == WindowMode::BORDERLESS)
    {
        if (glfwGetWindowMonitor(window) != nullptr)
        {
            glfwSetWindowMonitor(window, nullptr, m_windowed_position.x, m_windowed_position.y, to_int(m_windowed_size.width),
                                 to_int(m_windowed_size.height), GLFW_DONT_CARE);
        }
        glfwSetWindowAttrib(window, GLFW_DECORATED, glfw_bool(mode == WindowMode::WINDOWED));
        check_glfw(OPERATION, "glfwSetWindowMonitor");
        m_mode = mode;
        return;
    }

    // Fullscreen modes: remember the windowed place, then take the monitor at its mode (borderless) or
    // at the mode closest to the requested one (exclusive; GLFW picks it).
    if (glfwGetWindowMonitor(window) == nullptr)
    {
        if (!m_platform.runs_on(GLFW_PLATFORM_WAYLAND))
        {
            glfwGetWindowPos(window, &m_windowed_position.x, &m_windowed_position.y);
        }
        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        m_windowed_size = Extent{.width = static_cast<std::uint32_t>(std::max(width, 1)), .height = static_cast<std::uint32_t>(std::max(height, 1)), .depth = 1};
    }
    GLFWmonitor* const monitor = fullscreen.monitor.is_valid() ? m_platform.native_monitor(fullscreen.monitor, OPERATION)
                                                               : m_platform.native_monitor(this->monitor(), OPERATION);
    const GLFWvidmode* const current = glfwGetVideoMode(monitor);
    if (current == nullptr)
    {
        check_glfw(OPERATION, "glfwGetVideoMode");
        return;
    }
    int width = current->width;
    int height = current->height;
    int refresh_rate = current->refreshRate;
    if (mode == WindowMode::EXCLUSIVE_FULLSCREEN && fullscreen.video_mode)
    {
        width = to_int(fullscreen.video_mode->size.width);
        height = to_int(fullscreen.video_mode->size.height);
        refresh_rate = fullscreen.video_mode->refresh_rate > 0.0F ? static_cast<int>(std::lround(fullscreen.video_mode->refresh_rate)) : GLFW_DONT_CARE;
    }
    glfwSetWindowMonitor(window, monitor, 0, 0, width, height, refresh_rate);
    check_glfw(OPERATION, "glfwSetWindowMonitor");
    m_mode = mode;
}

void
GlfwWindow::show_native()
{
    // GLFW has no show event: it is reported at the next pump.
    glfwSetWindowAttrib(m_window, GLFW_FOCUS_ON_SHOW, glfw_bool(has_any(m_flags, WindowFlag::FOCUS_ON_SHOW)));
    glfwShowWindow(m_window);
    check_glfw("Window::show", "glfwShowWindow");
    const WindowEvent state = refresh();
    m_platform.deliver(Event::make<EventType::WINDOW_SHOWN>(m_handle, GlfwPlatform::now(), state));
}

void
GlfwWindow::show()
{
    static_cast<void>(require("Window::show"));
    show_native();
}

void
GlfwWindow::hide()
{
    // GLFW has no hide event: it is reported at the next pump.
    glfwHideWindow(require("Window::hide"));
    check_glfw("Window::hide", "glfwHideWindow");
    const WindowEvent state = refresh();
    m_platform.deliver(Event::make<EventType::WINDOW_HIDDEN>(m_handle, GlfwPlatform::now(), state));
}

void
GlfwWindow::minimize()
{
    glfwIconifyWindow(require("Window::minimize"));
    check_glfw("Window::minimize", "glfwIconifyWindow");
}

void
GlfwWindow::maximize()
{
    glfwMaximizeWindow(require("Window::maximize"));
    check_glfw("Window::maximize", "glfwMaximizeWindow");
}

void
GlfwWindow::restore()
{
    glfwRestoreWindow(require("Window::restore"));
    check_glfw("Window::restore", "glfwRestoreWindow");
}

void
GlfwWindow::focus()
{
    glfwFocusWindow(require("Window::focus"));
    check_glfw("Window::focus", "glfwFocusWindow");
}

auto
GlfwWindow::is_visible() const -> bool
{
    return glfwGetWindowAttrib(require("Window::is_visible"), GLFW_VISIBLE) == GLFW_TRUE;
}

auto
GlfwWindow::is_focused() const -> bool
{
    return glfwGetWindowAttrib(require("Window::is_focused"), GLFW_FOCUSED) == GLFW_TRUE;
}

auto
GlfwWindow::is_hovered() const -> bool
{
    return glfwGetWindowAttrib(require("Window::is_hovered"), GLFW_HOVERED) == GLFW_TRUE;
}

auto
GlfwWindow::is_maximized() const -> bool
{
    return glfwGetWindowAttrib(require("Window::is_maximized"), GLFW_MAXIMIZED) == GLFW_TRUE;
}

auto
GlfwWindow::is_occluded() const -> bool
{
    static_cast<void>(require("Window::is_occluded"));
    throw_unsupported("Window::is_occluded", "occlusion state", "GLFW 3.4 does not report whether a window is covered",
                      "check supports(PlatformFeature::WINDOW_OCCLUSION) first, or use the SDL3 backend");
}

auto
GlfwWindow::monitor() const -> MonitorHandle
{
    // The fullscreen monitor, or the monitor the window overlaps most (GLFW has no such query).
    GLFWwindow* const window = require("Window::monitor");
    if (GLFWmonitor* const fullscreen = glfwGetWindowMonitor(window))
    {
        return m_platform.monitor_handle(fullscreen);
    }
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    if (!m_platform.runs_on(GLFW_PLATFORM_WAYLAND))
    {
        glfwGetWindowPos(window, &x, &y);
    }
    glfwGetWindowSize(window, &width, &height);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long long best_area = 0;
    int count = 0;
    GLFWmonitor** const monitors = glfwGetMonitors(&count);
    for (int index = 0; monitors != nullptr && index < count; ++index)
    {
        int monitor_x = 0;
        int monitor_y = 0;
        glfwGetMonitorPos(monitors[index], &monitor_x, &monitor_y);
        const GLFWvidmode* const mode = glfwGetVideoMode(monitors[index]);
        if (mode == nullptr)
        {
            continue;
        }
        const long long overlap_width = std::max(0, std::min(x + width, monitor_x + mode->width) - std::max(x, monitor_x));
        const long long overlap_height = std::max(0, std::min(y + height, monitor_y + mode->height) - std::max(y, monitor_y));
        if (overlap_width * overlap_height > best_area)
        {
            best_area = overlap_width * overlap_height;
            best = monitors[index];
        }
    }
    return m_platform.monitor_handle(best);
}

void
GlfwWindow::set_cursor(CursorHandle cursor)
{
    // GLFW keeps a cursor per window.
    constexpr std::string_view OPERATION = "Window::set_cursor";
    GLFWwindow* const window = require(OPERATION);
    GLFWcursor* const native = m_platform.find_cursor(cursor);
    if (cursor.is_valid() && native == nullptr)
    {
        throw InvalidHandleError({.backend = backend(), .operation = std::string{OPERATION}, .handle_type = "Cursor",
                                  .problem = std::format("{} was destroyed", cursor)});
    }
    glfwSetCursor(window, native);
    check_glfw(OPERATION, "glfwSetCursor");
    m_cursor = cursor;
}

void
GlfwWindow::set_cursor_mode(CursorMode mode)
{
    // Captured: disabled cursor (hidden, locked, unbounded motion), raw when the system can; confined: captured.
    constexpr std::string_view OPERATION = "Window::set_cursor_mode";
    GLFWwindow* const window = require(OPERATION);
    int value = GLFW_CURSOR_NORMAL;
    switch (mode)
    {
        case CursorMode::NORMAL:
            value = GLFW_CURSOR_NORMAL;
            break;
        case CursorMode::HIDDEN:
            value = GLFW_CURSOR_HIDDEN;
            break;
        case CursorMode::CONFINED:
            value = GLFW_CURSOR_CAPTURED;
            break;
        case CursorMode::CAPTURED:
            value = GLFW_CURSOR_DISABLED;
            break;
    }
    glfwSetInputMode(window, GLFW_CURSOR, value);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, glfw_bool(mode == CursorMode::CAPTURED));
    }
    check_glfw(OPERATION, "glfwSetInputMode");
    m_cursor_mode = mode;
    m_last_cursor.reset();
}

auto
GlfwWindow::cursor_mode() const -> CursorMode
{
    static_cast<void>(require("Window::cursor_mode"));
    return m_cursor_mode;
}

auto
GlfwWindow::cursor_position() const -> Vector2
{
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(require("Window::cursor_position"), &x, &y);
    return Vector2{.x = static_cast<float>(x), .y = static_cast<float>(y)};
}

void
GlfwWindow::set_cursor_position(Vector2 position)
{
    glfwSetCursorPos(require("Window::set_cursor_position"), position.x, position.y);
    check_glfw("Window::set_cursor_position", "glfwSetCursorPos");
}

void
GlfwWindow::start_text_input(const TextInputDescription& description)
{
    // GLFW reports typed characters (the system's IME commits them); it cannot report compositions
    // nor place the IME's candidates (the area is not used).
    constexpr std::string_view OPERATION = "Window::start_text_input";
    static_cast<void>(require(OPERATION));
    if (description.composition_events)
    {
        throw_unsupported(OPERATION, "IME compositions", "GLFW 3.4 does not report IME compositions (the system shows them)",
                          "set TextInputDescription::composition_events to false, or use the SDL3 backend");
    }
    m_text_input = true;
}

void
GlfwWindow::stop_text_input()
{
    static_cast<void>(require("Window::stop_text_input"));
    m_text_input = false;
}

auto
GlfwWindow::is_text_input_active() const -> bool
{
    static_cast<void>(require("Window::is_text_input_active"));
    return m_text_input;
}

// ================================================================================================= GlfwCursor

GlfwCursor::GlfwCursor(GlfwPlatform& platform,
                       GLFWcursor* cursor,
                       std::optional<CursorShape> shape)
    : Cursor{platform.validation_level()}
    , m_platform{platform}
    , m_alive{platform.lifetime()}
    , m_shape{shape}
{
    m_handle = platform.register_cursor(cursor);
}

GlfwCursor::~GlfwCursor()
{
    // Without the platform, glfwTerminate destroyed the cursor already.
    if (!m_alive.expired())
    {
        m_platform.unregister_cursor(m_handle);
    }
}

auto
GlfwCursor::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::GLFW;
}

auto
GlfwCursor::handle() const noexcept -> CursorHandle
{
    return m_handle;
}

auto
GlfwCursor::shape() const noexcept -> std::optional<CursorShape>
{
    return m_shape;
}

} // namespace gfx::platform::glfw
