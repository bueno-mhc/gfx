/// @file sdl3_internal.cppm
/// @brief The classes of the SDL3 backend, shared by its implementation units: the platform, windows,
///        cursors and input devices, their records and the translations between SDL and gfx values.
///
/// Toolchain notes: handle-table lookups (find, remove) happen in the implementation units only
/// (defect 10); every member has a brace initializer (defect 7); constructors are defined out of class
/// without lambdas (defect 6).
module;

#include "sdl3_headers.hpp"

module gfx.platform.backends.sdl3:internal;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;

namespace gfx::platform::sdl3
{

class Sdl3Platform;
class Sdl3Window;

/// Throws BackendApiError: SDL function @p function failed, as SDL_GetError() explains.
[[noreturn]] void throw_sdl_error(std::string_view operation,
                                  std::string_view function,
                                  std::source_location location = std::source_location::current());

// ================================================================================================= Translations

/// Physical key of an SDL scancode (SDL scancodes are USB HID usages; unknown ones give Key::UNKNOWN).
[[nodiscard]] auto to_key(SDL_Scancode scancode) noexcept -> Key;

/// Logical key of an SDL keycode (both use code points and the same flag for non-printable keys).
[[nodiscard]] auto to_key_code(SDL_Keycode key) noexcept -> KeyCode;

/// Modifiers of an SDL modifier state.
[[nodiscard]] auto to_key_modifier(SDL_Keymod modifiers) noexcept -> KeyModifier;

/// Mouse button of an SDL button index (SDL_BUTTON_LEFT ... SDL_BUTTON_X2).
[[nodiscard]] auto to_mouse_button(Uint8 button) noexcept -> std::optional<MouseButton>;

/// Gamepad button of an SDL gamepad button (SDL's extra miscellaneous buttons have none).
[[nodiscard]] auto to_gamepad_button(Uint8 button) noexcept -> std::optional<GamepadButton>;

/// Gamepad axis of an SDL gamepad axis.
[[nodiscard]] auto to_gamepad_axis(Uint8 axis) noexcept -> std::optional<GamepadAxis>;

/// Gamepad family of an SDL gamepad type.
[[nodiscard]] auto to_gamepad_type(SDL_GamepadType type) noexcept -> GamepadType;

/// Hat directions of an SDL hat value (SDL_HAT_UP ... share the bits of HatPosition).
[[nodiscard]] auto to_hat_position(Uint8 value) noexcept -> HatPosition;

/// An SDL axis value in [-32768, 32767] as [-1, 1] (triggers report [0, 32767], so [0, 1]).
[[nodiscard]] auto to_axis_value(Sint16 value) noexcept -> float;

/// SDL gamepad button of @p button.
[[nodiscard]] auto to_sdl(GamepadButton button) noexcept -> SDL_GamepadButton;

/// SDL gamepad axis of @p axis.
[[nodiscard]] auto to_sdl(GamepadAxis axis) noexcept -> SDL_GamepadAxis;

/// SDL system cursor of @p shape.
[[nodiscard]] auto to_sdl(CursorShape shape) noexcept -> SDL_SystemCursor;

// ================================================================================================= Records

/// A window of the platform's table.
struct WindowRecord
{
    Sdl3Window* window = nullptr;
};

/// A cursor of the platform's table.
struct CursorRecord
{
    SDL_Cursor* cursor = nullptr;
};

/// A monitor of the platform's table.
struct MonitorRecord
{
    SDL_DisplayID display = 0;
};

/// An opened gamepad.
struct GamepadRecord
{
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID instance = 0;
    bool announced = false;
};

/// An opened joystick without gamepad mapping.
struct JoystickRecord
{
    SDL_Joystick* joystick = nullptr;
    SDL_JoystickID instance = 0;
    bool announced = false;
};

/// A touch device seen in events or enumerated.
struct TouchDeviceRecord
{
    SDL_TouchID touch = 0;
};

// ================================================================================================= Window

/// A window: an SDL_Window, released at the pump after its destruction.
class Sdl3Window final : public Window
{
public:
    Sdl3Window(Sdl3Platform& platform,
               const WindowDescription& description);
    ~Sdl3Window() override;

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

    // ----------------------------------------------------------------------------------------- used by the platform

    /// The SDL window.
    [[nodiscard]] auto native() const noexcept -> SDL_Window*;

    /// Reads the sizes, content scale and minimized state from SDL into the thread-safe values, and
    /// returns them as the payload of a window event.
    auto refresh() noexcept -> WindowEvent;

    /// The mouse entered the window: shows its cursor (and hides it in CursorMode::HIDDEN).
    void mouse_entered() const noexcept;

    /// The mouse left the window: shows the cursor again in CursorMode::HIDDEN.
    void mouse_left() const noexcept;

private:
    /// The SDL window, after checking the platform still exists and the caller is its thread.
    [[nodiscard]] auto require(std::string_view operation,
                               std::source_location location = std::source_location::current()) const -> SDL_Window*;

    /// Shows the window, taking the focus when FOCUS_ON_SHOW was requested.
    void show_native() const;

private:
    Sdl3Platform& m_platform;
    std::weak_ptr<Sdl3Platform* const> m_alive{};
    SDL_Window* m_window = nullptr;
    WindowHandle m_handle{};
    WindowMode m_mode = WindowMode::WINDOWED;
    WindowFlag m_flags = WindowFlag::NONE;
    CursorHandle m_cursor{};
    CursorMode m_cursor_mode = CursorMode::NORMAL;
};

// ================================================================================================= Cursor

/// A cursor: an SDL_Cursor, released at the pump after its destruction.
class Sdl3Cursor final : public Cursor
{
public:
    Sdl3Cursor(Sdl3Platform& platform,
               SDL_Cursor* cursor,
               std::optional<CursorShape> shape);
    ~Sdl3Cursor() override;

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override;
    [[nodiscard]] auto handle() const noexcept -> CursorHandle override;
    [[nodiscard]] auto shape() const noexcept -> std::optional<CursorShape> override;

private:
    Sdl3Platform& m_platform;
    std::weak_ptr<Sdl3Platform* const> m_alive{};
    CursorHandle m_handle{};
    std::optional<CursorShape> m_shape{};
};

// ================================================================================================= Input devices

/// Gamepads, joysticks and touch devices: opened when SDL reports them, closed when they leave.
class Sdl3InputDevices final : public InputDevices
{
public:
    explicit Sdl3InputDevices(Sdl3Platform& platform);
    ~Sdl3InputDevices() override;

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

    /// Opens the gamepads and joysticks connected at start-up.
    void open_connected_devices();

    /// Opens gamepad @p instance (or finds it); a null handle when SDL cannot open it. @p first_report
    /// is set when its connection was not reported yet.
    auto connect_gamepad(SDL_JoystickID instance,
                         bool& first_report) -> GamepadHandle;

    /// Closes gamepad @p instance; its handle (null when unknown) becomes stale.
    auto disconnect_gamepad(SDL_JoystickID instance) noexcept -> GamepadHandle;

    /// Handle of opened gamepad @p instance (null when unknown).
    [[nodiscard]] auto gamepad_handle(SDL_JoystickID instance) const noexcept -> GamepadHandle;

    /// Opens joystick @p instance (or finds it) unless it is a gamepad; see connect_gamepad().
    auto connect_joystick(SDL_JoystickID instance,
                          bool& first_report) -> JoystickHandle;

    /// Closes joystick @p instance; its handle (null when unknown) becomes stale.
    auto disconnect_joystick(SDL_JoystickID instance) noexcept -> JoystickHandle;

    /// Handle of opened joystick @p instance (null when unknown or a gamepad).
    [[nodiscard]] auto joystick_handle(SDL_JoystickID instance) const noexcept -> JoystickHandle;

    /// Handle of touch device @p touch, created when first seen.
    auto touch_device_handle(SDL_TouchID touch) -> TouchDeviceHandle;

    /// Closes every device (before SDL shuts down).
    void close_all() noexcept;

private:
    /// The SDL gamepad of @p gamepad, after the thread check.
    [[nodiscard]] auto require_gamepad(GamepadHandle gamepad,
                                       std::string_view operation,
                                       std::source_location location = std::source_location::current()) const -> SDL_Gamepad*;

private:
    Sdl3Platform& m_platform;
    gfx::backends::HandleTable<GamepadTag, GamepadRecord> m_gamepads{PlatformBackendKind::SDL3};
    std::unordered_map<SDL_JoystickID, GamepadHandle> m_gamepad_instances{};
    gfx::backends::HandleTable<JoystickTag, JoystickRecord> m_joysticks{PlatformBackendKind::SDL3};
    std::unordered_map<SDL_JoystickID, JoystickHandle> m_joystick_instances{};
    gfx::backends::HandleTable<TouchDeviceTag, TouchDeviceRecord> m_touch_devices{PlatformBackendKind::SDL3};
    std::unordered_map<SDL_TouchID, TouchDeviceHandle> m_touch_instances{};
};

// ================================================================================================= Platform

/// The SDL3 platform: SDL's video and gamepad subsystems, one per process.
class Sdl3Platform final : public Platform
{
public:
    explicit Sdl3Platform(const PlatformDescription& description);
    ~Sdl3Platform() override;

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

    // ----------------------------------------------------------------------------------------- used by windows, cursors and devices

    /// A token that expires with the platform (windows and cursors may outlive it).
    [[nodiscard]] auto lifetime() const noexcept -> std::weak_ptr<Sdl3Platform* const>;

    /// Throws InvalidUsageError unless the caller runs on the platform thread.
    void check_thread(std::string_view operation,
                      std::source_location location = std::source_location::current()) const;

    /// Registers @p window (named @p identifier by SDL) and returns its handle.
    auto register_window(Sdl3Window& window,
                         SDL_WindowID identifier) -> WindowHandle;

    /// Forgets @p window (named @p identifier by SDL): its events are dropped from now on.
    void unregister_window(WindowHandle window,
                           SDL_WindowID identifier) noexcept;

    /// The window SDL names @p identifier (null when unknown or destroyed).
    [[nodiscard]] auto find_window(SDL_WindowID identifier) const noexcept -> Sdl3Window*;

    /// Registers an SDL cursor and returns its handle.
    auto register_cursor(SDL_Cursor* cursor) -> CursorHandle;

    /// Forgets @p cursor; its SDL cursor is destroyed at the next pump.
    void unregister_cursor(CursorHandle cursor) noexcept;

    /// The SDL cursor of @p cursor (null when unknown or destroyed).
    [[nodiscard]] auto find_cursor(CursorHandle cursor) const noexcept -> SDL_Cursor*;

    /// Handle of display @p display (null when unknown).
    [[nodiscard]] auto monitor_handle(SDL_DisplayID display) const noexcept -> MonitorHandle;

    /// SDL display of @p monitor.
    /// @throws InvalidHandleError when the monitor was disconnected.
    [[nodiscard]] auto display_of(MonitorHandle monitor,
                                  std::string_view operation,
                                  std::source_location location = std::source_location::current()) const -> SDL_DisplayID;

    /// Whether SDL runs on Wayland (no window positions there).
    [[nodiscard]] auto is_wayland() const noexcept -> bool;

    /// Called by SDL for every event pushed (its event watch): translates it at once when SDL generates
    /// it on the platform thread during a pump (event watchers then run in real time, e.g. while Windows
    /// resizes a window in its own loop).
    void watch_native_event(const SDL_Event& event) noexcept;

protected:
    void pump(EventWaitMode mode,
              std::chrono::nanoseconds timeout) override;
    void wake_backend() noexcept override;
    [[nodiscard]] auto is_window_alive(WindowHandle window) const noexcept -> bool override;

private:
    /// Emits the normalized events of @p event.
    void translate(const SDL_Event& event);

    void translate_window_event(const SDL_Event& event);
    void translate_display_event(const SDL_Event& event);
    void translate_device_event(const SDL_Event& event);

    /// The window handle of SDL window @p identifier: null for 0 (no window); std::nullopt when the
    /// window is unknown or destroyed (the event is dropped).
    [[nodiscard]] auto window_of(SDL_WindowID identifier) const noexcept -> std::optional<WindowHandle>;

    /// An SDL timestamp (nanoseconds since SDL started) on the steady clock.
    [[nodiscard]] auto steady_time(Uint64 timestamp) const noexcept -> std::chrono::nanoseconds;

    /// Registers display @p display, if new, and returns its handle.
    auto register_display(SDL_DisplayID display) -> MonitorHandle;

private:
    std::shared_ptr<Sdl3Platform* const> m_alive{};
    Uint32 m_wake_event = 0;
    std::chrono::nanoseconds m_clock_offset{};
    bool m_wayland = false;
    gfx::backends::HandleTable<WindowTag, WindowRecord> m_windows{PlatformBackendKind::SDL3};
    std::unordered_map<SDL_WindowID, WindowHandle> m_window_identifiers{};
    gfx::backends::HandleTable<CursorTag, CursorRecord> m_cursors{PlatformBackendKind::SDL3};
    gfx::backends::HandleTable<MonitorTag, MonitorRecord> m_monitors{PlatformBackendKind::SDL3};
    std::unordered_map<SDL_DisplayID, MonitorHandle> m_displays{};
    std::unique_ptr<Sdl3InputDevices> m_devices{};

    // Events the event watch translated during a pump, still in SDL's queue: skipped when polled. SDL's
    // poll stops at a sentinel, so an event raised during a pump may be polled by the next one: entries
    // are kept for one more pump (with the number of the pump that translated them).
    bool m_in_native_pump = false;
    std::uint64_t m_pump_number = 0;
    std::vector<std::pair<std::uint64_t, SDL_Event>> m_translated{};
    std::exception_ptr m_watch_failure{};
};

} // namespace gfx::platform::sdl3
