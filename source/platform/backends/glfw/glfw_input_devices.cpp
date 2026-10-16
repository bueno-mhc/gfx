/// @file glfw_input_devices.cpp
/// @brief GLFW gamepads (GLFW's gamepad mappings) and joysticks: GLFW reports connections; their
///        input is polled at each pump and reported as the differences since the last pump.
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

/// The USB vendor and product of joystick @p joystick, from its SDL-style GUID (0 when the GUID does
/// not carry them, e.g. XInput devices).
auto
usb_identifiers(int joystick) noexcept -> std::pair<std::uint16_t, std::uint16_t>
{
    // The GUID is 16 bytes in hexadecimal: bus, CRC, vendor, 0, product, 0, version, ... (little endian).
    const char* const guid = glfwGetJoystickGUID(joystick);
    const std::string_view text = guid != nullptr ? std::string_view{guid} : std::string_view{};
    if (text.size() != 32)
    {
        return {0, 0};
    }
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const auto [end, error] = std::from_chars(text.data() + (index * 2), text.data() + (index * 2) + 2, bytes[index], 16);
        if (error != std::errc{} || end != text.data() + (index * 2) + 2)
        {
            return {0, 0};
        }
    }
    if (bytes[6] != 0 || bytes[7] != 0 || bytes[10] != 0 || bytes[11] != 0)
    {
        return {0, 0};
    }
    const auto word = [&bytes](std::size_t index)
    {
        return static_cast<std::uint16_t>(bytes[index] | (bytes[index + 1] << 8U));
    };
    return {word(4), word(8)};
}

/// The current axes, buttons and hats of joystick @p joystick.
void
read_joystick(int joystick,
              JoystickRecord& record)
{
    int count = 0;
    const float* const axes = glfwGetJoystickAxes(joystick, &count);
    record.axes.assign(axes, axes + (axes != nullptr ? count : 0));
    const unsigned char* const buttons = glfwGetJoystickButtons(joystick, &count);
    record.buttons.assign(buttons, buttons + (buttons != nullptr ? count : 0));
    const unsigned char* const hats = glfwGetJoystickHats(joystick, &count);
    record.hats.assign(hats, hats + (hats != nullptr ? count : 0));
}

} // namespace

GlfwInputDevices::GlfwInputDevices(GlfwPlatform& platform)
    : InputDevices{platform.validation_level()}
    , m_platform{platform}
{
}

auto
GlfwInputDevices::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::GLFW;
}

void
GlfwInputDevices::connect(int joystick,
                          std::chrono::nanoseconds time)
{
    // Once per device: as a gamepad when GLFW has a mapping for it, as a joystick otherwise.
    const auto slot = static_cast<std::size_t>(joystick);
    if (m_gamepad_slots[slot].is_valid() || m_joystick_slots[slot].is_valid())
    {
        return;
    }
    if (glfwJoystickIsGamepad(joystick) == GLFW_TRUE)
    {
        GamepadRecord record{.joystick = joystick};
        static_cast<void>(glfwGetGamepadState(joystick, &record.state));
        m_gamepad_slots[slot] = m_gamepads.insert(record);
        m_platform.deliver(Event::make<EventType::GAMEPAD_CONNECTED>(time, GamepadDeviceEvent{.gamepad = m_gamepad_slots[slot]}));
        return;
    }
    JoystickRecord record{.joystick = joystick};
    read_joystick(joystick, record);
    m_joystick_slots[slot] = m_joysticks.insert(std::move(record));
    m_platform.deliver(Event::make<EventType::JOYSTICK_CONNECTED>(time, JoystickDeviceEvent{.joystick = m_joystick_slots[slot]}));
}

void
GlfwInputDevices::disconnect(int joystick,
                             std::chrono::nanoseconds time)
{
    // Reported with its handle, which goes stale.
    const auto slot = static_cast<std::size_t>(joystick);
    if (const GamepadHandle gamepad = std::exchange(m_gamepad_slots[slot], GamepadHandle{}); gamepad.is_valid())
    {
        static_cast<void>(m_gamepads.remove(gamepad));
        m_platform.deliver(Event::make<EventType::GAMEPAD_DISCONNECTED>(time, GamepadDeviceEvent{.gamepad = gamepad}));
    }
    if (const JoystickHandle device = std::exchange(m_joystick_slots[slot], JoystickHandle{}); device.is_valid())
    {
        static_cast<void>(m_joysticks.remove(device));
        m_platform.deliver(Event::make<EventType::JOYSTICK_DISCONNECTED>(time, JoystickDeviceEvent{.joystick = device}));
    }
}

void
GlfwInputDevices::poll(std::chrono::nanoseconds time)
{
    for (std::size_t slot = 0; slot < m_gamepad_slots.size(); ++slot)
    {
        // Gamepads: buttons and axes that changed since the last pump.
        if (GamepadRecord* const record = m_gamepads.find(m_gamepad_slots[slot]))
        {
            GLFWgamepadstate state{};
            if (glfwGetGamepadState(record->joystick, &state) != GLFW_TRUE)
            {
                continue;
            }
            for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button)
            {
                const auto index = static_cast<std::size_t>(button);
                if (state.buttons[index] != record->state.buttons[index])
                {
                    const GamepadButtonEvent payload{.gamepad = m_gamepad_slots[slot], .button = to_gamepad_button(button)};
                    m_platform.deliver(state.buttons[index] == GLFW_PRESS ? Event::make<EventType::GAMEPAD_BUTTON_DOWN>(time, payload)
                                                                          : Event::make<EventType::GAMEPAD_BUTTON_UP>(time, payload));
                }
            }
            for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis)
            {
                const auto index = static_cast<std::size_t>(axis);
                if (state.axes[index] != record->state.axes[index])
                {
                    m_platform.deliver(Event::make<EventType::GAMEPAD_AXIS_MOTION>(
                        time, GamepadAxisEvent{.gamepad = m_gamepad_slots[slot],
                                               .axis = static_cast<GamepadAxis>(axis),
                                               .value = gamepad_axis_value(axis, state.axes[index])}));
                }
            }
            record->state = state;
        }

        // Joysticks: axes, buttons and hats that changed since the last pump.
        if (JoystickRecord* const record = m_joysticks.find(m_joystick_slots[slot]))
        {
            JoystickRecord now{.joystick = record->joystick};
            read_joystick(record->joystick, now);
            for (std::size_t index = 0; index < std::min(now.axes.size(), record->axes.size()); ++index)
            {
                if (now.axes[index] != record->axes[index])
                {
                    m_platform.deliver(Event::make<EventType::JOYSTICK_AXIS_MOTION>(
                        time, JoystickAxisEvent{.joystick = m_joystick_slots[slot], .axis = static_cast<std::uint32_t>(index), .value = now.axes[index]}));
                }
            }
            for (std::size_t index = 0; index < std::min(now.buttons.size(), record->buttons.size()); ++index)
            {
                if (now.buttons[index] != record->buttons[index])
                {
                    const JoystickButtonEvent payload{.joystick = m_joystick_slots[slot], .button = static_cast<std::uint32_t>(index)};
                    m_platform.deliver(now.buttons[index] == GLFW_PRESS ? Event::make<EventType::JOYSTICK_BUTTON_DOWN>(time, payload)
                                                                        : Event::make<EventType::JOYSTICK_BUTTON_UP>(time, payload));
                }
            }
            for (std::size_t index = 0; index < std::min(now.hats.size(), record->hats.size()); ++index)
            {
                if (now.hats[index] != record->hats[index])
                {
                    m_platform.deliver(Event::make<EventType::JOYSTICK_HAT_MOTION>(
                        time, JoystickHatEvent{.joystick = m_joystick_slots[slot],
                                               .hat = static_cast<std::uint32_t>(index),
                                               .position = static_cast<HatPosition>(now.hats[index] & 0x0FU)}));
                }
            }
            *record = std::move(now);
        }
    }
}

auto
GlfwInputDevices::require_gamepad(GamepadHandle gamepad,
                                  std::string_view operation,
                                  std::source_location location) const -> int
{
    m_platform.check_thread(operation, location);
    return m_gamepads.get(gamepad, operation, location).joystick;
}

auto
GlfwInputDevices::gamepads() const -> std::vector<GamepadHandle>
{
    m_platform.check_thread("InputDevices::gamepads");
    std::vector<GamepadHandle> handles;
    m_gamepads.for_each([&handles](GamepadHandle handle, const GamepadRecord& /*record*/)
    {
        handles.push_back(handle);
    });
    return handles;
}

auto
GlfwInputDevices::gamepad_properties(GamepadHandle gamepad) const -> GamepadProperties
{
    // GLFW knows the name and the USB identifiers, not the family, motors, lights or touchpads.
    const int joystick = require_gamepad(gamepad, "InputDevices::gamepad_properties");
    const char* const name = glfwGetGamepadName(joystick);
    const auto [vendor, product] = usb_identifiers(joystick);
    return GamepadProperties{
        .handle = gamepad,
        .name = name != nullptr ? name : "",
        .type = GamepadType::UNKNOWN,
        .vendor_identifier = vendor,
        .product_identifier = product,
        .touchpad_count = 0,
        .has_rumble = false,
        .has_trigger_rumble = false,
        .has_led = false,
    };
}

auto
GlfwInputDevices::is_gamepad_button_down(GamepadHandle gamepad,
                                         GamepadButton button) const -> bool
{
    // Buttons GLFW's gamepad model lacks (paddles, touchpad, miscellaneous) are never down.
    const int joystick = require_gamepad(gamepad, "InputDevices::is_gamepad_button_down");
    const int index = to_glfw(button);
    GLFWgamepadstate state{};
    return index >= 0 && glfwGetGamepadState(joystick, &state) == GLFW_TRUE && state.buttons[static_cast<std::size_t>(index)] == GLFW_PRESS;
}

auto
GlfwInputDevices::gamepad_axis(GamepadHandle gamepad,
                               GamepadAxis axis) const -> float
{
    const int joystick = require_gamepad(gamepad, "InputDevices::gamepad_axis");
    GLFWgamepadstate state{};
    if (glfwGetGamepadState(joystick, &state) != GLFW_TRUE)
    {
        return 0.0F;
    }
    const int index = std::to_underlying(axis);
    return gamepad_axis_value(index, state.axes[static_cast<std::size_t>(index)]);
}

void
GlfwInputDevices::set_gamepad_rumble(GamepadHandle /*gamepad*/,
                                     float /*low_frequency*/,
                                     float /*high_frequency*/,
                                     std::chrono::milliseconds /*duration*/)
{
    throw_unsupported("InputDevices::set_gamepad_rumble", "gamepad rumble", "GLFW 3.4 has no force feedback",
                      "check supports(PlatformFeature::GAMEPAD_RUMBLE) first, or use the SDL3 backend");
}

void
GlfwInputDevices::set_gamepad_trigger_rumble(GamepadHandle /*gamepad*/,
                                             float /*left*/,
                                             float /*right*/,
                                             std::chrono::milliseconds /*duration*/)
{
    throw_unsupported("InputDevices::set_gamepad_trigger_rumble", "trigger rumble", "GLFW 3.4 has no force feedback",
                      "check supports(PlatformFeature::GAMEPAD_TRIGGER_RUMBLE) first, or use the SDL3 backend");
}

void
GlfwInputDevices::set_gamepad_led(GamepadHandle /*gamepad*/,
                                  LedColor /*color*/)
{
    throw_unsupported("InputDevices::set_gamepad_led", "gamepad light bar", "GLFW 3.4 does not drive gamepad lights",
                      "check supports(PlatformFeature::GAMEPAD_LED) first, or use the SDL3 backend");
}

auto
GlfwInputDevices::joysticks() const -> std::vector<JoystickHandle>
{
    m_platform.check_thread("InputDevices::joysticks");
    std::vector<JoystickHandle> handles;
    m_joysticks.for_each([&handles](JoystickHandle handle, const JoystickRecord& /*record*/)
    {
        handles.push_back(handle);
    });
    return handles;
}

auto
GlfwInputDevices::joystick_properties(JoystickHandle joystick) const -> JoystickProperties
{
    constexpr std::string_view OPERATION = "InputDevices::joystick_properties";
    m_platform.check_thread(OPERATION);
    const JoystickRecord& record = m_joysticks.get(joystick, OPERATION);
    const char* const name = glfwGetJoystickName(record.joystick);
    const auto [vendor, product] = usb_identifiers(record.joystick);
    return JoystickProperties{
        .handle = joystick,
        .name = name != nullptr ? name : "",
        .vendor_identifier = vendor,
        .product_identifier = product,
        .axis_count = static_cast<std::uint32_t>(record.axes.size()),
        .button_count = static_cast<std::uint32_t>(record.buttons.size()),
        .hat_count = static_cast<std::uint32_t>(record.hats.size()),
    };
}

auto
GlfwInputDevices::touch_devices() const -> std::vector<TouchDeviceHandle>
{
    throw_unsupported("InputDevices::touch_devices", "touch input", "GLFW 3.4 does not report touch input",
                      "check supports(PlatformFeature::TOUCH) first, or use the SDL3 backend");
}

auto
GlfwInputDevices::touch_device_properties(TouchDeviceHandle /*device*/) const -> TouchDeviceProperties
{
    throw_unsupported("InputDevices::touch_device_properties", "touch input", "GLFW 3.4 does not report touch input",
                      "check supports(PlatformFeature::TOUCH) first, or use the SDL3 backend");
}

} // namespace gfx::platform::glfw
