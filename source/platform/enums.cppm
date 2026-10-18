/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx platform library (windowing and input). No other
///        file of the platform library defines either; the enumeration machinery (traits, verification,
///        bitmask operators, spelling and parsing) comes from gfx.core.enums, which this module
///        re-exports. The keyboard, mouse and cursor enumerations (Key, KeyCode, KeyModifier,
///        MouseButton, MouseButtonMask, CursorShape) are core's, shared with the UI library.
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

// The input enumerations the UI library shares, defined in gfx.core.enums (namespace gfx): spelled
// gfx::platform::Key etc. as before.
using gfx::CursorShape;
using gfx::Key;
using gfx::KEY_CODE_NON_PRINTABLE_FLAG;
using gfx::KeyCode;
using gfx::KeyModifier;
using gfx::MouseButton;
using gfx::MouseButtonMask;

// =================================================================================================
// Enumerations
// =================================================================================================

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

/// @brief How a window treats the mouse cursor.
enum class CursorMode : std::uint8_t
{
    NORMAL,   ///< Visible and free.
    HIDDEN,   ///< Invisible over the window, free to leave it.
    CONFINED, ///< Visible, kept inside the window.
    CAPTURED, ///< Invisible and locked; MOUSE_MOTION reports relative motion (cameras).
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
    NINTENDO_GAMECUBE,            ///< GameCube controller (through an adapter).
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
        EnumeratorName{platform::GamepadType::NINTENDO_GAMECUBE, "NINTENDO_GAMECUBE"},
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

static_assert(verify<platform::PlatformBackendKind>(), "EnumerationTraits<platform::PlatformBackendKind>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::PlatformFeature>(), "EnumerationTraits<platform::PlatformFeature>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::WindowMode>(), "EnumerationTraits<platform::WindowMode>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::WindowFlag>(), "EnumerationTraits<platform::WindowFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::SurfaceApi>(), "EnumerationTraits<platform::SurfaceApi>::NAMES is out of sync with the enumeration");
static_assert(verify<platform::CursorMode>(), "EnumerationTraits<platform::CursorMode>::NAMES is out of sync with the enumeration");
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
