// A UiContext for tests: the test font, in-memory platform services, and synthetic input (mouse, keys, text,
// focus, time, navigation commands, requests of assistive technologies) accumulated between frames and given to
// the next one.
export module gfx.tests.ui_context_support;

import std;
import gfx.ui;
import gfx.tests.context_latin;

export namespace gfx::tests
{

/// A UiContext driven frame by frame with synthetic input.
class UiHarness
{
public:
    /// The display the frames cover, UI units.
    static constexpr gfx::Vector2 DISPLAY{.x = 800.0F, .y = 600.0F};

    /// A context (with the test font at 16 units per em unless @p with_font is false).
    explicit UiHarness(bool with_font = true,
                       const gfx::ui::UiContextDescription& description = {})
        : m_context{with_services(description, m_services)}
    {
        if (with_font)
        {
            m_font = m_context.load_font(gfx::ui::FontAtlas::load(gfx::tests::context_atlases::context_latin), 16.0F);
        }
    }

    /// The context.
    [[nodiscard]] auto context() noexcept -> gfx::ui::UiContext&
    {
        return m_context;
    }

    /// The services the context uses (the cursor and text input it asked for).
    [[nodiscard]] auto services() noexcept -> gfx::ui::UiNullPlatformServices&
    {
        return m_services;
    }

    /// The test font.
    [[nodiscard]] auto font() const noexcept -> gfx::ui::FontHandle
    {
        return m_font;
    }

    /// Moves the mouse to (@p x, @p y).
    void move(float x,
              float y) noexcept
    {
        m_mouse = {.x = x, .y = y};
        m_present = true;
    }

    /// The mouse leaves the display.
    void leave() noexcept
    {
        m_present = false;
    }

    /// Presses @p button where the mouse is.
    void press(gfx::MouseButton button = gfx::MouseButton::LEFT)
    {
        m_buttons.push_back({.button = button, .down = true, .position = m_mouse});
        m_held = m_held | bit(button);
    }

    /// Releases @p button where the mouse is.
    void release(gfx::MouseButton button = gfx::MouseButton::LEFT)
    {
        m_buttons.push_back({.button = button, .down = false, .position = m_mouse});
        m_held = m_held & ~bit(button);
    }

    /// Turns the wheel by @p y notches (positive: away from the user) and @p x.
    void wheel(float y,
               float x = 0.0F) noexcept
    {
        m_wheel = {.x = m_wheel.x + x, .y = m_wheel.y + y};
    }

    /// A key event.
    void key(gfx::Key key,
             bool down,
             bool repeat = false)
    {
        m_keys.push_back({.key = key, .down = down, .repeat = repeat});
    }

    /// Presses and releases @p key in the next frame.
    void tap(gfx::Key key)
    {
        this->key(key, true);
        this->key(key, false);
    }

    /// Presses and releases the key of letter @p letter ('a' to 'z') of a US layout, whose key code is the letter:
    /// with Control held, a shortcut.
    void shortcut(char letter)
    {
        const auto key = static_cast<gfx::Key>(std::to_underlying(gfx::Key::A) + (letter - 'a'));
        const auto code = static_cast<gfx::KeyCode>(letter);
        m_keys.push_back({.key = key, .key_code = code, .down = true});
        m_keys.push_back({.key = key, .key_code = code, .down = false});
    }

    /// Presses and releases the key @p key whose symbol is @p code in the next frame.
    void tap_code(gfx::Key key,
                  gfx::KeyCode code)
    {
        m_keys.push_back({.key = key, .key_code = code, .down = true});
        m_keys.push_back({.key = key, .key_code = code, .down = false});
    }

    /// A key event of @p key whose symbol is @p code (a repeat when @p repeat).
    void key_code(gfx::Key key,
                  gfx::KeyCode code,
                  bool down,
                  bool repeat = false)
    {
        m_keys.push_back({.key = key, .key_code = code, .down = down, .repeat = repeat});
    }

    /// Navigation commands pressed in the next frame (from a gamepad, as UiFrameInput::navigation).
    void navigate(gfx::ui::UiNavigationCommand pressed) noexcept
    {
        m_navigation.pressed = m_navigation.pressed | pressed;
    }

    /// Navigation commands held from the next frame.
    void hold_navigation(gfx::ui::UiNavigationCommand down) noexcept
    {
        m_navigation.down = down;
    }

    /// The navigation scroll (a stick) from the next frame.
    void scroll_navigation(gfx::Vector2 scroll) noexcept
    {
        m_navigation.scroll = scroll;
    }

    /// A request of an assistive technology in the next frame (its text kept until then).
    void request(gfx::ui::AccessibilityAction action,
                 gfx::ui::AccessibilityNodeId target,
                 std::string_view text = {},
                 std::optional<double> number = std::nullopt)
    {
        m_request_texts.emplace_back(text);
        m_requests.push_back({.action = action, .target = target, .number = number.value_or(0.0), .has_number = number.has_value()});
    }

    /// The IME composition in progress from the next frame (empty: none), its cursor @p cursor code points in.
    void compose(std::string_view text,
                 std::int32_t cursor = 0)
    {
        m_composition.assign(text);
        m_composition_cursor = cursor;
    }

    /// The modifiers held from the next frame.
    void modifiers(gfx::KeyModifier modifiers) noexcept
    {
        m_modifiers = modifiers;
    }

    /// Text typed in the next frame.
    void type(std::string_view text)
    {
        m_text.append(text);
    }

    /// Whether the window has the keyboard focus from the next frame.
    void focus(bool focused) noexcept
    {
        m_focused = focused;
    }

    /// Pixels per UI unit from the next frame.
    void scale(float pixels_per_unit) noexcept
    {
        m_scale = pixels_per_unit;
    }

    /// Advances the clock by @p seconds more than the 1/60 s of each frame.
    void wait(double seconds) noexcept
    {
        m_time += seconds;
    }

    /// Runs a frame: the input accumulated since the last one, then @p body with the frame, then end_frame().
    template<class Body>
    auto frame(Body&& body) -> gfx::ui::UiFrameReport
    {
        for (std::size_t index = 0; index < m_requests.size(); ++index)
        {
            m_requests[index].text = m_request_texts[index];
        }
        const gfx::ui::UiFrameInput input{.time = m_time,
                                          .display_size = DISPLAY,
                                          .pixels_per_unit = m_scale,
                                          .focused = m_focused,
                                          .mouse_present = m_present,
                                          .mouse_position = m_mouse,
                                          .mouse_buttons = m_buttons,
                                          .mouse_buttons_held = m_held,
                                          .mouse_wheel = m_wheel,
                                          .keys = m_keys,
                                          .modifiers = m_modifiers,
                                          .text = m_text,
                                          .composition = m_composition,
                                          .composition_cursor = m_composition_cursor,
                                          .navigation = m_navigation,
                                          .accessibility_requests = m_requests};
        gfx::ui::UiFrame& frame = m_context.begin_frame(input);
        std::forward<Body>(body)(frame);
        const gfx::ui::UiFrameReport report = m_context.end_frame();
        m_buttons.clear();
        m_keys.clear();
        m_text.clear();
        m_wheel = {};
        m_navigation.pressed = gfx::ui::UiNavigationCommand::NONE;
        m_requests.clear();
        m_request_texts.clear();
        m_time += 1.0 / 60.0;
        return report;
    }

    /// Runs @p count frames of @p body.
    template<class Body>
    auto frames(std::uint32_t count,
                Body&& body) -> gfx::ui::UiFrameReport
    {
        gfx::ui::UiFrameReport report{};
        for (std::uint32_t frame = 0; frame < count; ++frame)
        {
            report = this->frame(body);
        }
        return report;
    }

    /// Clicks at (@p x, @p y): a frame with the mouse there, one pressing, one releasing (each running @p body).
    template<class Body>
    void click(float x,
               float y,
               Body&& body)
    {
        move(x, y);
        frame(body);
        press();
        frame(body);
        release();
        frame(body);
    }

private:
    static auto with_services(gfx::ui::UiContextDescription description,
                              gfx::ui::UiNullPlatformServices& services) -> gfx::ui::UiContextDescription
    {
        description.services = &services;
        return description;
    }

    static constexpr auto bit(gfx::MouseButton button) noexcept -> gfx::MouseButtonMask
    {
        return static_cast<gfx::MouseButtonMask>(1U << std::to_underlying(button));
    }

    gfx::ui::UiNullPlatformServices m_services{};
    gfx::ui::UiContext m_context;
    gfx::ui::FontHandle m_font{};
    std::vector<gfx::ui::UiMouseButtonEvent> m_buttons{};
    std::vector<gfx::ui::UiKeyEvent> m_keys{};
    std::string m_text{};
    std::string m_composition{};
    std::int32_t m_composition_cursor = 0;
    gfx::Vector2 m_mouse{};
    bool m_present = false;
    gfx::MouseButtonMask m_held = gfx::MouseButtonMask::NONE;
    gfx::Vector2 m_wheel{};
    gfx::ui::UiNavigationInput m_navigation{};
    std::vector<gfx::ui::UiAccessibilityRequest> m_requests{};
    std::deque<std::string> m_request_texts{};
    gfx::KeyModifier m_modifiers = gfx::KeyModifier::NONE;
    bool m_focused = true;
    float m_scale = 1.0F;
    double m_time = 1.0;
};

/// The center of @p rectangle.
[[nodiscard]] constexpr auto
center(const gfx::ui::UiRectangle& rectangle) noexcept -> gfx::Vector2
{
    return {.x = (rectangle.left + rectangle.right) * 0.5F, .y = (rectangle.top + rectangle.bottom) * 0.5F};
}

/// Whether @p report has an issue of @p problem (named @p name, when given).
[[nodiscard]] inline auto
has_issue(const gfx::ui::UiFrameReport& report,
          gfx::ui::UiFrameProblem problem,
          std::string_view name = {}) -> bool
{
    return std::ranges::any_of(report.issues, [&](const gfx::ui::UiFrameIssue& issue)
    {
        return issue.problem == problem && (name.empty() || issue.name() == name);
    });
}

} // namespace gfx::tests
