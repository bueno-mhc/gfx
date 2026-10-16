/// @file glfw_translation.cpp
/// @brief Translations between GLFW values and gfx values (a compile-time key table), and GLFW errors.
module;

#include "glfw_headers.hpp"

module gfx.platform.backends.glfw;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::glfw
{

namespace
{

/// Every GLFW key token, and the physical key it names (GLFW names keys after the US layout). Both
/// "world" keys are the ISO key between left Shift and Z: Win32 and Wayland report it as WORLD_2, X11
/// as WORLD_1. F25 has no USB HID usage.
constexpr std::array<std::pair<int, Key>, 120> KEY_TOKENS{{
    {GLFW_KEY_SPACE, Key::SPACE},
    {GLFW_KEY_APOSTROPHE, Key::APOSTROPHE},
    {GLFW_KEY_COMMA, Key::COMMA},
    {GLFW_KEY_MINUS, Key::MINUS},
    {GLFW_KEY_PERIOD, Key::PERIOD},
    {GLFW_KEY_SLASH, Key::SLASH},
    {GLFW_KEY_0, Key::NUMBER_0},
    {GLFW_KEY_1, Key::NUMBER_1},
    {GLFW_KEY_2, Key::NUMBER_2},
    {GLFW_KEY_3, Key::NUMBER_3},
    {GLFW_KEY_4, Key::NUMBER_4},
    {GLFW_KEY_5, Key::NUMBER_5},
    {GLFW_KEY_6, Key::NUMBER_6},
    {GLFW_KEY_7, Key::NUMBER_7},
    {GLFW_KEY_8, Key::NUMBER_8},
    {GLFW_KEY_9, Key::NUMBER_9},
    {GLFW_KEY_SEMICOLON, Key::SEMICOLON},
    {GLFW_KEY_EQUAL, Key::EQUALS},
    {GLFW_KEY_A, Key::A},
    {GLFW_KEY_B, Key::B},
    {GLFW_KEY_C, Key::C},
    {GLFW_KEY_D, Key::D},
    {GLFW_KEY_E, Key::E},
    {GLFW_KEY_F, Key::F},
    {GLFW_KEY_G, Key::G},
    {GLFW_KEY_H, Key::H},
    {GLFW_KEY_I, Key::I},
    {GLFW_KEY_J, Key::J},
    {GLFW_KEY_K, Key::K},
    {GLFW_KEY_L, Key::L},
    {GLFW_KEY_M, Key::M},
    {GLFW_KEY_N, Key::N},
    {GLFW_KEY_O, Key::O},
    {GLFW_KEY_P, Key::P},
    {GLFW_KEY_Q, Key::Q},
    {GLFW_KEY_R, Key::R},
    {GLFW_KEY_S, Key::S},
    {GLFW_KEY_T, Key::T},
    {GLFW_KEY_U, Key::U},
    {GLFW_KEY_V, Key::V},
    {GLFW_KEY_W, Key::W},
    {GLFW_KEY_X, Key::X},
    {GLFW_KEY_Y, Key::Y},
    {GLFW_KEY_Z, Key::Z},
    {GLFW_KEY_LEFT_BRACKET, Key::LEFT_BRACKET},
    {GLFW_KEY_BACKSLASH, Key::BACKSLASH},
    {GLFW_KEY_RIGHT_BRACKET, Key::RIGHT_BRACKET},
    {GLFW_KEY_GRAVE_ACCENT, Key::GRAVE},
    {GLFW_KEY_WORLD_1, Key::NON_US_BACKSLASH},
    {GLFW_KEY_WORLD_2, Key::NON_US_BACKSLASH},
    {GLFW_KEY_ESCAPE, Key::ESCAPE},
    {GLFW_KEY_ENTER, Key::RETURN},
    {GLFW_KEY_TAB, Key::TAB},
    {GLFW_KEY_BACKSPACE, Key::BACKSPACE},
    {GLFW_KEY_INSERT, Key::INSERT},
    {GLFW_KEY_DELETE, Key::DELETE_FORWARD},
    {GLFW_KEY_RIGHT, Key::RIGHT},
    {GLFW_KEY_LEFT, Key::LEFT},
    {GLFW_KEY_DOWN, Key::DOWN},
    {GLFW_KEY_UP, Key::UP},
    {GLFW_KEY_PAGE_UP, Key::PAGE_UP},
    {GLFW_KEY_PAGE_DOWN, Key::PAGE_DOWN},
    {GLFW_KEY_HOME, Key::HOME},
    {GLFW_KEY_END, Key::END},
    {GLFW_KEY_CAPS_LOCK, Key::CAPS_LOCK},
    {GLFW_KEY_SCROLL_LOCK, Key::SCROLL_LOCK},
    {GLFW_KEY_NUM_LOCK, Key::NUM_LOCK},
    {GLFW_KEY_PRINT_SCREEN, Key::PRINT_SCREEN},
    {GLFW_KEY_PAUSE, Key::PAUSE},
    {GLFW_KEY_F1, Key::F1},
    {GLFW_KEY_F2, Key::F2},
    {GLFW_KEY_F3, Key::F3},
    {GLFW_KEY_F4, Key::F4},
    {GLFW_KEY_F5, Key::F5},
    {GLFW_KEY_F6, Key::F6},
    {GLFW_KEY_F7, Key::F7},
    {GLFW_KEY_F8, Key::F8},
    {GLFW_KEY_F9, Key::F9},
    {GLFW_KEY_F10, Key::F10},
    {GLFW_KEY_F11, Key::F11},
    {GLFW_KEY_F12, Key::F12},
    {GLFW_KEY_F13, Key::F13},
    {GLFW_KEY_F14, Key::F14},
    {GLFW_KEY_F15, Key::F15},
    {GLFW_KEY_F16, Key::F16},
    {GLFW_KEY_F17, Key::F17},
    {GLFW_KEY_F18, Key::F18},
    {GLFW_KEY_F19, Key::F19},
    {GLFW_KEY_F20, Key::F20},
    {GLFW_KEY_F21, Key::F21},
    {GLFW_KEY_F22, Key::F22},
    {GLFW_KEY_F23, Key::F23},
    {GLFW_KEY_F24, Key::F24},
    {GLFW_KEY_F25, Key::UNKNOWN},
    {GLFW_KEY_KP_0, Key::KEYPAD_0},
    {GLFW_KEY_KP_1, Key::KEYPAD_1},
    {GLFW_KEY_KP_2, Key::KEYPAD_2},
    {GLFW_KEY_KP_3, Key::KEYPAD_3},
    {GLFW_KEY_KP_4, Key::KEYPAD_4},
    {GLFW_KEY_KP_5, Key::KEYPAD_5},
    {GLFW_KEY_KP_6, Key::KEYPAD_6},
    {GLFW_KEY_KP_7, Key::KEYPAD_7},
    {GLFW_KEY_KP_8, Key::KEYPAD_8},
    {GLFW_KEY_KP_9, Key::KEYPAD_9},
    {GLFW_KEY_KP_DECIMAL, Key::KEYPAD_PERIOD},
    {GLFW_KEY_KP_DIVIDE, Key::KEYPAD_DIVIDE},
    {GLFW_KEY_KP_MULTIPLY, Key::KEYPAD_MULTIPLY},
    {GLFW_KEY_KP_SUBTRACT, Key::KEYPAD_MINUS},
    {GLFW_KEY_KP_ADD, Key::KEYPAD_PLUS},
    {GLFW_KEY_KP_ENTER, Key::KEYPAD_ENTER},
    {GLFW_KEY_KP_EQUAL, Key::KEYPAD_EQUALS},
    {GLFW_KEY_LEFT_SHIFT, Key::LEFT_SHIFT},
    {GLFW_KEY_LEFT_CONTROL, Key::LEFT_CONTROL},
    {GLFW_KEY_LEFT_ALT, Key::LEFT_ALT},
    {GLFW_KEY_LEFT_SUPER, Key::LEFT_SUPER},
    {GLFW_KEY_RIGHT_SHIFT, Key::RIGHT_SHIFT},
    {GLFW_KEY_RIGHT_CONTROL, Key::RIGHT_CONTROL},
    {GLFW_KEY_RIGHT_ALT, Key::RIGHT_ALT},
    {GLFW_KEY_RIGHT_SUPER, Key::RIGHT_SUPER},
    {GLFW_KEY_MENU, Key::APPLICATION},
}};

/// The physical key of every GLFW key token, indexed by token: computed at compile time.
constexpr auto GLFW_KEYS = []
{
    std::array<Key, GLFW_KEY_LAST + 1> keys{};
    for (const auto& [token, key] : KEY_TOKENS)
    {
        keys[static_cast<std::size_t>(token)] = key;
    }
    return keys;
}();

// Every token is listed once, and every token but F25 names a key.
static_assert(GLFW_KEY_LAST == GLFW_KEY_MENU && KEY_TOKENS.back().first == GLFW_KEY_LAST);
static_assert(std::ranges::count(GLFW_KEYS, Key::UNKNOWN) == GLFW_KEYS.size() - (KEY_TOKENS.size() - 1),
              "every GLFW key token except F25 maps to a Key, and no token is listed twice");
static_assert([]
{
    for (std::size_t index = 1; index < KEY_TOKENS.size(); ++index)
    {
        if (KEY_TOKENS[index - 1].first >= KEY_TOKENS[index].first)
        {
            return false;
        }
    }
    return true;
}(), "KEY_TOKENS is sorted and has no duplicate token");

/// Name of a GLFW error code.
auto
error_name(int code) noexcept -> std::string_view
{
    switch (code)
    {
        case GLFW_NOT_INITIALIZED:
            return "GLFW_NOT_INITIALIZED";
        case GLFW_NO_CURRENT_CONTEXT:
            return "GLFW_NO_CURRENT_CONTEXT";
        case GLFW_INVALID_ENUM:
            return "GLFW_INVALID_ENUM";
        case GLFW_INVALID_VALUE:
            return "GLFW_INVALID_VALUE";
        case GLFW_OUT_OF_MEMORY:
            return "GLFW_OUT_OF_MEMORY";
        case GLFW_API_UNAVAILABLE:
            return "GLFW_API_UNAVAILABLE";
        case GLFW_VERSION_UNAVAILABLE:
            return "GLFW_VERSION_UNAVAILABLE";
        case GLFW_PLATFORM_ERROR:
            return "GLFW_PLATFORM_ERROR";
        case GLFW_FORMAT_UNAVAILABLE:
            return "GLFW_FORMAT_UNAVAILABLE";
        case GLFW_NO_WINDOW_CONTEXT:
            return "GLFW_NO_WINDOW_CONTEXT";
        case GLFW_CURSOR_UNAVAILABLE:
            return "GLFW_CURSOR_UNAVAILABLE";
        case GLFW_FEATURE_UNAVAILABLE:
            return "GLFW_FEATURE_UNAVAILABLE";
        case GLFW_FEATURE_UNIMPLEMENTED:
            return "GLFW_FEATURE_UNIMPLEMENTED";
        case GLFW_PLATFORM_UNAVAILABLE:
            return "GLFW_PLATFORM_UNAVAILABLE";
        default:
            return "unknown GLFW error";
    }
}

} // namespace

void
check_glfw(std::string_view operation,
           std::string_view function,
           std::source_location location)
{
    // GLFW keeps the last error of the thread, with its description.
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    if (code == GLFW_NO_ERROR)
    {
        return;
    }
    const std::string text = description != nullptr ? description : "no description";
    if (code == GLFW_FEATURE_UNAVAILABLE || code == GLFW_FEATURE_UNIMPLEMENTED || code == GLFW_CURSOR_UNAVAILABLE)
    {
        throw BackendCapabilityError(
            {
                .backend = PlatformBackendKind::GLFW,
                .operation = std::string{operation},
                .capability = std::string{function},
                .reason = std::format("GLFW cannot do it on this system: {} ({})", text, error_name(code)),
                .suggestion = "check Platform::supports() first, or use the SDL3 backend",
            },
            location);
    }
    throw BackendApiError(
        {
            .backend = PlatformBackendKind::GLFW,
            .operation = std::string{operation},
            .api_function = std::string{function},
            .result_code = code,
            .result_name = std::format("{} \"{}\"", error_name(code), text),
        },
        location);
}

void
clear_glfw_error() noexcept
{
    static_cast<void>(glfwGetError(nullptr));
}

auto
to_key(int key) noexcept -> Key
{
    return key >= 0 && key <= GLFW_KEY_LAST ? GLFW_KEYS[static_cast<std::size_t>(key)] : Key::UNKNOWN;
}

auto
to_key_code(int key,
            int scancode) noexcept -> KeyCode
{
    // Control characters are code points, as on other backends.
    switch (key)
    {
        case GLFW_KEY_SPACE:
            return KeyCode::SPACE;
        case GLFW_KEY_ENTER:
            return KeyCode::RETURN;
        case GLFW_KEY_ESCAPE:
            return KeyCode::ESCAPE;
        case GLFW_KEY_TAB:
            return KeyCode::TAB;
        case GLFW_KEY_BACKSPACE:
            return KeyCode::BACKSPACE;
        case GLFW_KEY_DELETE:
            return KeyCode::DELETE_FORWARD;
        default:
            break;
    }

    // The main block's printable keys: the character the layout gives them (lowercase ASCII letters).
    if (key >= GLFW_KEY_APOSTROPHE && key <= GLFW_KEY_WORLD_2)
    {
        const char* const name = glfwGetKeyName(key, scancode);
        clear_glfw_error();
        if (name != nullptr && *name != '\0')
        {
            const char32_t code_point = *code_points(name).begin();
            return static_cast<KeyCode>(code_point >= U'A' && code_point <= U'Z' ? code_point + (U'a' - U'A') : code_point);
        }
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        {
            return static_cast<KeyCode>(static_cast<std::uint32_t>(key - GLFW_KEY_A) + U'a');
        }
    }

    // Other keys (keypad, functions, navigation, modifiers): the flag and the HID usage.
    const Key physical = to_key(key);
    return physical == Key::UNKNOWN ? KeyCode::UNKNOWN : static_cast<KeyCode>(KEY_CODE_NON_PRINTABLE_FLAG | std::to_underlying(physical));
}

auto
to_key_modifier(GLFWwindow* window,
                int modifiers) noexcept -> KeyModifier
{
    // GLFW reports modifier groups; the keys held tell the sides (the left one when neither is held).
    struct Group
    {
        int bit;
        int left_key;
        int right_key;
        KeyModifier left;
        KeyModifier right;
    };
    constexpr std::array<Group, 4> GROUPS{{
        {GLFW_MOD_SHIFT, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, KeyModifier::LEFT_SHIFT, KeyModifier::RIGHT_SHIFT},
        {GLFW_MOD_CONTROL, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL, KeyModifier::LEFT_CONTROL, KeyModifier::RIGHT_CONTROL},
        {GLFW_MOD_ALT, GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT, KeyModifier::LEFT_ALT, KeyModifier::RIGHT_ALT},
        {GLFW_MOD_SUPER, GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER, KeyModifier::LEFT_SUPER, KeyModifier::RIGHT_SUPER},
    }};
    KeyModifier result = KeyModifier::NONE;
    for (const Group& group : GROUPS)
    {
        if ((modifiers & group.bit) == 0)
        {
            continue;
        }
        const bool left = glfwGetKey(window, group.left_key) == GLFW_PRESS;
        const bool right = glfwGetKey(window, group.right_key) == GLFW_PRESS;
        result |= left || !right ? group.left : KeyModifier::NONE;
        result |= right ? group.right : KeyModifier::NONE;
    }
    if ((modifiers & GLFW_MOD_CAPS_LOCK) != 0)
    {
        result |= KeyModifier::CAPS_LOCK;
    }
    if ((modifiers & GLFW_MOD_NUM_LOCK) != 0)
    {
        result |= KeyModifier::NUM_LOCK;
    }
    return result;
}

auto
to_mouse_button(int button) noexcept -> std::optional<MouseButton>
{
    switch (button)
    {
        case GLFW_MOUSE_BUTTON_LEFT:
            return MouseButton::LEFT;
        case GLFW_MOUSE_BUTTON_RIGHT:
            return MouseButton::RIGHT;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            return MouseButton::MIDDLE;
        case GLFW_MOUSE_BUTTON_4:
            return MouseButton::BACK;
        case GLFW_MOUSE_BUTTON_5:
            return MouseButton::FORWARD;
        default:
            return std::nullopt;
    }
}

auto
to_glfw(CursorShape shape) noexcept -> int
{
    switch (shape)
    {
        case CursorShape::ARROW:
            return GLFW_ARROW_CURSOR;
        case CursorShape::I_BEAM:
            return GLFW_IBEAM_CURSOR;
        case CursorShape::CROSSHAIR:
            return GLFW_CROSSHAIR_CURSOR;
        case CursorShape::POINTER:
            return GLFW_POINTING_HAND_CURSOR;
        case CursorShape::RESIZE_EAST_WEST:
            return GLFW_RESIZE_EW_CURSOR;
        case CursorShape::RESIZE_NORTH_SOUTH:
            return GLFW_RESIZE_NS_CURSOR;
        case CursorShape::RESIZE_NORTHWEST_SOUTHEAST:
            return GLFW_RESIZE_NWSE_CURSOR;
        case CursorShape::RESIZE_NORTHEAST_SOUTHWEST:
            return GLFW_RESIZE_NESW_CURSOR;
        case CursorShape::MOVE:
            return GLFW_RESIZE_ALL_CURSOR;
        case CursorShape::NOT_ALLOWED:
            return GLFW_NOT_ALLOWED_CURSOR;
        case CursorShape::WAIT:
        case CursorShape::PROGRESS:
            return 0;
    }
    return 0;
}

auto
to_gamepad_button(int button) noexcept -> GamepadButton
{
    // GLFW's gamepad buttons, in GLFW order (positions: A is the bottom face button).
    constexpr std::array<GamepadButton, GLFW_GAMEPAD_BUTTON_LAST + 1> BUTTONS{
        GamepadButton::SOUTH,         GamepadButton::EAST,          GamepadButton::WEST,           GamepadButton::NORTH,
        GamepadButton::LEFT_SHOULDER, GamepadButton::RIGHT_SHOULDER, GamepadButton::BACK,          GamepadButton::START,
        GamepadButton::GUIDE,         GamepadButton::LEFT_STICK,    GamepadButton::RIGHT_STICK,    GamepadButton::DPAD_UP,
        GamepadButton::DPAD_RIGHT,    GamepadButton::DPAD_DOWN,     GamepadButton::DPAD_LEFT,
    };
    static_assert(GLFW_GAMEPAD_BUTTON_DPAD_LEFT == 14 && GLFW_GAMEPAD_BUTTON_A == 0 && GLFW_GAMEPAD_BUTTON_GUIDE == 8);
    return BUTTONS[static_cast<std::size_t>(button)];
}

auto
to_glfw(GamepadButton button) noexcept -> int
{
    for (int index = 0; index <= GLFW_GAMEPAD_BUTTON_LAST; ++index)
    {
        if (to_gamepad_button(index) == button)
        {
            return index;
        }
    }
    return -1;
}

auto
gamepad_axis_value(int axis,
                   float value) noexcept -> float
{
    // GLFW's axes follow gfx's order; its triggers rest at -1.
    static_assert(GLFW_GAMEPAD_AXIS_LEFT_X == 0 && GLFW_GAMEPAD_AXIS_RIGHT_Y == 3 && GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER == 5);
    const bool trigger = axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER || axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
    return trigger ? (value + 1.0F) * 0.5F : value;
}

auto
utf8(unsigned int code_point) -> std::string
{
    // One to four bytes; invalid code points become U+FFFD.
    if (code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU))
    {
        code_point = 0xFFFDU;
    }
    std::string text;
    if (code_point < 0x80U)
    {
        text += static_cast<char>(code_point);
    }
    else if (code_point < 0x800U)
    {
        text += static_cast<char>(0xC0U | (code_point >> 6U));
        text += static_cast<char>(0x80U | (code_point & 0x3FU));
    }
    else if (code_point < 0x10000U)
    {
        text += static_cast<char>(0xE0U | (code_point >> 12U));
        text += static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU));
        text += static_cast<char>(0x80U | (code_point & 0x3FU));
    }
    else
    {
        text += static_cast<char>(0xF0U | (code_point >> 18U));
        text += static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU));
        text += static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU));
        text += static_cast<char>(0x80U | (code_point & 0x3FU));
    }
    return text;
}

} // namespace gfx::platform::glfw
