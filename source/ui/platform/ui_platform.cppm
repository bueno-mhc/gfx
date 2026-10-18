/// @file ui_platform.cppm
/// @brief gfx.ui.platform: the UI on the gfx platform library. UiPlatformInput turns a window's events into the
///        UiFrameInput of a frame (positions in UI units: pixels divided by the window's content scale), and
///        UiWindowServices implements UiPlatformServices with the platform's clipboard and cursors and the window's
///        text input. Built when the platform library is (target gfx::ui_platform); gfx::ui does not need it.
export module gfx.ui.platform;

import std;
import gfx.platform;
import gfx.ui;

export namespace gfx::ui
{

/// @brief Collects the events of one window during a frame and gives them to the UI as a UiFrameInput.
///
/// @code
/// gfx::ui::UiPlatformInput ui_input{*window};
/// // Each frame:
/// ui_input.begin_frame();
/// ui_input.add_events(platform->poll_events());
/// gfx::ui::UiFrame& frame = ui.begin_frame(ui_input.frame_input(seconds));
/// @endcode
class UiPlatformInput
{
public:
    /// @brief Input of @p window (which must outlive it).
    explicit UiPlatformInput(const platform::Window& window);

    /// @brief Starts a frame: the previous frame's events are dropped (held keys and buttons stay held).
    void begin_frame() noexcept;

    /// @brief Adds the events of the window among @p events (other windows' events are skipped).
    void add_events(platform::EventSpan events);

    /// @brief The frame's input at @p time seconds (a monotonic clock): spans and text stay valid until
    ///        begin_frame().
    [[nodiscard]] auto frame_input(double time) const noexcept -> UiFrameInput;

    /// @brief Pixels per UI unit of the window (its content scale).
    [[nodiscard]] auto pixels_per_unit() const noexcept -> float;

    /// @brief @p position (window coordinates) in UI units.
    [[nodiscard]] auto to_units(Vector2 position) const noexcept -> Vector2;

private:
    const platform::Window* m_window = nullptr;
    std::vector<UiMouseButtonEvent> m_buttons{};
    std::vector<UiKeyEvent> m_keys{};
    std::string m_text{};
    Vector2 m_mouse{};
    bool m_mouse_present = false;
    MouseButtonMask m_held = MouseButtonMask::NONE;
    Vector2 m_wheel{};
    KeyModifier m_modifiers = KeyModifier::NONE;
    bool m_focused = true;
};

/// @brief UiPlatformServices of a window: the platform's clipboard, cursors made by the platform (one per shape,
///        on first use) shown over the window, and the window's text input.
class UiWindowServices final : public UiPlatformServices
{
public:
    /// @brief Services of @p window, of @p platform (both must outlive them).
    UiWindowServices(platform::Platform& platform,
                     platform::Window& window);

    [[nodiscard]] auto clipboard_text() -> std::string override;

    void set_clipboard_text(std::string_view text) override;

    void set_cursor(CursorShape shape) override;

    void set_text_input(bool active,
                        const UiRectangle& area) override;

    /// @brief Links cannot be opened through the platform library: always false.
    [[nodiscard]] auto open_url(std::string_view url) -> bool override;

private:
    platform::Platform* m_platform = nullptr;
    platform::Window* m_window = nullptr;
    std::array<std::unique_ptr<platform::Cursor>, ENUMERATOR_COUNT<CursorShape>> m_cursors{};
};

} // namespace gfx::ui
