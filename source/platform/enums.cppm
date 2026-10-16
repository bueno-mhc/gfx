/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx platform library (windowing and input). No other
///        file of the platform library defines either; the enumeration machinery (traits, verification,
///        bitmask operators, spelling and parsing) comes from gfx.core.enums, which this module
///        re-exports. Generated tables: Key and KeyCode list their enumerators in value order.
///
/// Conventions: enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform
/// headers define as macros are avoided (`DELETE` → `DELETE_FORWARD`), which a CTest test checks.
export module gfx.platform.enums;

import std;
export import gfx.core.enums;

export namespace gfx::platform
{

// The bitmask operators and helpers of gfx, found by argument-dependent lookup for the bitmask
// enumerations of this namespace too.
using gfx::operator|;
using gfx::operator&;
using gfx::operator^;
using gfx::operator~;
using gfx::operator|=;
using gfx::operator&=;
using gfx::operator^=;
using gfx::has_all;
using gfx::has_any;
using gfx::is_empty;
using gfx::to_string;

// =================================================================================================
// Enumerations
// =================================================================================================

/// @brief Physical key: a position on the keyboard, independent of the layout (WASD stays in place on
///        AZERTY). Values are USB HID usage IDs of the keyboard page (0x07), like SDL scancodes, so
///        they are stable when saved. Names follow the US layout.
enum class Key : std::uint16_t
{
    UNKNOWN          = 0, ///< No key, or a key gfx does not know.
    A                = 4, ///< The key labeled A on a US keyboard.
    B                = 5, ///< The key labeled B on a US keyboard.
    C                = 6, ///< The key labeled C on a US keyboard.
    D                = 7, ///< The key labeled D on a US keyboard.
    E                = 8, ///< The key labeled E on a US keyboard.
    F                = 9, ///< The key labeled F on a US keyboard.
    G                = 10, ///< The key labeled G on a US keyboard.
    H                = 11, ///< The key labeled H on a US keyboard.
    I                = 12, ///< The key labeled I on a US keyboard.
    J                = 13, ///< The key labeled J on a US keyboard.
    K                = 14, ///< The key labeled K on a US keyboard.
    L                = 15, ///< The key labeled L on a US keyboard.
    M                = 16, ///< The key labeled M on a US keyboard.
    N                = 17, ///< The key labeled N on a US keyboard.
    O                = 18, ///< The key labeled O on a US keyboard.
    P                = 19, ///< The key labeled P on a US keyboard.
    Q                = 20, ///< The key labeled Q on a US keyboard.
    R                = 21, ///< The key labeled R on a US keyboard.
    S                = 22, ///< The key labeled S on a US keyboard.
    T                = 23, ///< The key labeled T on a US keyboard.
    U                = 24, ///< The key labeled U on a US keyboard.
    V                = 25, ///< The key labeled V on a US keyboard.
    W                = 26, ///< The key labeled W on a US keyboard.
    X                = 27, ///< The key labeled X on a US keyboard.
    Y                = 28, ///< The key labeled Y on a US keyboard.
    Z                = 29, ///< The key labeled Z on a US keyboard.
    NUMBER_1         = 30, ///< The 1 key of the main block (US layout).
    NUMBER_2         = 31, ///< The 2 key of the main block (US layout).
    NUMBER_3         = 32, ///< The 3 key of the main block (US layout).
    NUMBER_4         = 33, ///< The 4 key of the main block (US layout).
    NUMBER_5         = 34, ///< The 5 key of the main block (US layout).
    NUMBER_6         = 35, ///< The 6 key of the main block (US layout).
    NUMBER_7         = 36, ///< The 7 key of the main block (US layout).
    NUMBER_8         = 37, ///< The 8 key of the main block (US layout).
    NUMBER_9         = 38, ///< The 9 key of the main block (US layout).
    NUMBER_0         = 39, ///< The 0 key of the main block (US layout).
    RETURN           = 40, ///< Return (Enter of the main block).
    ESCAPE           = 41, ///< Escape.
    BACKSPACE        = 42, ///< Backspace.
    TAB              = 43, ///< Tab.
    SPACE            = 44, ///< Space bar.
    MINUS            = 45, ///< Minus (US layout).
    EQUALS           = 46, ///< Equals (US layout).
    LEFT_BRACKET     = 47, ///< Left bracket (US layout).
    RIGHT_BRACKET    = 48, ///< Right bracket (US layout).
    BACKSLASH        = 49, ///< Backslash (US layout).
    NON_US_HASH      = 50, ///< ISO keyboards: the key left of Return.
    SEMICOLON        = 51, ///< Semicolon (US layout).
    APOSTROPHE       = 52, ///< Apostrophe (US layout).
    GRAVE            = 53, ///< Grave accent, left of 1 (US layout).
    COMMA            = 54, ///< Comma (US layout).
    PERIOD           = 55, ///< Period (US layout).
    SLASH            = 56, ///< Slash (US layout).
    CAPS_LOCK        = 57, ///< Caps Lock.
    F1               = 58, ///< Function key F1.
    F2               = 59, ///< Function key F2.
    F3               = 60, ///< Function key F3.
    F4               = 61, ///< Function key F4.
    F5               = 62, ///< Function key F5.
    F6               = 63, ///< Function key F6.
    F7               = 64, ///< Function key F7.
    F8               = 65, ///< Function key F8.
    F9               = 66, ///< Function key F9.
    F10              = 67, ///< Function key F10.
    F11              = 68, ///< Function key F11.
    F12              = 69, ///< Function key F12.
    PRINT_SCREEN     = 70, ///< Print Screen.
    SCROLL_LOCK      = 71, ///< Scroll Lock.
    PAUSE            = 72, ///< Pause.
    INSERT           = 73, ///< Insert.
    HOME             = 74, ///< Home.
    PAGE_UP          = 75, ///< Page Up.
    DELETE_FORWARD   = 76, ///< Delete (HID "Delete Forward"; DELETE is a <windows.h> macro).
    END              = 77, ///< End.
    PAGE_DOWN        = 78, ///< Page Down.
    RIGHT            = 79, ///< Right arrow.
    LEFT             = 80, ///< Left arrow.
    DOWN             = 81, ///< Down arrow.
    UP               = 82, ///< Up arrow.
    NUM_LOCK         = 83, ///< Num Lock (Clear on Mac keyboards).
    KEYPAD_DIVIDE    = 84, ///< Keypad /.
    KEYPAD_MULTIPLY  = 85, ///< Keypad *.
    KEYPAD_MINUS     = 86, ///< Keypad -.
    KEYPAD_PLUS      = 87, ///< Keypad +.
    KEYPAD_ENTER     = 88, ///< Keypad Enter.
    KEYPAD_1         = 89, ///< Keypad 1.
    KEYPAD_2         = 90, ///< Keypad 2.
    KEYPAD_3         = 91, ///< Keypad 3.
    KEYPAD_4         = 92, ///< Keypad 4.
    KEYPAD_5         = 93, ///< Keypad 5.
    KEYPAD_6         = 94, ///< Keypad 6.
    KEYPAD_7         = 95, ///< Keypad 7.
    KEYPAD_8         = 96, ///< Keypad 8.
    KEYPAD_9         = 97, ///< Keypad 9.
    KEYPAD_0         = 98, ///< Keypad 0.
    KEYPAD_PERIOD    = 99, ///< Keypad . (decimal separator).
    NON_US_BACKSLASH = 100, ///< ISO keyboards: the key between left Shift and Z.
    APPLICATION      = 101, ///< Application (context menu) key.
    POWER            = 102, ///< Power.
    KEYPAD_EQUALS    = 103, ///< Keypad =.
    F13              = 104, ///< Function key F13.
    F14              = 105, ///< Function key F14.
    F15              = 106, ///< Function key F15.
    F16              = 107, ///< Function key F16.
    F17              = 108, ///< Function key F17.
    F18              = 109, ///< Function key F18.
    F19              = 110, ///< Function key F19.
    F20              = 111, ///< Function key F20.
    F21              = 112, ///< Function key F21.
    F22              = 113, ///< Function key F22.
    F23              = 114, ///< Function key F23.
    F24              = 115, ///< Function key F24.
    EXECUTE          = 116, ///< Execute.
    HELP             = 117, ///< Help.
    MENU             = 118, ///< Menu.
    SELECT           = 119, ///< Select.
    STOP             = 120, ///< Stop.
    AGAIN            = 121, ///< Again (redo).
    UNDO             = 122, ///< Undo.
    CUT              = 123, ///< Cut.
    COPY             = 124, ///< Copy.
    PASTE            = 125, ///< Paste.
    FIND             = 126, ///< Find.
    MUTE             = 127, ///< Mute.
    VOLUME_UP        = 128, ///< Volume up.
    VOLUME_DOWN      = 129, ///< Volume down.
    KEYPAD_COMMA     = 133, ///< Keypad , (Brazilian keyboards).
    INTERNATIONAL_1  = 135, ///< International key 1 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_2  = 136, ///< International key 2 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_3  = 137, ///< International key 3 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_4  = 138, ///< International key 4 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_5  = 139, ///< International key 5 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_6  = 140, ///< International key 6 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_7  = 141, ///< International key 7 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_8  = 142, ///< International key 8 (e.g. Japanese and Korean keyboards).
    INTERNATIONAL_9  = 143, ///< International key 9 (e.g. Japanese and Korean keyboards).
    LANGUAGE_1       = 144, ///< Language key 1 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_2       = 145, ///< Language key 2 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_3       = 146, ///< Language key 3 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_4       = 147, ///< Language key 4 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_5       = 148, ///< Language key 5 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_6       = 149, ///< Language key 6 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_7       = 150, ///< Language key 7 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_8       = 151, ///< Language key 8 (e.g. Hangul/Hanja, Katakana).
    LANGUAGE_9       = 152, ///< Language key 9 (e.g. Hangul/Hanja, Katakana).
    LEFT_CONTROL     = 224, ///< Left Control.
    LEFT_SHIFT       = 225, ///< Left Shift.
    LEFT_ALT         = 226, ///< Left Alt (Option on Mac keyboards).
    LEFT_SUPER       = 227, ///< Left Super (Windows, Command).
    RIGHT_CONTROL    = 228, ///< Right Control.
    RIGHT_SHIFT      = 229, ///< Right Shift.
    RIGHT_ALT        = 230, ///< Right Alt (AltGr on many layouts).
    RIGHT_SUPER      = 231, ///< Right Super (Windows, Command).
};

/// @brief Logical key: the symbol the current keyboard layout gives a key (Ctrl+Z is the key labeled
///        Z). Printable keys are their Unicode code point (lowercase letters; any code point is a valid
///        KeyCode, the enumerators name the ASCII ones); non-printable keys are
///        KEY_CODE_NON_PRINTABLE_FLAG | their HID usage (SDL's keycode convention).
enum class KeyCode : std::uint32_t
{
    UNKNOWN           = 0x00000000, ///< No key, or a symbol gfx does not name (any other code point is valid too).
    BACKSPACE         = 0x00000008, ///< Backspace.
    TAB               = 0x00000009, ///< Tab.
    RETURN            = 0x0000000D, ///< Return.
    ESCAPE            = 0x0000001B, ///< Escape.
    SPACE             = 0x00000020, ///< Space.
    EXCLAMATION_MARK  = 0x00000021, ///< The character '!'.
    QUOTATION_MARK    = 0x00000022, ///< The character '"'.
    HASH              = 0x00000023, ///< The character '#'.
    DOLLAR            = 0x00000024, ///< The character '$'.
    PERCENT           = 0x00000025, ///< The character '%'.
    AMPERSAND         = 0x00000026, ///< The character '&'.
    APOSTROPHE        = 0x00000027, ///< The character '''.
    LEFT_PARENTHESIS  = 0x00000028, ///< The character '('.
    RIGHT_PARENTHESIS = 0x00000029, ///< The character ')'.
    ASTERISK          = 0x0000002A, ///< The character '*'.
    PLUS              = 0x0000002B, ///< The character '+'.
    COMMA             = 0x0000002C, ///< The character ','.
    MINUS             = 0x0000002D, ///< The character '-'.
    PERIOD            = 0x0000002E, ///< The character '.'.
    SLASH             = 0x0000002F, ///< The character '/'.
    NUMBER_0          = 0x00000030, ///< The character '0'.
    NUMBER_1          = 0x00000031, ///< The character '1'.
    NUMBER_2          = 0x00000032, ///< The character '2'.
    NUMBER_3          = 0x00000033, ///< The character '3'.
    NUMBER_4          = 0x00000034, ///< The character '4'.
    NUMBER_5          = 0x00000035, ///< The character '5'.
    NUMBER_6          = 0x00000036, ///< The character '6'.
    NUMBER_7          = 0x00000037, ///< The character '7'.
    NUMBER_8          = 0x00000038, ///< The character '8'.
    NUMBER_9          = 0x00000039, ///< The character '9'.
    COLON             = 0x0000003A, ///< The character ':'.
    SEMICOLON         = 0x0000003B, ///< The character ';'.
    LESS              = 0x0000003C, ///< The character '<'.
    EQUALS            = 0x0000003D, ///< The character '='.
    GREATER           = 0x0000003E, ///< The character '>'.
    QUESTION_MARK     = 0x0000003F, ///< The character '?'.
    AT                = 0x00000040, ///< The character '@'.
    LEFT_BRACKET      = 0x0000005B, ///< The character '['.
    BACKSLASH         = 0x0000005C, ///< The character '\'.
    RIGHT_BRACKET     = 0x0000005D, ///< The character ']'.
    CARET             = 0x0000005E, ///< The character '^'.
    UNDERSCORE        = 0x0000005F, ///< The character '_'.
    GRAVE             = 0x00000060, ///< The character '`'.
    A                 = 0x00000061, ///< The character 'a'.
    B                 = 0x00000062, ///< The character 'b'.
    C                 = 0x00000063, ///< The character 'c'.
    D                 = 0x00000064, ///< The character 'd'.
    E                 = 0x00000065, ///< The character 'e'.
    F                 = 0x00000066, ///< The character 'f'.
    G                 = 0x00000067, ///< The character 'g'.
    H                 = 0x00000068, ///< The character 'h'.
    I                 = 0x00000069, ///< The character 'i'.
    J                 = 0x0000006A, ///< The character 'j'.
    K                 = 0x0000006B, ///< The character 'k'.
    L                 = 0x0000006C, ///< The character 'l'.
    M                 = 0x0000006D, ///< The character 'm'.
    N                 = 0x0000006E, ///< The character 'n'.
    O                 = 0x0000006F, ///< The character 'o'.
    P                 = 0x00000070, ///< The character 'p'.
    Q                 = 0x00000071, ///< The character 'q'.
    R                 = 0x00000072, ///< The character 'r'.
    S                 = 0x00000073, ///< The character 's'.
    T                 = 0x00000074, ///< The character 't'.
    U                 = 0x00000075, ///< The character 'u'.
    V                 = 0x00000076, ///< The character 'v'.
    W                 = 0x00000077, ///< The character 'w'.
    X                 = 0x00000078, ///< The character 'x'.
    Y                 = 0x00000079, ///< The character 'y'.
    Z                 = 0x0000007A, ///< The character 'z'.
    LEFT_BRACE        = 0x0000007B, ///< The character '{'.
    PIPE              = 0x0000007C, ///< The character '|'.
    RIGHT_BRACE       = 0x0000007D, ///< The character '}'.
    TILDE             = 0x0000007E, ///< The character '~'.
    DELETE_FORWARD    = 0x0000007F, ///< Delete (DELETE is a <windows.h> macro).
    CAPS_LOCK         = 0x40000039, ///< Caps Lock.
    F1                = 0x4000003A, ///< Function key F1.
    F2                = 0x4000003B, ///< Function key F2.
    F3                = 0x4000003C, ///< Function key F3.
    F4                = 0x4000003D, ///< Function key F4.
    F5                = 0x4000003E, ///< Function key F5.
    F6                = 0x4000003F, ///< Function key F6.
    F7                = 0x40000040, ///< Function key F7.
    F8                = 0x40000041, ///< Function key F8.
    F9                = 0x40000042, ///< Function key F9.
    F10               = 0x40000043, ///< Function key F10.
    F11               = 0x40000044, ///< Function key F11.
    F12               = 0x40000045, ///< Function key F12.
    PRINT_SCREEN      = 0x40000046, ///< Print Screen.
    SCROLL_LOCK       = 0x40000047, ///< Scroll Lock.
    PAUSE             = 0x40000048, ///< Pause.
    INSERT            = 0x40000049, ///< Insert.
    HOME              = 0x4000004A, ///< Home.
    PAGE_UP           = 0x4000004B, ///< Page Up.
    END               = 0x4000004D, ///< End.
    PAGE_DOWN         = 0x4000004E, ///< Page Down.
    RIGHT             = 0x4000004F, ///< Right arrow.
    LEFT              = 0x40000050, ///< Left arrow.
    DOWN              = 0x40000051, ///< Down arrow.
    UP                = 0x40000052, ///< Up arrow.
    NUM_LOCK          = 0x40000053, ///< Num Lock (Clear on Mac keyboards).
    KEYPAD_DIVIDE     = 0x40000054, ///< Keypad /.
    KEYPAD_MULTIPLY   = 0x40000055, ///< Keypad *.
    KEYPAD_MINUS      = 0x40000056, ///< Keypad -.
    KEYPAD_PLUS       = 0x40000057, ///< Keypad +.
    KEYPAD_ENTER      = 0x40000058, ///< Keypad Enter.
    KEYPAD_1          = 0x40000059, ///< Keypad 1.
    KEYPAD_2          = 0x4000005A, ///< Keypad 2.
    KEYPAD_3          = 0x4000005B, ///< Keypad 3.
    KEYPAD_4          = 0x4000005C, ///< Keypad 4.
    KEYPAD_5          = 0x4000005D, ///< Keypad 5.
    KEYPAD_6          = 0x4000005E, ///< Keypad 6.
    KEYPAD_7          = 0x4000005F, ///< Keypad 7.
    KEYPAD_8          = 0x40000060, ///< Keypad 8.
    KEYPAD_9          = 0x40000061, ///< Keypad 9.
    KEYPAD_0          = 0x40000062, ///< Keypad 0.
    KEYPAD_PERIOD     = 0x40000063, ///< Keypad . (decimal separator).
    APPLICATION       = 0x40000065, ///< Application (context menu) key.
    POWER             = 0x40000066, ///< Power.
    KEYPAD_EQUALS     = 0x40000067, ///< Keypad =.
    F13               = 0x40000068, ///< Function key F13.
    F14               = 0x40000069, ///< Function key F14.
    F15               = 0x4000006A, ///< Function key F15.
    F16               = 0x4000006B, ///< Function key F16.
    F17               = 0x4000006C, ///< Function key F17.
    F18               = 0x4000006D, ///< Function key F18.
    F19               = 0x4000006E, ///< Function key F19.
    F20               = 0x4000006F, ///< Function key F20.
    F21               = 0x40000070, ///< Function key F21.
    F22               = 0x40000071, ///< Function key F22.
    F23               = 0x40000072, ///< Function key F23.
    F24               = 0x40000073, ///< Function key F24.
    EXECUTE           = 0x40000074, ///< Execute.
    HELP              = 0x40000075, ///< Help.
    MENU              = 0x40000076, ///< Menu.
    SELECT            = 0x40000077, ///< Select.
    STOP              = 0x40000078, ///< Stop.
    AGAIN             = 0x40000079, ///< Again (redo).
    UNDO              = 0x4000007A, ///< Undo.
    CUT               = 0x4000007B, ///< Cut.
    COPY              = 0x4000007C, ///< Copy.
    PASTE             = 0x4000007D, ///< Paste.
    FIND              = 0x4000007E, ///< Find.
    MUTE              = 0x4000007F, ///< Mute.
    VOLUME_UP         = 0x40000080, ///< Volume up.
    VOLUME_DOWN       = 0x40000081, ///< Volume down.
    KEYPAD_COMMA      = 0x40000085, ///< Keypad , (Brazilian keyboards).
    LEFT_CONTROL      = 0x400000E0, ///< Left Control.
    LEFT_SHIFT        = 0x400000E1, ///< Left Shift.
    LEFT_ALT          = 0x400000E2, ///< Left Alt (Option on Mac keyboards).
    LEFT_SUPER        = 0x400000E3, ///< Left Super (Windows, Command).
    RIGHT_CONTROL     = 0x400000E4, ///< Right Control.
    RIGHT_SHIFT       = 0x400000E5, ///< Right Shift.
    RIGHT_ALT         = 0x400000E6, ///< Right Alt (AltGr on many layouts).
    RIGHT_SUPER       = 0x400000E7, ///< Right Super (Windows, Command).
};

/// @brief Windowing library or system a platform backend is built on.
enum class PlatformBackendKind : std::uint8_t
{
    SDL3,    ///< SDL 3 (Windows, Linux X11/Wayland, macOS, mobile, web).
    GLFW,    ///< GLFW 3.4 (Windows, Linux X11/Wayland, macOS).
    WINDOWS, ///< Native Win32 (not implemented).
    XCB,     ///< Native X11 through XCB (not implemented).
    COCOA,   ///< Native macOS Cocoa (not implemented).
    WEB,     ///< Native web through Emscripten (not implemented).
};

/// @brief Optional capability of a platform backend (Platform::supports()).
enum class PlatformFeature : std::uint8_t
{
    WINDOW_ICON,            ///< Window::set_icon().
    WINDOW_POSITION,        ///< Window::position() and set_position() (not on Wayland).
    EXCLUSIVE_FULLSCREEN,   ///< WindowMode::EXCLUSIVE_FULLSCREEN with a video mode.
    WINDOW_OCCLUSION,       ///< Window::is_occluded() and WINDOW_OCCLUDED events.
    WINDOW_HOVER,           ///< Window::is_hovered().
    HIGH_PIXEL_DENSITY,     ///< WindowFlag::HIGH_PIXEL_DENSITY: framebuffers in pixels on high-density displays.
    CUSTOM_CURSORS,         ///< Platform::create_cursor() from RGBA images.
    CAPTURED_CURSOR,        ///< CursorMode::CAPTURED: hidden, locked cursor with relative motion.
    RAW_MOUSE_MOTION,       ///< Unaccelerated mouse motion in CursorMode::CAPTURED.
    TEXT_COMPOSITION,       ///< IME composition: TEXT_COMPOSITION events while text input is active.
    CLIPBOARD_IMAGE,        ///< Platform::clipboard_image() and set_clipboard_image().
    DROP_TEXT,              ///< DROP_TEXT events (text dropped on a window).
    TOUCH,                  ///< Touch screens and touch pads: TOUCH_* events.
    GAMEPAD_RUMBLE,         ///< InputDevices::set_gamepad_rumble().
    GAMEPAD_TRIGGER_RUMBLE, ///< InputDevices::set_gamepad_trigger_rumble() (impulse triggers).
    GAMEPAD_LED,            ///< InputDevices::set_gamepad_led().
    GAMEPAD_TOUCHPAD,       ///< GAMEPAD_TOUCHPAD_* events (e.g. PlayStation touchpads).
    GAMEPAD_SENSORS,        ///< Gamepad gyroscopes and accelerometers.
};

/// @brief How a window occupies the screen.
enum class WindowMode : std::uint8_t
{
    WINDOWED,              ///< Decorated window (title bar, borders).
    BORDERLESS,            ///< Window without decorations.
    BORDERLESS_FULLSCREEN, ///< Borderless window covering a monitor at the desktop's video mode.
    EXCLUSIVE_FULLSCREEN,  ///< Fullscreen with a video mode of its own (WindowDescription::video_mode).
};

/// @brief Options of a window, validated when it is created. Bitmask.
enum class WindowFlag : std::uint16_t
{
    NONE                    = 0, ///< No bit.
    RESIZABLE               = 1U << 0, ///< The user can resize the window.
    HIDDEN                  = 1U << 1, ///< Created hidden (Window::show() reveals it).
    ALWAYS_ON_TOP           = 1U << 2, ///< Kept above other windows.
    TRANSPARENT_FRAMEBUFFER = 1U << 3, ///< The framebuffer's alpha blends with the desktop.
    HIGH_PIXEL_DENSITY      = 1U << 4, ///< Framebuffer in pixels on high-density displays (else in window coordinates).
    FOCUS_ON_SHOW           = 1U << 5, ///< Takes the input focus when shown.
    MOUSE_PASSTHROUGH       = 1U << 6, ///< Mouse input passes through to the windows below.
    UTILITY                 = 1U << 7, ///< Utility window (no task bar entry).
};

/// @brief Graphics API a window is created for (some backends prepare the window for it).
enum class SurfaceApi : std::uint8_t
{
    NONE,       ///< No graphics API (or one that needs no preparation).
    VULKAN,     ///< Vulkan (SDL3: SDL_WINDOW_VULKAN; GLFW: GLFW_NO_API).
    DIRECT3D12, ///< Direct3D 12.
    METAL,      ///< Metal (the backend attaches a CAMetalLayer).
    WEBGPU,     ///< WebGPU.
};

/// @brief Standard cursor shapes of the operating system.
enum class CursorShape : std::uint8_t
{
    ARROW,                      ///< Default arrow.
    I_BEAM,                     ///< Text selection (I-beam; TEXT is a <windows.h> macro).
    WAIT,                       ///< Busy (hourglass, spinning wheel).
    PROGRESS,                   ///< Busy in the background (arrow with a wait indicator).
    CROSSHAIR,                  ///< Crosshair.
    POINTER,                    ///< Pointing hand (links).
    RESIZE_EAST_WEST,           ///< Horizontal resize.
    RESIZE_NORTH_SOUTH,         ///< Vertical resize.
    RESIZE_NORTHWEST_SOUTHEAST, ///< Diagonal resize (top-left / bottom-right).
    RESIZE_NORTHEAST_SOUTHWEST, ///< Diagonal resize (top-right / bottom-left).
    MOVE,                       ///< Move (four arrows).
    NOT_ALLOWED,                ///< Action not allowed.
};

/// @brief How a window treats the mouse cursor.
enum class CursorMode : std::uint8_t
{
    NORMAL,   ///< Visible and free.
    HIDDEN,   ///< Invisible over the window, free to leave it.
    CONFINED, ///< Visible, kept inside the window.
    CAPTURED, ///< Invisible and locked; MOUSE_MOTION reports relative motion (cameras).
};

/// @brief Mouse buttons.
enum class MouseButton : std::uint8_t
{
    LEFT,    ///< Left (primary) button.
    MIDDLE,  ///< Middle button (wheel click).
    RIGHT,   ///< Right (secondary) button.
    BACK,    ///< First side button (browser back).
    FORWARD, ///< Second side button (browser forward).
};

/// @brief Mouse buttons held at once. Bitmask.
enum class MouseButtonMask : std::uint8_t
{
    NONE    = 0, ///< No bit.
    LEFT    = 1U << 0, ///< Left button held.
    MIDDLE  = 1U << 1, ///< Middle button held.
    RIGHT   = 1U << 2, ///< Right button held.
    BACK    = 1U << 3, ///< First side button held.
    FORWARD = 1U << 4, ///< Second side button held.
};

/// @brief Keyboard modifiers held during an event. Bitmask.
enum class KeyModifier : std::uint16_t
{
    NONE          = 0, ///< No bit.
    LEFT_SHIFT    = 1U << 0, ///< Left Shift.
    RIGHT_SHIFT   = 1U << 1, ///< Right Shift.
    LEFT_CONTROL  = 1U << 2, ///< Left Control.
    RIGHT_CONTROL = 1U << 3, ///< Right Control.
    LEFT_ALT      = 1U << 4, ///< Left Alt.
    RIGHT_ALT     = 1U << 5, ///< Right Alt (AltGr).
    LEFT_SUPER    = 1U << 6, ///< Left Super (Windows, Command).
    RIGHT_SUPER   = 1U << 7, ///< Right Super.
    CAPS_LOCK     = 1U << 8, ///< Caps Lock is on.
    NUM_LOCK      = 1U << 9, ///< Num Lock is on.
    SCROLL_LOCK   = 1U << 10, ///< Scroll Lock is on.
    SHIFT         = LEFT_SHIFT | RIGHT_SHIFT, ///< Either Shift key.
    CONTROL       = LEFT_CONTROL | RIGHT_CONTROL, ///< Either Control key.
    ALT           = LEFT_ALT | RIGHT_ALT, ///< Either Alt key.
    SUPER         = LEFT_SUPER | RIGHT_SUPER, ///< Either Super key.
};

/// @brief Gamepad buttons, named by position (south = A on Xbox, Cross on PlayStation, B on Nintendo).
enum class GamepadButton : std::uint8_t
{
    SOUTH,           ///< Bottom face button.
    EAST,            ///< Right face button.
    WEST,            ///< Left face button.
    NORTH,           ///< Top face button.
    BACK,            ///< Back (View, Select, Share, Minus).
    GUIDE,           ///< Guide (Xbox, PS, Home).
    START,           ///< Start (Menu, Options, Plus).
    LEFT_STICK,      ///< Left stick click.
    RIGHT_STICK,     ///< Right stick click.
    LEFT_SHOULDER,   ///< Left shoulder (bumper).
    RIGHT_SHOULDER,  ///< Right shoulder (bumper).
    DPAD_UP,         ///< D-pad up.
    DPAD_DOWN,       ///< D-pad down.
    DPAD_LEFT,       ///< D-pad left.
    DPAD_RIGHT,      ///< D-pad right.
    MISCELLANEOUS_1, ///< Extra button (Xbox Share, PS5 microphone, Switch capture).
    RIGHT_PADDLE_1,  ///< Upper right paddle (Xbox Elite P1).
    LEFT_PADDLE_1,   ///< Upper left paddle (Xbox Elite P3).
    RIGHT_PADDLE_2,  ///< Lower right paddle (Xbox Elite P2).
    LEFT_PADDLE_2,   ///< Lower left paddle (Xbox Elite P4).
    TOUCHPAD,        ///< Touchpad click (PlayStation).
};

/// @brief Gamepad axes: sticks in [-1, 1] (y down is positive), triggers in [0, 1].
enum class GamepadAxis : std::uint8_t
{
    LEFT_X,        ///< Left stick, horizontal.
    LEFT_Y,        ///< Left stick, vertical.
    RIGHT_X,       ///< Right stick, horizontal.
    RIGHT_Y,       ///< Right stick, vertical.
    LEFT_TRIGGER,  ///< Left trigger.
    RIGHT_TRIGGER, ///< Right trigger.
};

/// @brief Family of a gamepad (button labels and features differ).
enum class GamepadType : std::uint8_t
{
    UNKNOWN,                      ///< Not recognized.
    STANDARD,                     ///< Generic gamepad with the standard layout.
    XBOX_360,                     ///< Xbox 360 controller.
    XBOX_ONE,                     ///< Xbox One / Series controller.
    PLAYSTATION_3,                ///< DualShock 3.
    PLAYSTATION_4,                ///< DualShock 4.
    PLAYSTATION_5,                ///< DualSense.
    NINTENDO_SWITCH_PRO,          ///< Nintendo Switch Pro Controller.
    NINTENDO_SWITCH_JOYCON_LEFT,  ///< Left Joy-Con alone.
    NINTENDO_SWITCH_JOYCON_RIGHT, ///< Right Joy-Con alone.
    NINTENDO_SWITCH_JOYCON_PAIR,  ///< Two Joy-Cons used as one gamepad.
};

/// @brief Directions a joystick hat is pushed to (NONE = centered). Bitmask.
enum class HatPosition : std::uint8_t
{
    NONE  = 0, ///< No bit.
    UP    = 1U << 0, ///< Up.
    RIGHT = 1U << 1, ///< Right.
    DOWN  = 1U << 2, ///< Down.
    LEFT  = 1U << 3, ///< Left.
};

/// @brief Kind of touch device.
enum class TouchDeviceType : std::uint8_t
{
    DIRECT,            ///< Touch screen: positions are on the display.
    INDIRECT_ABSOLUTE, ///< Touch pad mapped to the display (absolute positions).
    INDIRECT_RELATIVE, ///< Touch pad moving the cursor (relative motion).
};

/// @brief Kind of an Event; each kind carries one payload type (Event::get_if()).
enum class EventType : std::uint8_t
{
    NONE,                         ///< No event.
    WINDOW_SHOWN,                 ///< The window became visible.
    WINDOW_HIDDEN,                ///< The window was hidden.
    WINDOW_EXPOSED,               ///< Part of the window must be redrawn.
    WINDOW_MOVED,                 ///< The window moved (WindowEvent::position).
    WINDOW_RESIZED,               ///< The window's size changed (WindowEvent::size, window coordinates).
    WINDOW_FRAMEBUFFER_RESIZED,   ///< The framebuffer's size changed (WindowEvent::framebuffer_size, pixels): recreate the swapchain.
    WINDOW_CONTENT_SCALE_CHANGED, ///< The content scale changed (WindowEvent::content_scale): rescale the UI.
    WINDOW_MINIMIZED,             ///< The window was minimized.
    WINDOW_MAXIMIZED,             ///< The window was maximized.
    WINDOW_RESTORED,              ///< The window left the minimized or maximized state.
    WINDOW_FOCUS_GAINED,          ///< The window received the keyboard focus.
    WINDOW_FOCUS_LOST,            ///< The window lost the keyboard focus.
    WINDOW_OCCLUDED,              ///< The window became fully covered.
    WINDOW_MOUSE_ENTERED,         ///< The mouse entered the window.
    WINDOW_MOUSE_LEFT,            ///< The mouse left the window.
    WINDOW_CLOSE_REQUESTED,       ///< The user asked to close the window (the application decides).
    KEY_DOWN,                     ///< A key was pressed (KeyboardEvent::repeat for key repeat).
    KEY_UP,                       ///< A key was released.
    TEXT_INPUT,                   ///< Text was entered (TextInputEvent; UTF-8 in the event span's text).
    TEXT_COMPOSITION,             ///< IME composition text changed (TextCompositionEvent).
    MOUSE_MOTION,                 ///< The mouse moved.
    MOUSE_BUTTON_DOWN,            ///< A mouse button was pressed.
    MOUSE_BUTTON_UP,              ///< A mouse button was released.
    MOUSE_WHEEL,                  ///< The mouse wheel turned.
    GAMEPAD_CONNECTED,            ///< A gamepad was connected.
    GAMEPAD_DISCONNECTED,         ///< A gamepad was disconnected.
    GAMEPAD_REMAPPED,             ///< A gamepad's mapping changed.
    GAMEPAD_BUTTON_DOWN,          ///< A gamepad button was pressed.
    GAMEPAD_BUTTON_UP,            ///< A gamepad button was released.
    GAMEPAD_AXIS_MOTION,          ///< A gamepad axis moved.
    GAMEPAD_TOUCHPAD_DOWN,        ///< A finger touched a gamepad touchpad.
    GAMEPAD_TOUCHPAD_MOTION,      ///< A finger moved on a gamepad touchpad.
    GAMEPAD_TOUCHPAD_UP,          ///< A finger left a gamepad touchpad.
    JOYSTICK_CONNECTED,           ///< A joystick (without gamepad mapping) was connected.
    JOYSTICK_DISCONNECTED,        ///< A joystick was disconnected.
    JOYSTICK_BUTTON_DOWN,         ///< A joystick button was pressed.
    JOYSTICK_BUTTON_UP,           ///< A joystick button was released.
    JOYSTICK_AXIS_MOTION,         ///< A joystick axis moved.
    JOYSTICK_HAT_MOTION,          ///< A joystick hat moved.
    TOUCH_DOWN,                   ///< A finger touched a touch device.
    TOUCH_MOTION,                 ///< A finger moved on a touch device.
    TOUCH_UP,                     ///< A finger left a touch device.
    TOUCH_CANCELED,               ///< A touch was canceled (e.g. taken over by a system gesture).
    DROP_FILE,                    ///< A file was dropped on the window (DropEvent; UTF-8 path in the event span's text).
    DROP_TEXT,                    ///< Text was dropped on the window.
    MONITOR_CONNECTED,            ///< A monitor was connected.
    MONITOR_DISCONNECTED,         ///< A monitor was disconnected (its handle becomes stale).
    QUIT_REQUESTED,               ///< The application was asked to quit (last window closed, system shutdown, signal).
    KEYBOARD_LAYOUT_CHANGED,      ///< The keyboard layout changed: key codes and key names may differ now.
    CLIPBOARD_UPDATED,            ///< The clipboard's contents changed.
};

/// @brief How a platform backend pumps events (Platform::poll_events(), wait_events(), wait_events_timeout()).
enum class EventWaitMode : std::uint8_t
{
    POLL,              ///< Return at once with the pending events.
    WAIT,              ///< Block until at least one event arrives.
    WAIT_WITH_TIMEOUT, ///< Block until an event arrives or the timeout expires.
};

/// @brief Kind of value an action produces (Unreal Enhanced Input's value types).
enum class ActionValueType : std::uint8_t
{
    BUTTON,  ///< Pressed or not (analog sources press it above the actuation threshold).
    AXIS_1D, ///< One signed value (throttle, zoom).
    AXIS_2D, ///< Two values, x right and y up (movement, camera).
};

/// @brief Kind of input a binding reads.
enum class InputSourceKind : std::uint8_t
{
    NONE,           ///< No source.
    KEY,            ///< A physical key (Key).
    KEY_CODE,       ///< A logical key (KeyCode): the key the current layout labels so.
    MOUSE_BUTTON,   ///< A mouse button (MouseButton).
    MOUSE_AXIS,     ///< Mouse motion or wheel (MouseAxis).
    GAMEPAD_BUTTON, ///< A gamepad button (GamepadButton).
    GAMEPAD_AXIS,   ///< A gamepad axis (GamepadAxis).
};

/// @brief How a binding turns its sources into a value.
enum class BindingKind : std::uint8_t
{
    NONE,           ///< No binding (removed).
    BUTTON,         ///< One digital or analog source: key, key code, mouse button, gamepad button or axis.
    CHORD,          ///< A key or key code with required modifiers (Ctrl+S).
    GAMEPAD_CHORD,  ///< Up to four gamepad buttons held together.
    MOUSE_AXIS,     ///< Mouse motion or wheel during the frame.
    COMPOSITE_AXIS, ///< Two digital sources: positive minus negative (1D).
    COMPOSITE_2D,   ///< Four digital sources: up, down, left, right (2D, e.g. WASD).
    STICK,          ///< Two gamepad axes (2D; y flipped so that up is positive).
};

/// @brief How a dead zone filters a value (Unreal's dead zone types).
enum class DeadzoneKind : std::uint8_t
{
    NONE,          ///< No dead zone.
    AXIAL,         ///< Each axis separately, rescaled from the lower to the upper threshold.
    RADIAL,        ///< The vector's length below the lower threshold becomes 0; the rest is unchanged.
    SCALED_RADIAL, ///< The vector's length rescaled from the lower to the upper threshold (smooth sticks).
};

/// @brief How a value's magnitude maps to the action's (finer control near the center).
enum class ResponseCurve : std::uint8_t
{
    LINEAR,    ///< Unchanged.
    QUADRATIC, ///< Squared.
    CUBIC,     ///< Cubed.
    POWER,     ///< Raised to the binding's exponent.
};

/// @brief When a binding presses its action (Unreal's triggers).
enum class TriggerKind : std::uint8_t
{
    DOWN, ///< While the binding is actuated.
    HOLD, ///< While the binding stays actuated, after its hold time.
    TAP,  ///< For one frame, when the binding is released within its tap time.
};

/// @brief Mouse motion and wheel axes, as accumulated during a frame.
enum class MouseAxis : std::uint8_t
{
    MOTION_X, ///< Horizontal motion in window coordinates (relative motion when captured).
    MOTION_Y, ///< Vertical motion (down is positive, as on screen).
    WHEEL_X,  ///< Horizontal wheel.
    WHEEL_Y,  ///< Vertical wheel (away from the user is positive).
};

/// @brief How gamepads find their player.
enum class GamepadAssignment : std::uint8_t
{
    AUTOMATIC, ///< A gamepad that connects goes to the first player without one.
    MANUAL,    ///< Gamepads go to the player InputSystem::assign_gamepad() names.
};

/// @brief What is wrong with an input schema (validate_input_schema()).
enum class InputSchemaProblem : std::uint8_t
{
    NONE,                   ///< Nothing.
    MISSING_NAME,           ///< An action or a binding context has an empty name.
    DUPLICATE_ACTION,       ///< Two actions have the same name (or names whose hashes collide).
    DUPLICATE_CONTEXT,      ///< Two binding contexts have the same name (or names whose hashes collide).
    UNKNOWN_ACTION,         ///< A binding names an action the schema does not declare.
    UNKNOWN_CONTEXT,        ///< A binding names a context the schema does not declare.
    MALFORMED_BINDING,      ///< A binding lacks the sources its kind needs (e.g. COMPOSITE_2D needs four digital sources).
    INCOMPATIBLE_BINDING,   ///< A two-dimensional binding (COMPOSITE_2D, STICK) on an action that is not AXIS_2D.
    TRIGGER_ON_AXIS,        ///< A HOLD or TAP trigger on an action that is not a BUTTON.
    INVALID_TRIGGER_TIME,   ///< A HOLD or TAP trigger without a positive time.
    INVALID_DEADZONE,       ///< A dead zone whose thresholds are not 0 <= lower < upper <= 1.
    INVALID_RESPONSE_CURVE, ///< A POWER response curve whose exponent is not positive.
};

/// @brief Kinds of input InputSystem::capture_next_binding() listens to. Bitmask.
enum class BindingCaptureSource : std::uint8_t
{
    NONE           = 0, ///< No bit.
    KEYBOARD       = 1U << 0, ///< Keys.
    MOUSE_BUTTON   = 1U << 1, ///< Mouse buttons.
    GAMEPAD_BUTTON = 1U << 2, ///< Gamepad buttons.
    GAMEPAD_AXIS   = 1U << 3, ///< Gamepad axes pushed past the actuation threshold.
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief Bit set in the KeyCode of every non-printable key, above its HID usage.
inline constexpr std::uint32_t KEY_CODE_NON_PRINTABLE_FLAG = 0x40000000U;

/// @brief Largest size of an Event, in bytes (checked at compile time).
inline constexpr std::size_t MAXIMUM_EVENT_SIZE = 64;

/// @brief Events an EventQueue holds before growing (a queue grows only when a pump exceeds it).
inline constexpr std::size_t DEFAULT_EVENT_QUEUE_CAPACITY = 1024;

/// @brief Bytes of text (typed text, compositions, dropped paths) an EventQueue holds before growing.
inline constexpr std::size_t DEFAULT_EVENT_TEXT_CAPACITY = 16384;

/// @brief Most local players an InputSystem routes devices to.
inline constexpr std::uint32_t MAXIMUM_PLAYERS = 8;

/// @brief Magnitude at which an analog source presses a BUTTON action (InputSystemDescription default).
inline constexpr float DEFAULT_ACTUATION_THRESHOLD = 0.5F;

/// @brief Most sources a binding reads (COMPOSITE_2D reads four, GAMEPAD_CHORD up to four).
inline constexpr std::size_t MAXIMUM_BINDING_SOURCES = 4;

/// @brief Version of the saved-bindings format (InputSystem::save_bindings()).
inline constexpr int BINDING_STORAGE_VERSION = 1;

} // namespace gfx::platform

// =================================================================================================
// Enumeration descriptions
// =================================================================================================
export namespace gfx
{

template<>
struct EnumerationTraits<platform::Key>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr bool IS_SPARSE = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::Key::UNKNOWN, "UNKNOWN"},
        EnumeratorName{platform::Key::A, "A"},
        EnumeratorName{platform::Key::B, "B"},
        EnumeratorName{platform::Key::C, "C"},
        EnumeratorName{platform::Key::D, "D"},
        EnumeratorName{platform::Key::E, "E"},
        EnumeratorName{platform::Key::F, "F"},
        EnumeratorName{platform::Key::G, "G"},
        EnumeratorName{platform::Key::H, "H"},
        EnumeratorName{platform::Key::I, "I"},
        EnumeratorName{platform::Key::J, "J"},
        EnumeratorName{platform::Key::K, "K"},
        EnumeratorName{platform::Key::L, "L"},
        EnumeratorName{platform::Key::M, "M"},
        EnumeratorName{platform::Key::N, "N"},
        EnumeratorName{platform::Key::O, "O"},
        EnumeratorName{platform::Key::P, "P"},
        EnumeratorName{platform::Key::Q, "Q"},
        EnumeratorName{platform::Key::R, "R"},
        EnumeratorName{platform::Key::S, "S"},
        EnumeratorName{platform::Key::T, "T"},
        EnumeratorName{platform::Key::U, "U"},
        EnumeratorName{platform::Key::V, "V"},
        EnumeratorName{platform::Key::W, "W"},
        EnumeratorName{platform::Key::X, "X"},
        EnumeratorName{platform::Key::Y, "Y"},
        EnumeratorName{platform::Key::Z, "Z"},
        EnumeratorName{platform::Key::NUMBER_1, "NUMBER_1"},
        EnumeratorName{platform::Key::NUMBER_2, "NUMBER_2"},
        EnumeratorName{platform::Key::NUMBER_3, "NUMBER_3"},
        EnumeratorName{platform::Key::NUMBER_4, "NUMBER_4"},
        EnumeratorName{platform::Key::NUMBER_5, "NUMBER_5"},
        EnumeratorName{platform::Key::NUMBER_6, "NUMBER_6"},
        EnumeratorName{platform::Key::NUMBER_7, "NUMBER_7"},
        EnumeratorName{platform::Key::NUMBER_8, "NUMBER_8"},
        EnumeratorName{platform::Key::NUMBER_9, "NUMBER_9"},
        EnumeratorName{platform::Key::NUMBER_0, "NUMBER_0"},
        EnumeratorName{platform::Key::RETURN, "RETURN"},
        EnumeratorName{platform::Key::ESCAPE, "ESCAPE"},
        EnumeratorName{platform::Key::BACKSPACE, "BACKSPACE"},
        EnumeratorName{platform::Key::TAB, "TAB"},
        EnumeratorName{platform::Key::SPACE, "SPACE"},
        EnumeratorName{platform::Key::MINUS, "MINUS"},
        EnumeratorName{platform::Key::EQUALS, "EQUALS"},
        EnumeratorName{platform::Key::LEFT_BRACKET, "LEFT_BRACKET"},
        EnumeratorName{platform::Key::RIGHT_BRACKET, "RIGHT_BRACKET"},
        EnumeratorName{platform::Key::BACKSLASH, "BACKSLASH"},
        EnumeratorName{platform::Key::NON_US_HASH, "NON_US_HASH"},
        EnumeratorName{platform::Key::SEMICOLON, "SEMICOLON"},
        EnumeratorName{platform::Key::APOSTROPHE, "APOSTROPHE"},
        EnumeratorName{platform::Key::GRAVE, "GRAVE"},
        EnumeratorName{platform::Key::COMMA, "COMMA"},
        EnumeratorName{platform::Key::PERIOD, "PERIOD"},
        EnumeratorName{platform::Key::SLASH, "SLASH"},
        EnumeratorName{platform::Key::CAPS_LOCK, "CAPS_LOCK"},
        EnumeratorName{platform::Key::F1, "F1"},
        EnumeratorName{platform::Key::F2, "F2"},
        EnumeratorName{platform::Key::F3, "F3"},
        EnumeratorName{platform::Key::F4, "F4"},
        EnumeratorName{platform::Key::F5, "F5"},
        EnumeratorName{platform::Key::F6, "F6"},
        EnumeratorName{platform::Key::F7, "F7"},
        EnumeratorName{platform::Key::F8, "F8"},
        EnumeratorName{platform::Key::F9, "F9"},
        EnumeratorName{platform::Key::F10, "F10"},
        EnumeratorName{platform::Key::F11, "F11"},
        EnumeratorName{platform::Key::F12, "F12"},
        EnumeratorName{platform::Key::PRINT_SCREEN, "PRINT_SCREEN"},
        EnumeratorName{platform::Key::SCROLL_LOCK, "SCROLL_LOCK"},
        EnumeratorName{platform::Key::PAUSE, "PAUSE"},
        EnumeratorName{platform::Key::INSERT, "INSERT"},
        EnumeratorName{platform::Key::HOME, "HOME"},
        EnumeratorName{platform::Key::PAGE_UP, "PAGE_UP"},
        EnumeratorName{platform::Key::DELETE_FORWARD, "DELETE_FORWARD"},
        EnumeratorName{platform::Key::END, "END"},
        EnumeratorName{platform::Key::PAGE_DOWN, "PAGE_DOWN"},
        EnumeratorName{platform::Key::RIGHT, "RIGHT"},
        EnumeratorName{platform::Key::LEFT, "LEFT"},
        EnumeratorName{platform::Key::DOWN, "DOWN"},
        EnumeratorName{platform::Key::UP, "UP"},
        EnumeratorName{platform::Key::NUM_LOCK, "NUM_LOCK"},
        EnumeratorName{platform::Key::KEYPAD_DIVIDE, "KEYPAD_DIVIDE"},
        EnumeratorName{platform::Key::KEYPAD_MULTIPLY, "KEYPAD_MULTIPLY"},
        EnumeratorName{platform::Key::KEYPAD_MINUS, "KEYPAD_MINUS"},
        EnumeratorName{platform::Key::KEYPAD_PLUS, "KEYPAD_PLUS"},
        EnumeratorName{platform::Key::KEYPAD_ENTER, "KEYPAD_ENTER"},
        EnumeratorName{platform::Key::KEYPAD_1, "KEYPAD_1"},
        EnumeratorName{platform::Key::KEYPAD_2, "KEYPAD_2"},
        EnumeratorName{platform::Key::KEYPAD_3, "KEYPAD_3"},
        EnumeratorName{platform::Key::KEYPAD_4, "KEYPAD_4"},
        EnumeratorName{platform::Key::KEYPAD_5, "KEYPAD_5"},
        EnumeratorName{platform::Key::KEYPAD_6, "KEYPAD_6"},
        EnumeratorName{platform::Key::KEYPAD_7, "KEYPAD_7"},
        EnumeratorName{platform::Key::KEYPAD_8, "KEYPAD_8"},
        EnumeratorName{platform::Key::KEYPAD_9, "KEYPAD_9"},
        EnumeratorName{platform::Key::KEYPAD_0, "KEYPAD_0"},
        EnumeratorName{platform::Key::KEYPAD_PERIOD, "KEYPAD_PERIOD"},
        EnumeratorName{platform::Key::NON_US_BACKSLASH, "NON_US_BACKSLASH"},
        EnumeratorName{platform::Key::APPLICATION, "APPLICATION"},
        EnumeratorName{platform::Key::POWER, "POWER"},
        EnumeratorName{platform::Key::KEYPAD_EQUALS, "KEYPAD_EQUALS"},
        EnumeratorName{platform::Key::F13, "F13"},
        EnumeratorName{platform::Key::F14, "F14"},
        EnumeratorName{platform::Key::F15, "F15"},
        EnumeratorName{platform::Key::F16, "F16"},
        EnumeratorName{platform::Key::F17, "F17"},
        EnumeratorName{platform::Key::F18, "F18"},
        EnumeratorName{platform::Key::F19, "F19"},
        EnumeratorName{platform::Key::F20, "F20"},
        EnumeratorName{platform::Key::F21, "F21"},
        EnumeratorName{platform::Key::F22, "F22"},
        EnumeratorName{platform::Key::F23, "F23"},
        EnumeratorName{platform::Key::F24, "F24"},
        EnumeratorName{platform::Key::EXECUTE, "EXECUTE"},
        EnumeratorName{platform::Key::HELP, "HELP"},
        EnumeratorName{platform::Key::MENU, "MENU"},
        EnumeratorName{platform::Key::SELECT, "SELECT"},
        EnumeratorName{platform::Key::STOP, "STOP"},
        EnumeratorName{platform::Key::AGAIN, "AGAIN"},
        EnumeratorName{platform::Key::UNDO, "UNDO"},
        EnumeratorName{platform::Key::CUT, "CUT"},
        EnumeratorName{platform::Key::COPY, "COPY"},
        EnumeratorName{platform::Key::PASTE, "PASTE"},
        EnumeratorName{platform::Key::FIND, "FIND"},
        EnumeratorName{platform::Key::MUTE, "MUTE"},
        EnumeratorName{platform::Key::VOLUME_UP, "VOLUME_UP"},
        EnumeratorName{platform::Key::VOLUME_DOWN, "VOLUME_DOWN"},
        EnumeratorName{platform::Key::KEYPAD_COMMA, "KEYPAD_COMMA"},
        EnumeratorName{platform::Key::INTERNATIONAL_1, "INTERNATIONAL_1"},
        EnumeratorName{platform::Key::INTERNATIONAL_2, "INTERNATIONAL_2"},
        EnumeratorName{platform::Key::INTERNATIONAL_3, "INTERNATIONAL_3"},
        EnumeratorName{platform::Key::INTERNATIONAL_4, "INTERNATIONAL_4"},
        EnumeratorName{platform::Key::INTERNATIONAL_5, "INTERNATIONAL_5"},
        EnumeratorName{platform::Key::INTERNATIONAL_6, "INTERNATIONAL_6"},
        EnumeratorName{platform::Key::INTERNATIONAL_7, "INTERNATIONAL_7"},
        EnumeratorName{platform::Key::INTERNATIONAL_8, "INTERNATIONAL_8"},
        EnumeratorName{platform::Key::INTERNATIONAL_9, "INTERNATIONAL_9"},
        EnumeratorName{platform::Key::LANGUAGE_1, "LANGUAGE_1"},
        EnumeratorName{platform::Key::LANGUAGE_2, "LANGUAGE_2"},
        EnumeratorName{platform::Key::LANGUAGE_3, "LANGUAGE_3"},
        EnumeratorName{platform::Key::LANGUAGE_4, "LANGUAGE_4"},
        EnumeratorName{platform::Key::LANGUAGE_5, "LANGUAGE_5"},
        EnumeratorName{platform::Key::LANGUAGE_6, "LANGUAGE_6"},
        EnumeratorName{platform::Key::LANGUAGE_7, "LANGUAGE_7"},
        EnumeratorName{platform::Key::LANGUAGE_8, "LANGUAGE_8"},
        EnumeratorName{platform::Key::LANGUAGE_9, "LANGUAGE_9"},
        EnumeratorName{platform::Key::LEFT_CONTROL, "LEFT_CONTROL"},
        EnumeratorName{platform::Key::LEFT_SHIFT, "LEFT_SHIFT"},
        EnumeratorName{platform::Key::LEFT_ALT, "LEFT_ALT"},
        EnumeratorName{platform::Key::LEFT_SUPER, "LEFT_SUPER"},
        EnumeratorName{platform::Key::RIGHT_CONTROL, "RIGHT_CONTROL"},
        EnumeratorName{platform::Key::RIGHT_SHIFT, "RIGHT_SHIFT"},
        EnumeratorName{platform::Key::RIGHT_ALT, "RIGHT_ALT"},
        EnumeratorName{platform::Key::RIGHT_SUPER, "RIGHT_SUPER"},
    };
};

template<>
struct EnumerationTraits<platform::KeyCode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr bool IS_SPARSE = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::KeyCode::UNKNOWN, "UNKNOWN"},
        EnumeratorName{platform::KeyCode::BACKSPACE, "BACKSPACE"},
        EnumeratorName{platform::KeyCode::TAB, "TAB"},
        EnumeratorName{platform::KeyCode::RETURN, "RETURN"},
        EnumeratorName{platform::KeyCode::ESCAPE, "ESCAPE"},
        EnumeratorName{platform::KeyCode::SPACE, "SPACE"},
        EnumeratorName{platform::KeyCode::EXCLAMATION_MARK, "EXCLAMATION_MARK"},
        EnumeratorName{platform::KeyCode::QUOTATION_MARK, "QUOTATION_MARK"},
        EnumeratorName{platform::KeyCode::HASH, "HASH"},
        EnumeratorName{platform::KeyCode::DOLLAR, "DOLLAR"},
        EnumeratorName{platform::KeyCode::PERCENT, "PERCENT"},
        EnumeratorName{platform::KeyCode::AMPERSAND, "AMPERSAND"},
        EnumeratorName{platform::KeyCode::APOSTROPHE, "APOSTROPHE"},
        EnumeratorName{platform::KeyCode::LEFT_PARENTHESIS, "LEFT_PARENTHESIS"},
        EnumeratorName{platform::KeyCode::RIGHT_PARENTHESIS, "RIGHT_PARENTHESIS"},
        EnumeratorName{platform::KeyCode::ASTERISK, "ASTERISK"},
        EnumeratorName{platform::KeyCode::PLUS, "PLUS"},
        EnumeratorName{platform::KeyCode::COMMA, "COMMA"},
        EnumeratorName{platform::KeyCode::MINUS, "MINUS"},
        EnumeratorName{platform::KeyCode::PERIOD, "PERIOD"},
        EnumeratorName{platform::KeyCode::SLASH, "SLASH"},
        EnumeratorName{platform::KeyCode::NUMBER_0, "NUMBER_0"},
        EnumeratorName{platform::KeyCode::NUMBER_1, "NUMBER_1"},
        EnumeratorName{platform::KeyCode::NUMBER_2, "NUMBER_2"},
        EnumeratorName{platform::KeyCode::NUMBER_3, "NUMBER_3"},
        EnumeratorName{platform::KeyCode::NUMBER_4, "NUMBER_4"},
        EnumeratorName{platform::KeyCode::NUMBER_5, "NUMBER_5"},
        EnumeratorName{platform::KeyCode::NUMBER_6, "NUMBER_6"},
        EnumeratorName{platform::KeyCode::NUMBER_7, "NUMBER_7"},
        EnumeratorName{platform::KeyCode::NUMBER_8, "NUMBER_8"},
        EnumeratorName{platform::KeyCode::NUMBER_9, "NUMBER_9"},
        EnumeratorName{platform::KeyCode::COLON, "COLON"},
        EnumeratorName{platform::KeyCode::SEMICOLON, "SEMICOLON"},
        EnumeratorName{platform::KeyCode::LESS, "LESS"},
        EnumeratorName{platform::KeyCode::EQUALS, "EQUALS"},
        EnumeratorName{platform::KeyCode::GREATER, "GREATER"},
        EnumeratorName{platform::KeyCode::QUESTION_MARK, "QUESTION_MARK"},
        EnumeratorName{platform::KeyCode::AT, "AT"},
        EnumeratorName{platform::KeyCode::LEFT_BRACKET, "LEFT_BRACKET"},
        EnumeratorName{platform::KeyCode::BACKSLASH, "BACKSLASH"},
        EnumeratorName{platform::KeyCode::RIGHT_BRACKET, "RIGHT_BRACKET"},
        EnumeratorName{platform::KeyCode::CARET, "CARET"},
        EnumeratorName{platform::KeyCode::UNDERSCORE, "UNDERSCORE"},
        EnumeratorName{platform::KeyCode::GRAVE, "GRAVE"},
        EnumeratorName{platform::KeyCode::A, "A"},
        EnumeratorName{platform::KeyCode::B, "B"},
        EnumeratorName{platform::KeyCode::C, "C"},
        EnumeratorName{platform::KeyCode::D, "D"},
        EnumeratorName{platform::KeyCode::E, "E"},
        EnumeratorName{platform::KeyCode::F, "F"},
        EnumeratorName{platform::KeyCode::G, "G"},
        EnumeratorName{platform::KeyCode::H, "H"},
        EnumeratorName{platform::KeyCode::I, "I"},
        EnumeratorName{platform::KeyCode::J, "J"},
        EnumeratorName{platform::KeyCode::K, "K"},
        EnumeratorName{platform::KeyCode::L, "L"},
        EnumeratorName{platform::KeyCode::M, "M"},
        EnumeratorName{platform::KeyCode::N, "N"},
        EnumeratorName{platform::KeyCode::O, "O"},
        EnumeratorName{platform::KeyCode::P, "P"},
        EnumeratorName{platform::KeyCode::Q, "Q"},
        EnumeratorName{platform::KeyCode::R, "R"},
        EnumeratorName{platform::KeyCode::S, "S"},
        EnumeratorName{platform::KeyCode::T, "T"},
        EnumeratorName{platform::KeyCode::U, "U"},
        EnumeratorName{platform::KeyCode::V, "V"},
        EnumeratorName{platform::KeyCode::W, "W"},
        EnumeratorName{platform::KeyCode::X, "X"},
        EnumeratorName{platform::KeyCode::Y, "Y"},
        EnumeratorName{platform::KeyCode::Z, "Z"},
        EnumeratorName{platform::KeyCode::LEFT_BRACE, "LEFT_BRACE"},
        EnumeratorName{platform::KeyCode::PIPE, "PIPE"},
        EnumeratorName{platform::KeyCode::RIGHT_BRACE, "RIGHT_BRACE"},
        EnumeratorName{platform::KeyCode::TILDE, "TILDE"},
        EnumeratorName{platform::KeyCode::DELETE_FORWARD, "DELETE_FORWARD"},
        EnumeratorName{platform::KeyCode::CAPS_LOCK, "CAPS_LOCK"},
        EnumeratorName{platform::KeyCode::F1, "F1"},
        EnumeratorName{platform::KeyCode::F2, "F2"},
        EnumeratorName{platform::KeyCode::F3, "F3"},
        EnumeratorName{platform::KeyCode::F4, "F4"},
        EnumeratorName{platform::KeyCode::F5, "F5"},
        EnumeratorName{platform::KeyCode::F6, "F6"},
        EnumeratorName{platform::KeyCode::F7, "F7"},
        EnumeratorName{platform::KeyCode::F8, "F8"},
        EnumeratorName{platform::KeyCode::F9, "F9"},
        EnumeratorName{platform::KeyCode::F10, "F10"},
        EnumeratorName{platform::KeyCode::F11, "F11"},
        EnumeratorName{platform::KeyCode::F12, "F12"},
        EnumeratorName{platform::KeyCode::PRINT_SCREEN, "PRINT_SCREEN"},
        EnumeratorName{platform::KeyCode::SCROLL_LOCK, "SCROLL_LOCK"},
        EnumeratorName{platform::KeyCode::PAUSE, "PAUSE"},
        EnumeratorName{platform::KeyCode::INSERT, "INSERT"},
        EnumeratorName{platform::KeyCode::HOME, "HOME"},
        EnumeratorName{platform::KeyCode::PAGE_UP, "PAGE_UP"},
        EnumeratorName{platform::KeyCode::END, "END"},
        EnumeratorName{platform::KeyCode::PAGE_DOWN, "PAGE_DOWN"},
        EnumeratorName{platform::KeyCode::RIGHT, "RIGHT"},
        EnumeratorName{platform::KeyCode::LEFT, "LEFT"},
        EnumeratorName{platform::KeyCode::DOWN, "DOWN"},
        EnumeratorName{platform::KeyCode::UP, "UP"},
        EnumeratorName{platform::KeyCode::NUM_LOCK, "NUM_LOCK"},
        EnumeratorName{platform::KeyCode::KEYPAD_DIVIDE, "KEYPAD_DIVIDE"},
        EnumeratorName{platform::KeyCode::KEYPAD_MULTIPLY, "KEYPAD_MULTIPLY"},
        EnumeratorName{platform::KeyCode::KEYPAD_MINUS, "KEYPAD_MINUS"},
        EnumeratorName{platform::KeyCode::KEYPAD_PLUS, "KEYPAD_PLUS"},
        EnumeratorName{platform::KeyCode::KEYPAD_ENTER, "KEYPAD_ENTER"},
        EnumeratorName{platform::KeyCode::KEYPAD_1, "KEYPAD_1"},
        EnumeratorName{platform::KeyCode::KEYPAD_2, "KEYPAD_2"},
        EnumeratorName{platform::KeyCode::KEYPAD_3, "KEYPAD_3"},
        EnumeratorName{platform::KeyCode::KEYPAD_4, "KEYPAD_4"},
        EnumeratorName{platform::KeyCode::KEYPAD_5, "KEYPAD_5"},
        EnumeratorName{platform::KeyCode::KEYPAD_6, "KEYPAD_6"},
        EnumeratorName{platform::KeyCode::KEYPAD_7, "KEYPAD_7"},
        EnumeratorName{platform::KeyCode::KEYPAD_8, "KEYPAD_8"},
        EnumeratorName{platform::KeyCode::KEYPAD_9, "KEYPAD_9"},
        EnumeratorName{platform::KeyCode::KEYPAD_0, "KEYPAD_0"},
        EnumeratorName{platform::KeyCode::KEYPAD_PERIOD, "KEYPAD_PERIOD"},
        EnumeratorName{platform::KeyCode::APPLICATION, "APPLICATION"},
        EnumeratorName{platform::KeyCode::POWER, "POWER"},
        EnumeratorName{platform::KeyCode::KEYPAD_EQUALS, "KEYPAD_EQUALS"},
        EnumeratorName{platform::KeyCode::F13, "F13"},
        EnumeratorName{platform::KeyCode::F14, "F14"},
        EnumeratorName{platform::KeyCode::F15, "F15"},
        EnumeratorName{platform::KeyCode::F16, "F16"},
        EnumeratorName{platform::KeyCode::F17, "F17"},
        EnumeratorName{platform::KeyCode::F18, "F18"},
        EnumeratorName{platform::KeyCode::F19, "F19"},
        EnumeratorName{platform::KeyCode::F20, "F20"},
        EnumeratorName{platform::KeyCode::F21, "F21"},
        EnumeratorName{platform::KeyCode::F22, "F22"},
        EnumeratorName{platform::KeyCode::F23, "F23"},
        EnumeratorName{platform::KeyCode::F24, "F24"},
        EnumeratorName{platform::KeyCode::EXECUTE, "EXECUTE"},
        EnumeratorName{platform::KeyCode::HELP, "HELP"},
        EnumeratorName{platform::KeyCode::MENU, "MENU"},
        EnumeratorName{platform::KeyCode::SELECT, "SELECT"},
        EnumeratorName{platform::KeyCode::STOP, "STOP"},
        EnumeratorName{platform::KeyCode::AGAIN, "AGAIN"},
        EnumeratorName{platform::KeyCode::UNDO, "UNDO"},
        EnumeratorName{platform::KeyCode::CUT, "CUT"},
        EnumeratorName{platform::KeyCode::COPY, "COPY"},
        EnumeratorName{platform::KeyCode::PASTE, "PASTE"},
        EnumeratorName{platform::KeyCode::FIND, "FIND"},
        EnumeratorName{platform::KeyCode::MUTE, "MUTE"},
        EnumeratorName{platform::KeyCode::VOLUME_UP, "VOLUME_UP"},
        EnumeratorName{platform::KeyCode::VOLUME_DOWN, "VOLUME_DOWN"},
        EnumeratorName{platform::KeyCode::KEYPAD_COMMA, "KEYPAD_COMMA"},
        EnumeratorName{platform::KeyCode::LEFT_CONTROL, "LEFT_CONTROL"},
        EnumeratorName{platform::KeyCode::LEFT_SHIFT, "LEFT_SHIFT"},
        EnumeratorName{platform::KeyCode::LEFT_ALT, "LEFT_ALT"},
        EnumeratorName{platform::KeyCode::LEFT_SUPER, "LEFT_SUPER"},
        EnumeratorName{platform::KeyCode::RIGHT_CONTROL, "RIGHT_CONTROL"},
        EnumeratorName{platform::KeyCode::RIGHT_SHIFT, "RIGHT_SHIFT"},
        EnumeratorName{platform::KeyCode::RIGHT_ALT, "RIGHT_ALT"},
        EnumeratorName{platform::KeyCode::RIGHT_SUPER, "RIGHT_SUPER"},
    };
};

template<>
struct EnumerationTraits<platform::PlatformBackendKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::platform"; ///< Errors name these backends "gfx::platform::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{platform::PlatformBackendKind::SDL3, "SDL3"},
        EnumeratorName{platform::PlatformBackendKind::GLFW, "GLFW"},
        EnumeratorName{platform::PlatformBackendKind::WINDOWS, "WINDOWS"},
        EnumeratorName{platform::PlatformBackendKind::XCB, "XCB"},
        EnumeratorName{platform::PlatformBackendKind::COCOA, "COCOA"},
        EnumeratorName{platform::PlatformBackendKind::WEB, "WEB"},
    };
};

template<>
struct EnumerationTraits<platform::PlatformFeature>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::PlatformFeature::WINDOW_ICON, "WINDOW_ICON"},
        EnumeratorName{platform::PlatformFeature::WINDOW_POSITION, "WINDOW_POSITION"},
        EnumeratorName{platform::PlatformFeature::EXCLUSIVE_FULLSCREEN, "EXCLUSIVE_FULLSCREEN"},
        EnumeratorName{platform::PlatformFeature::WINDOW_OCCLUSION, "WINDOW_OCCLUSION"},
        EnumeratorName{platform::PlatformFeature::WINDOW_HOVER, "WINDOW_HOVER"},
        EnumeratorName{platform::PlatformFeature::HIGH_PIXEL_DENSITY, "HIGH_PIXEL_DENSITY"},
        EnumeratorName{platform::PlatformFeature::CUSTOM_CURSORS, "CUSTOM_CURSORS"},
        EnumeratorName{platform::PlatformFeature::CAPTURED_CURSOR, "CAPTURED_CURSOR"},
        EnumeratorName{platform::PlatformFeature::RAW_MOUSE_MOTION, "RAW_MOUSE_MOTION"},
        EnumeratorName{platform::PlatformFeature::TEXT_COMPOSITION, "TEXT_COMPOSITION"},
        EnumeratorName{platform::PlatformFeature::CLIPBOARD_IMAGE, "CLIPBOARD_IMAGE"},
        EnumeratorName{platform::PlatformFeature::DROP_TEXT, "DROP_TEXT"},
        EnumeratorName{platform::PlatformFeature::TOUCH, "TOUCH"},
        EnumeratorName{platform::PlatformFeature::GAMEPAD_RUMBLE, "GAMEPAD_RUMBLE"},
        EnumeratorName{platform::PlatformFeature::GAMEPAD_TRIGGER_RUMBLE, "GAMEPAD_TRIGGER_RUMBLE"},
        EnumeratorName{platform::PlatformFeature::GAMEPAD_LED, "GAMEPAD_LED"},
        EnumeratorName{platform::PlatformFeature::GAMEPAD_TOUCHPAD, "GAMEPAD_TOUCHPAD"},
        EnumeratorName{platform::PlatformFeature::GAMEPAD_SENSORS, "GAMEPAD_SENSORS"},
    };
};

template<>
struct EnumerationTraits<platform::WindowMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::WindowMode::WINDOWED, "WINDOWED"},
        EnumeratorName{platform::WindowMode::BORDERLESS, "BORDERLESS"},
        EnumeratorName{platform::WindowMode::BORDERLESS_FULLSCREEN, "BORDERLESS_FULLSCREEN"},
        EnumeratorName{platform::WindowMode::EXCLUSIVE_FULLSCREEN, "EXCLUSIVE_FULLSCREEN"},
    };
};

template<>
struct EnumerationTraits<platform::WindowFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::WindowFlag::RESIZABLE, "RESIZABLE"},
        EnumeratorName{platform::WindowFlag::HIDDEN, "HIDDEN"},
        EnumeratorName{platform::WindowFlag::ALWAYS_ON_TOP, "ALWAYS_ON_TOP"},
        EnumeratorName{platform::WindowFlag::TRANSPARENT_FRAMEBUFFER, "TRANSPARENT_FRAMEBUFFER"},
        EnumeratorName{platform::WindowFlag::HIGH_PIXEL_DENSITY, "HIGH_PIXEL_DENSITY"},
        EnumeratorName{platform::WindowFlag::FOCUS_ON_SHOW, "FOCUS_ON_SHOW"},
        EnumeratorName{platform::WindowFlag::MOUSE_PASSTHROUGH, "MOUSE_PASSTHROUGH"},
        EnumeratorName{platform::WindowFlag::UTILITY, "UTILITY"},
    };
};

template<>
struct EnumerationTraits<platform::SurfaceApi>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::SurfaceApi::NONE, "NONE"},
        EnumeratorName{platform::SurfaceApi::VULKAN, "VULKAN"},
        EnumeratorName{platform::SurfaceApi::DIRECT3D12, "DIRECT3D12"},
        EnumeratorName{platform::SurfaceApi::METAL, "METAL"},
        EnumeratorName{platform::SurfaceApi::WEBGPU, "WEBGPU"},
    };
};

template<>
struct EnumerationTraits<platform::CursorShape>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::CursorShape::ARROW, "ARROW"},
        EnumeratorName{platform::CursorShape::I_BEAM, "I_BEAM"},
        EnumeratorName{platform::CursorShape::WAIT, "WAIT"},
        EnumeratorName{platform::CursorShape::PROGRESS, "PROGRESS"},
        EnumeratorName{platform::CursorShape::CROSSHAIR, "CROSSHAIR"},
        EnumeratorName{platform::CursorShape::POINTER, "POINTER"},
        EnumeratorName{platform::CursorShape::RESIZE_EAST_WEST, "RESIZE_EAST_WEST"},
        EnumeratorName{platform::CursorShape::RESIZE_NORTH_SOUTH, "RESIZE_NORTH_SOUTH"},
        EnumeratorName{platform::CursorShape::RESIZE_NORTHWEST_SOUTHEAST, "RESIZE_NORTHWEST_SOUTHEAST"},
        EnumeratorName{platform::CursorShape::RESIZE_NORTHEAST_SOUTHWEST, "RESIZE_NORTHEAST_SOUTHWEST"},
        EnumeratorName{platform::CursorShape::MOVE, "MOVE"},
        EnumeratorName{platform::CursorShape::NOT_ALLOWED, "NOT_ALLOWED"},
    };
};

template<>
struct EnumerationTraits<platform::CursorMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::CursorMode::NORMAL, "NORMAL"},
        EnumeratorName{platform::CursorMode::HIDDEN, "HIDDEN"},
        EnumeratorName{platform::CursorMode::CONFINED, "CONFINED"},
        EnumeratorName{platform::CursorMode::CAPTURED, "CAPTURED"},
    };
};

template<>
struct EnumerationTraits<platform::MouseButton>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::MouseButton::LEFT, "LEFT"},
        EnumeratorName{platform::MouseButton::MIDDLE, "MIDDLE"},
        EnumeratorName{platform::MouseButton::RIGHT, "RIGHT"},
        EnumeratorName{platform::MouseButton::BACK, "BACK"},
        EnumeratorName{platform::MouseButton::FORWARD, "FORWARD"},
    };
};

template<>
struct EnumerationTraits<platform::MouseButtonMask>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::MouseButtonMask::LEFT, "LEFT"},
        EnumeratorName{platform::MouseButtonMask::MIDDLE, "MIDDLE"},
        EnumeratorName{platform::MouseButtonMask::RIGHT, "RIGHT"},
        EnumeratorName{platform::MouseButtonMask::BACK, "BACK"},
        EnumeratorName{platform::MouseButtonMask::FORWARD, "FORWARD"},
    };
};

template<>
struct EnumerationTraits<platform::KeyModifier>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::KeyModifier::LEFT_SHIFT, "LEFT_SHIFT"},
        EnumeratorName{platform::KeyModifier::RIGHT_SHIFT, "RIGHT_SHIFT"},
        EnumeratorName{platform::KeyModifier::LEFT_CONTROL, "LEFT_CONTROL"},
        EnumeratorName{platform::KeyModifier::RIGHT_CONTROL, "RIGHT_CONTROL"},
        EnumeratorName{platform::KeyModifier::LEFT_ALT, "LEFT_ALT"},
        EnumeratorName{platform::KeyModifier::RIGHT_ALT, "RIGHT_ALT"},
        EnumeratorName{platform::KeyModifier::LEFT_SUPER, "LEFT_SUPER"},
        EnumeratorName{platform::KeyModifier::RIGHT_SUPER, "RIGHT_SUPER"},
        EnumeratorName{platform::KeyModifier::CAPS_LOCK, "CAPS_LOCK"},
        EnumeratorName{platform::KeyModifier::NUM_LOCK, "NUM_LOCK"},
        EnumeratorName{platform::KeyModifier::SCROLL_LOCK, "SCROLL_LOCK"},
    };
};

template<>
struct EnumerationTraits<platform::GamepadButton>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::GamepadButton::SOUTH, "SOUTH"},
        EnumeratorName{platform::GamepadButton::EAST, "EAST"},
        EnumeratorName{platform::GamepadButton::WEST, "WEST"},
        EnumeratorName{platform::GamepadButton::NORTH, "NORTH"},
        EnumeratorName{platform::GamepadButton::BACK, "BACK"},
        EnumeratorName{platform::GamepadButton::GUIDE, "GUIDE"},
        EnumeratorName{platform::GamepadButton::START, "START"},
        EnumeratorName{platform::GamepadButton::LEFT_STICK, "LEFT_STICK"},
        EnumeratorName{platform::GamepadButton::RIGHT_STICK, "RIGHT_STICK"},
        EnumeratorName{platform::GamepadButton::LEFT_SHOULDER, "LEFT_SHOULDER"},
        EnumeratorName{platform::GamepadButton::RIGHT_SHOULDER, "RIGHT_SHOULDER"},
        EnumeratorName{platform::GamepadButton::DPAD_UP, "DPAD_UP"},
        EnumeratorName{platform::GamepadButton::DPAD_DOWN, "DPAD_DOWN"},
        EnumeratorName{platform::GamepadButton::DPAD_LEFT, "DPAD_LEFT"},
        EnumeratorName{platform::GamepadButton::DPAD_RIGHT, "DPAD_RIGHT"},
        EnumeratorName{platform::GamepadButton::MISCELLANEOUS_1, "MISCELLANEOUS_1"},
        EnumeratorName{platform::GamepadButton::RIGHT_PADDLE_1, "RIGHT_PADDLE_1"},
        EnumeratorName{platform::GamepadButton::LEFT_PADDLE_1, "LEFT_PADDLE_1"},
        EnumeratorName{platform::GamepadButton::RIGHT_PADDLE_2, "RIGHT_PADDLE_2"},
        EnumeratorName{platform::GamepadButton::LEFT_PADDLE_2, "LEFT_PADDLE_2"},
        EnumeratorName{platform::GamepadButton::TOUCHPAD, "TOUCHPAD"},
    };
};

template<>
struct EnumerationTraits<platform::GamepadAxis>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::GamepadAxis::LEFT_X, "LEFT_X"},
        EnumeratorName{platform::GamepadAxis::LEFT_Y, "LEFT_Y"},
        EnumeratorName{platform::GamepadAxis::RIGHT_X, "RIGHT_X"},
        EnumeratorName{platform::GamepadAxis::RIGHT_Y, "RIGHT_Y"},
        EnumeratorName{platform::GamepadAxis::LEFT_TRIGGER, "LEFT_TRIGGER"},
        EnumeratorName{platform::GamepadAxis::RIGHT_TRIGGER, "RIGHT_TRIGGER"},
    };
};

template<>
struct EnumerationTraits<platform::GamepadType>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::GamepadType::UNKNOWN, "UNKNOWN"},
        EnumeratorName{platform::GamepadType::STANDARD, "STANDARD"},
        EnumeratorName{platform::GamepadType::XBOX_360, "XBOX_360"},
        EnumeratorName{platform::GamepadType::XBOX_ONE, "XBOX_ONE"},
        EnumeratorName{platform::GamepadType::PLAYSTATION_3, "PLAYSTATION_3"},
        EnumeratorName{platform::GamepadType::PLAYSTATION_4, "PLAYSTATION_4"},
        EnumeratorName{platform::GamepadType::PLAYSTATION_5, "PLAYSTATION_5"},
        EnumeratorName{platform::GamepadType::NINTENDO_SWITCH_PRO, "NINTENDO_SWITCH_PRO"},
        EnumeratorName{platform::GamepadType::NINTENDO_SWITCH_JOYCON_LEFT, "NINTENDO_SWITCH_JOYCON_LEFT"},
        EnumeratorName{platform::GamepadType::NINTENDO_SWITCH_JOYCON_RIGHT, "NINTENDO_SWITCH_JOYCON_RIGHT"},
        EnumeratorName{platform::GamepadType::NINTENDO_SWITCH_JOYCON_PAIR, "NINTENDO_SWITCH_JOYCON_PAIR"},
    };
};

template<>
struct EnumerationTraits<platform::HatPosition>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::HatPosition::UP, "UP"},
        EnumeratorName{platform::HatPosition::RIGHT, "RIGHT"},
        EnumeratorName{platform::HatPosition::DOWN, "DOWN"},
        EnumeratorName{platform::HatPosition::LEFT, "LEFT"},
    };
};

template<>
struct EnumerationTraits<platform::TouchDeviceType>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::TouchDeviceType::DIRECT, "DIRECT"},
        EnumeratorName{platform::TouchDeviceType::INDIRECT_ABSOLUTE, "INDIRECT_ABSOLUTE"},
        EnumeratorName{platform::TouchDeviceType::INDIRECT_RELATIVE, "INDIRECT_RELATIVE"},
    };
};

template<>
struct EnumerationTraits<platform::EventType>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::EventType::NONE, "NONE"},
        EnumeratorName{platform::EventType::WINDOW_SHOWN, "WINDOW_SHOWN"},
        EnumeratorName{platform::EventType::WINDOW_HIDDEN, "WINDOW_HIDDEN"},
        EnumeratorName{platform::EventType::WINDOW_EXPOSED, "WINDOW_EXPOSED"},
        EnumeratorName{platform::EventType::WINDOW_MOVED, "WINDOW_MOVED"},
        EnumeratorName{platform::EventType::WINDOW_RESIZED, "WINDOW_RESIZED"},
        EnumeratorName{platform::EventType::WINDOW_FRAMEBUFFER_RESIZED, "WINDOW_FRAMEBUFFER_RESIZED"},
        EnumeratorName{platform::EventType::WINDOW_CONTENT_SCALE_CHANGED, "WINDOW_CONTENT_SCALE_CHANGED"},
        EnumeratorName{platform::EventType::WINDOW_MINIMIZED, "WINDOW_MINIMIZED"},
        EnumeratorName{platform::EventType::WINDOW_MAXIMIZED, "WINDOW_MAXIMIZED"},
        EnumeratorName{platform::EventType::WINDOW_RESTORED, "WINDOW_RESTORED"},
        EnumeratorName{platform::EventType::WINDOW_FOCUS_GAINED, "WINDOW_FOCUS_GAINED"},
        EnumeratorName{platform::EventType::WINDOW_FOCUS_LOST, "WINDOW_FOCUS_LOST"},
        EnumeratorName{platform::EventType::WINDOW_OCCLUDED, "WINDOW_OCCLUDED"},
        EnumeratorName{platform::EventType::WINDOW_MOUSE_ENTERED, "WINDOW_MOUSE_ENTERED"},
        EnumeratorName{platform::EventType::WINDOW_MOUSE_LEFT, "WINDOW_MOUSE_LEFT"},
        EnumeratorName{platform::EventType::WINDOW_CLOSE_REQUESTED, "WINDOW_CLOSE_REQUESTED"},
        EnumeratorName{platform::EventType::KEY_DOWN, "KEY_DOWN"},
        EnumeratorName{platform::EventType::KEY_UP, "KEY_UP"},
        EnumeratorName{platform::EventType::TEXT_INPUT, "TEXT_INPUT"},
        EnumeratorName{platform::EventType::TEXT_COMPOSITION, "TEXT_COMPOSITION"},
        EnumeratorName{platform::EventType::MOUSE_MOTION, "MOUSE_MOTION"},
        EnumeratorName{platform::EventType::MOUSE_BUTTON_DOWN, "MOUSE_BUTTON_DOWN"},
        EnumeratorName{platform::EventType::MOUSE_BUTTON_UP, "MOUSE_BUTTON_UP"},
        EnumeratorName{platform::EventType::MOUSE_WHEEL, "MOUSE_WHEEL"},
        EnumeratorName{platform::EventType::GAMEPAD_CONNECTED, "GAMEPAD_CONNECTED"},
        EnumeratorName{platform::EventType::GAMEPAD_DISCONNECTED, "GAMEPAD_DISCONNECTED"},
        EnumeratorName{platform::EventType::GAMEPAD_REMAPPED, "GAMEPAD_REMAPPED"},
        EnumeratorName{platform::EventType::GAMEPAD_BUTTON_DOWN, "GAMEPAD_BUTTON_DOWN"},
        EnumeratorName{platform::EventType::GAMEPAD_BUTTON_UP, "GAMEPAD_BUTTON_UP"},
        EnumeratorName{platform::EventType::GAMEPAD_AXIS_MOTION, "GAMEPAD_AXIS_MOTION"},
        EnumeratorName{platform::EventType::GAMEPAD_TOUCHPAD_DOWN, "GAMEPAD_TOUCHPAD_DOWN"},
        EnumeratorName{platform::EventType::GAMEPAD_TOUCHPAD_MOTION, "GAMEPAD_TOUCHPAD_MOTION"},
        EnumeratorName{platform::EventType::GAMEPAD_TOUCHPAD_UP, "GAMEPAD_TOUCHPAD_UP"},
        EnumeratorName{platform::EventType::JOYSTICK_CONNECTED, "JOYSTICK_CONNECTED"},
        EnumeratorName{platform::EventType::JOYSTICK_DISCONNECTED, "JOYSTICK_DISCONNECTED"},
        EnumeratorName{platform::EventType::JOYSTICK_BUTTON_DOWN, "JOYSTICK_BUTTON_DOWN"},
        EnumeratorName{platform::EventType::JOYSTICK_BUTTON_UP, "JOYSTICK_BUTTON_UP"},
        EnumeratorName{platform::EventType::JOYSTICK_AXIS_MOTION, "JOYSTICK_AXIS_MOTION"},
        EnumeratorName{platform::EventType::JOYSTICK_HAT_MOTION, "JOYSTICK_HAT_MOTION"},
        EnumeratorName{platform::EventType::TOUCH_DOWN, "TOUCH_DOWN"},
        EnumeratorName{platform::EventType::TOUCH_MOTION, "TOUCH_MOTION"},
        EnumeratorName{platform::EventType::TOUCH_UP, "TOUCH_UP"},
        EnumeratorName{platform::EventType::TOUCH_CANCELED, "TOUCH_CANCELED"},
        EnumeratorName{platform::EventType::DROP_FILE, "DROP_FILE"},
        EnumeratorName{platform::EventType::DROP_TEXT, "DROP_TEXT"},
        EnumeratorName{platform::EventType::MONITOR_CONNECTED, "MONITOR_CONNECTED"},
        EnumeratorName{platform::EventType::MONITOR_DISCONNECTED, "MONITOR_DISCONNECTED"},
        EnumeratorName{platform::EventType::QUIT_REQUESTED, "QUIT_REQUESTED"},
        EnumeratorName{platform::EventType::KEYBOARD_LAYOUT_CHANGED, "KEYBOARD_LAYOUT_CHANGED"},
        EnumeratorName{platform::EventType::CLIPBOARD_UPDATED, "CLIPBOARD_UPDATED"},
    };
};

template<>
struct EnumerationTraits<platform::EventWaitMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::EventWaitMode::POLL, "POLL"},
        EnumeratorName{platform::EventWaitMode::WAIT, "WAIT"},
        EnumeratorName{platform::EventWaitMode::WAIT_WITH_TIMEOUT, "WAIT_WITH_TIMEOUT"},
    };
};

template<>
struct EnumerationTraits<platform::ActionValueType>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::ActionValueType::BUTTON, "BUTTON"},
        EnumeratorName{platform::ActionValueType::AXIS_1D, "AXIS_1D"},
        EnumeratorName{platform::ActionValueType::AXIS_2D, "AXIS_2D"},
    };
};

template<>
struct EnumerationTraits<platform::InputSourceKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::InputSourceKind::NONE, "NONE"},
        EnumeratorName{platform::InputSourceKind::KEY, "KEY"},
        EnumeratorName{platform::InputSourceKind::KEY_CODE, "KEY_CODE"},
        EnumeratorName{platform::InputSourceKind::MOUSE_BUTTON, "MOUSE_BUTTON"},
        EnumeratorName{platform::InputSourceKind::MOUSE_AXIS, "MOUSE_AXIS"},
        EnumeratorName{platform::InputSourceKind::GAMEPAD_BUTTON, "GAMEPAD_BUTTON"},
        EnumeratorName{platform::InputSourceKind::GAMEPAD_AXIS, "GAMEPAD_AXIS"},
    };
};

template<>
struct EnumerationTraits<platform::BindingKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::BindingKind::NONE, "NONE"},
        EnumeratorName{platform::BindingKind::BUTTON, "BUTTON"},
        EnumeratorName{platform::BindingKind::CHORD, "CHORD"},
        EnumeratorName{platform::BindingKind::GAMEPAD_CHORD, "GAMEPAD_CHORD"},
        EnumeratorName{platform::BindingKind::MOUSE_AXIS, "MOUSE_AXIS"},
        EnumeratorName{platform::BindingKind::COMPOSITE_AXIS, "COMPOSITE_AXIS"},
        EnumeratorName{platform::BindingKind::COMPOSITE_2D, "COMPOSITE_2D"},
        EnumeratorName{platform::BindingKind::STICK, "STICK"},
    };
};

template<>
struct EnumerationTraits<platform::DeadzoneKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::DeadzoneKind::NONE, "NONE"},
        EnumeratorName{platform::DeadzoneKind::AXIAL, "AXIAL"},
        EnumeratorName{platform::DeadzoneKind::RADIAL, "RADIAL"},
        EnumeratorName{platform::DeadzoneKind::SCALED_RADIAL, "SCALED_RADIAL"},
    };
};

template<>
struct EnumerationTraits<platform::ResponseCurve>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::ResponseCurve::LINEAR, "LINEAR"},
        EnumeratorName{platform::ResponseCurve::QUADRATIC, "QUADRATIC"},
        EnumeratorName{platform::ResponseCurve::CUBIC, "CUBIC"},
        EnumeratorName{platform::ResponseCurve::POWER, "POWER"},
    };
};

template<>
struct EnumerationTraits<platform::TriggerKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::TriggerKind::DOWN, "DOWN"},
        EnumeratorName{platform::TriggerKind::HOLD, "HOLD"},
        EnumeratorName{platform::TriggerKind::TAP, "TAP"},
    };
};

template<>
struct EnumerationTraits<platform::MouseAxis>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::MouseAxis::MOTION_X, "MOTION_X"},
        EnumeratorName{platform::MouseAxis::MOTION_Y, "MOTION_Y"},
        EnumeratorName{platform::MouseAxis::WHEEL_X, "WHEEL_X"},
        EnumeratorName{platform::MouseAxis::WHEEL_Y, "WHEEL_Y"},
    };
};

template<>
struct EnumerationTraits<platform::GamepadAssignment>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::GamepadAssignment::AUTOMATIC, "AUTOMATIC"},
        EnumeratorName{platform::GamepadAssignment::MANUAL, "MANUAL"},
    };
};

template<>
struct EnumerationTraits<platform::InputSchemaProblem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{platform::InputSchemaProblem::NONE, "NONE"},
        EnumeratorName{platform::InputSchemaProblem::MISSING_NAME, "MISSING_NAME"},
        EnumeratorName{platform::InputSchemaProblem::DUPLICATE_ACTION, "DUPLICATE_ACTION"},
        EnumeratorName{platform::InputSchemaProblem::DUPLICATE_CONTEXT, "DUPLICATE_CONTEXT"},
        EnumeratorName{platform::InputSchemaProblem::UNKNOWN_ACTION, "UNKNOWN_ACTION"},
        EnumeratorName{platform::InputSchemaProblem::UNKNOWN_CONTEXT, "UNKNOWN_CONTEXT"},
        EnumeratorName{platform::InputSchemaProblem::MALFORMED_BINDING, "MALFORMED_BINDING"},
        EnumeratorName{platform::InputSchemaProblem::INCOMPATIBLE_BINDING, "INCOMPATIBLE_BINDING"},
        EnumeratorName{platform::InputSchemaProblem::TRIGGER_ON_AXIS, "TRIGGER_ON_AXIS"},
        EnumeratorName{platform::InputSchemaProblem::INVALID_TRIGGER_TIME, "INVALID_TRIGGER_TIME"},
        EnumeratorName{platform::InputSchemaProblem::INVALID_DEADZONE, "INVALID_DEADZONE"},
        EnumeratorName{platform::InputSchemaProblem::INVALID_RESPONSE_CURVE, "INVALID_RESPONSE_CURVE"},
    };
};

template<>
struct EnumerationTraits<platform::BindingCaptureSource>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{platform::BindingCaptureSource::KEYBOARD, "KEYBOARD"},
        EnumeratorName{platform::BindingCaptureSource::MOUSE_BUTTON, "MOUSE_BUTTON"},
        EnumeratorName{platform::BindingCaptureSource::GAMEPAD_BUTTON, "GAMEPAD_BUTTON"},
        EnumeratorName{platform::BindingCaptureSource::GAMEPAD_AXIS, "GAMEPAD_AXIS"},
    };
};

} // namespace gfx

namespace gfx::enumeration_verification
{

static_assert(verify<platform::Key>(), "EnumerationTraits<platform::Key>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::KeyCode>(), "EnumerationTraits<platform::KeyCode>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::PlatformBackendKind>(), "EnumerationTraits<platform::PlatformBackendKind>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::PlatformFeature>(), "EnumerationTraits<platform::PlatformFeature>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::WindowMode>(), "EnumerationTraits<platform::WindowMode>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::WindowFlag>(), "EnumerationTraits<platform::WindowFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::SurfaceApi>(), "EnumerationTraits<platform::SurfaceApi>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::CursorShape>(), "EnumerationTraits<platform::CursorShape>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::CursorMode>(), "EnumerationTraits<platform::CursorMode>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::MouseButton>(), "EnumerationTraits<platform::MouseButton>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::MouseButtonMask>(), "EnumerationTraits<platform::MouseButtonMask>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::KeyModifier>(), "EnumerationTraits<platform::KeyModifier>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::GamepadButton>(), "EnumerationTraits<platform::GamepadButton>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::GamepadAxis>(), "EnumerationTraits<platform::GamepadAxis>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::GamepadType>(), "EnumerationTraits<platform::GamepadType>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::HatPosition>(), "EnumerationTraits<platform::HatPosition>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::TouchDeviceType>(), "EnumerationTraits<platform::TouchDeviceType>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::EventType>(), "EnumerationTraits<platform::EventType>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::EventWaitMode>(), "EnumerationTraits<platform::EventWaitMode>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::ActionValueType>(), "EnumerationTraits<platform::ActionValueType>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::InputSourceKind>(), "EnumerationTraits<platform::InputSourceKind>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::BindingKind>(), "EnumerationTraits<platform::BindingKind>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::DeadzoneKind>(), "EnumerationTraits<platform::DeadzoneKind>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::ResponseCurve>(), "EnumerationTraits<platform::ResponseCurve>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::TriggerKind>(), "EnumerationTraits<platform::TriggerKind>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::MouseAxis>(), "EnumerationTraits<platform::MouseAxis>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::GamepadAssignment>(), "EnumerationTraits<platform::GamepadAssignment>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::InputSchemaProblem>(), "EnumerationTraits<platform::InputSchemaProblem>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::BindingCaptureSource>(), "EnumerationTraits<platform::BindingCaptureSource>::NAMES is out of sync with the enumeration");

} // namespace gfx::enumeration_verification
