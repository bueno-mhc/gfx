/// @file input_devices.cppm
/// @brief InputDevices: the input devices a platform backend knows (gamepads, joysticks, touch devices)
///        and the services the input system needs from it (device names and types, current gamepad
///        state, rumble, LEDs). Device events arrive through the event pump; these functions describe
///        and drive the devices. Platform thread only.
export module gfx.platform.interface:input_devices;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import :platform_object;

export namespace gfx::platform
{

/// @brief What a gamepad is.
struct GamepadProperties
{
    GamepadHandle handle{};                  ///< The gamepad.
    std::string name{};                      ///< Human-readable name (UTF-8).
    GamepadType type = GamepadType::UNKNOWN; ///< Family (button labels differ).
    std::uint16_t vendor_identifier = 0;     ///< USB vendor ID (0 when unknown).
    std::uint16_t product_identifier = 0;    ///< USB product ID (0 when unknown).
    std::uint32_t touchpad_count = 0;        ///< Touchpads (GAMEPAD_TOUCHPAD_* events).
    bool has_rumble = false;                 ///< InputDevices::set_gamepad_rumble() works.
    bool has_trigger_rumble = false;         ///< InputDevices::set_gamepad_trigger_rumble() works.
    bool has_led = false;                    ///< InputDevices::set_gamepad_led() works.
};

/// @brief What a joystick (a device without gamepad mapping) is.
struct JoystickProperties
{
    JoystickHandle handle{};              ///< The joystick.
    std::string name{};                   ///< Human-readable name (UTF-8).
    std::uint16_t vendor_identifier = 0;  ///< USB vendor ID (0 when unknown).
    std::uint16_t product_identifier = 0; ///< USB product ID (0 when unknown).
    std::uint32_t axis_count = 0;         ///< Axes.
    std::uint32_t button_count = 0;       ///< Buttons.
    std::uint32_t hat_count = 0;          ///< Hats.
};

/// @brief What a touch device is.
struct TouchDeviceProperties
{
    TouchDeviceHandle handle{};                    ///< The touch device.
    std::string name{};                            ///< Human-readable name (UTF-8).
    TouchDeviceType type = TouchDeviceType::DIRECT; ///< Screen or pad.
};

/// @brief Color of a gamepad's light bar.
struct LedColor
{
    std::uint8_t red = 0;   ///< Red.
    std::uint8_t green = 0; ///< Green.
    std::uint8_t blue = 0;  ///< Blue.

    constexpr bool operator==(const LedColor&) const = default;
};

/// @brief The input devices of a platform. Owned by the Platform (Platform::input_devices()).
class InputDevices : public PlatformObject
{
public:
    /// @brief The gamepads connected now.
    [[nodiscard]] virtual auto gamepads() const -> std::vector<GamepadHandle> = 0;

    /// @brief What @p gamepad is.
    /// @throws InvalidHandleError when the gamepad was disconnected.
    [[nodiscard]] virtual auto gamepad_properties(GamepadHandle gamepad) const -> GamepadProperties = 0;

    /// @brief Whether @p button of @p gamepad is held now.
    [[nodiscard]] virtual auto is_gamepad_button_down(GamepadHandle gamepad,
                                                      GamepadButton button) const -> bool = 0;

    /// @brief Position of @p axis of @p gamepad now (sticks in [-1, 1], triggers in [0, 1]).
    [[nodiscard]] virtual auto gamepad_axis(GamepadHandle gamepad,
                                            GamepadAxis axis) const -> float = 0;

    /// @brief Rumbles @p gamepad (PlatformFeature::GAMEPAD_RUMBLE): motor strengths in [0, 1] for
    ///        @p duration (0 strengths stop it).
    virtual void set_gamepad_rumble(GamepadHandle gamepad,
                                    float low_frequency,
                                    float high_frequency,
                                    std::chrono::milliseconds duration) = 0;

    /// @brief Rumbles the triggers of @p gamepad (PlatformFeature::GAMEPAD_TRIGGER_RUMBLE).
    virtual void set_gamepad_trigger_rumble(GamepadHandle gamepad,
                                            float left,
                                            float right,
                                            std::chrono::milliseconds duration) = 0;

    /// @brief Sets the light bar of @p gamepad (PlatformFeature::GAMEPAD_LED).
    virtual void set_gamepad_led(GamepadHandle gamepad,
                                 LedColor color) = 0;

    /// @brief The joysticks connected now (devices without a gamepad mapping).
    [[nodiscard]] virtual auto joysticks() const -> std::vector<JoystickHandle> = 0;

    /// @brief What @p joystick is.
    /// @throws InvalidHandleError when the joystick was disconnected.
    [[nodiscard]] virtual auto joystick_properties(JoystickHandle joystick) const -> JoystickProperties = 0;

    /// @brief The touch devices known now (PlatformFeature::TOUCH).
    [[nodiscard]] virtual auto touch_devices() const -> std::vector<TouchDeviceHandle> = 0;

    /// @brief What @p device is.
    [[nodiscard]] virtual auto touch_device_properties(TouchDeviceHandle device) const -> TouchDeviceProperties = 0;

protected:
    using PlatformObject::PlatformObject;
};

// ------------------------------------------------------------------------------------------------- Default definitions

auto
InputDevices::gamepads() const -> std::vector<GamepadHandle>
{
    throw_not_implemented("InputDevices::gamepads");
}

auto
InputDevices::gamepad_properties(GamepadHandle /*gamepad*/) const -> GamepadProperties
{
    throw_not_implemented("InputDevices::gamepad_properties");
}

auto
InputDevices::is_gamepad_button_down(GamepadHandle /*gamepad*/,
                                     GamepadButton /*button*/) const -> bool
{
    throw_not_implemented("InputDevices::is_gamepad_button_down");
}

auto
InputDevices::gamepad_axis(GamepadHandle /*gamepad*/,
                           GamepadAxis /*axis*/) const -> float
{
    throw_not_implemented("InputDevices::gamepad_axis");
}

void
InputDevices::set_gamepad_rumble(GamepadHandle /*gamepad*/,
                                 float /*low_frequency*/,
                                 float /*high_frequency*/,
                                 std::chrono::milliseconds /*duration*/)
{
    throw_not_implemented("InputDevices::set_gamepad_rumble");
}

void
InputDevices::set_gamepad_trigger_rumble(GamepadHandle /*gamepad*/,
                                         float /*left*/,
                                         float /*right*/,
                                         std::chrono::milliseconds /*duration*/)
{
    throw_not_implemented("InputDevices::set_gamepad_trigger_rumble");
}

void
InputDevices::set_gamepad_led(GamepadHandle /*gamepad*/,
                              LedColor /*color*/)
{
    throw_not_implemented("InputDevices::set_gamepad_led");
}

auto
InputDevices::joysticks() const -> std::vector<JoystickHandle>
{
    throw_not_implemented("InputDevices::joysticks");
}

auto
InputDevices::joystick_properties(JoystickHandle /*joystick*/) const -> JoystickProperties
{
    throw_not_implemented("InputDevices::joystick_properties");
}

auto
InputDevices::touch_devices() const -> std::vector<TouchDeviceHandle>
{
    throw_not_implemented("InputDevices::touch_devices");
}

auto
InputDevices::touch_device_properties(TouchDeviceHandle /*device*/) const -> TouchDeviceProperties
{
    throw_not_implemented("InputDevices::touch_device_properties");
}

} // namespace gfx::platform
