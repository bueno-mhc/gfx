/// @file ui_platform.cppm
/// @brief gfx.ui.platform: the UI on the gfx platform library. UiPlatformInput turns a window's events into the
///        UiFrameInput of a frame (positions in UI units: pixels divided by the window's content scale);
///        UiWindowServices implements UiPlatformServices with the platform's clipboard and cursors and the window's
///        text input; the UI's navigation context (ui_navigation_context()) makes its keymap and gamepad navigation
///        rebindable actions of the input system (navigation_input() reads them); UiAccessibilityAdapter gives the
///        accessibility tree to screen readers through AccessKit. Built when the platform library is (target
///        gfx::ui_platform); gfx::ui does not need it.
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
    std::string m_composition{};
    std::int32_t m_composition_cursor = 0;
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

// ================================================================================================= navigation

/// @brief The number of the UI's navigation actions: one per UiNavigationCommand, scrolling and moving.
inline constexpr std::size_t UI_NAVIGATION_ACTION_COUNT = UI_NAVIGATION_ACTIONS.size() + 2;

/// @brief The UI's navigation actions for platform::make_input_schema(): UI_NAVIGATION_ACTIONS (buttons, one per
///        UiNavigationCommand), UI_SCROLL_ACTION and UI_MOVE_ACTION (two-dimensional).
[[nodiscard]] consteval auto
ui_navigation_actions() -> std::array<platform::ActionDefinition, UI_NAVIGATION_ACTION_COUNT>
{
    std::array<platform::ActionDefinition, UI_NAVIGATION_ACTION_COUNT> result{};
    for (std::size_t index = 0; index < UI_NAVIGATION_ACTIONS.size(); ++index)
    {
        result[index] = platform::action(UI_NAVIGATION_ACTIONS[index]);
    }
    result[UI_NAVIGATION_ACTIONS.size()] = platform::action(UI_SCROLL_ACTION, platform::ActionValueType::AXIS_2D);
    result[UI_NAVIGATION_ACTIONS.size() + 1] = platform::action(UI_MOVE_ACTION, platform::ActionValueType::AXIS_2D);
    return result;
}

/// @brief The application's @p actions followed by the UI's navigation actions, for platform::make_input_schema().
template<std::size_t COUNT>
[[nodiscard]] consteval auto
with_ui_navigation_actions(const std::array<platform::ActionDefinition, COUNT>& actions)
    -> std::array<platform::ActionDefinition, COUNT + UI_NAVIGATION_ACTION_COUNT>
{
    std::array<platform::ActionDefinition, COUNT + UI_NAVIGATION_ACTION_COUNT> result{};
    std::ranges::copy(actions, result.begin());
    std::ranges::copy(ui_navigation_actions(), result.begin() + COUNT);
    return result;
}

/// @brief How the UI's navigation context takes part by default: above gameplay's contexts, the inputs it binds
///        hidden from them (activate it while the UI is navigated).
inline constexpr platform::ContextDescription UI_NAVIGATION_CONTEXT_DESCRIPTION{.priority = 1000, .consumes_input = true};

/// @cond
namespace navigation_bindings
{

/// The identifier of navigation action @p command.
[[nodiscard]] consteval auto
action_of(UiNavigationCommand command) -> platform::ActionIdentifier
{
    return platform::action(UI_NAVIGATION_ACTIONS[static_cast<std::size_t>(std::countr_zero(std::to_underlying(command)))]).identifier();
}

/// A gamepad's bindings: the face buttons, the d-pad and the left stick, the shoulders, the right stick.
[[nodiscard]] consteval auto
gamepad()
{
    // Types are named in full: a using-declaration of another module's enumeration in a function of a module
    // interface crashes GCC 16 (toolchain defect 12).
    using platform::bind;
    return std::array{
        bind(action_of(UiNavigationCommand::ACTIVATE), platform::GamepadButton::SOUTH),
        bind(action_of(UiNavigationCommand::CANCEL), platform::GamepadButton::EAST),
        bind(action_of(UiNavigationCommand::INPUT), platform::GamepadButton::NORTH),
        bind(action_of(UiNavigationCommand::MENU), platform::GamepadButton::WEST),
        bind(action_of(UiNavigationCommand::CONTEXT_MENU), platform::GamepadButton::BACK),
        bind(action_of(UiNavigationCommand::MOVE_LEFT), platform::GamepadButton::DPAD_LEFT),
        bind(action_of(UiNavigationCommand::MOVE_RIGHT), platform::GamepadButton::DPAD_RIGHT),
        bind(action_of(UiNavigationCommand::MOVE_UP), platform::GamepadButton::DPAD_UP),
        bind(action_of(UiNavigationCommand::MOVE_DOWN), platform::GamepadButton::DPAD_DOWN),
        bind(platform::action(UI_MOVE_ACTION).identifier(), platform::stick(platform::GamepadAxis::LEFT_X, platform::GamepadAxis::LEFT_Y)),
        bind(action_of(UiNavigationCommand::NEXT_WINDOW), platform::GamepadButton::RIGHT_SHOULDER),
        bind(action_of(UiNavigationCommand::PREVIOUS_WINDOW), platform::GamepadButton::LEFT_SHOULDER),
        bind(platform::action(UI_SCROLL_ACTION).identifier(),
             platform::stick(platform::GamepadAxis::RIGHT_X, platform::GamepadAxis::RIGHT_Y).with_deadzone(platform::DeadzoneKind::SCALED_RADIAL, 0.2F)),
    };
}

/// The keyboard's bindings: the UI's keymap (UiInputSettings::keyboard_navigation).
[[nodiscard]] consteval auto
keyboard()
{
    using platform::bind;
    using platform::chord;
    using platform::single;
    return std::array{
        bind(action_of(UiNavigationCommand::ACTIVATE), Key::SPACE),
        bind(action_of(UiNavigationCommand::ACTIVATE), Key::RETURN),
        bind(action_of(UiNavigationCommand::ACTIVATE), Key::KEYPAD_ENTER),
        bind(action_of(UiNavigationCommand::CANCEL), Key::ESCAPE),
        bind(action_of(UiNavigationCommand::INPUT), Key::RETURN),
        bind(action_of(UiNavigationCommand::INPUT), Key::KEYPAD_ENTER),
        bind(action_of(UiNavigationCommand::MENU), single(Key::LEFT_ALT).tapped_within(std::chrono::milliseconds{UI_MENU_TAP_MILLISECONDS})),
        bind(action_of(UiNavigationCommand::CONTEXT_MENU), Key::APPLICATION),
        bind(action_of(UiNavigationCommand::CONTEXT_MENU), chord(Key::F10, KeyModifier::SHIFT)),
        bind(action_of(UiNavigationCommand::MOVE_LEFT), Key::LEFT),
        bind(action_of(UiNavigationCommand::MOVE_RIGHT), Key::RIGHT),
        bind(action_of(UiNavigationCommand::MOVE_UP), Key::UP),
        bind(action_of(UiNavigationCommand::MOVE_DOWN), Key::DOWN),
        bind(action_of(UiNavigationCommand::NEXT), chord(Key::TAB, KeyModifier::NONE)),
        bind(action_of(UiNavigationCommand::PREVIOUS), chord(Key::TAB, KeyModifier::SHIFT)),
        bind(action_of(UiNavigationCommand::NEXT_WINDOW), chord(Key::TAB, KeyModifier::CONTROL)),
        bind(action_of(UiNavigationCommand::PREVIOUS_WINDOW), chord(Key::TAB, KeyModifier::CONTROL | KeyModifier::SHIFT)),
    };
}

/// A context named UI_NAVIGATION_CONTEXT of @p description with @p bindings.
template<std::size_t COUNT>
[[nodiscard]] consteval auto
context_of(platform::ContextDescription description,
           const std::array<platform::ContextBinding, COUNT>& bindings) -> platform::ContextBlock<COUNT>
{
    return platform::ContextBlock<COUNT>{.definition = {.name = UI_NAVIGATION_CONTEXT, .description = description}, .bindings = bindings};
}

/// @p first then @p second.
template<std::size_t FIRST, std::size_t SECOND>
[[nodiscard]] consteval auto
joined(const std::array<platform::ContextBinding, FIRST>& first,
       const std::array<platform::ContextBinding, SECOND>& second) -> std::array<platform::ContextBinding, FIRST + SECOND>
{
    std::array<platform::ContextBinding, FIRST + SECOND> result{};
    std::ranges::copy(first, result.begin());
    std::ranges::copy(second, result.begin() + FIRST);
    return result;
}

} // namespace navigation_bindings
/// @endcond

/// @brief The UI's navigation context (UI_NAVIGATION_CONTEXT) for platform::make_input_schema(), with its default
///        bindings, rebindable and saved like the application's: the keyboard's (the keymap the UI applies itself
///        with UiInputSettings::keyboard_navigation: turn that setting off when using this context) and a gamepad's
///        (south: ACTIVATE, east: CANCEL, north: INPUT, west: MENU, back: CONTEXT_MENU, the d-pad and the left
///        stick: the moves, the shoulders: the windows, the right stick: scrolling). Activate it while the UI is
///        navigated; give navigation_input() to each frame.
///
/// @code
/// constexpr auto SCHEMA = gfx::platform::make_input_schema(
///     gfx::ui::with_ui_navigation_actions(gfx::platform::actions(gfx::platform::action("jump"))),
///     gfx::platform::context("gameplay", gfx::platform::bind("jump"_action, gfx::Key::SPACE)),
///     gfx::ui::ui_navigation_context());
/// @endcode
[[nodiscard]] consteval auto
ui_navigation_context(platform::ContextDescription description = UI_NAVIGATION_CONTEXT_DESCRIPTION)
{
    return navigation_bindings::context_of(description, navigation_bindings::joined(navigation_bindings::keyboard(), navigation_bindings::gamepad()));
}

/// @brief The UI's navigation context with a gamepad's bindings only (the keyboard stays the UI's own keymap,
///        UiInputSettings::keyboard_navigation).
[[nodiscard]] consteval auto
ui_gamepad_navigation_context(platform::ContextDescription description = UI_NAVIGATION_CONTEXT_DESCRIPTION)
{
    return navigation_bindings::context_of(description, navigation_bindings::gamepad());
}

/// @brief The navigation commands of @p frame for @p player, as UiFrameInput::navigation takes them: the actions
///        held and just pressed, the moving stick's direction held past UI_STICK_MOVE_THRESHOLD, the scrolling stick
///        (+y downwards).
/// @throws InvalidUsageError when the frame's schema lacks the UI's navigation actions.
[[nodiscard]] auto navigation_input(const platform::InputFrame& frame,
                                    platform::PlayerIdentifier player = {}) -> UiNavigationInput;

// ================================================================================================= accessibility

/// @brief Gives a UiContext's accessibility tree to the assistive technologies (screen readers) of the platform
///        through AccessKit, for one window, and their requests back to the UI. The tree is built only once an
///        assistive technology has asked for it (update() enables it then). Windows only for now (UI Automation).
///
/// @code
/// gfx::ui::UiAccessibilityAdapter accessibility{*window};
/// // Each frame:
/// gfx::ui::UiFrameInput input = ui_input.frame_input(seconds);
/// input.accessibility_requests = accessibility.take_requests();
/// gfx::ui::UiFrame& frame = ui.begin_frame(input);
/// // ... windows and widgets ...
/// static_cast<void>(ui.end_frame());
/// accessibility.update(ui);
/// @endcode
class UiAccessibilityAdapter
{
public:
    /// @brief An adapter of @p window (which must outlive it), made on the window's thread (the platform's).
    /// @throws UiCapabilityError (ACCESSIBILITY) where no AccessKit adapter is built, or for a window without a
    ///         native handle; BackendApiError when the window cannot be subclassed.
    explicit UiAccessibilityAdapter(platform::Window& window);

    UiAccessibilityAdapter(const UiAccessibilityAdapter&) = delete;
    auto operator=(const UiAccessibilityAdapter&) -> UiAccessibilityAdapter& = delete;
    ~UiAccessibilityAdapter();

    /// @brief Whether an assistive technology has asked for the tree.
    [[nodiscard]] auto active() const noexcept -> bool;

    /// @brief After each end_frame(), on the window's thread: once active, enables @p context's tree and gives
    ///        AccessKit what changed (every node the first time, or after a frame it was not given).
    /// @throws InvalidUsageError on another thread.
    void update(UiContext& context);

    /// @brief The requests assistive technologies made since the last call (valid until the next one), for the
    ///        next frame's UiFrameInput::accessibility_requests. Requests arrive on any thread.
    [[nodiscard]] auto take_requests() -> std::span<const UiAccessibilityRequest>;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace gfx::ui
