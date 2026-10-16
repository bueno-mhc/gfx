// Test support for the input system: synthetic events, and gamepad handles issued as a backend would
// (no gamepad is needed to test).
export module gfx.tests.input_support;

import std;
import gfx.platform;

export namespace gfx::tests
{

/// Issues gamepad handles, as a platform backend does.
class GamepadIssuer : public gfx::HandleIssuer
{
public:
    /// The handle of gamepad slot @p index.
    [[nodiscard]] static auto gamepad(std::uint32_t index) -> gfx::platform::GamepadHandle
    {
        return gfx::platform::GamepadHandle::create(handle_key(), index, 1);
    }
};

/// The events of one frame, built in order: `input.update(Events{}.key_down(Key::SPACE).span(), 16ms)`.
class Events
{
public:
    auto key_down(gfx::platform::Key key,
                  gfx::platform::KeyCode key_code = gfx::platform::KeyCode::UNKNOWN,
                  bool repeat = false) -> Events&
    {
        return keyboard<gfx::platform::EventType::KEY_DOWN>(key, key_code, repeat);
    }

    auto key_up(gfx::platform::Key key,
                gfx::platform::KeyCode key_code = gfx::platform::KeyCode::UNKNOWN) -> Events&
    {
        return keyboard<gfx::platform::EventType::KEY_UP>(key, key_code, false);
    }

    auto mouse_motion(gfx::Vector2 delta,
                      gfx::Vector2 position = {}) -> Events&
    {
        gfx::platform::MouseMotionEvent payload{};
        payload.delta = delta;
        payload.position = position;
        m_events.push_back(gfx::platform::Event::make<gfx::platform::EventType::MOUSE_MOTION>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    auto mouse_down(gfx::platform::MouseButton button) -> Events&
    {
        return mouse_button<gfx::platform::EventType::MOUSE_BUTTON_DOWN>(button);
    }

    auto mouse_up(gfx::platform::MouseButton button) -> Events&
    {
        return mouse_button<gfx::platform::EventType::MOUSE_BUTTON_UP>(button);
    }

    auto wheel(gfx::Vector2 delta) -> Events&
    {
        gfx::platform::MouseWheelEvent payload{};
        payload.delta = delta;
        m_events.push_back(gfx::platform::Event::make<gfx::platform::EventType::MOUSE_WHEEL>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    auto focus_lost() -> Events&
    {
        m_events.push_back(gfx::platform::Event::make<gfx::platform::EventType::WINDOW_FOCUS_LOST>(std::chrono::nanoseconds{},
                                                                                                    gfx::platform::WindowEvent{}));
        return *this;
    }

    auto gamepad_connected(gfx::platform::GamepadHandle gamepad) -> Events&
    {
        return gamepad_device<gfx::platform::EventType::GAMEPAD_CONNECTED>(gamepad);
    }

    auto gamepad_disconnected(gfx::platform::GamepadHandle gamepad) -> Events&
    {
        return gamepad_device<gfx::platform::EventType::GAMEPAD_DISCONNECTED>(gamepad);
    }

    auto gamepad_down(gfx::platform::GamepadHandle gamepad,
                      gfx::platform::GamepadButton button) -> Events&
    {
        return gamepad_button<gfx::platform::EventType::GAMEPAD_BUTTON_DOWN>(gamepad, button);
    }

    auto gamepad_up(gfx::platform::GamepadHandle gamepad,
                    gfx::platform::GamepadButton button) -> Events&
    {
        return gamepad_button<gfx::platform::EventType::GAMEPAD_BUTTON_UP>(gamepad, button);
    }

    auto gamepad_axis(gfx::platform::GamepadHandle gamepad,
                      gfx::platform::GamepadAxis axis,
                      float value) -> Events&
    {
        gfx::platform::GamepadAxisEvent payload{};
        payload.gamepad = gamepad;
        payload.axis = axis;
        payload.value = value;
        m_events.push_back(gfx::platform::Event::make<gfx::platform::EventType::GAMEPAD_AXIS_MOTION>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    /// The events, valid while this object lives.
    [[nodiscard]] auto span() const noexcept -> gfx::platform::EventSpan
    {
        return gfx::platform::EventSpan{m_events, {}};
    }

private:
    template<gfx::platform::EventType TYPE>
    auto keyboard(gfx::platform::Key key,
                  gfx::platform::KeyCode key_code,
                  bool repeat) -> Events&
    {
        gfx::platform::KeyboardEvent payload{};
        payload.key = key;
        payload.key_code = key_code;
        payload.repeat = repeat;
        m_events.push_back(gfx::platform::Event::make<TYPE>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    template<gfx::platform::EventType TYPE>
    auto mouse_button(gfx::platform::MouseButton button) -> Events&
    {
        gfx::platform::MouseButtonEvent payload{};
        payload.button = button;
        m_events.push_back(gfx::platform::Event::make<TYPE>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    template<gfx::platform::EventType TYPE>
    auto gamepad_device(gfx::platform::GamepadHandle gamepad) -> Events&
    {
        gfx::platform::GamepadDeviceEvent payload{};
        payload.gamepad = gamepad;
        m_events.push_back(gfx::platform::Event::make<TYPE>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    template<gfx::platform::EventType TYPE>
    auto gamepad_button(gfx::platform::GamepadHandle gamepad,
                        gfx::platform::GamepadButton button) -> Events&
    {
        gfx::platform::GamepadButtonEvent payload{};
        payload.gamepad = gamepad;
        payload.button = button;
        m_events.push_back(gfx::platform::Event::make<TYPE>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

private:
    std::vector<gfx::platform::Event> m_events;
};

} // namespace gfx::tests
