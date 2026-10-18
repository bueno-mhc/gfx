/// @file enums.cppm
/// @brief The enumeration machinery shared by the gfx libraries (traits, verification, bitmask
///        operators, spelling and parsing, std::format support), the enumerations and the constants
///        several libraries use (validation, memory domains, native windows; the keyboard, mouse and
///        cursor enumerations of the platform and UI libraries). Each library's own enumerations and
///        constants live only in its own enums.cppm.
///
/// Conventions:
/// - Enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform headers
///   define as macros are avoided (checked by a CTest test).
/// - Every enumeration has an EnumerationTraits specialization listing the spelling of each enumerator,
///   verified at compile time (order, uniqueness, single bits for bitmasks) and, on GCC and Clang,
///   against the compiler's own spelling of each enumerator.
export module gfx.core.enums;

import std;

// Set to 1 by CMake for Debug builds (source/core/CMakeLists.txt); everything else is a Release build.
#ifndef GFX_DEBUG_BUILD
#define GFX_DEBUG_BUILD 0
#endif

export namespace gfx
{

// =================================================================================================
// Enumerations shared by the libraries
// =================================================================================================

/// @brief How much the library checks. Levels are ordered: each includes the checks of the previous one.
enum class ValidationLevel : std::uint8_t
{
    NONE,     ///< No checks: invalid usage is undefined behavior (the native API's own rules apply).
    CREATION, ///< Checks when objects are created and recordings begin; nothing per command (Release default).
    COMMANDS, ///< Also checks every command, descriptor write and push-constant write (Debug default).
};

/// @brief Memory heap an allocation failure refers to.
enum class MemoryDomain : std::uint8_t
{
    HOST,   ///< System memory.
    DEVICE, ///< GPU memory.
};

/// @brief Windowing system of a NativeWindowHandle: which alternative it holds.
enum class NativeWindowKind : std::uint8_t
{
    NONE,       ///< No window.
    WINDOWS,    ///< Win32 window (HINSTANCE, HWND).
    XLIB,       ///< X11 window through Xlib (Display*, Window).
    XCB,        ///< X11 window through XCB (xcb_connection_t*, xcb_window_t).
    WAYLAND,    ///< Wayland surface (wl_display*, wl_surface*).
    COCOA,      ///< macOS window (NSWindow*, CAMetalLayer*).
    WEB_CANVAS, ///< HTML canvas (CSS selector).
};

// Input enumerations shared by the platform library (events, bindings) and the UI library (frame input).
// Generated tables: Key and KeyCode list their enumerators in value order.

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

// =================================================================================================
// Constants shared by the libraries
// =================================================================================================

/// @brief Number of bits of a handle value holding the slot index.
inline constexpr std::uint32_t HANDLE_INDEX_BITS = 24;

/// @brief Number of bits of a handle value holding the slot generation.
inline constexpr std::uint32_t HANDLE_GENERATION_BITS = 8;

/// @brief Largest slot index a handle can address.
inline constexpr std::uint32_t MAXIMUM_HANDLE_INDEX = (1U << HANDLE_INDEX_BITS) - 1U;

/// @brief Largest slot generation; generations count from 1 to this value and then wrap to 1.
inline constexpr std::uint32_t MAXIMUM_HANDLE_GENERATION = (1U << HANDLE_GENERATION_BITS) - 1U;

/// @brief Values of a sparse enumeration below this limit are all checked at compile time for an
///        enumerator missing from its NAMES table (larger values only through their entries).
inline constexpr std::size_t SPARSE_VERIFICATION_LIMIT = 1024;

/// @brief FNV-1a offset basis: the hash of the empty string (32 bits).
inline constexpr std::uint32_t NAME_HASH_OFFSET_BASIS = 2166136261U;

/// @brief FNV-1a prime (32 bits).
inline constexpr std::uint32_t NAME_HASH_PRIME = 16777619U;

/// @brief Whether this build of the library is a Debug build.
inline constexpr bool DEBUG_BUILD = GFX_DEBUG_BUILD != 0;

/// @brief Validation level of instances that do not choose one: every check in Debug builds, only the
///        checks that cost nothing per command in Release builds.
inline constexpr ValidationLevel DEFAULT_VALIDATION_LEVEL = DEBUG_BUILD ? ValidationLevel::COMMANDS : ValidationLevel::CREATION;

/// @brief Timeout that never expires.
inline constexpr std::chrono::nanoseconds WAIT_INDEFINITELY = std::chrono::nanoseconds::max();

/// @brief Bit set in the KeyCode of every non-printable key, above its HID usage.
inline constexpr std::uint32_t KEY_CODE_NON_PRINTABLE_FLAG = 0x40000000U;

// =================================================================================================
// Enumeration descriptions
// =================================================================================================

/// @brief Associates an enumerator with its spelling, for diagnostics and formatting.
template<class Enumeration>
struct EnumeratorName
{
    Enumeration value;     ///< Enumerator.
    std::string_view name; ///< Its spelling, without the enumeration name.
};

/// @brief Compile-time description of an enumeration. Specialized for every enumeration of the
///        library with `IS_BITMASK` and `NAMES` (the single-bit enumerators of a bitmask, or every
///        enumerator of a sequential enumeration in declaration order). Enumerations whose values have
///        gaps (e.g. keys numbered by USB HID usage) also set `IS_SPARSE = true` and list their
///        enumerators in ascending value order.
template<class Enumeration>
struct EnumerationTraits;

/// @brief An enumeration with an EnumerationTraits specialization.
template<class Enumeration>
concept DescribedEnumeration = std::is_enum_v<Enumeration> // Must be an enumeration type
                                    && requires {
                                        { EnumerationTraits<Enumeration>::IS_BITMASK } -> std::convertible_to<bool>; // Must have a specialization with IS_BITMASK
                                        EnumerationTraits<Enumeration>::NAMES.size(); // Must have a specialization with NAMES
                                    };

/// @brief An enumeration whose enumerators are single bits combined with |, & and ^.
template<class Enumeration>
concept BitmaskEnumeration = DescribedEnumeration<Enumeration>
                                && EnumerationTraits<Enumeration>::IS_BITMASK;

/// @brief An enumeration whose values have gaps (traits with `IS_SPARSE = true`), e.g. keys numbered
///        by USB HID usage. NAMES lists its enumerators in ascending value order.
template<class Enumeration>
concept SparseEnumeration = DescribedEnumeration<Enumeration>
                                && !EnumerationTraits<Enumeration>::IS_BITMASK
                                && requires {
                                       requires EnumerationTraits<Enumeration>::IS_SPARSE;
                                   };

/// @brief An enumeration whose enumerators are numbered 0, 1, 2, ... without gaps.
template<class Enumeration>
concept SequentialEnumeration = DescribedEnumeration<Enumeration>
                                    && !EnumerationTraits<Enumeration>::IS_BITMASK
                                    && !SparseEnumeration<Enumeration>;

/// @brief Number of enumerators of a sequential enumeration (e.g. to size translation tables).
template<SequentialEnumeration Enumeration>
inline constexpr std::size_t ENUMERATOR_COUNT = EnumerationTraits<Enumeration>::NAMES.size();

/// @brief An enumeration naming the backends of one library: its traits also give BACKEND_LIBRARY,
///        the library's name in error messages ("gfx", "gfx::platform").
template<class Enumeration>
concept BackendEnumeration = SequentialEnumeration<Enumeration>
                                 && requires {
                                        { EnumerationTraits<Enumeration>::BACKEND_LIBRARY } -> std::convertible_to<std::string_view>;
                                    };

template<>
struct EnumerationTraits<ValidationLevel>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ValidationLevel::NONE, "NONE"},
        EnumeratorName{ValidationLevel::CREATION, "CREATION"},
        EnumeratorName{ValidationLevel::COMMANDS, "COMMANDS"},
    };
};

template<>
struct EnumerationTraits<MemoryDomain>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{MemoryDomain::HOST, "HOST"},
        EnumeratorName{MemoryDomain::DEVICE, "DEVICE"},
    };
};

template<>
struct EnumerationTraits<NativeWindowKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{NativeWindowKind::NONE, "NONE"},
        EnumeratorName{NativeWindowKind::WINDOWS, "WINDOWS"},
        EnumeratorName{NativeWindowKind::XLIB, "XLIB"},
        EnumeratorName{NativeWindowKind::XCB, "XCB"},
        EnumeratorName{NativeWindowKind::WAYLAND, "WAYLAND"},
        EnumeratorName{NativeWindowKind::COCOA, "COCOA"},
        EnumeratorName{NativeWindowKind::WEB_CANVAS, "WEB_CANVAS"},
    };
};

template<>
struct EnumerationTraits<Key>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr bool IS_SPARSE = true;
    static constexpr std::array NAMES{
        EnumeratorName{Key::UNKNOWN, "UNKNOWN"},
        EnumeratorName{Key::A, "A"},
        EnumeratorName{Key::B, "B"},
        EnumeratorName{Key::C, "C"},
        EnumeratorName{Key::D, "D"},
        EnumeratorName{Key::E, "E"},
        EnumeratorName{Key::F, "F"},
        EnumeratorName{Key::G, "G"},
        EnumeratorName{Key::H, "H"},
        EnumeratorName{Key::I, "I"},
        EnumeratorName{Key::J, "J"},
        EnumeratorName{Key::K, "K"},
        EnumeratorName{Key::L, "L"},
        EnumeratorName{Key::M, "M"},
        EnumeratorName{Key::N, "N"},
        EnumeratorName{Key::O, "O"},
        EnumeratorName{Key::P, "P"},
        EnumeratorName{Key::Q, "Q"},
        EnumeratorName{Key::R, "R"},
        EnumeratorName{Key::S, "S"},
        EnumeratorName{Key::T, "T"},
        EnumeratorName{Key::U, "U"},
        EnumeratorName{Key::V, "V"},
        EnumeratorName{Key::W, "W"},
        EnumeratorName{Key::X, "X"},
        EnumeratorName{Key::Y, "Y"},
        EnumeratorName{Key::Z, "Z"},
        EnumeratorName{Key::NUMBER_1, "NUMBER_1"},
        EnumeratorName{Key::NUMBER_2, "NUMBER_2"},
        EnumeratorName{Key::NUMBER_3, "NUMBER_3"},
        EnumeratorName{Key::NUMBER_4, "NUMBER_4"},
        EnumeratorName{Key::NUMBER_5, "NUMBER_5"},
        EnumeratorName{Key::NUMBER_6, "NUMBER_6"},
        EnumeratorName{Key::NUMBER_7, "NUMBER_7"},
        EnumeratorName{Key::NUMBER_8, "NUMBER_8"},
        EnumeratorName{Key::NUMBER_9, "NUMBER_9"},
        EnumeratorName{Key::NUMBER_0, "NUMBER_0"},
        EnumeratorName{Key::RETURN, "RETURN"},
        EnumeratorName{Key::ESCAPE, "ESCAPE"},
        EnumeratorName{Key::BACKSPACE, "BACKSPACE"},
        EnumeratorName{Key::TAB, "TAB"},
        EnumeratorName{Key::SPACE, "SPACE"},
        EnumeratorName{Key::MINUS, "MINUS"},
        EnumeratorName{Key::EQUALS, "EQUALS"},
        EnumeratorName{Key::LEFT_BRACKET, "LEFT_BRACKET"},
        EnumeratorName{Key::RIGHT_BRACKET, "RIGHT_BRACKET"},
        EnumeratorName{Key::BACKSLASH, "BACKSLASH"},
        EnumeratorName{Key::NON_US_HASH, "NON_US_HASH"},
        EnumeratorName{Key::SEMICOLON, "SEMICOLON"},
        EnumeratorName{Key::APOSTROPHE, "APOSTROPHE"},
        EnumeratorName{Key::GRAVE, "GRAVE"},
        EnumeratorName{Key::COMMA, "COMMA"},
        EnumeratorName{Key::PERIOD, "PERIOD"},
        EnumeratorName{Key::SLASH, "SLASH"},
        EnumeratorName{Key::CAPS_LOCK, "CAPS_LOCK"},
        EnumeratorName{Key::F1, "F1"},
        EnumeratorName{Key::F2, "F2"},
        EnumeratorName{Key::F3, "F3"},
        EnumeratorName{Key::F4, "F4"},
        EnumeratorName{Key::F5, "F5"},
        EnumeratorName{Key::F6, "F6"},
        EnumeratorName{Key::F7, "F7"},
        EnumeratorName{Key::F8, "F8"},
        EnumeratorName{Key::F9, "F9"},
        EnumeratorName{Key::F10, "F10"},
        EnumeratorName{Key::F11, "F11"},
        EnumeratorName{Key::F12, "F12"},
        EnumeratorName{Key::PRINT_SCREEN, "PRINT_SCREEN"},
        EnumeratorName{Key::SCROLL_LOCK, "SCROLL_LOCK"},
        EnumeratorName{Key::PAUSE, "PAUSE"},
        EnumeratorName{Key::INSERT, "INSERT"},
        EnumeratorName{Key::HOME, "HOME"},
        EnumeratorName{Key::PAGE_UP, "PAGE_UP"},
        EnumeratorName{Key::DELETE_FORWARD, "DELETE_FORWARD"},
        EnumeratorName{Key::END, "END"},
        EnumeratorName{Key::PAGE_DOWN, "PAGE_DOWN"},
        EnumeratorName{Key::RIGHT, "RIGHT"},
        EnumeratorName{Key::LEFT, "LEFT"},
        EnumeratorName{Key::DOWN, "DOWN"},
        EnumeratorName{Key::UP, "UP"},
        EnumeratorName{Key::NUM_LOCK, "NUM_LOCK"},
        EnumeratorName{Key::KEYPAD_DIVIDE, "KEYPAD_DIVIDE"},
        EnumeratorName{Key::KEYPAD_MULTIPLY, "KEYPAD_MULTIPLY"},
        EnumeratorName{Key::KEYPAD_MINUS, "KEYPAD_MINUS"},
        EnumeratorName{Key::KEYPAD_PLUS, "KEYPAD_PLUS"},
        EnumeratorName{Key::KEYPAD_ENTER, "KEYPAD_ENTER"},
        EnumeratorName{Key::KEYPAD_1, "KEYPAD_1"},
        EnumeratorName{Key::KEYPAD_2, "KEYPAD_2"},
        EnumeratorName{Key::KEYPAD_3, "KEYPAD_3"},
        EnumeratorName{Key::KEYPAD_4, "KEYPAD_4"},
        EnumeratorName{Key::KEYPAD_5, "KEYPAD_5"},
        EnumeratorName{Key::KEYPAD_6, "KEYPAD_6"},
        EnumeratorName{Key::KEYPAD_7, "KEYPAD_7"},
        EnumeratorName{Key::KEYPAD_8, "KEYPAD_8"},
        EnumeratorName{Key::KEYPAD_9, "KEYPAD_9"},
        EnumeratorName{Key::KEYPAD_0, "KEYPAD_0"},
        EnumeratorName{Key::KEYPAD_PERIOD, "KEYPAD_PERIOD"},
        EnumeratorName{Key::NON_US_BACKSLASH, "NON_US_BACKSLASH"},
        EnumeratorName{Key::APPLICATION, "APPLICATION"},
        EnumeratorName{Key::POWER, "POWER"},
        EnumeratorName{Key::KEYPAD_EQUALS, "KEYPAD_EQUALS"},
        EnumeratorName{Key::F13, "F13"},
        EnumeratorName{Key::F14, "F14"},
        EnumeratorName{Key::F15, "F15"},
        EnumeratorName{Key::F16, "F16"},
        EnumeratorName{Key::F17, "F17"},
        EnumeratorName{Key::F18, "F18"},
        EnumeratorName{Key::F19, "F19"},
        EnumeratorName{Key::F20, "F20"},
        EnumeratorName{Key::F21, "F21"},
        EnumeratorName{Key::F22, "F22"},
        EnumeratorName{Key::F23, "F23"},
        EnumeratorName{Key::F24, "F24"},
        EnumeratorName{Key::EXECUTE, "EXECUTE"},
        EnumeratorName{Key::HELP, "HELP"},
        EnumeratorName{Key::MENU, "MENU"},
        EnumeratorName{Key::SELECT, "SELECT"},
        EnumeratorName{Key::STOP, "STOP"},
        EnumeratorName{Key::AGAIN, "AGAIN"},
        EnumeratorName{Key::UNDO, "UNDO"},
        EnumeratorName{Key::CUT, "CUT"},
        EnumeratorName{Key::COPY, "COPY"},
        EnumeratorName{Key::PASTE, "PASTE"},
        EnumeratorName{Key::FIND, "FIND"},
        EnumeratorName{Key::MUTE, "MUTE"},
        EnumeratorName{Key::VOLUME_UP, "VOLUME_UP"},
        EnumeratorName{Key::VOLUME_DOWN, "VOLUME_DOWN"},
        EnumeratorName{Key::KEYPAD_COMMA, "KEYPAD_COMMA"},
        EnumeratorName{Key::INTERNATIONAL_1, "INTERNATIONAL_1"},
        EnumeratorName{Key::INTERNATIONAL_2, "INTERNATIONAL_2"},
        EnumeratorName{Key::INTERNATIONAL_3, "INTERNATIONAL_3"},
        EnumeratorName{Key::INTERNATIONAL_4, "INTERNATIONAL_4"},
        EnumeratorName{Key::INTERNATIONAL_5, "INTERNATIONAL_5"},
        EnumeratorName{Key::INTERNATIONAL_6, "INTERNATIONAL_6"},
        EnumeratorName{Key::INTERNATIONAL_7, "INTERNATIONAL_7"},
        EnumeratorName{Key::INTERNATIONAL_8, "INTERNATIONAL_8"},
        EnumeratorName{Key::INTERNATIONAL_9, "INTERNATIONAL_9"},
        EnumeratorName{Key::LANGUAGE_1, "LANGUAGE_1"},
        EnumeratorName{Key::LANGUAGE_2, "LANGUAGE_2"},
        EnumeratorName{Key::LANGUAGE_3, "LANGUAGE_3"},
        EnumeratorName{Key::LANGUAGE_4, "LANGUAGE_4"},
        EnumeratorName{Key::LANGUAGE_5, "LANGUAGE_5"},
        EnumeratorName{Key::LANGUAGE_6, "LANGUAGE_6"},
        EnumeratorName{Key::LANGUAGE_7, "LANGUAGE_7"},
        EnumeratorName{Key::LANGUAGE_8, "LANGUAGE_8"},
        EnumeratorName{Key::LANGUAGE_9, "LANGUAGE_9"},
        EnumeratorName{Key::LEFT_CONTROL, "LEFT_CONTROL"},
        EnumeratorName{Key::LEFT_SHIFT, "LEFT_SHIFT"},
        EnumeratorName{Key::LEFT_ALT, "LEFT_ALT"},
        EnumeratorName{Key::LEFT_SUPER, "LEFT_SUPER"},
        EnumeratorName{Key::RIGHT_CONTROL, "RIGHT_CONTROL"},
        EnumeratorName{Key::RIGHT_SHIFT, "RIGHT_SHIFT"},
        EnumeratorName{Key::RIGHT_ALT, "RIGHT_ALT"},
        EnumeratorName{Key::RIGHT_SUPER, "RIGHT_SUPER"},
    };
};

template<>
struct EnumerationTraits<KeyCode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr bool IS_SPARSE = true;
    static constexpr std::array NAMES{
        EnumeratorName{KeyCode::UNKNOWN, "UNKNOWN"},
        EnumeratorName{KeyCode::BACKSPACE, "BACKSPACE"},
        EnumeratorName{KeyCode::TAB, "TAB"},
        EnumeratorName{KeyCode::RETURN, "RETURN"},
        EnumeratorName{KeyCode::ESCAPE, "ESCAPE"},
        EnumeratorName{KeyCode::SPACE, "SPACE"},
        EnumeratorName{KeyCode::EXCLAMATION_MARK, "EXCLAMATION_MARK"},
        EnumeratorName{KeyCode::QUOTATION_MARK, "QUOTATION_MARK"},
        EnumeratorName{KeyCode::HASH, "HASH"},
        EnumeratorName{KeyCode::DOLLAR, "DOLLAR"},
        EnumeratorName{KeyCode::PERCENT, "PERCENT"},
        EnumeratorName{KeyCode::AMPERSAND, "AMPERSAND"},
        EnumeratorName{KeyCode::APOSTROPHE, "APOSTROPHE"},
        EnumeratorName{KeyCode::LEFT_PARENTHESIS, "LEFT_PARENTHESIS"},
        EnumeratorName{KeyCode::RIGHT_PARENTHESIS, "RIGHT_PARENTHESIS"},
        EnumeratorName{KeyCode::ASTERISK, "ASTERISK"},
        EnumeratorName{KeyCode::PLUS, "PLUS"},
        EnumeratorName{KeyCode::COMMA, "COMMA"},
        EnumeratorName{KeyCode::MINUS, "MINUS"},
        EnumeratorName{KeyCode::PERIOD, "PERIOD"},
        EnumeratorName{KeyCode::SLASH, "SLASH"},
        EnumeratorName{KeyCode::NUMBER_0, "NUMBER_0"},
        EnumeratorName{KeyCode::NUMBER_1, "NUMBER_1"},
        EnumeratorName{KeyCode::NUMBER_2, "NUMBER_2"},
        EnumeratorName{KeyCode::NUMBER_3, "NUMBER_3"},
        EnumeratorName{KeyCode::NUMBER_4, "NUMBER_4"},
        EnumeratorName{KeyCode::NUMBER_5, "NUMBER_5"},
        EnumeratorName{KeyCode::NUMBER_6, "NUMBER_6"},
        EnumeratorName{KeyCode::NUMBER_7, "NUMBER_7"},
        EnumeratorName{KeyCode::NUMBER_8, "NUMBER_8"},
        EnumeratorName{KeyCode::NUMBER_9, "NUMBER_9"},
        EnumeratorName{KeyCode::COLON, "COLON"},
        EnumeratorName{KeyCode::SEMICOLON, "SEMICOLON"},
        EnumeratorName{KeyCode::LESS, "LESS"},
        EnumeratorName{KeyCode::EQUALS, "EQUALS"},
        EnumeratorName{KeyCode::GREATER, "GREATER"},
        EnumeratorName{KeyCode::QUESTION_MARK, "QUESTION_MARK"},
        EnumeratorName{KeyCode::AT, "AT"},
        EnumeratorName{KeyCode::LEFT_BRACKET, "LEFT_BRACKET"},
        EnumeratorName{KeyCode::BACKSLASH, "BACKSLASH"},
        EnumeratorName{KeyCode::RIGHT_BRACKET, "RIGHT_BRACKET"},
        EnumeratorName{KeyCode::CARET, "CARET"},
        EnumeratorName{KeyCode::UNDERSCORE, "UNDERSCORE"},
        EnumeratorName{KeyCode::GRAVE, "GRAVE"},
        EnumeratorName{KeyCode::A, "A"},
        EnumeratorName{KeyCode::B, "B"},
        EnumeratorName{KeyCode::C, "C"},
        EnumeratorName{KeyCode::D, "D"},
        EnumeratorName{KeyCode::E, "E"},
        EnumeratorName{KeyCode::F, "F"},
        EnumeratorName{KeyCode::G, "G"},
        EnumeratorName{KeyCode::H, "H"},
        EnumeratorName{KeyCode::I, "I"},
        EnumeratorName{KeyCode::J, "J"},
        EnumeratorName{KeyCode::K, "K"},
        EnumeratorName{KeyCode::L, "L"},
        EnumeratorName{KeyCode::M, "M"},
        EnumeratorName{KeyCode::N, "N"},
        EnumeratorName{KeyCode::O, "O"},
        EnumeratorName{KeyCode::P, "P"},
        EnumeratorName{KeyCode::Q, "Q"},
        EnumeratorName{KeyCode::R, "R"},
        EnumeratorName{KeyCode::S, "S"},
        EnumeratorName{KeyCode::T, "T"},
        EnumeratorName{KeyCode::U, "U"},
        EnumeratorName{KeyCode::V, "V"},
        EnumeratorName{KeyCode::W, "W"},
        EnumeratorName{KeyCode::X, "X"},
        EnumeratorName{KeyCode::Y, "Y"},
        EnumeratorName{KeyCode::Z, "Z"},
        EnumeratorName{KeyCode::LEFT_BRACE, "LEFT_BRACE"},
        EnumeratorName{KeyCode::PIPE, "PIPE"},
        EnumeratorName{KeyCode::RIGHT_BRACE, "RIGHT_BRACE"},
        EnumeratorName{KeyCode::TILDE, "TILDE"},
        EnumeratorName{KeyCode::DELETE_FORWARD, "DELETE_FORWARD"},
        EnumeratorName{KeyCode::CAPS_LOCK, "CAPS_LOCK"},
        EnumeratorName{KeyCode::F1, "F1"},
        EnumeratorName{KeyCode::F2, "F2"},
        EnumeratorName{KeyCode::F3, "F3"},
        EnumeratorName{KeyCode::F4, "F4"},
        EnumeratorName{KeyCode::F5, "F5"},
        EnumeratorName{KeyCode::F6, "F6"},
        EnumeratorName{KeyCode::F7, "F7"},
        EnumeratorName{KeyCode::F8, "F8"},
        EnumeratorName{KeyCode::F9, "F9"},
        EnumeratorName{KeyCode::F10, "F10"},
        EnumeratorName{KeyCode::F11, "F11"},
        EnumeratorName{KeyCode::F12, "F12"},
        EnumeratorName{KeyCode::PRINT_SCREEN, "PRINT_SCREEN"},
        EnumeratorName{KeyCode::SCROLL_LOCK, "SCROLL_LOCK"},
        EnumeratorName{KeyCode::PAUSE, "PAUSE"},
        EnumeratorName{KeyCode::INSERT, "INSERT"},
        EnumeratorName{KeyCode::HOME, "HOME"},
        EnumeratorName{KeyCode::PAGE_UP, "PAGE_UP"},
        EnumeratorName{KeyCode::END, "END"},
        EnumeratorName{KeyCode::PAGE_DOWN, "PAGE_DOWN"},
        EnumeratorName{KeyCode::RIGHT, "RIGHT"},
        EnumeratorName{KeyCode::LEFT, "LEFT"},
        EnumeratorName{KeyCode::DOWN, "DOWN"},
        EnumeratorName{KeyCode::UP, "UP"},
        EnumeratorName{KeyCode::NUM_LOCK, "NUM_LOCK"},
        EnumeratorName{KeyCode::KEYPAD_DIVIDE, "KEYPAD_DIVIDE"},
        EnumeratorName{KeyCode::KEYPAD_MULTIPLY, "KEYPAD_MULTIPLY"},
        EnumeratorName{KeyCode::KEYPAD_MINUS, "KEYPAD_MINUS"},
        EnumeratorName{KeyCode::KEYPAD_PLUS, "KEYPAD_PLUS"},
        EnumeratorName{KeyCode::KEYPAD_ENTER, "KEYPAD_ENTER"},
        EnumeratorName{KeyCode::KEYPAD_1, "KEYPAD_1"},
        EnumeratorName{KeyCode::KEYPAD_2, "KEYPAD_2"},
        EnumeratorName{KeyCode::KEYPAD_3, "KEYPAD_3"},
        EnumeratorName{KeyCode::KEYPAD_4, "KEYPAD_4"},
        EnumeratorName{KeyCode::KEYPAD_5, "KEYPAD_5"},
        EnumeratorName{KeyCode::KEYPAD_6, "KEYPAD_6"},
        EnumeratorName{KeyCode::KEYPAD_7, "KEYPAD_7"},
        EnumeratorName{KeyCode::KEYPAD_8, "KEYPAD_8"},
        EnumeratorName{KeyCode::KEYPAD_9, "KEYPAD_9"},
        EnumeratorName{KeyCode::KEYPAD_0, "KEYPAD_0"},
        EnumeratorName{KeyCode::KEYPAD_PERIOD, "KEYPAD_PERIOD"},
        EnumeratorName{KeyCode::APPLICATION, "APPLICATION"},
        EnumeratorName{KeyCode::POWER, "POWER"},
        EnumeratorName{KeyCode::KEYPAD_EQUALS, "KEYPAD_EQUALS"},
        EnumeratorName{KeyCode::F13, "F13"},
        EnumeratorName{KeyCode::F14, "F14"},
        EnumeratorName{KeyCode::F15, "F15"},
        EnumeratorName{KeyCode::F16, "F16"},
        EnumeratorName{KeyCode::F17, "F17"},
        EnumeratorName{KeyCode::F18, "F18"},
        EnumeratorName{KeyCode::F19, "F19"},
        EnumeratorName{KeyCode::F20, "F20"},
        EnumeratorName{KeyCode::F21, "F21"},
        EnumeratorName{KeyCode::F22, "F22"},
        EnumeratorName{KeyCode::F23, "F23"},
        EnumeratorName{KeyCode::F24, "F24"},
        EnumeratorName{KeyCode::EXECUTE, "EXECUTE"},
        EnumeratorName{KeyCode::HELP, "HELP"},
        EnumeratorName{KeyCode::MENU, "MENU"},
        EnumeratorName{KeyCode::SELECT, "SELECT"},
        EnumeratorName{KeyCode::STOP, "STOP"},
        EnumeratorName{KeyCode::AGAIN, "AGAIN"},
        EnumeratorName{KeyCode::UNDO, "UNDO"},
        EnumeratorName{KeyCode::CUT, "CUT"},
        EnumeratorName{KeyCode::COPY, "COPY"},
        EnumeratorName{KeyCode::PASTE, "PASTE"},
        EnumeratorName{KeyCode::FIND, "FIND"},
        EnumeratorName{KeyCode::MUTE, "MUTE"},
        EnumeratorName{KeyCode::VOLUME_UP, "VOLUME_UP"},
        EnumeratorName{KeyCode::VOLUME_DOWN, "VOLUME_DOWN"},
        EnumeratorName{KeyCode::KEYPAD_COMMA, "KEYPAD_COMMA"},
        EnumeratorName{KeyCode::LEFT_CONTROL, "LEFT_CONTROL"},
        EnumeratorName{KeyCode::LEFT_SHIFT, "LEFT_SHIFT"},
        EnumeratorName{KeyCode::LEFT_ALT, "LEFT_ALT"},
        EnumeratorName{KeyCode::LEFT_SUPER, "LEFT_SUPER"},
        EnumeratorName{KeyCode::RIGHT_CONTROL, "RIGHT_CONTROL"},
        EnumeratorName{KeyCode::RIGHT_SHIFT, "RIGHT_SHIFT"},
        EnumeratorName{KeyCode::RIGHT_ALT, "RIGHT_ALT"},
        EnumeratorName{KeyCode::RIGHT_SUPER, "RIGHT_SUPER"},
    };
};

template<>
struct EnumerationTraits<CursorShape>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{CursorShape::ARROW, "ARROW"},
        EnumeratorName{CursorShape::I_BEAM, "I_BEAM"},
        EnumeratorName{CursorShape::WAIT, "WAIT"},
        EnumeratorName{CursorShape::PROGRESS, "PROGRESS"},
        EnumeratorName{CursorShape::CROSSHAIR, "CROSSHAIR"},
        EnumeratorName{CursorShape::POINTER, "POINTER"},
        EnumeratorName{CursorShape::RESIZE_EAST_WEST, "RESIZE_EAST_WEST"},
        EnumeratorName{CursorShape::RESIZE_NORTH_SOUTH, "RESIZE_NORTH_SOUTH"},
        EnumeratorName{CursorShape::RESIZE_NORTHWEST_SOUTHEAST, "RESIZE_NORTHWEST_SOUTHEAST"},
        EnumeratorName{CursorShape::RESIZE_NORTHEAST_SOUTHWEST, "RESIZE_NORTHEAST_SOUTHWEST"},
        EnumeratorName{CursorShape::MOVE, "MOVE"},
        EnumeratorName{CursorShape::NOT_ALLOWED, "NOT_ALLOWED"},
    };
};

template<>
struct EnumerationTraits<MouseButton>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{MouseButton::LEFT, "LEFT"},
        EnumeratorName{MouseButton::MIDDLE, "MIDDLE"},
        EnumeratorName{MouseButton::RIGHT, "RIGHT"},
        EnumeratorName{MouseButton::BACK, "BACK"},
        EnumeratorName{MouseButton::FORWARD, "FORWARD"},
    };
};

template<>
struct EnumerationTraits<MouseButtonMask>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{MouseButtonMask::LEFT, "LEFT"},
        EnumeratorName{MouseButtonMask::MIDDLE, "MIDDLE"},
        EnumeratorName{MouseButtonMask::RIGHT, "RIGHT"},
        EnumeratorName{MouseButtonMask::BACK, "BACK"},
        EnumeratorName{MouseButtonMask::FORWARD, "FORWARD"},
    };
};

template<>
struct EnumerationTraits<KeyModifier>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{KeyModifier::LEFT_SHIFT, "LEFT_SHIFT"},
        EnumeratorName{KeyModifier::RIGHT_SHIFT, "RIGHT_SHIFT"},
        EnumeratorName{KeyModifier::LEFT_CONTROL, "LEFT_CONTROL"},
        EnumeratorName{KeyModifier::RIGHT_CONTROL, "RIGHT_CONTROL"},
        EnumeratorName{KeyModifier::LEFT_ALT, "LEFT_ALT"},
        EnumeratorName{KeyModifier::RIGHT_ALT, "RIGHT_ALT"},
        EnumeratorName{KeyModifier::LEFT_SUPER, "LEFT_SUPER"},
        EnumeratorName{KeyModifier::RIGHT_SUPER, "RIGHT_SUPER"},
        EnumeratorName{KeyModifier::CAPS_LOCK, "CAPS_LOCK"},
        EnumeratorName{KeyModifier::NUM_LOCK, "NUM_LOCK"},
        EnumeratorName{KeyModifier::SCROLL_LOCK, "SCROLL_LOCK"},
    };
};

// =================================================================================================
// Bitmask operators
// =================================================================================================

/// @brief Union of two sets of bits.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator|(Enumeration left, Enumeration right) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) | std::to_underlying(right)));
}

/// @brief Intersection of two sets of bits.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator&(Enumeration left, Enumeration right) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) & std::to_underlying(right)));
}

/// @brief Bits set in exactly one of the operands.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator^(Enumeration left, Enumeration right) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) ^ std::to_underlying(right)));
}

/// @brief Complement of a set of bits, restricted to the bits named by the enumeration.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator~(Enumeration value) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    Underlying named_bits = 0;
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
    {
        named_bits = static_cast<Underlying>(named_bits | std::to_underlying(entry.value));
    }
    return static_cast<Enumeration>(static_cast<Underlying>(named_bits & static_cast<Underlying>(~std::to_underlying(value))));
}

/// @brief Adds the bits of @p right to @p left.
template<BitmaskEnumeration Enumeration>
constexpr auto
operator|=(Enumeration& left, Enumeration right) noexcept -> Enumeration&
{
    return left = left | right;
}

/// @brief Keeps in @p left only the bits also set in @p right.
template<BitmaskEnumeration Enumeration>
constexpr auto
operator&=(Enumeration& left, Enumeration right) noexcept -> Enumeration&
{
    return left = left & right;
}

/// @brief Toggles in @p left the bits set in @p right.
template<BitmaskEnumeration Enumeration>
constexpr auto
operator^=(Enumeration& left, Enumeration right) noexcept -> Enumeration&
{
    return left = left ^ right;
}

/// @brief Whether every bit of @p bits is set in @p value (true when @p bits is empty).
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
has_all(Enumeration value, Enumeration bits) noexcept -> bool
{
    return (value & bits) == bits;
}

/// @brief Whether at least one bit of @p bits is set in @p value.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
has_any(Enumeration value, Enumeration bits) noexcept -> bool
{
    return std::to_underlying(value & bits) != 0;
}

/// @brief Whether no bit is set.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
is_empty(Enumeration value) noexcept -> bool
{
    return std::to_underlying(value) == 0;
}

// =================================================================================================
// Spelling
// =================================================================================================

/// @brief Spelling of an enumerator of a sequential enumeration, e.g. "R8G8B8A8_UNORM".
/// @return The spelling, or "<invalid>" for a value that names no enumerator.
template<SequentialEnumeration Enumeration>
[[nodiscard]] constexpr auto
to_string(Enumeration value) noexcept -> std::string_view
{
    const auto index = static_cast<std::size_t>(std::to_underlying(value));
    const auto& names = EnumerationTraits<Enumeration>::NAMES;
    return index < names.size() ? names[index].name : std::string_view{"<invalid>"};
}

/// @brief Spelling of an enumerator of a sparse enumeration, e.g. "SPACE".
/// @return The spelling, or "<invalid>" for a value that names no enumerator.
template<SparseEnumeration Enumeration>
[[nodiscard]] constexpr auto
to_string(Enumeration value) noexcept -> std::string_view
{
    // NAMES is sorted by value: binary search.
    const auto& names = EnumerationTraits<Enumeration>::NAMES;
    const auto found = std::ranges::lower_bound(names, std::to_underlying(value), {},
                                                [](const auto& entry) { return std::to_underlying(entry.value); });
    return found != names.end() && found->value == value ? found->name : std::string_view{"<invalid>"};
}

/// @brief Spelling of a set of bits, e.g. "VERTEX | FRAGMENT"; "NONE" when empty. Bits that no
///        enumerator names are appended in hexadecimal.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
to_string(Enumeration value) -> std::string
{
    using Underlying = std::underlying_type_t<Enumeration>;
    auto remaining = std::to_underlying(value);

    // Check for the empty set first, so that we don't print "NONE | 0x..." for a value with no named bits.
    if (remaining == 0)
    {
        return "NONE";
    }

    // Iterate over the enumerators in order, appending their names to the text if their bit is set.
    std::string text;
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
    {
        // Skip enumerators that are not single bits, so that we don't print "ALL_GRAPHICS" when the value is "VERTEX | FRAGMENT | COMPUTE".
        const auto bit = std::to_underlying(entry.value);
        if ((remaining & bit) != 0)
        {
            // Append a separator if this is not the first enumerator name.
            if (!text.empty())
            {
                text += " | ";
            }

            // Append the enumerator name and remove its bit from the remaining bits.
            text += entry.name;

            // Remove the bit from the remaining bits, so that we don't print it again if another enumerator has the same bit set.
            remaining = static_cast<Underlying>(remaining & static_cast<Underlying>(~bit));
        }
    }

    // If any bits remain that no enumerator names, append them in hexadecimal.
    if (remaining != 0)
    {
        // Append the remaining bits in hexadecimal, with the most significant digit first. The digits are appended to the front of the string, so that the final string is in big-endian order.
        constexpr std::string_view digits = "0123456789ABCDEF";
        std::string hexadecimal;

        // Shift the remaining bits right by 4 each iteration, appending the least significant hex digit to the front of the string.
        for (auto rest = static_cast<std::uint64_t>(remaining); rest != 0; rest >>= 4U)
         {
            hexadecimal.insert(hexadecimal.begin(), digits[static_cast<std::size_t>(rest & 0xFU)]);
        }

        // Append a separator if this is not the first enumerator name, then append the hexadecimal representation of the remaining bits.
        if (!text.empty())
        {
            text += " | ";
        }

        // Append the hexadecimal representation of the remaining bits, prefixed with "0x".
        text += "0x" + hexadecimal;
    }
    return text;
}

// =================================================================================================
// Parsing
// =================================================================================================

/// @brief The enumerator spelled @p name, as to_string() spells it (e.g. "R8G8B8A8_UNORM").
/// @return The enumerator, or std::nullopt when no enumerator has that spelling.
template<class Enumeration>
    requires SequentialEnumeration<Enumeration> || SparseEnumeration<Enumeration>
[[nodiscard]] constexpr auto
from_string(std::string_view name) noexcept -> std::optional<Enumeration>
{
    // Look the spelling up in the enumeration's table.
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
    {
        if (entry.name == name)
        {
            return entry.value;
        }
    }
    return std::nullopt;
}

/// @brief The set of bits spelled @p text, as to_string() spells it: enumerator names separated by
///        "|" (spaces around them optional), "NONE" for the empty set, unnamed bits in hexadecimal
///        ("0x40").
/// @return The bits, or std::nullopt when a part names no enumerator.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
from_string(std::string_view text) noexcept -> std::optional<Enumeration>
{
    using Underlying = std::underlying_type_t<Enumeration>;
    std::uint64_t bits = 0;

    // Every part between separators names an enumerator, NONE, or a hexadecimal value.
    std::size_t start = 0;
    while (start <= text.size())
    {
        // The next part, without surrounding spaces.
        const std::size_t separator = std::min(text.find('|', start), text.size());
        std::string_view part = text.substr(start, separator - start);
        while (!part.empty() && part.front() == ' ')
        {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ')
        {
            part.remove_suffix(1);
        }

        // An enumerator, the empty set, or bits in hexadecimal.
        bool known = false;
        for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
        {
            if (entry.name == part)
            {
                bits |= static_cast<std::uint64_t>(std::to_underlying(entry.value));
                known = true;
            }
        }
        if (!known && part == "NONE")
        {
            known = true;
        }
        if (!known && part.size() > 2 && part.starts_with("0x"))
        {
            std::uint64_t value = 0;
            known = true;
            for (const char digit : part.substr(2))
            {
                const int nibble = digit >= '0' && digit <= '9'   ? digit - '0'
                                   : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                   : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                                                                  : -1;
                if (nibble < 0)
                {
                    known = false;
                    break;
                }
                value = (value << 4U) | static_cast<std::uint64_t>(nibble);
            }
            bits |= value;
        }
        if (!known)
        {
            return std::nullopt;
        }
        start = separator + 1;
    }
    return static_cast<Enumeration>(static_cast<Underlying>(bits));
}

} // namespace gfx

// =================================================================================================
// Compile-time verification of enumeration tables, used by every library's enums.cppm
// =================================================================================================
export namespace gfx::enumeration_verification
{

/// Checks order (sequential: value == index; bitmask: single ascending bits; sparse: ascending values)
/// and unique spellings.
template<class Enumeration, std::size_t Count>
consteval auto
names_are_consistent(const std::array<EnumeratorName<Enumeration>, Count>& names,
                     bool is_bitmask,
                     bool is_sparse) -> bool
{
    // The underlying type of the enumeration, converted to unsigned so that we can compare values without worrying about signedness.
    using Unsigned = std::make_unsigned_t<std::underlying_type_t<Enumeration>>;

    // Check that every enumerator has a non-empty name, that the enumerators are in the right order, and that no two enumerators have the same name.
    for (std::size_t index = 0; index < Count; ++index)
    {
        // The enumerator's value, converted to unsigned so that we can compare values without worrying about signedness.
        const auto value = static_cast<Unsigned>(std::to_underlying(names[index].value));

        // Check that the enumerator has a non-empty name.
        if (names[index].name.empty())
        {
            return false;
        }

        // Check that the enumerators are in the right order, and that no two enumerators have the same name.
        if (is_bitmask)
        {
            // Check that the enumerators are in ascending order, and that each enumerator is a single bit (1, 2, 4, 8, ...).
            if (!std::has_single_bit(value))
            {
                return false;
            }

            // Check that the enumerators are in ascending order, and that each enumerator is a single bit (1, 2, 4, 8, ...).
            if (index > 0 && value <= static_cast<Unsigned>(std::to_underlying(names[index - 1].value)))
            {
                return false;
            }
        }
        else if (is_sparse) // Sparse enumerations list strictly ascending values.
        {
            if (index > 0 && value <= static_cast<Unsigned>(std::to_underlying(names[index - 1].value)))
            {
                return false;
            }
        }
        else if (value != index) // Sequential enumerations must have values equal to their index.
        {
            return false;
        }

        // Iterate over all previous enumerators to check that no two enumerators have the same name.
        for (std::size_t other = 0; other < index; ++other)
        {
            // Check that no two enumerators have the same name.
            if (names[other].name == names[index].name)
            {
                return false;
            }
        }
    }
    return true;
}

#if defined(__GNUC__)
/// The compiler's own spelling of the enumerator Value: its last `::` component, or an empty view
/// when Value names no enumerator (GCC and Clang then print a cast such as "(gfx::Format)72").
template<auto Value>
consteval auto
compiler_spelling() -> std::string_view
{
    // The compiler's own spelling of the enumerator Value, as printed by __PRETTY_FUNCTION__.
    const std::string_view signature = __PRETTY_FUNCTION__;

    // The signature is of the form:
    const auto start = signature.find("Value = ");

    // If not found, the signature is not in the expected format, so we return an empty view.
    if (start == std::string_view::npos)
    {
        return {};
    }

    // The signature is of the form:
    const auto end = signature.find_first_of(";]", start);
    const auto value = signature.substr(start + 8, end - start - 8);

    // If not found, the signature is not in the expected format, so we return an empty view.
    if (value.find(')') != std::string_view::npos)
    {
        return {};
    }

    // The signature is of the form:
    const auto separator = value.rfind("::");

    // If not found, the signature is not in the expected format, so we return the whole value.
    return separator == std::string_view::npos ? value
                                               : value.substr(separator + 2);
}

/// Every NAMES entry is spelled exactly like its enumerator, and the enumeration declares no
/// enumerator missing from NAMES (sequential: right after the last entry; bitmask: any other bit).
template<DescribedEnumeration Enumeration>
consteval auto
names_match_compiler() -> bool
{
    using Traits = EnumerationTraits<Enumeration>;
    using Underlying = std::underlying_type_t<Enumeration>;

    // The number of entries in NAMES, so that we can check every entry for a spelling match.
    constexpr auto count = Traits::NAMES.size();

    // Check that every NAMES entry is spelled exactly like its enumerator.
    const bool entries_match = []<std::size_t... Index>(std::index_sequence<Index...>) {
        return ((compiler_spelling<Traits::NAMES[Index].value>() == Traits::NAMES[Index].name) && ...);
    }(std::make_index_sequence<count>{});

    // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
    if constexpr (Traits::IS_BITMASK)
    {
        // The number of bits in the underlying type of the enumeration, so that we can check every bit for a missing enumerator.
        constexpr auto bit_count = std::numeric_limits<std::make_unsigned_t<Underlying>>::digits;

        // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
        const bool no_unnamed_bit = []<std::size_t... Bit>(std::index_sequence<Bit...>)
        {
            constexpr auto named = [] {
                // The union of all bits named by the enumerators in NAMES, so that we can check every bit for a missing enumerator.
                std::make_unsigned_t<Underlying> bits = 0;

                // Compute the union of all bits named by the enumerators in NAMES.
                for (const auto& entry : Traits::NAMES)
                {
                    bits = static_cast<std::make_unsigned_t<Underlying>>(bits | static_cast<std::make_unsigned_t<Underlying>>(std::to_underlying(entry.value)));
                }

                // Return the union of all bits named by the enumerators in NAMES.
                return bits;
            }();

            // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
            return ((((named >> Bit) & 1U) != 0 ||
                     compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(std::uint64_t{1} << Bit))>().empty()) && ...);
        }(std::make_index_sequence<bit_count>{});

        // The entries in NAMES match the compiler's spelling, and the enumeration declares no enumerator missing from NAMES.
        return entries_match && no_unnamed_bit;
    }
    else if constexpr (SparseEnumeration<Enumeration>)
    {
        // Every value below SPARSE_VERIFICATION_LIMIT (and up to just past the largest named one) that NAMES
        // does not list must name no enumerator; larger values are only checked through their NAMES entries.
        constexpr auto largest = static_cast<std::size_t>(std::to_underlying(Traits::NAMES[count - 1].value));
        constexpr auto checked = std::min<std::size_t>(largest + 2, SPARSE_VERIFICATION_LIMIT);
        const bool no_unnamed_value = []<std::size_t... Value>(std::index_sequence<Value...>)
        {
            constexpr auto is_named = [](std::size_t value)
            {
                for (const auto& entry : Traits::NAMES)
                {
                    if (static_cast<std::size_t>(std::to_underlying(entry.value)) == value)
                    {
                        return true;
                    }
                }
                return false;
            };
            return ((is_named(Value) || compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(Value))>().empty()) && ...);
        }(std::make_index_sequence<checked>{});
        return entries_match && no_unnamed_value;
    }
    else
    {
        // The entries in NAMES match the compiler's spelling, and the enumeration declares no enumerator missing from NAMES (the last enumerator is right after the last entry in NAMES).
        return entries_match && compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(count))>().empty();
    }
}
#else
template<DescribedEnumeration Enumeration>
consteval auto names_match_compiler() -> bool {
    return true;
}
#endif

template<DescribedEnumeration Enumeration>
consteval auto
verify() -> bool
{
    // Check that the entries in NAMES are consistent with the enumeration, and that they match the compiler's spelling of the enumerators.
    return names_are_consistent(EnumerationTraits<Enumeration>::NAMES, EnumerationTraits<Enumeration>::IS_BITMASK,
                                SparseEnumeration<Enumeration>)
        && names_match_compiler<Enumeration>();
}

static_assert(verify<ValidationLevel>(), "EnumerationTraits<ValidationLevel>::NAMES is out of sync with the enumeration");
static_assert(verify<MemoryDomain>(), "EnumerationTraits<MemoryDomain>::NAMES is out of sync with the enumeration");
static_assert(verify<NativeWindowKind>(), "EnumerationTraits<NativeWindowKind>::NAMES is out of sync with the enumeration");
static_assert(verify<Key>(), "EnumerationTraits<Key>::NAMES is out of sync with the enumeration");
static_assert(verify<KeyCode>(), "EnumerationTraits<KeyCode>::NAMES is out of sync with the enumeration");
static_assert(verify<CursorShape>(), "EnumerationTraits<CursorShape>::NAMES is out of sync with the enumeration");
static_assert(verify<MouseButton>(), "EnumerationTraits<MouseButton>::NAMES is out of sync with the enumeration");
static_assert(verify<MouseButtonMask>(), "EnumerationTraits<MouseButtonMask>::NAMES is out of sync with the enumeration");
static_assert(verify<KeyModifier>(), "EnumerationTraits<KeyModifier>::NAMES is out of sync with the enumeration");

} // namespace gfx::enumeration_verification

// =================================================================================================
// std::format support: std::format("{}", gfx::Format::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM"
// =================================================================================================

/// @brief Formats any described gfx enumeration by its spelling (bitmasks as "A | B").
template<gfx::DescribedEnumeration Enumeration>
struct std::formatter<Enumeration, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(Enumeration value, FormatContext& context) const -> FormatContext::iterator
    {
        // Check if the enumeration is a bitmask enumeration, and format it accordingly.
        if constexpr (gfx::BitmaskEnumeration<Enumeration>)
        {
            // For bitmask enumerations, we can format the value as a string by using the to_string function, which will return a string with the names of
            // the enumerators that are set in the bitmask, separated by " | ". If no enumerators are set, it will return "0x0". We can then use the
            // std::formatter for std::string_view to format this string.
            const std::string text = gfx::to_string(value);
            return std::formatter<std::string_view, char>::format(text, context);
        }
        else
        {
            // For non-bitmask enumerations, we can use the underlying type to format the value as a string.
            return std::formatter<std::string_view, char>::format(gfx::to_string(value), context);
        }
    }
};
