/// @file sdl3_translation.cpp
/// @brief Translations between SDL values and gfx values, and the SDL error helper.
module;

#include "sdl3_headers.hpp"

module gfx.platform.backends.sdl3;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::sdl3
{

void
throw_sdl_error(std::string_view operation,
                std::string_view function,
                std::source_location location)
{
    // SDL reports failures as text; the result code is not meaningful.
    const char* const error = SDL_GetError();
    throw BackendApiError(
        {
            .backend = PlatformBackendKind::SDL3,
            .operation = std::string{operation},
            .api_function = std::string{function},
            .result_code = 0,
            .result_name = std::format("\"{}\"", error != nullptr && *error != '\0' ? error : "no error text"),
        },
        location);
}

auto
to_key(SDL_Scancode scancode) noexcept -> Key
{
    // Scancodes are HID usages; the ones gfx does not name (SDL's extended media keys) are unknown.
    const auto value = static_cast<std::uint32_t>(scancode);
    if (value > std::numeric_limits<std::uint16_t>::max())
    {
        return Key::UNKNOWN;
    }
    const auto key = static_cast<Key>(value);
    return to_string(key) != "<invalid>" ? key : Key::UNKNOWN;
}

auto
to_key_code(SDL_Keycode key) noexcept -> KeyCode
{
    // SDL keycodes are code points, or SDLK_SCANCODE_MASK (KEY_CODE_NON_PRINTABLE_FLAG) | scancode.
    static_assert(SDLK_SCANCODE_MASK == KEY_CODE_NON_PRINTABLE_FLAG);
    return static_cast<KeyCode>(key);
}

auto
to_key_modifier(SDL_Keymod modifiers) noexcept -> KeyModifier
{
    // Each SDL modifier bit has its gfx bit (SDL's level-5 shift and mode keys have none).
    constexpr std::array<std::pair<SDL_Keymod, KeyModifier>, 11> BITS{{
        {SDL_KMOD_LSHIFT, KeyModifier::LEFT_SHIFT},
        {SDL_KMOD_RSHIFT, KeyModifier::RIGHT_SHIFT},
        {SDL_KMOD_LCTRL, KeyModifier::LEFT_CONTROL},
        {SDL_KMOD_RCTRL, KeyModifier::RIGHT_CONTROL},
        {SDL_KMOD_LALT, KeyModifier::LEFT_ALT},
        {SDL_KMOD_RALT, KeyModifier::RIGHT_ALT},
        {SDL_KMOD_LGUI, KeyModifier::LEFT_SUPER},
        {SDL_KMOD_RGUI, KeyModifier::RIGHT_SUPER},
        {SDL_KMOD_CAPS, KeyModifier::CAPS_LOCK},
        {SDL_KMOD_NUM, KeyModifier::NUM_LOCK},
        {SDL_KMOD_SCROLL, KeyModifier::SCROLL_LOCK},
    }};
    KeyModifier result = KeyModifier::NONE;
    for (const auto& [sdl_bit, bit] : BITS)
    {
        if ((modifiers & sdl_bit) != 0)
        {
            result |= bit;
        }
    }
    return result;
}

auto
to_mouse_button(Uint8 button) noexcept -> std::optional<MouseButton>
{
    switch (button)
    {
        case SDL_BUTTON_LEFT:
            return MouseButton::LEFT;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::MIDDLE;
        case SDL_BUTTON_RIGHT:
            return MouseButton::RIGHT;
        case SDL_BUTTON_X1:
            return MouseButton::BACK;
        case SDL_BUTTON_X2:
            return MouseButton::FORWARD;
        default:
            return std::nullopt;
    }
}

auto
to_gamepad_button(Uint8 button) noexcept -> std::optional<GamepadButton>
{
    switch (static_cast<SDL_GamepadButton>(button))
    {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return GamepadButton::SOUTH;
        case SDL_GAMEPAD_BUTTON_EAST:
            return GamepadButton::EAST;
        case SDL_GAMEPAD_BUTTON_WEST:
            return GamepadButton::WEST;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return GamepadButton::NORTH;
        case SDL_GAMEPAD_BUTTON_BACK:
            return GamepadButton::BACK;
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return GamepadButton::GUIDE;
        case SDL_GAMEPAD_BUTTON_START:
            return GamepadButton::START;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return GamepadButton::LEFT_STICK;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return GamepadButton::RIGHT_STICK;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return GamepadButton::LEFT_SHOULDER;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return GamepadButton::RIGHT_SHOULDER;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return GamepadButton::DPAD_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return GamepadButton::DPAD_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return GamepadButton::DPAD_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return GamepadButton::DPAD_RIGHT;
        case SDL_GAMEPAD_BUTTON_MISC1:
            return GamepadButton::MISCELLANEOUS_1;
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
            return GamepadButton::RIGHT_PADDLE_1;
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
            return GamepadButton::LEFT_PADDLE_1;
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
            return GamepadButton::RIGHT_PADDLE_2;
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
            return GamepadButton::LEFT_PADDLE_2;
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
            return GamepadButton::TOUCHPAD;
        default:
            return std::nullopt;
    }
}

auto
to_gamepad_axis(Uint8 axis) noexcept -> std::optional<GamepadAxis>
{
    switch (static_cast<SDL_GamepadAxis>(axis))
    {
        case SDL_GAMEPAD_AXIS_LEFTX:
            return GamepadAxis::LEFT_X;
        case SDL_GAMEPAD_AXIS_LEFTY:
            return GamepadAxis::LEFT_Y;
        case SDL_GAMEPAD_AXIS_RIGHTX:
            return GamepadAxis::RIGHT_X;
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return GamepadAxis::RIGHT_Y;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return GamepadAxis::LEFT_TRIGGER;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return GamepadAxis::RIGHT_TRIGGER;
        default:
            return std::nullopt;
    }
}

auto
to_gamepad_type(SDL_GamepadType type) noexcept -> GamepadType
{
    switch (type)
    {
        case SDL_GAMEPAD_TYPE_STANDARD:
            return GamepadType::STANDARD;
        case SDL_GAMEPAD_TYPE_XBOX360:
            return GamepadType::XBOX_360;
        case SDL_GAMEPAD_TYPE_XBOXONE:
            return GamepadType::XBOX_ONE;
        case SDL_GAMEPAD_TYPE_PS3:
            return GamepadType::PLAYSTATION_3;
        case SDL_GAMEPAD_TYPE_PS4:
            return GamepadType::PLAYSTATION_4;
        case SDL_GAMEPAD_TYPE_PS5:
            return GamepadType::PLAYSTATION_5;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
            return GamepadType::NINTENDO_SWITCH_PRO;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            return GamepadType::NINTENDO_SWITCH_JOYCON_LEFT;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            return GamepadType::NINTENDO_SWITCH_JOYCON_RIGHT;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return GamepadType::NINTENDO_SWITCH_JOYCON_PAIR;
        case SDL_GAMEPAD_TYPE_GAMECUBE:
            return GamepadType::NINTENDO_GAMECUBE;
        default:
            return GamepadType::UNKNOWN;
    }
}

auto
to_hat_position(Uint8 value) noexcept -> HatPosition
{
    // SDL_HAT_UP, RIGHT, DOWN and LEFT are the bits of HatPosition.
    static_assert(SDL_HAT_UP == std::to_underlying(HatPosition::UP) && SDL_HAT_RIGHT == std::to_underlying(HatPosition::RIGHT)
                  && SDL_HAT_DOWN == std::to_underlying(HatPosition::DOWN) && SDL_HAT_LEFT == std::to_underlying(HatPosition::LEFT));
    return static_cast<HatPosition>(value & (SDL_HAT_UP | SDL_HAT_RIGHT | SDL_HAT_DOWN | SDL_HAT_LEFT));
}

auto
to_axis_value(Sint16 value) noexcept -> float
{
    // Symmetric scale: -32768 clamps to -1.
    return std::max(-1.0F, static_cast<float>(value) / static_cast<float>(SDL_JOYSTICK_AXIS_MAX));
}

auto
to_sdl(GamepadButton button) noexcept -> SDL_GamepadButton
{
    // GamepadButton follows SDL's order (SDL_GAMEPAD_BUTTON_SOUTH ... SDL_GAMEPAD_BUTTON_TOUCHPAD).
    static_assert(SDL_GAMEPAD_BUTTON_TOUCHPAD == std::to_underlying(GamepadButton::TOUCHPAD)
                  && SDL_GAMEPAD_BUTTON_DPAD_UP == std::to_underlying(GamepadButton::DPAD_UP)
                  && SDL_GAMEPAD_BUTTON_MISC1 == std::to_underlying(GamepadButton::MISCELLANEOUS_1));
    return static_cast<SDL_GamepadButton>(std::to_underlying(button));
}

auto
to_sdl(GamepadAxis axis) noexcept -> SDL_GamepadAxis
{
    // GamepadAxis follows SDL's order.
    static_assert(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER == std::to_underlying(GamepadAxis::RIGHT_TRIGGER)
                  && SDL_GAMEPAD_AXIS_LEFTY == std::to_underlying(GamepadAxis::LEFT_Y));
    return static_cast<SDL_GamepadAxis>(std::to_underlying(axis));
}

auto
to_sdl(CursorShape shape) noexcept -> SDL_SystemCursor
{
    switch (shape)
    {
        case CursorShape::ARROW:
            return SDL_SYSTEM_CURSOR_DEFAULT;
        case CursorShape::I_BEAM:
            return SDL_SYSTEM_CURSOR_TEXT;
        case CursorShape::WAIT:
            return SDL_SYSTEM_CURSOR_WAIT;
        case CursorShape::PROGRESS:
            return SDL_SYSTEM_CURSOR_PROGRESS;
        case CursorShape::CROSSHAIR:
            return SDL_SYSTEM_CURSOR_CROSSHAIR;
        case CursorShape::POINTER:
            return SDL_SYSTEM_CURSOR_POINTER;
        case CursorShape::RESIZE_EAST_WEST:
            return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case CursorShape::RESIZE_NORTH_SOUTH:
            return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case CursorShape::RESIZE_NORTHWEST_SOUTHEAST:
            return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case CursorShape::RESIZE_NORTHEAST_SOUTHWEST:
            return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case CursorShape::MOVE:
            return SDL_SYSTEM_CURSOR_MOVE;
        case CursorShape::NOT_ALLOWED:
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

} // namespace gfx::platform::sdl3
