/// @file fake_platform.cppm
/// @brief A test-only platform backend: windows and cursors held in memory, native events scripted by
///        the test, waits that a condition variable ends. It exercises the library's platform logic
///        (pump, threads, watchers, deferred releases, validation) without a windowing system. It reports
///        itself as the SDL3 backend in messages; some features are deliberately unsupported, to check
///        the error contract.
export module gfx.tests.fake_platform;

import std;
import gfx.platform;
import gfx.core.handle_table;

export namespace gfx::tests
{

using namespace gfx::platform;

class FakePlatform;

/// @brief A window that only remembers its state.
class FakeWindow final : public Window
{
public:
    FakeWindow(FakePlatform& platform,
               const WindowDescription& description);
    ~FakeWindow() override;

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override
    {
        return PlatformBackendKind::SDL3;
    }

    [[nodiscard]] auto handle() const noexcept -> WindowHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto title() const -> std::string override
    {
        return m_title;
    }

    void set_title(std::string_view title) override
    {
        m_title = title;
    }

    void set_icon(std::span<const ImageData> images) override
    {
        // Every image is decoded and validated, as real backends do.
        m_icon_sizes.clear();
        for (const ImageData& image : images)
        {
            m_icon_sizes.push_back(decode_image(image, "Window::set_icon").size);
        }
    }

    void set_size(Extent size) override
    {
        simulate_resize(size, size);
    }

    [[nodiscard]] auto position() const -> Offset override
    {
        return m_position;
    }

    void set_position(Offset position) override
    {
        m_position = position;
    }

    void set_size_limits(std::optional<Extent> /*minimum_size*/,
                         std::optional<Extent> /*maximum_size*/) override
    {
    }

    void set_aspect_ratio(std::optional<AspectRatio> /*aspect_ratio*/) override
    {
    }

    void set_resizable(bool /*resizable*/) override
    {
    }

    [[nodiscard]] auto mode() const -> WindowMode override
    {
        return m_mode;
    }

    void set_mode(WindowMode mode,
                  const FullscreenDescription& /*fullscreen*/) override
    {
        m_mode = mode;
    }

    void show() override
    {
        m_visible = true;
    }

    void hide() override
    {
        m_visible = false;
    }

    void minimize() override
    {
        update_minimized(true);
    }

    void maximize() override
    {
    }

    void restore() override
    {
        update_minimized(false);
    }

    void focus() override
    {
    }

    [[nodiscard]] auto is_visible() const -> bool override
    {
        return m_visible;
    }

    [[nodiscard]] auto is_focused() const -> bool override
    {
        return false;
    }

    [[nodiscard]] auto is_hovered() const -> bool override
    {
        return false;
    }

    [[nodiscard]] auto is_maximized() const -> bool override
    {
        return false;
    }

    [[nodiscard]] auto is_occluded() const -> bool override
    {
        throw_unsupported("Window::is_occluded", "occlusion state", "the fake backend cannot see other windows",
                          "check supports(PlatformFeature::WINDOW_OCCLUSION) first");
    }

    [[nodiscard]] auto monitor() const -> MonitorHandle override;

    void set_cursor(CursorHandle cursor) override
    {
        m_cursor = cursor;
    }

    void set_cursor_mode(CursorMode mode) override
    {
        m_cursor_mode = mode;
    }

    [[nodiscard]] auto cursor_mode() const -> CursorMode override
    {
        return m_cursor_mode;
    }

    [[nodiscard]] auto cursor_position() const -> Vector2 override
    {
        return m_cursor_position;
    }

    void set_cursor_position(Vector2 position) override
    {
        m_cursor_position = position;
    }

    void start_text_input(const TextInputDescription& /*description*/) override
    {
        m_text_input = true;
    }

    void stop_text_input() override
    {
        m_text_input = false;
    }

    [[nodiscard]] auto is_text_input_active() const -> bool override
    {
        return m_text_input;
    }

    /// @brief What the pump would record when the system resizes the window.
    void simulate_resize(Extent size,
                         Extent framebuffer_size) noexcept
    {
        update_size(size, framebuffer_size);
    }

    /// @brief Sizes of the icon images last set.
    [[nodiscard]] auto icon_sizes() const -> std::span<const Extent>
    {
        return m_icon_sizes;
    }

    /// @brief Cursor shown.
    [[nodiscard]] auto cursor() const noexcept -> CursorHandle
    {
        return m_cursor;
    }

private:
    FakePlatform& m_platform;
    WindowHandle m_handle{};
    std::string m_title;
    Offset m_position{};
    WindowMode m_mode = WindowMode::WINDOWED;
    bool m_visible = true;
    CursorHandle m_cursor{};
    CursorMode m_cursor_mode = CursorMode::NORMAL;
    Vector2 m_cursor_position{};
    bool m_text_input = false;
    std::vector<Extent> m_icon_sizes;
};

/// @brief A cursor that only remembers its shape.
class FakeCursor final : public Cursor
{
public:
    FakeCursor(CursorHandle handle,
               std::optional<CursorShape> shape) noexcept
        : m_handle{handle}
        , m_shape{shape}
    {
    }

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override
    {
        return PlatformBackendKind::SDL3;
    }

    [[nodiscard]] auto handle() const noexcept -> CursorHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto shape() const noexcept -> std::optional<CursorShape> override
    {
        return m_shape;
    }

private:
    CursorHandle m_handle{};
    std::optional<CursorShape> m_shape;
};

/// @brief No device is connected; rumble is unsupported, the rest forwards to the interface's
///        "not implemented" definitions.
class FakeInputDevices final : public InputDevices
{
public:
    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override
    {
        return PlatformBackendKind::SDL3;
    }

    [[nodiscard]] auto gamepads() const -> std::vector<GamepadHandle> override
    {
        return {};
    }

    [[nodiscard]] auto gamepad_properties(GamepadHandle gamepad) const -> GamepadProperties override
    {
        return InputDevices::gamepad_properties(gamepad);
    }

    [[nodiscard]] auto is_gamepad_button_down(GamepadHandle gamepad,
                                              GamepadButton button) const -> bool override
    {
        return InputDevices::is_gamepad_button_down(gamepad, button);
    }

    [[nodiscard]] auto gamepad_axis(GamepadHandle gamepad,
                                    GamepadAxis axis) const -> float override
    {
        return InputDevices::gamepad_axis(gamepad, axis);
    }

    void set_gamepad_rumble(GamepadHandle /*gamepad*/,
                            float /*low_frequency*/,
                            float /*high_frequency*/,
                            std::chrono::milliseconds /*duration*/) override
    {
        throw_unsupported("InputDevices::set_gamepad_rumble", "gamepad rumble", "the fake backend has no gamepad",
                          "check supports(PlatformFeature::GAMEPAD_RUMBLE) first");
    }

    void set_gamepad_trigger_rumble(GamepadHandle gamepad,
                                    float left,
                                    float right,
                                    std::chrono::milliseconds duration) override
    {
        InputDevices::set_gamepad_trigger_rumble(gamepad, left, right, duration);
    }

    void set_gamepad_led(GamepadHandle gamepad,
                         LedColor color) override
    {
        InputDevices::set_gamepad_led(gamepad, color);
    }

    [[nodiscard]] auto joysticks() const -> std::vector<JoystickHandle> override
    {
        return {};
    }

    [[nodiscard]] auto joystick_properties(JoystickHandle joystick) const -> JoystickProperties override
    {
        return InputDevices::joystick_properties(joystick);
    }

    [[nodiscard]] auto touch_devices() const -> std::vector<TouchDeviceHandle> override
    {
        return {};
    }

    [[nodiscard]] auto touch_device_properties(TouchDeviceHandle device) const -> TouchDeviceProperties override
    {
        return InputDevices::touch_device_properties(device);
    }
};

/// @brief What the fake platform keeps about a window.
struct FakeWindowRecord
{
    FakeWindow* window = nullptr;
};

/// @brief The fake platform.
class FakePlatform final : public Platform
{
public:
    explicit FakePlatform(const PlatformDescription& description = {})
        : Platform{description}
    {
        m_monitor = MonitorHandle::create(handle_key(), 0, 1);
    }

    ~FakePlatform() override
    {
        release_deferred();
    }

    [[nodiscard]] auto backend() const noexcept -> PlatformBackendKind override
    {
        return PlatformBackendKind::SDL3;
    }

    [[nodiscard]] auto supports(PlatformFeature feature) const noexcept -> bool override
    {
        return feature == PlatformFeature::WINDOW_POSITION || feature == PlatformFeature::CUSTOM_CURSORS;
    }

    [[nodiscard]] auto supports(CursorShape shape) const noexcept -> bool override
    {
        return shape != CursorShape::WAIT;
    }

    [[nodiscard]] auto supported_window_flags() const noexcept -> WindowFlag override
    {
        return WindowFlag::RESIZABLE | WindowFlag::HIDDEN | WindowFlag::HIGH_PIXEL_DENSITY | WindowFlag::FOCUS_ON_SHOW;
    }

    [[nodiscard]] auto create_window(const WindowDescription& description) -> std::unique_ptr<Window> override
    {
        require_platform_thread("Platform::create_window");
        if (validates(ValidationLevel::CREATION))
        {
            validate_window_description(description, supported_window_flags(), backend());
        }
        return std::make_unique<FakeWindow>(*this, description);
    }

    [[nodiscard]] auto create_cursor(CursorShape shape) -> std::unique_ptr<Cursor> override
    {
        if (!supports(shape))
        {
            throw_unsupported("Platform::create_cursor", std::format("cursor shape {}", shape),
                              "the fake backend has no busy cursor", "use CursorShape::PROGRESS");
        }
        return std::make_unique<FakeCursor>(CursorHandle::create(handle_key(), m_next_cursor++, 1), shape);
    }

    [[nodiscard]] auto create_cursor(const CursorImageDescription& description) -> std::unique_ptr<Cursor> override
    {
        (void)decode_image(description.image, "Platform::create_cursor");
        return std::make_unique<FakeCursor>(CursorHandle::create(handle_key(), m_next_cursor++, 1), std::nullopt);
    }

    [[nodiscard]] auto monitors() const -> std::vector<MonitorHandle> override
    {
        return {m_monitor};
    }

    [[nodiscard]] auto primary_monitor() const -> MonitorHandle override
    {
        return m_monitor;
    }

    [[nodiscard]] auto monitor_properties(MonitorHandle monitor) const -> MonitorProperties override
    {
        return MonitorProperties{.handle = monitor,
                                 .name = "Fake monitor",
                                 .bounds = {.x = 0, .y = 0, .width = 1920, .height = 1080},
                                 .work_area = {.x = 0, .y = 0, .width = 1920, .height = 1040},
                                 .content_scale = 1.25F,
                                 .current_mode = {.size = {.width = 1920, .height = 1080, .depth = 1}, .refresh_rate = 60.0F, .pixel_density = 1.0F},
                                 .primary = true};
    }

    [[nodiscard]] auto video_modes(MonitorHandle monitor) const -> std::vector<VideoMode> override
    {
        return {monitor_properties(monitor).current_mode};
    }

    [[nodiscard]] auto clipboard_text() const -> std::string override
    {
        return m_clipboard;
    }

    void set_clipboard_text(std::string_view text) override
    {
        m_clipboard = text;
    }

    [[nodiscard]] auto clipboard_image() const -> DecodedImage override
    {
        throw_unsupported("Platform::clipboard_image", "clipboard images", "the fake clipboard holds text only",
                          "check supports(PlatformFeature::CLIPBOARD_IMAGE) first");
    }

    void set_clipboard_image(const ImageData& image) override
    {
        Platform::set_clipboard_image(image);
    }

    [[nodiscard]] auto input_devices() noexcept -> InputDevices& override
    {
        return m_devices;
    }

    // ----------------------------------------------------------------------------------------- test controls

    /// @brief An event the next pump emits, as if the system reported it.
    void queue_native_event(const Event& event,
                            std::string text = {})
    {
        m_native.emplace_back(event, std::move(text));
    }

    /// @brief Calls @p action during the next pump, after the scripted events (e.g. to destroy a window).
    void during_next_pump(std::move_only_function<void()> action)
    {
        m_during_pump = std::move(action);
    }

    /// @brief Number of pumps so far.
    [[nodiscard]] auto pump_count() const noexcept -> int
    {
        return m_pump_count;
    }

    /// @brief How the last pump was asked to wait.
    [[nodiscard]] auto last_wait_mode() const noexcept -> EventWaitMode
    {
        return m_last_mode;
    }

    /// @brief Native window releases run so far (deferred to the pump after the window's destruction).
    [[nodiscard]] auto released_windows() const noexcept -> int
    {
        return m_released_windows;
    }

    // ----------------------------------------------------------------------------------------- used by FakeWindow

    // Defined in fake_platform.cpp: HandleTable::find() and remove() instantiated in a module interface
    // make its module file unreadable for importers (toolchain defect 10).
    [[nodiscard]] auto register_window(FakeWindow& window) -> WindowHandle;
    void unregister_window(WindowHandle window) noexcept;

    void count_release() noexcept
    {
        ++m_released_windows;
    }

    [[nodiscard]] auto fake_monitor() const noexcept -> MonitorHandle
    {
        return m_monitor;
    }

protected:
    void pump(EventWaitMode mode,
              std::chrono::nanoseconds timeout) override
    {
        ++m_pump_count;
        m_last_mode = mode;

        // Wait for scripted events or a wake.
        if (mode != EventWaitMode::POLL && m_native.empty())
        {
            // Plain loops, not predicates: a capturing lambda here would hit toolchain defect 6.
            std::unique_lock lock{m_wait_mutex};
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!m_woken)
            {
                if (mode == EventWaitMode::WAIT)
                {
                    m_wake.wait(lock);
                }
                else if (m_wake.wait_until(lock, deadline) == std::cv_status::timeout)
                {
                    break;
                }
            }
            m_woken = false;
        }

        // The scripted events, then the scripted action.
        const std::vector<std::pair<Event, std::string>> native = std::exchange(m_native, {});
        for (const std::pair<Event, std::string>& scripted : native)
        {
            if (scripted.first.text())
            {
                emit(scripted.first, scripted.second);
            }
            else
            {
                emit(scripted.first);
            }
        }
        if (m_during_pump)
        {
            std::exchange(m_during_pump, {})();
        }
    }

    void wake_backend() noexcept override
    {
        {
            const std::scoped_lock lock{m_wait_mutex};
            m_woken = true;
        }
        m_wake.notify_all();
    }

    [[nodiscard]] auto is_window_alive(WindowHandle window) const noexcept -> bool override;

private:
    gfx::backends::HandleTable<WindowTag, FakeWindowRecord> m_windows{PlatformBackendKind::SDL3};
    FakeInputDevices m_devices;
    MonitorHandle m_monitor{};
    std::uint32_t m_next_cursor = 0;
    std::string m_clipboard;
    std::vector<std::pair<Event, std::string>> m_native;
    std::move_only_function<void()> m_during_pump;
    int m_pump_count = 0;
    EventWaitMode m_last_mode = EventWaitMode::POLL;
    int m_destroyed_windows = 0;
    int m_released_windows = 0;
    std::mutex m_wait_mutex;
    std::condition_variable m_wake;
    bool m_woken = false;
};

// ------------------------------------------------------------------------------------------------- FakeWindow

/// The deferred "native release" of a fake window: counted by the platform.
struct CountRelease
{
    FakePlatform* platform = nullptr;

    void operator()() const
    {
        platform->count_release();
    }
};

FakeWindow::FakeWindow(FakePlatform& platform,
                       const WindowDescription& description)
    : Window{platform.validation_level()}
    , m_platform{platform}
    , m_title(description.title)
{
    m_handle = platform.register_window(*this);
    set_native_handle(NativeWindowHandle{Win32WindowHandle{.instance = 0x400000, .window = 0x1000U + m_handle.index(handle_key())}});
    update_size(description.size, Extent{.width = description.size.width * 2, .height = description.size.height * 2, .depth = 1});
    update_content_scale(2.0F);
    m_visible = !has_any(description.flags, WindowFlag::HIDDEN);
    if (!description.icon.empty())
    {
        set_icon(description.icon);
    }
}

FakeWindow::~FakeWindow()
{
    // The handle dies now; the "native window" is released at the next pump.
    m_platform.unregister_window(m_handle);
    m_platform.defer_release(handle_key(), CountRelease{&m_platform});
}

auto
FakeWindow::monitor() const -> MonitorHandle
{
    return m_platform.fake_monitor();
}

} // namespace gfx::tests
