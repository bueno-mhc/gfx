// Gamepads and joysticks of the SDL3 backend, without hardware: SDL virtual joysticks
// (SDL_AttachVirtualJoystick) report connections, buttons and axes through SDL's normal path, and
// record the rumble and light requests that reach them. No window is created: SDL ignores device
// input while windows exist and none has the focus.
#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>

import std;
import gfx.platform;
import gfx.tests.sdl3_support;

namespace
{

using namespace gfx::platform;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;
using gfx::tests::pump_until;
using gfx::tests::pump_until_type;
using gfx::tests::ReceivedEvent;

/// What reached a virtual device.
struct DeviceRecorder
{
    Uint16 low_frequency = 0;
    Uint16 high_frequency = 0;
    Uint16 left_trigger = 0;
    Uint16 right_trigger = 0;
    std::array<Uint8, 3> led{};
};

bool SDLCALL
record_rumble(void* userdata,
              Uint16 low_frequency,
              Uint16 high_frequency)
{
    static_cast<DeviceRecorder*>(userdata)->low_frequency = low_frequency;
    static_cast<DeviceRecorder*>(userdata)->high_frequency = high_frequency;
    return true;
}

bool SDLCALL
record_trigger_rumble(void* userdata,
                      Uint16 left,
                      Uint16 right)
{
    static_cast<DeviceRecorder*>(userdata)->left_trigger = left;
    static_cast<DeviceRecorder*>(userdata)->right_trigger = right;
    return true;
}

bool SDLCALL
record_led(void* userdata,
           Uint8 red,
           Uint8 green,
           Uint8 blue)
{
    static_cast<DeviceRecorder*>(userdata)->led = {red, green, blue};
    return true;
}

/// A virtual device attached for the lifetime of the object, and opened to drive its inputs.
class VirtualDevice
{
public:
    VirtualDevice(Uint16 type,
                  const char* name,
                  DeviceRecorder* recorder)
    {
        SDL_VirtualJoystickDesc description;
        SDL_INIT_INTERFACE(&description);
        description.type = type;
        description.name = name;
        if (type == SDL_JOYSTICK_TYPE_GAMEPAD)
        {
            description.naxes = SDL_GAMEPAD_AXIS_COUNT;
            description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        }
        else
        {
            description.naxes = 2;
            description.nbuttons = 3;
            description.nhats = 1;
        }
        if (recorder != nullptr)
        {
            description.userdata = recorder;
            description.Rumble = &record_rumble;
            description.RumbleTriggers = &record_trigger_rumble;
            description.SetLED = &record_led;
        }
        m_instance = SDL_AttachVirtualJoystick(&description);
        m_joystick = SDL_OpenJoystick(m_instance);
    }

    ~VirtualDevice()
    {
        SDL_CloseJoystick(m_joystick);
        SDL_DetachVirtualJoystick(m_instance);
    }

    VirtualDevice(const VirtualDevice&) = delete;
    auto operator=(const VirtualDevice&) -> VirtualDevice& = delete;

    [[nodiscard]] auto attached() const noexcept -> bool
    {
        return m_instance != 0 && m_joystick != nullptr;
    }

    void detach() noexcept
    {
        SDL_CloseJoystick(std::exchange(m_joystick, nullptr));
        SDL_DetachVirtualJoystick(std::exchange(m_instance, 0));
    }

    void set_button(int button,
                    bool down) const
    {
        SDL_SetJoystickVirtualButton(m_joystick, button, down);
    }

    void set_axis(int axis,
                  Sint16 value) const
    {
        SDL_SetJoystickVirtualAxis(m_joystick, axis, value);
    }

    void set_hat(int hat,
                 Uint8 value) const
    {
        SDL_SetJoystickVirtualHat(m_joystick, hat, value);
    }

private:
    SDL_JoystickID m_instance = 0;
    SDL_Joystick* m_joystick = nullptr;
};

/// The handle of the gamepad whose connection is reported in the next pumps.
auto
connected_gamepad(Platform& platform) -> GamepadHandle
{
    const std::optional<ReceivedEvent> connected = pump_until_type(platform, EventType::GAMEPAD_CONNECTED);
    return connected ? connected->event.as<GamepadDeviceEvent>().gamepad : GamepadHandle{};
}

TEST(Sdl3GamepadTest, GamepadsConnectAndReportTheirButtonsAndAxes)
{
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    DeviceRecorder recorder;
    const VirtualDevice device{SDL_JOYSTICK_TYPE_GAMEPAD, "gfx virtual gamepad", &recorder};
    ASSERT_TRUE(device.attached()) << SDL_GetError();

    // Connected once, with its name and capabilities.
    const GamepadHandle gamepad = connected_gamepad(*platform);
    ASSERT_TRUE(gamepad.is_valid());
    InputDevices& devices = platform->input_devices();
    EXPECT_EQ(devices.gamepads(), std::vector<GamepadHandle>{gamepad});
    const GamepadProperties properties = devices.gamepad_properties(gamepad);
    EXPECT_EQ(properties.name, "gfx virtual gamepad");
    EXPECT_TRUE(properties.has_rumble);
    EXPECT_TRUE(properties.has_trigger_rumble);
    EXPECT_TRUE(properties.has_led);

    // A button and an axis, as events and as current state.
    device.set_button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    std::vector<ReceivedEvent> received;
    ASSERT_TRUE(pump_until(*platform, [](const Event& event) { return event.type() == EventType::GAMEPAD_BUTTON_DOWN; }, received));
    const auto button = std::ranges::find_if(received, [](const ReceivedEvent& event) { return event.event.type() == EventType::GAMEPAD_BUTTON_DOWN; });
    EXPECT_EQ(button->event.as<GamepadButtonEvent>().gamepad, gamepad);
    EXPECT_EQ(button->event.as<GamepadButtonEvent>().button, GamepadButton::SOUTH);
    EXPECT_TRUE(devices.is_gamepad_button_down(gamepad, GamepadButton::SOUTH));
    EXPECT_FALSE(devices.is_gamepad_button_down(gamepad, GamepadButton::EAST));

    device.set_axis(SDL_GAMEPAD_AXIS_LEFTX, 16384);
    std::optional<float> axis_value;
    std::vector<ReceivedEvent> motions;
    ASSERT_TRUE(pump_until(*platform, [&axis_value](const Event& event)
    {
        if (const auto* axis = event.get_if<GamepadAxisEvent>(); axis != nullptr && axis->axis == GamepadAxis::LEFT_X)
        {
            axis_value = axis->value;
        }
        return axis_value.has_value();
    }, motions));
    EXPECT_NEAR(*axis_value, 0.5F, 0.001F);
    EXPECT_NEAR(devices.gamepad_axis(gamepad, GamepadAxis::LEFT_X), 0.5F, 0.001F);
}

TEST(Sdl3GamepadTest, GamepadsConnectedBeforeThePlatformAreReportedOnce)
{
    // A gamepad attached before the platform starts: known at once, reported once by the first pumps.
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_GAMEPAD)) << SDL_GetError();
    std::optional<VirtualDevice> device{std::in_place, SDL_JOYSTICK_TYPE_GAMEPAD, "gfx early gamepad", nullptr};
    ASSERT_TRUE(device->attached()) << SDL_GetError();
    std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    EXPECT_EQ(platform->input_devices().gamepads().size(), 1U);
    std::vector<ReceivedEvent> received;
    EXPECT_TRUE(pump_until(*platform, [](const Event& event) { return event.type() == EventType::GAMEPAD_CONNECTED; }, received));
    for (int pump = 0; pump < 5; ++pump)
    {
        for (const Event& event : platform->wait_events_timeout(10ms))
        {
            received.push_back(ReceivedEvent{.event = event, .text = {}});
        }
    }
    const auto connections = std::ranges::count_if(received, [](const ReceivedEvent& event) { return event.event.type() == EventType::GAMEPAD_CONNECTED; });
    EXPECT_EQ(connections, 1);
    device.reset();
    platform.reset();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

TEST(Sdl3GamepadTest, RumbleAndLightReachTheDevice)
{
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    DeviceRecorder recorder;
    const VirtualDevice device{SDL_JOYSTICK_TYPE_GAMEPAD, "gfx rumble gamepad", &recorder};
    const GamepadHandle gamepad = connected_gamepad(*platform);
    ASSERT_TRUE(gamepad.is_valid());

    // Strengths in [0, 1] arrive as SDL's 16-bit intensities.
    InputDevices& devices = platform->input_devices();
    devices.set_gamepad_rumble(gamepad, 0.5F, 1.0F, 100ms);
    EXPECT_EQ(recorder.low_frequency, 32768);
    EXPECT_EQ(recorder.high_frequency, 65535);
    devices.set_gamepad_trigger_rumble(gamepad, 1.0F, 0.0F, 50ms);
    EXPECT_EQ(recorder.left_trigger, 65535);
    EXPECT_EQ(recorder.right_trigger, 0);
    devices.set_gamepad_led(gamepad, LedColor{.red = 10, .green = 20, .blue = 30});
    EXPECT_EQ(recorder.led, (std::array<Uint8, 3>{10, 20, 30}));
}

TEST(Sdl3GamepadTest, GamepadsWithoutMotorsSaySo)
{
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    const VirtualDevice device{SDL_JOYSTICK_TYPE_GAMEPAD, "gfx plain gamepad", nullptr};
    const GamepadHandle gamepad = connected_gamepad(*platform);
    ASSERT_TRUE(gamepad.is_valid());
    InputDevices& devices = platform->input_devices();
    EXPECT_FALSE(devices.gamepad_properties(gamepad).has_rumble);
    try
    {
        devices.set_gamepad_rumble(gamepad, 1.0F, 1.0F, 100ms);
        FAIL() << "a gamepad without motors rumbled";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("has no rumble motors"), std::string_view::npos) << error.what();
    }
    EXPECT_THROW(devices.set_gamepad_led(gamepad, LedColor{}), gfx::BackendCapabilityError);
}

TEST(Sdl3GamepadTest, DisconnectedGamepadsGoStale)
{
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    VirtualDevice device{SDL_JOYSTICK_TYPE_GAMEPAD, "gfx leaving gamepad", nullptr};
    const GamepadHandle gamepad = connected_gamepad(*platform);
    ASSERT_TRUE(gamepad.is_valid());
    device.detach();
    const std::optional<ReceivedEvent> disconnected = pump_until_type(*platform, EventType::GAMEPAD_DISCONNECTED);
    ASSERT_TRUE(disconnected.has_value());
    EXPECT_EQ(disconnected->event.as<GamepadDeviceEvent>().gamepad, gamepad);
    EXPECT_TRUE(platform->input_devices().gamepads().empty());
    EXPECT_THROW((void)platform->input_devices().gamepad_properties(gamepad), gfx::InvalidHandleError);
}

TEST(Sdl3GamepadTest, JoysticksWithoutMappingReportAxesButtonsAndHats)
{
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    const VirtualDevice device{SDL_JOYSTICK_TYPE_FLIGHT_STICK, "gfx flight stick", nullptr};
    const std::optional<ReceivedEvent> connected = pump_until_type(*platform, EventType::JOYSTICK_CONNECTED);
    ASSERT_TRUE(connected.has_value());
    const JoystickHandle joystick = connected->event.as<JoystickDeviceEvent>().joystick;
    const JoystickProperties properties = platform->input_devices().joystick_properties(joystick);
    EXPECT_EQ(properties.name, "gfx flight stick");
    EXPECT_EQ(properties.axis_count, 2U);
    EXPECT_EQ(properties.button_count, 3U);
    EXPECT_EQ(properties.hat_count, 1U);
    EXPECT_TRUE(platform->input_devices().gamepads().empty());

    // A hat pushed up and right.
    device.set_hat(0, SDL_HAT_RIGHTUP);
    const std::optional<ReceivedEvent> hat = pump_until_type(*platform, EventType::JOYSTICK_HAT_MOTION);
    ASSERT_TRUE(hat.has_value());
    EXPECT_EQ(hat->event.as<JoystickHatEvent>().position, HatPosition::UP | HatPosition::RIGHT);
}

TEST(Sdl3GamepadTest, TheInputSystemReadsGamepadsThroughThePlatform)
{
    // A virtual gamepad presses "jump" through the platform's events.
    constexpr auto SCHEMA = make_input_schema(actions(action("jump")), context("gameplay", bind("jump"_action, GamepadButton::SOUTH)));
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    InputSystem input{SCHEMA};
    const ContextActivation gameplay = input.activate_context("gameplay"_context);
    const VirtualDevice device{SDL_JOYSTICK_TYPE_GAMEPAD, "gfx player gamepad", nullptr};
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!input.gamepad_of(PlayerIdentifier{0}).is_valid() && std::chrono::steady_clock::now() < deadline)
    {
        static_cast<void>(input.update(platform->wait_events_timeout(10ms)));
    }

    // SDL reports the press of a gamepad the platform has opened.
    device.set_button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    bool jumped = false;
    while (!jumped && std::chrono::steady_clock::now() < deadline)
    {
        jumped = input.update(platform->wait_events_timeout(10ms)).is_action_pressed("jump"_action);
    }
    EXPECT_TRUE(jumped);
    EXPECT_TRUE(input.gamepad_of(PlayerIdentifier{0}).is_valid());
}

} // namespace
