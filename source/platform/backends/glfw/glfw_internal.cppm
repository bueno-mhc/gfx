/// @file glfw_internal.cppm
/// @brief The classes of the GLFW backend, shared by its implementation units: the platform, windows,
///        cursors and input devices, their records and the translations between GLFW and gfx values.
///
/// GLFW reports input through callbacks, run while GLFW processes events: during a pump they emit at
/// once (event watchers run in real time, also inside Windows' resize loop); outside a pump (e.g.
/// glfwSetWindowSize reporting the resize synchronously) they wait for the next pump. Gamepads and
/// joysticks have no events in GLFW: the pump polls their state and reports the differences.
///
/// Toolchain notes: handle-table lookups (find, remove) happen in the implementation units only
/// (defect 10); every member has a brace initializer (defect 7); constructors are defined out of class
/// without lambdas (defect 6).
module;

#include "glfw_headers.hpp"

module gfx.platform.backends.glfw:internal;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;

namespace gfx::platform::glfw
{

class GlfwPlatform;
class GlfwWindow;

/// Throws the error GLFW reported for @p function, if any: BackendCapabilityError when GLFW lacks the
/// feature on this system (GLFW_FEATURE_UNAVAILABLE, GLFW_FEATURE_UNIMPLEMENTED, GLFW_CURSOR_UNAVAILABLE),
/// BackendApiError otherwise, with GLFW's description.
void check_glfw(std::string_view operation,
                std::string_view function,
                std::source_location location = std::source_location::current());

/// Forgets the error GLFW reported last, if any (before a call whose failure is checked).
void clear_glfw_error() noexcept;

// ================================================================================================= Translations

/// Physical key of a GLFW key token (GLFW names keys by their US-layout position).
[[nodiscard]] auto to_key(int key) noexcept -> Key;

/// Logical key of @p key (a GLFW token): the character the layout gives printable keys
/// (glfwGetKeyName), KEY_CODE_NON_PRINTABLE_FLAG | the HID usage for the others.
[[nodiscard]] auto to_key_code(int key,
                               int scancode) noexcept -> KeyCode;

/// Modifiers of GLFW modifier bits, with the side read from @p window's key states.
[[nodiscard]] auto to_key_modifier(GLFWwindow* window,
                                   int modifiers) noexcept -> KeyModifier;

/// Mouse button of a GLFW mouse button.
[[nodiscard]] auto to_mouse_button(int button) noexcept -> std::optional<MouseButton>;

/// GLFW standard cursor of @p shape (0 when GLFW has none).
[[nodiscard]] auto to_glfw(CursorShape shape) noexcept -> int;

/// Gamepad button of a GLFW gamepad button index.
[[nodiscard]] auto to_gamepad_button(int button) noexcept -> GamepadButton;

/// GLFW gamepad button index of @p button (-1 when GLFW has none).
[[nodiscard]] auto to_glfw(GamepadButton button) noexcept -> int;

/// Value of GLFW gamepad axis @p axis: sticks as they are, triggers moved from [-1, 1] to [0, 1].
[[nodiscard]] auto gamepad_axis_value(int axis,
                                      float value) noexcept -> float;

/// UTF-8 encoding of @p code_point.
[[nodiscard]] auto utf8(unsigned int code_point) -> std::string;

/// Installs the GLFW callbacks of @p window (glfw_events.cpp).
void install_window_callbacks(GLFWwindow* window) noexcept;

/// Installs GLFW's monitor and joystick callbacks for @p platform (null removes them).
void install_platform_callbacks(GlfwPlatform* platform) noexcept;

// ================================================================================================= Records

/// A window of the platform's table.
struct WindowRecord
{
    GlfwWindow* window = nullptr;
};

/// A cursor of the platform's table.
struct CursorRecord
{
    GLFWcursor* cursor = nullptr;
};

/// A monitor of the platform's table.
struct MonitorRecord
{
    GLFWmonitor* monitor = nullptr;
};

/// A connected gamepad and its state at the last pump.
struct GamepadRecord
{
    int joystick = 0;
    GLFWgamepadstate state{};
};

/// A connected joystick without gamepad mapping and its state at the last pump.
struct JoystickRecord
{
    int joystick = 0;
    std::vector<float> axes{};
    std::vector<unsigned char> buttons{};
    std::vector<unsigned char> hats{};
};

// ================================================================================================= Window

/// A window: a GLFWwindow without graphics context, destroyed at the pump after its destruction.
class GlfwWindow final : public Window
{
public:
    GlfwWindow(GlfwPlatform& platform,
               const WindowDescription& description);
    ~GlfwWindow() override;

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override;
    [[nodiscard]] auto handle() const noexcept -> WindowHandle override;
    [[nodiscard]] auto title() const -> std::string override;
    void set_title(std::string_view title) override;
    void set_icon(std::span<const ImageData> images) override;
    void set_size(Extent size) override;
    [[nodiscard]] auto position() const -> Offset override;
    void set_position(Offset position) override;
    void set_size_limits(std::optional<Extent> minimum_size,
                         std::optional<Extent> maximum_size) override;
    void set_aspect_ratio(std::optional<AspectRatio> aspect_ratio) override;
    void set_resizable(bool resizable) override;
    [[nodiscard]] auto mode() const -> WindowMode override;
    void set_mode(WindowMode mode,
                  const FullscreenDescription& fullscreen) override;
    void show() override;
    void hide() override;
    void minimize() override;
    void maximize() override;
    void restore() override;
    void focus() override;
    [[nodiscard]] auto is_visible() const -> bool override;
    [[nodiscard]] auto is_focused() const -> bool override;
    [[nodiscard]] auto is_hovered() const -> bool override;
    [[nodiscard]] auto is_maximized() const -> bool override;
    [[nodiscard]] auto is_occluded() const -> bool override;
    [[nodiscard]] auto monitor() const -> MonitorHandle override;
    void set_cursor(CursorHandle cursor) override;
    void set_cursor_mode(CursorMode mode) override;
    [[nodiscard]] auto cursor_mode() const -> CursorMode override;
    [[nodiscard]] auto cursor_position() const -> Vector2 override;
    void set_cursor_position(Vector2 position) override;
    void start_text_input(const TextInputDescription& description) override;
    void stop_text_input() override;
    [[nodiscard]] auto is_text_input_active() const -> bool override;

    // ----------------------------------------------------------------------------------------- used by the callbacks

    /// The platform.
    [[nodiscard]] auto platform() const noexcept -> GlfwPlatform&;

    /// Reads the sizes, content scale and minimized state from GLFW into the thread-safe values, and
    /// returns them as the payload of a window event.
    auto refresh() noexcept -> WindowEvent;

    /// The cursor position of the last motion (the origin of the next delta).
    [[nodiscard]] auto last_cursor_position() const noexcept -> std::optional<Vector2>;

    /// Records the cursor position of a motion.
    void set_last_cursor_position(std::optional<Vector2> position) noexcept;

    /// Mouse buttons held over the window.
    [[nodiscard]] auto held_buttons() const noexcept -> MouseButtonMask;

    /// Records a press or release of @p button.
    void set_button(MouseButton button,
                    bool down) noexcept;

    /// Adds a wheel offset and returns the whole notches it completes (fractions accumulate).
    auto wheel_steps(double x,
                     double y) noexcept -> std::pair<std::int32_t, std::int32_t>;

    /// Whether characters typed in the window are reported (start_text_input()).
    [[nodiscard]] auto reports_text() const noexcept -> bool;

private:
    /// The GLFW window, after checking the platform still exists and the caller is its thread.
    [[nodiscard]] auto require(std::string_view operation,
                               std::source_location location = std::source_location::current()) const -> GLFWwindow*;

    /// Shows the window, with GLFW's focus-on-show attribute as requested.
    void show_native();

private:
    GlfwPlatform& m_platform;
    std::weak_ptr<GlfwPlatform* const> m_alive{};
    GLFWwindow* m_window = nullptr;
    WindowHandle m_handle{};
    WindowMode m_mode = WindowMode::WINDOWED;
    WindowFlag m_flags = WindowFlag::NONE;
    CursorHandle m_cursor{};
    CursorMode m_cursor_mode = CursorMode::NORMAL;
    Offset m_windowed_position{};
    Extent m_windowed_size{};
    std::optional<Vector2> m_last_cursor{};
    MouseButtonMask m_buttons = MouseButtonMask::NONE;
    double m_wheel_x = 0.0;
    double m_wheel_y = 0.0;
    bool m_text_input = false;
};

// ================================================================================================= Cursor

/// A cursor: a GLFWcursor, destroyed at the pump after its destruction.
class GlfwCursor final : public Cursor
{
public:
    GlfwCursor(GlfwPlatform& platform,
               GLFWcursor* cursor,
               std::optional<CursorShape> shape);
    ~GlfwCursor() override;

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override;
    [[nodiscard]] auto handle() const noexcept -> CursorHandle override;
    [[nodiscard]] auto shape() const noexcept -> std::optional<CursorShape> override;

private:
    GlfwPlatform& m_platform;
    std::weak_ptr<GlfwPlatform* const> m_alive{};
    CursorHandle m_handle{};
    std::optional<CursorShape> m_shape{};
};

// ================================================================================================= Input devices

/// Gamepads (GLFW's gamepad mappings) and joysticks: GLFW reports connections; the pump polls states.
class GlfwInputDevices final : public InputDevices
{
public:
    explicit GlfwInputDevices(GlfwPlatform& platform);

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override;
    [[nodiscard]] auto gamepads() const -> std::vector<GamepadHandle> override;
    [[nodiscard]] auto gamepad_properties(GamepadHandle gamepad) const -> GamepadProperties override;
    [[nodiscard]] auto is_gamepad_button_down(GamepadHandle gamepad,
                                              GamepadButton button) const -> bool override;
    [[nodiscard]] auto gamepad_axis(GamepadHandle gamepad,
                                    GamepadAxis axis) const -> float override;
    void set_gamepad_rumble(GamepadHandle gamepad,
                            float low_frequency,
                            float high_frequency,
                            std::chrono::milliseconds duration) override;
    void set_gamepad_trigger_rumble(GamepadHandle gamepad,
                                    float left,
                                    float right,
                                    std::chrono::milliseconds duration) override;
    void set_gamepad_led(GamepadHandle gamepad,
                         LedColor color) override;
    [[nodiscard]] auto joysticks() const -> std::vector<JoystickHandle> override;
    [[nodiscard]] auto joystick_properties(JoystickHandle joystick) const -> JoystickProperties override;
    [[nodiscard]] auto touch_devices() const -> std::vector<TouchDeviceHandle> override;
    [[nodiscard]] auto touch_device_properties(TouchDeviceHandle device) const -> TouchDeviceProperties override;

    // ----------------------------------------------------------------------------------------- used by the platform

    /// Registers joystick @p joystick (a gamepad when GLFW maps it) and emits its connection.
    void connect(int joystick,
                 std::chrono::nanoseconds time);

    /// Forgets joystick @p joystick and emits its disconnection.
    void disconnect(int joystick,
                    std::chrono::nanoseconds time);

    /// Polls every device and emits the differences since the last pump.
    void poll(std::chrono::nanoseconds time);

private:
    /// The joystick of @p gamepad, after the thread check.
    [[nodiscard]] auto require_gamepad(GamepadHandle gamepad,
                                       std::string_view operation,
                                       std::source_location location = std::source_location::current()) const -> int;

private:
    GlfwPlatform& m_platform;
    gfx::backends::HandleTable<GamepadTag, GamepadRecord> m_gamepads{PlatformBackendKind::GLFW};
    gfx::backends::HandleTable<JoystickTag, JoystickRecord> m_joysticks{PlatformBackendKind::GLFW};
    std::array<GamepadHandle, GLFW_JOYSTICK_LAST + 1> m_gamepad_slots{};
    std::array<JoystickHandle, GLFW_JOYSTICK_LAST + 1> m_joystick_slots{};
};

// ================================================================================================= Platform

/// The GLFW platform: GLFW initialized once per process.
class GlfwPlatform final : public Platform
{
public:
    explicit GlfwPlatform(const PlatformDescription& description);
    ~GlfwPlatform() override;

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override;
    [[nodiscard]] auto supports(PlatformFeature feature) const noexcept -> bool override;
    [[nodiscard]] auto supports(CursorShape shape) const noexcept -> bool override;
    [[nodiscard]] auto supported_window_flags() const noexcept -> WindowFlag override;
    [[nodiscard]] auto create_window(const WindowDescription& description) -> std::unique_ptr<Window> override;
    [[nodiscard]] auto create_cursor(CursorShape shape) -> std::unique_ptr<Cursor> override;
    [[nodiscard]] auto create_cursor(const CursorImageDescription& description) -> std::unique_ptr<Cursor> override;
    [[nodiscard]] auto monitors() const -> std::vector<MonitorHandle> override;
    [[nodiscard]] auto primary_monitor() const -> MonitorHandle override;
    [[nodiscard]] auto monitor_properties(MonitorHandle monitor) const -> MonitorProperties override;
    [[nodiscard]] auto video_modes(MonitorHandle monitor) const -> std::vector<VideoMode> override;
    [[nodiscard]] auto clipboard_text() const -> std::string override;
    void set_clipboard_text(std::string_view text) override;
    [[nodiscard]] auto clipboard_image() const -> DecodedImage override;
    void set_clipboard_image(const ImageData& image) override;
    [[nodiscard]] auto input_devices() noexcept -> InputDevices& override;

    // ----------------------------------------------------------------------------------------- used by windows, cursors, devices and callbacks

    /// A token that expires with the platform (windows and cursors may outlive it).
    [[nodiscard]] auto lifetime() const noexcept -> std::weak_ptr<GlfwPlatform* const>;

    /// The gamepads and joysticks.
    [[nodiscard]] auto devices() noexcept -> GlfwInputDevices&;

    /// Throws InvalidUsageError unless the caller runs on the platform thread.
    void check_thread(std::string_view operation,
                      std::source_location location = std::source_location::current()) const;

    /// Registers @p window and returns its handle.
    auto register_window(GlfwWindow& window) -> WindowHandle;

    /// Forgets @p window: its events are dropped from now on.
    void unregister_window(WindowHandle window) noexcept;

    /// Registers a GLFW cursor and returns its handle.
    auto register_cursor(GLFWcursor* cursor) -> CursorHandle;

    /// Forgets @p cursor; GLFW destroys it at the next pump.
    void unregister_cursor(CursorHandle cursor) noexcept;

    /// The GLFW cursor of @p cursor (null when unknown or destroyed).
    [[nodiscard]] auto find_cursor(CursorHandle cursor) const noexcept -> GLFWcursor*;

    /// Handle of @p monitor (null when unknown).
    [[nodiscard]] auto monitor_handle(GLFWmonitor* monitor) const noexcept -> MonitorHandle;

    /// GLFW monitor of @p monitor.
    /// @throws InvalidHandleError when the monitor was disconnected.
    [[nodiscard]] auto native_monitor(MonitorHandle monitor,
                                      std::string_view operation,
                                      std::source_location location = std::source_location::current()) const -> GLFWmonitor*;

    /// Registers @p monitor, if new, and returns its handle.
    auto register_monitor(GLFWmonitor* monitor) -> MonitorHandle;

    /// Forgets @p monitor; returns its (now stale) handle.
    auto unregister_monitor(GLFWmonitor* monitor) noexcept -> MonitorHandle;

    /// Whether GLFW runs on @p platform (GLFW_PLATFORM_*).
    [[nodiscard]] auto runs_on(int platform) const noexcept -> bool;

    /// Reports @p event (with @p text when its payload references text): at once during a pump, at the
    /// next pump otherwise.
    void deliver(const Event& event,
                 std::string_view text = {});

    /// Records an exception a callback caught (C callbacks cannot throw); the pump rethrows it.
    void record_failure(std::exception_ptr failure) noexcept;

    /// The current time on the steady clock (GLFW events carry no time).
    [[nodiscard]] static auto now() noexcept -> std::chrono::nanoseconds;

protected:
    void pump(EventWaitMode mode,
              std::chrono::nanoseconds timeout) override;
    void wake_backend() noexcept override;
    [[nodiscard]] auto is_window_alive(WindowHandle window) const noexcept -> bool override;

private:
    std::shared_ptr<GlfwPlatform* const> m_alive{};
    int m_platform = 0;
    gfx::backends::HandleTable<WindowTag, WindowRecord> m_windows{PlatformBackendKind::GLFW};
    gfx::backends::HandleTable<CursorTag, CursorRecord> m_cursors{PlatformBackendKind::GLFW};
    gfx::backends::HandleTable<MonitorTag, MonitorRecord> m_monitors{PlatformBackendKind::GLFW};
    std::vector<std::pair<GLFWmonitor*, MonitorHandle>> m_monitor_pointers{};
    std::unique_ptr<GlfwInputDevices> m_devices{};
    bool m_in_pump = false;
    std::atomic<bool> m_wake_requested{false};
    EventQueue m_pending{64, 1024};
    std::exception_ptr m_callback_failure{};
};

} // namespace gfx::platform::glfw
