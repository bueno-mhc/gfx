/// @file glfw_platform.cpp
/// @brief The GLFW platform: start-up and shutdown, features, the window, cursor and monitor
///        registries, monitors, clipboard and cursors. Callbacks and the pump are in glfw_events.cpp.
module;

#include "glfw_headers.hpp"

module gfx.platform.backends.glfw;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::glfw
{

namespace
{

/// Whether a GLFW platform exists: GLFW is initialized once per process.
std::atomic<bool> g_platform_exists{false};

/// Destroys a GLFW cursor at the next pump.
struct DestroyCursor
{
    GLFWcursor* cursor = nullptr;

    void operator()() const noexcept
    {
        glfwDestroyCursor(cursor);
    }
};

/// A VideoMode of a GLFW video mode (GLFW modes are in pixels).
auto
to_video_mode(const GLFWvidmode& mode) noexcept -> VideoMode
{
    return VideoMode{
        .size = {.width = static_cast<std::uint32_t>(std::max(mode.width, 0)), .height = static_cast<std::uint32_t>(std::max(mode.height, 0)), .depth = 1},
        .refresh_rate = static_cast<float>(mode.refreshRate),
        .pixel_density = 1.0F,
    };
}

} // namespace

// ================================================================================================= Module functions

auto
unavailable_reason() -> std::string
{
    // GLFW is linked into the library; whether it can start is known only by starting it.
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
    // One GLFW platform at a time: GLFW's state is process-wide.
    if (g_platform_exists.exchange(true))
    {
        throw InvalidUsageError({
            .backend = PlatformBackendKind::GLFW,
            .operation = "create_platform",
            .problem = "a GLFW platform exists already; GLFW is initialized once per process",
            .suggestion = "share the existing platform, or destroy it before creating another",
        });
    }
    try
    {
        return std::make_unique<GlfwPlatform>(description);
    }
    catch (...)
    {
        g_platform_exists = false;
        throw;
    }
}

// ================================================================================================= GlfwPlatform

GlfwPlatform::GlfwPlatform(const PlatformDescription& description)
    : Platform{description}
    , m_alive{std::make_shared<GlfwPlatform* const>(this)}
{
    // Hats are reported as hats only (not also as buttons).
    clear_glfw_error();
    glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
    if (glfwInit() != GLFW_TRUE)
    {
        check_glfw("create_platform", "glfwInit");
        throw BackendApiError({.backend = PlatformBackendKind::GLFW, .operation = "create_platform", .api_function = "glfwInit",
                               .result_code = 0, .result_name = "failure without an error code"});
    }
    try
    {
        // Monitors and devices present now (their connection is reported by the first pump), then the callbacks.
        m_platform = glfwGetPlatform();
        int count = 0;
        GLFWmonitor** const monitors = glfwGetMonitors(&count);
        for (int index = 0; monitors != nullptr && index < count; ++index)
        {
            static_cast<void>(register_monitor(monitors[index]));
        }
        m_devices = std::make_unique<GlfwInputDevices>(*this);
        for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick)
        {
            if (glfwJoystickPresent(joystick) == GLFW_TRUE)
            {
                m_devices->connect(joystick, now());
            }
        }
        install_platform_callbacks(this);
        clear_glfw_error();
    }
    catch (...)
    {
        // The destructor does not run for a constructor that throws: shut GLFW down here.
        install_platform_callbacks(nullptr);
        glfwTerminate();
        throw;
    }
}

GlfwPlatform::~GlfwPlatform()
{
    // Pending releases first, then what the application still holds: its windows and cursors detach
    // (glfwTerminate destroys them).
    release_deferred();
    install_platform_callbacks(nullptr);
    m_alive.reset();
    glfwTerminate();
    g_platform_exists = false;
}

auto
GlfwPlatform::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::GLFW;
}

auto
GlfwPlatform::supports(PlatformFeature feature) const noexcept -> bool
{
    switch (feature)
    {
        case PlatformFeature::WINDOW_ICON:
            // Wayland and macOS have no per-window icons.
            return runs_on(GLFW_PLATFORM_WIN32) || runs_on(GLFW_PLATFORM_X11);
        case PlatformFeature::WINDOW_POSITION:
            return !runs_on(GLFW_PLATFORM_WAYLAND);
        case PlatformFeature::RAW_MOUSE_MOTION:
            return glfwRawMouseMotionSupported() == GLFW_TRUE;
        case PlatformFeature::EXCLUSIVE_FULLSCREEN:
        case PlatformFeature::WINDOW_HOVER:
        case PlatformFeature::HIGH_PIXEL_DENSITY:
        case PlatformFeature::CUSTOM_CURSORS:
        case PlatformFeature::CAPTURED_CURSOR:
            return true;
        case PlatformFeature::WINDOW_OCCLUSION:
        case PlatformFeature::TEXT_COMPOSITION:
        case PlatformFeature::CLIPBOARD_IMAGE:
        case PlatformFeature::DROP_TEXT:
        case PlatformFeature::TOUCH:
        case PlatformFeature::GAMEPAD_RUMBLE:
        case PlatformFeature::GAMEPAD_TRIGGER_RUMBLE:
        case PlatformFeature::GAMEPAD_LED:
        case PlatformFeature::GAMEPAD_TOUCHPAD:
        case PlatformFeature::GAMEPAD_SENSORS:
            // GLFW 3.4 has none of these.
            return false;
    }
    return false;
}

auto
GlfwPlatform::supports(CursorShape shape) const noexcept -> bool
{
    return to_glfw(shape) != 0;
}

auto
GlfwPlatform::supported_window_flags() const noexcept -> WindowFlag
{
    // GLFW has no utility window.
    return WindowFlag::RESIZABLE | WindowFlag::HIDDEN | WindowFlag::ALWAYS_ON_TOP | WindowFlag::TRANSPARENT_FRAMEBUFFER
           | WindowFlag::HIGH_PIXEL_DENSITY | WindowFlag::FOCUS_ON_SHOW | WindowFlag::MOUSE_PASSTHROUGH;
}

auto
GlfwPlatform::create_window(const WindowDescription& description) -> std::unique_ptr<Window>
{
    check_thread("Platform::create_window");
    if (validates(ValidationLevel::CREATION))
    {
        validate_window_description(description, supported_window_flags(), backend());
    }
    return std::make_unique<GlfwWindow>(*this, description);
}

auto
GlfwPlatform::create_cursor(CursorShape shape) -> std::unique_ptr<Cursor>
{
    constexpr std::string_view OPERATION = "Platform::create_cursor";
    check_thread(OPERATION);
    if (!supports(shape))
    {
        throw_unsupported(OPERATION, std::format("cursor shape {}", shape),
                          "GLFW 3.4 has arrow, text, crosshair, pointer, resize, move and not-allowed cursors, but no wait or "
                          "progress cursor",
                          "create a custom cursor from RGBA (Platform::create_cursor(CursorImageDescription)), or use the SDL3 backend");
    }
    clear_glfw_error();
    GLFWcursor* const cursor = glfwCreateStandardCursor(to_glfw(shape));
    if (cursor == nullptr)
    {
        check_glfw(OPERATION, "glfwCreateStandardCursor");
        throw_unsupported(OPERATION, std::format("cursor shape {}", shape), "the system has no such cursor", "use another shape");
    }
    return std::make_unique<GlfwCursor>(*this, cursor, shape);
}

auto
GlfwPlatform::create_cursor(const CursorImageDescription& description) -> std::unique_ptr<Cursor>
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
    const GLFWimage pixels{.width = static_cast<int>(image.size.width),
                           .height = static_cast<int>(image.size.height),
                           .pixels = reinterpret_cast<unsigned char*>(image.rgba.data())};
    clear_glfw_error();
    GLFWcursor* const cursor = glfwCreateCursor(&pixels, description.hot_spot.x, description.hot_spot.y);
    if (cursor == nullptr)
    {
        check_glfw(OPERATION, "glfwCreateCursor");
    }
    return std::make_unique<GlfwCursor>(*this, cursor, std::nullopt);
}

auto
GlfwPlatform::monitors() const -> std::vector<MonitorHandle>
{
    check_thread("Platform::monitors");
    std::vector<MonitorHandle> result;
    int count = 0;
    GLFWmonitor** const monitors = glfwGetMonitors(&count);
    for (int index = 0; monitors != nullptr && index < count; ++index)
    {
        if (const MonitorHandle monitor = monitor_handle(monitors[index]); monitor.is_valid())
        {
            result.push_back(monitor);
        }
    }
    return result;
}

auto
GlfwPlatform::primary_monitor() const -> MonitorHandle
{
    check_thread("Platform::primary_monitor");
    return monitor_handle(glfwGetPrimaryMonitor());
}

auto
GlfwPlatform::monitor_properties(MonitorHandle monitor) const -> MonitorProperties
{
    // Name, desktop area, work area, scale and current mode, as GLFW reports them.
    constexpr std::string_view OPERATION = "Platform::monitor_properties";
    check_thread(OPERATION);
    GLFWmonitor* const native = native_monitor(monitor, OPERATION);
    MonitorProperties properties{.handle = monitor};
    const char* const name = glfwGetMonitorName(native);
    properties.name = name != nullptr ? name : "";
    if (const GLFWvidmode* const mode = glfwGetVideoMode(native))
    {
        properties.current_mode = to_video_mode(*mode);
    }
    int x = 0;
    int y = 0;
    glfwGetMonitorPos(native, &x, &y);
    properties.bounds = Rectangle{.x = x, .y = y, .width = properties.current_mode.size.width, .height = properties.current_mode.size.height};
    int work_x = 0;
    int work_y = 0;
    int work_width = 0;
    int work_height = 0;
    glfwGetMonitorWorkarea(native, &work_x, &work_y, &work_width, &work_height);
    properties.work_area = Rectangle{.x = work_x,
                                     .y = work_y,
                                     .width = static_cast<std::uint32_t>(std::max(work_width, 0)),
                                     .height = static_cast<std::uint32_t>(std::max(work_height, 0))};
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    glfwGetMonitorContentScale(native, &scale_x, &scale_y);
    properties.content_scale = scale_x > 0.0F ? scale_x : 1.0F;
    properties.primary = native == glfwGetPrimaryMonitor();
    check_glfw(OPERATION, "glfwGetMonitorWorkarea");
    return properties;
}

auto
GlfwPlatform::video_modes(MonitorHandle monitor) const -> std::vector<VideoMode>
{
    constexpr std::string_view OPERATION = "Platform::video_modes";
    check_thread(OPERATION);
    int count = 0;
    clear_glfw_error();
    const GLFWvidmode* const modes = glfwGetVideoModes(native_monitor(monitor, OPERATION), &count);
    if (modes == nullptr)
    {
        check_glfw(OPERATION, "glfwGetVideoModes");
    }
    std::vector<VideoMode> result;
    for (int index = 0; modes != nullptr && index < count; ++index)
    {
        result.push_back(to_video_mode(modes[index]));
    }
    return result;
}

auto
GlfwPlatform::clipboard_text() const -> std::string
{
    // A clipboard without text is not an error here.
    check_thread("Platform::clipboard_text");
    const char* const text = glfwGetClipboardString(nullptr);
    clear_glfw_error();
    return text != nullptr ? std::string{text} : std::string{};
}

void
GlfwPlatform::set_clipboard_text(std::string_view text)
{
    constexpr std::string_view OPERATION = "Platform::set_clipboard_text";
    check_thread(OPERATION);
    if (validates(ValidationLevel::CREATION) && !is_valid_utf8(text))
    {
        throw InvalidUsageError({.backend = backend(), .operation = std::string{OPERATION}, .problem = "the text is not valid UTF-8",
                                 .suggestion = "pass UTF-8 text"});
    }
    clear_glfw_error();
    glfwSetClipboardString(nullptr, std::string{text}.c_str());
    check_glfw(OPERATION, "glfwSetClipboardString");
}

auto
GlfwPlatform::clipboard_image() const -> DecodedImage
{
    throw_unsupported("Platform::clipboard_image", "clipboard images", "GLFW 3.4 reads and writes text only on the clipboard",
                      "check supports(PlatformFeature::CLIPBOARD_IMAGE) first, or use the SDL3 backend");
}

void
GlfwPlatform::set_clipboard_image(const ImageData& /*image*/)
{
    throw_unsupported("Platform::set_clipboard_image", "clipboard images", "GLFW 3.4 reads and writes text only on the clipboard",
                      "check supports(PlatformFeature::CLIPBOARD_IMAGE) first, or use the SDL3 backend");
}

auto
GlfwPlatform::input_devices() noexcept -> InputDevices&
{
    return *m_devices;
}

auto
GlfwPlatform::devices() noexcept -> GlfwInputDevices&
{
    return *m_devices;
}

// ------------------------------------------------------------------------------------------------- Registries

auto
GlfwPlatform::lifetime() const noexcept -> std::weak_ptr<GlfwPlatform* const>
{
    return m_alive;
}

void
GlfwPlatform::check_thread(std::string_view operation,
                           std::source_location location) const
{
    require_platform_thread(operation, location);
}

auto
GlfwPlatform::register_window(GlfwWindow& window) -> WindowHandle
{
    return m_windows.insert({.window = &window});
}

void
GlfwPlatform::unregister_window(WindowHandle window) noexcept
{
    static_cast<void>(m_windows.remove(window));
}

auto
GlfwPlatform::is_window_alive(WindowHandle window) const noexcept -> bool
{
    return m_windows.find(window) != nullptr;
}

auto
GlfwPlatform::register_cursor(GLFWcursor* cursor) -> CursorHandle
{
    return m_cursors.insert({.cursor = cursor});
}

void
GlfwPlatform::unregister_cursor(CursorHandle cursor) noexcept
{
    // The handle dies now; GLFW destroys the cursor at the next pump (windows showing it go back to the arrow).
    if (const std::optional<CursorRecord> record = m_cursors.remove(cursor))
    {
        try
        {
            defer_release(handle_key(), DestroyCursor{record->cursor});
        }
        catch (...)
        {
            glfwDestroyCursor(record->cursor);
        }
    }
}

auto
GlfwPlatform::find_cursor(CursorHandle cursor) const noexcept -> GLFWcursor*
{
    const CursorRecord* const record = m_cursors.find(cursor);
    return record != nullptr ? record->cursor : nullptr;
}

auto
GlfwPlatform::register_monitor(GLFWmonitor* monitor) -> MonitorHandle
{
    if (const MonitorHandle known = monitor_handle(monitor); known.is_valid())
    {
        return known;
    }
    const MonitorHandle handle = m_monitors.insert({.monitor = monitor});
    m_monitor_pointers.emplace_back(monitor, handle);
    return handle;
}

auto
GlfwPlatform::unregister_monitor(GLFWmonitor* monitor) noexcept -> MonitorHandle
{
    const auto found = std::ranges::find(m_monitor_pointers, monitor, &std::pair<GLFWmonitor*, MonitorHandle>::first);
    if (found == m_monitor_pointers.end())
    {
        return MonitorHandle{};
    }
    const MonitorHandle handle = found->second;
    m_monitor_pointers.erase(found);
    static_cast<void>(m_monitors.remove(handle));
    return handle;
}

auto
GlfwPlatform::monitor_handle(GLFWmonitor* monitor) const noexcept -> MonitorHandle
{
    const auto found = std::ranges::find(m_monitor_pointers, monitor, &std::pair<GLFWmonitor*, MonitorHandle>::first);
    return found != m_monitor_pointers.end() ? found->second : MonitorHandle{};
}

auto
GlfwPlatform::native_monitor(MonitorHandle monitor,
                             std::string_view operation,
                             std::source_location location) const -> GLFWmonitor*
{
    return m_monitors.get(monitor, operation, location).monitor;
}

auto
GlfwPlatform::runs_on(int platform) const noexcept -> bool
{
    return m_platform == platform;
}

auto
GlfwPlatform::now() noexcept -> std::chrono::nanoseconds
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

} // namespace gfx::platform::glfw
