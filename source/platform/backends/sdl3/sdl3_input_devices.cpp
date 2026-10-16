/// @file sdl3_input_devices.cpp
/// @brief SDL3 gamepads (SDL's gamepad mappings), joysticks without mapping, and touch devices.
module;

#include "sdl3_headers.hpp"

module gfx.platform.backends.sdl3;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::sdl3
{

namespace
{

/// Frees memory SDL allocated, when the holder goes.
struct SdlFree
{
    void operator()(void* memory) const noexcept
    {
        SDL_free(memory);
    }
};

/// A motor strength in [0, 1] as SDL's 16-bit intensity.
auto
to_intensity(float strength) noexcept -> Uint16
{
    return static_cast<Uint16>(std::lround(std::clamp(strength, 0.0F, 1.0F) * 65535.0F));
}

/// A rumble duration in SDL's milliseconds.
auto
to_duration(std::chrono::milliseconds duration) noexcept -> Uint32
{
    return static_cast<Uint32>(std::clamp<std::chrono::milliseconds::rep>(duration.count(), 0, std::numeric_limits<Uint32>::max()));
}

} // namespace

Sdl3InputDevices::Sdl3InputDevices(Sdl3Platform& platform)
    : InputDevices{platform.validation_level()}
    , m_platform{platform}
{
}

Sdl3InputDevices::~Sdl3InputDevices()
{
    close_all();
}

auto
Sdl3InputDevices::backend() const noexcept -> PlatformBackendKind
{
    return PlatformBackendKind::SDL3;
}

void
Sdl3InputDevices::open_connected_devices()
{
    // Gamepads, then joysticks without a gamepad mapping: opened now, reported when SDL's start-up
    // events for them are pumped (connect_*() then sees them unreported).
    bool first_report = false;
    int count = 0;
    const std::unique_ptr<SDL_JoystickID, SdlFree> gamepads{SDL_GetGamepads(&count)};
    for (int index = 0; gamepads && index < count; ++index)
    {
        if (GamepadRecord* const record = m_gamepads.find(connect_gamepad(gamepads.get()[index], first_report)))
        {
            record->announced = false;
        }
    }
    const std::unique_ptr<SDL_JoystickID, SdlFree> joysticks{SDL_GetJoysticks(&count)};
    for (int index = 0; joysticks && index < count; ++index)
    {
        if (JoystickRecord* const record = m_joysticks.find(connect_joystick(joysticks.get()[index], first_report)))
        {
            record->announced = false;
        }
    }
}

auto
Sdl3InputDevices::connect_gamepad(SDL_JoystickID instance,
                                  bool& first_report) -> GamepadHandle
{
    // Known: report it once. New: open it.
    if (const auto found = m_gamepad_instances.find(instance); found != m_gamepad_instances.end())
    {
        GamepadRecord* const record = m_gamepads.find(found->second);
        first_report = record != nullptr && !record->announced;
        if (record != nullptr)
        {
            record->announced = true;
        }
        return found->second;
    }
    SDL_Gamepad* const gamepad = SDL_OpenGamepad(instance);
    if (gamepad == nullptr)
    {
        first_report = false;
        return GamepadHandle{};
    }
    const GamepadHandle handle = m_gamepads.insert({.gamepad = gamepad, .instance = instance, .announced = true});
    m_gamepad_instances.emplace(instance, handle);
    first_report = true;
    return handle;
}

auto
Sdl3InputDevices::disconnect_gamepad(SDL_JoystickID instance) noexcept -> GamepadHandle
{
    const auto found = m_gamepad_instances.find(instance);
    if (found == m_gamepad_instances.end())
    {
        return GamepadHandle{};
    }
    const GamepadHandle handle = found->second;
    m_gamepad_instances.erase(found);
    if (const std::optional<GamepadRecord> record = m_gamepads.remove(handle))
    {
        SDL_CloseGamepad(record->gamepad);
    }
    return handle;
}

auto
Sdl3InputDevices::gamepad_handle(SDL_JoystickID instance) const noexcept -> GamepadHandle
{
    const auto found = m_gamepad_instances.find(instance);
    return found != m_gamepad_instances.end() ? found->second : GamepadHandle{};
}

auto
Sdl3InputDevices::connect_joystick(SDL_JoystickID instance,
                                   bool& first_report) -> JoystickHandle
{
    // Gamepads are reported as gamepads only.
    first_report = false;
    if (SDL_IsGamepad(instance))
    {
        return JoystickHandle{};
    }
    if (const auto found = m_joystick_instances.find(instance); found != m_joystick_instances.end())
    {
        JoystickRecord* const record = m_joysticks.find(found->second);
        first_report = record != nullptr && !record->announced;
        if (record != nullptr)
        {
            record->announced = true;
        }
        return found->second;
    }
    SDL_Joystick* const joystick = SDL_OpenJoystick(instance);
    if (joystick == nullptr)
    {
        return JoystickHandle{};
    }
    const JoystickHandle handle = m_joysticks.insert({.joystick = joystick, .instance = instance, .announced = true});
    m_joystick_instances.emplace(instance, handle);
    first_report = true;
    return handle;
}

auto
Sdl3InputDevices::disconnect_joystick(SDL_JoystickID instance) noexcept -> JoystickHandle
{
    const auto found = m_joystick_instances.find(instance);
    if (found == m_joystick_instances.end())
    {
        return JoystickHandle{};
    }
    const JoystickHandle handle = found->second;
    m_joystick_instances.erase(found);
    if (const std::optional<JoystickRecord> record = m_joysticks.remove(handle))
    {
        SDL_CloseJoystick(record->joystick);
    }
    return handle;
}

auto
Sdl3InputDevices::joystick_handle(SDL_JoystickID instance) const noexcept -> JoystickHandle
{
    const auto found = m_joystick_instances.find(instance);
    return found != m_joystick_instances.end() ? found->second : JoystickHandle{};
}

auto
Sdl3InputDevices::touch_device_handle(SDL_TouchID touch) -> TouchDeviceHandle
{
    if (const auto found = m_touch_instances.find(touch); found != m_touch_instances.end())
    {
        return found->second;
    }
    const TouchDeviceHandle handle = m_touch_devices.insert({.touch = touch});
    m_touch_instances.emplace(touch, handle);
    return handle;
}

void
Sdl3InputDevices::close_all() noexcept
{
    // Every opened device, closed once.
    for (const auto& [instance, handle] : m_gamepad_instances)
    {
        if (const std::optional<GamepadRecord> record = m_gamepads.remove(handle))
        {
            SDL_CloseGamepad(record->gamepad);
        }
    }
    m_gamepad_instances.clear();
    for (const auto& [instance, handle] : m_joystick_instances)
    {
        if (const std::optional<JoystickRecord> record = m_joysticks.remove(handle))
        {
            SDL_CloseJoystick(record->joystick);
        }
    }
    m_joystick_instances.clear();
}

auto
Sdl3InputDevices::require_gamepad(GamepadHandle gamepad,
                                  std::string_view operation,
                                  std::source_location location) const -> SDL_Gamepad*
{
    m_platform.check_thread(operation, location);
    return m_gamepads.get(gamepad, operation, location).gamepad;
}

auto
Sdl3InputDevices::gamepads() const -> std::vector<GamepadHandle>
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
Sdl3InputDevices::gamepad_properties(GamepadHandle gamepad) const -> GamepadProperties
{
    // Name, family, USB identifiers and capabilities, as SDL reports them.
    SDL_Gamepad* const native = require_gamepad(gamepad, "InputDevices::gamepad_properties");
    const char* const name = SDL_GetGamepadName(native);
    const SDL_PropertiesID properties = SDL_GetGamepadProperties(native);
    return GamepadProperties{
        .handle = gamepad,
        .name = name != nullptr ? name : "",
        .type = to_gamepad_type(SDL_GetGamepadType(native)),
        .vendor_identifier = SDL_GetGamepadVendor(native),
        .product_identifier = SDL_GetGamepadProduct(native),
        .touchpad_count = static_cast<std::uint32_t>(std::max(SDL_GetNumGamepadTouchpads(native), 0)),
        .has_rumble = SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false),
        .has_trigger_rumble = SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false),
        .has_led = SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false),
    };
}

auto
Sdl3InputDevices::is_gamepad_button_down(GamepadHandle gamepad,
                                         GamepadButton button) const -> bool
{
    return SDL_GetGamepadButton(require_gamepad(gamepad, "InputDevices::is_gamepad_button_down"), to_sdl(button));
}

auto
Sdl3InputDevices::gamepad_axis(GamepadHandle gamepad,
                               GamepadAxis axis) const -> float
{
    return to_axis_value(SDL_GetGamepadAxis(require_gamepad(gamepad, "InputDevices::gamepad_axis"), to_sdl(axis)));
}

void
Sdl3InputDevices::set_gamepad_rumble(GamepadHandle gamepad,
                                     float low_frequency,
                                     float high_frequency,
                                     std::chrono::milliseconds duration)
{
    // A gamepad without motors says so; one with motors that refuses is an SDL failure.
    constexpr std::string_view OPERATION = "InputDevices::set_gamepad_rumble";
    SDL_Gamepad* const native = require_gamepad(gamepad, OPERATION);
    if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(native), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false))
    {
        throw_unsupported(OPERATION, "gamepad rumble", std::format("{} has no rumble motors", gamepad),
                          "check GamepadProperties::has_rumble first");
    }
    if (!SDL_RumbleGamepad(native, to_intensity(low_frequency), to_intensity(high_frequency), to_duration(duration)))
    {
        throw_sdl_error(OPERATION, "SDL_RumbleGamepad");
    }
}

void
Sdl3InputDevices::set_gamepad_trigger_rumble(GamepadHandle gamepad,
                                             float left,
                                             float right,
                                             std::chrono::milliseconds duration)
{
    constexpr std::string_view OPERATION = "InputDevices::set_gamepad_trigger_rumble";
    SDL_Gamepad* const native = require_gamepad(gamepad, OPERATION);
    if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(native), SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false))
    {
        throw_unsupported(OPERATION, "trigger rumble", std::format("{} has no trigger motors", gamepad),
                          "check GamepadProperties::has_trigger_rumble first");
    }
    if (!SDL_RumbleGamepadTriggers(native, to_intensity(left), to_intensity(right), to_duration(duration)))
    {
        throw_sdl_error(OPERATION, "SDL_RumbleGamepadTriggers");
    }
}

void
Sdl3InputDevices::set_gamepad_led(GamepadHandle gamepad,
                                  LedColor color)
{
    constexpr std::string_view OPERATION = "InputDevices::set_gamepad_led";
    SDL_Gamepad* const native = require_gamepad(gamepad, OPERATION);
    if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(native), SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false))
    {
        throw_unsupported(OPERATION, "gamepad light bar", std::format("{} has no color light", gamepad),
                          "check GamepadProperties::has_led first");
    }
    if (!SDL_SetGamepadLED(native, color.red, color.green, color.blue))
    {
        throw_sdl_error(OPERATION, "SDL_SetGamepadLED");
    }
}

auto
Sdl3InputDevices::joysticks() const -> std::vector<JoystickHandle>
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
Sdl3InputDevices::joystick_properties(JoystickHandle joystick) const -> JoystickProperties
{
    constexpr std::string_view OPERATION = "InputDevices::joystick_properties";
    m_platform.check_thread(OPERATION);
    SDL_Joystick* const native = m_joysticks.get(joystick, OPERATION).joystick;
    const char* const name = SDL_GetJoystickName(native);
    return JoystickProperties{
        .handle = joystick,
        .name = name != nullptr ? name : "",
        .vendor_identifier = SDL_GetJoystickVendor(native),
        .product_identifier = SDL_GetJoystickProduct(native),
        .axis_count = static_cast<std::uint32_t>(std::max(SDL_GetNumJoystickAxes(native), 0)),
        .button_count = static_cast<std::uint32_t>(std::max(SDL_GetNumJoystickButtons(native), 0)),
        .hat_count = static_cast<std::uint32_t>(std::max(SDL_GetNumJoystickHats(native), 0)),
    };
}

auto
Sdl3InputDevices::touch_devices() const -> std::vector<TouchDeviceHandle>
{
    // The devices SDL knows now, plus those seen in events.
    m_platform.check_thread("InputDevices::touch_devices");
    int count = 0;
    const std::unique_ptr<SDL_TouchID, SdlFree> touches{SDL_GetTouchDevices(&count)};
    for (int index = 0; touches && index < count; ++index)
    {
        static_cast<void>(const_cast<Sdl3InputDevices*>(this)->touch_device_handle(touches.get()[index]));
    }
    std::vector<TouchDeviceHandle> handles;
    m_touch_devices.for_each([&handles](TouchDeviceHandle handle, const TouchDeviceRecord& /*record*/)
    {
        handles.push_back(handle);
    });
    return handles;
}

auto
Sdl3InputDevices::touch_device_properties(TouchDeviceHandle device) const -> TouchDeviceProperties
{
    constexpr std::string_view OPERATION = "InputDevices::touch_device_properties";
    m_platform.check_thread(OPERATION);
    const SDL_TouchID touch = m_touch_devices.get(device, OPERATION).touch;
    const char* const name = SDL_GetTouchDeviceName(touch);
    TouchDeviceType type = TouchDeviceType::DIRECT;
    switch (SDL_GetTouchDeviceType(touch))
    {
        case SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE:
            type = TouchDeviceType::INDIRECT_ABSOLUTE;
            break;
        case SDL_TOUCH_DEVICE_INDIRECT_RELATIVE:
            type = TouchDeviceType::INDIRECT_RELATIVE;
            break;
        default:
            break;
    }
    return TouchDeviceProperties{.handle = device, .name = name != nullptr ? name : "", .type = type};
}

} // namespace gfx::platform::sdl3
