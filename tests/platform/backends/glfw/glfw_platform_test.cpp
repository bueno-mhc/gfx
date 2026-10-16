// What is particular to the GLFW backend (the contract shared with the other backends is tested in
// ../platform_backend_test.cpp): its feature set and the errors that name what GLFW lacks.
#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.backend_support;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;

/// A hidden window description of 320 x 200.
auto
hidden_window() -> WindowDescription
{
    WindowDescription description;
    description.title = "gfx GLFW test";
    description.size = gfx::Extent{.width = 320, .height = 200, .depth = 1};
    description.flags = WindowFlag::HIDDEN;
    return description;
}

/// The message of the BackendCapabilityError @p function throws (empty when it throws none).
template<class Function>
auto
capability_error_of(Function function) -> std::string
{
    try
    {
        function();
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        return error.what();
    }
    return {};
}

/// A platform per test (GLFW allows one at a time).
class GlfwPlatformTest : public ::testing::Test
{
protected:
    std::unique_ptr<Platform> m_platform = create_platform(PlatformBackendKind::GLFW);
};

TEST_F(GlfwPlatformTest, FeaturesAreTheDocumentedOnes)
{
    // On Win32: windows (icons, positions, hover, fullscreen, density), custom and captured cursors.
    const std::array supported{PlatformFeature::WINDOW_ICON,      PlatformFeature::WINDOW_POSITION, PlatformFeature::EXCLUSIVE_FULLSCREEN,
                               PlatformFeature::WINDOW_HOVER,     PlatformFeature::HIGH_PIXEL_DENSITY,
                               PlatformFeature::CUSTOM_CURSORS,   PlatformFeature::CAPTURED_CURSOR};
    for (const auto& entry : gfx::EnumerationTraits<PlatformFeature>::NAMES)
    {
        if (entry.value == PlatformFeature::RAW_MOUSE_MOTION)
        {
            continue; // depends on the system
        }
        EXPECT_EQ(m_platform->supports(entry.value), std::ranges::contains(supported, entry.value)) << entry.name;
    }
    EXPECT_FALSE(m_platform->supports(CursorShape::WAIT));
    EXPECT_FALSE(m_platform->supports(CursorShape::PROGRESS));
    EXPECT_TRUE(m_platform->supports(CursorShape::NOT_ALLOWED));
    EXPECT_EQ(m_platform->supported_window_flags(), ~WindowFlag::UTILITY);
}

TEST_F(GlfwPlatformTest, ErrorsNameWhatGlfwLacks)
{
    // Every missing capability says what GLFW lacks and what to do instead.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    InputDevices& devices = m_platform->input_devices();
    const std::string wait = capability_error_of([&] { (void)m_platform->create_cursor(CursorShape::WAIT); });
    EXPECT_NE(wait.find("[gfx::platform::GLFW] Platform::create_cursor"), std::string::npos) << wait;
    EXPECT_NE(wait.find("no wait or progress cursor"), std::string::npos) << wait;
    EXPECT_NE(capability_error_of([&] { (void)window->is_occluded(); }).find("does not report whether a window is covered"), std::string::npos);
    EXPECT_NE(capability_error_of([&] { (void)m_platform->clipboard_image(); }).find("text only"), std::string::npos);
    EXPECT_NE(capability_error_of([&] { devices.set_gamepad_rumble(GamepadHandle{}, 1.0F, 1.0F, 10ms); }).find("no force feedback"),
              std::string::npos);
    EXPECT_NE(capability_error_of([&] { (void)devices.touch_devices(); }).find("touch input"), std::string::npos);
    EXPECT_NE(capability_error_of([&] { window->start_text_input({.area = {}, .cursor = 0, .composition_events = true}); })
                  .find("IME compositions"),
              std::string::npos);
    WindowDescription utility = hidden_window();
    utility.flags |= WindowFlag::UTILITY;
    EXPECT_NE(capability_error_of([&] { (void)m_platform->create_window(utility); }).find("UTILITY"), std::string::npos);

    // No gamepad is connected here.
    EXPECT_TRUE(devices.gamepads().empty());
}

TEST_F(GlfwPlatformTest, TheClipboardHoldsText)
{
    // The real clipboard: only when asked (GFX_TEST_CLIPBOARD=1). GLFW cannot tell whether the
    // clipboard holds more than text: only its text is put back.
    const char* const enabled = std::getenv("GFX_TEST_CLIPBOARD");
    if (enabled == nullptr || std::string_view{enabled} != "1")
    {
        GTEST_SKIP() << "uses the system clipboard: set GFX_TEST_CLIPBOARD=1 to run it";
    }
    const std::string saved = m_platform->clipboard_text();
    m_platform->set_clipboard_text("gfx clipboard \xE2\x9C\x93");
    EXPECT_EQ(m_platform->clipboard_text(), "gfx clipboard \xE2\x9C\x93");
    m_platform->set_clipboard_text(saved);
}

} // namespace
