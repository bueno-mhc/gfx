#include <gtest/gtest.h>

import std;
import gfx.platform;

namespace
{

using namespace gfx::platform;

TEST(PlatformEnumsTest, KeysAreUsbHidUsages)
{
    // Physical keys keep the values of the USB HID keyboard page (SDL scancodes): stable when saved.
    static_assert(std::to_underlying(Key::A) == 4);
    static_assert(std::to_underlying(Key::NUMBER_1) == 30 && std::to_underlying(Key::NUMBER_0) == 39);
    static_assert(std::to_underlying(Key::SPACE) == 44);
    static_assert(std::to_underlying(Key::DELETE_FORWARD) == 76);
    static_assert(std::to_underlying(Key::RIGHT_SUPER) == 231);
    EXPECT_EQ(to_string(Key::W), "W");
    EXPECT_EQ(to_string(Key::KEYPAD_ENTER), "KEYPAD_ENTER");
    EXPECT_EQ(to_string(static_cast<Key>(200)), "<invalid>") << "a value without an enumerator";
    EXPECT_EQ(std::format("{}", Key::LEFT_SHIFT), "LEFT_SHIFT");
}

TEST(PlatformEnumsTest, KeyCodesAreCodePointsOrFlaggedUsages)
{
    // Printable keys are their (lowercase) code point; others carry the flag above their HID usage.
    static_assert(std::to_underlying(KeyCode::A) == U'a');
    static_assert(std::to_underlying(KeyCode::SPACE) == U' ');
    static_assert(std::to_underlying(KeyCode::F1) == (KEY_CODE_NON_PRINTABLE_FLAG | std::to_underlying(Key::F1)));
    EXPECT_EQ(to_string(KeyCode::QUESTION_MARK), "QUESTION_MARK");
    EXPECT_EQ(to_string(KeyCode{U'é'}), "<invalid>") << "any code point is a key code, but only ASCII ones are named";
}

TEST(PlatformEnumsTest, EveryKeyAndKeyCodeRoundTripsThroughItsName)
{
    // Saved bindings store names: every enumerator must come back from its spelling.
    for (const auto& entry : gfx::EnumerationTraits<Key>::NAMES)
    {
        EXPECT_EQ(gfx::from_string<Key>(entry.name), entry.value) << entry.name;
    }
    for (const auto& entry : gfx::EnumerationTraits<KeyCode>::NAMES)
    {
        EXPECT_EQ(gfx::from_string<KeyCode>(entry.name), entry.value) << entry.name;
    }
    for (const auto& entry : gfx::EnumerationTraits<GamepadButton>::NAMES)
    {
        EXPECT_EQ(gfx::from_string<GamepadButton>(entry.name), entry.value) << entry.name;
    }
    EXPECT_EQ(gfx::from_string<Key>("ESC"), std::nullopt);
}

TEST(PlatformEnumsTest, BitmasksCombineOutsideTheLibraryNamespace)
{
    // The bitmask operators are found by argument-dependent lookup for platform enumerations too.
    constexpr KeyModifier held = KeyModifier::LEFT_SHIFT | KeyModifier::RIGHT_CONTROL;
    static_assert(has_any(held, KeyModifier::SHIFT));
    static_assert(has_any(held, KeyModifier::CONTROL));
    static_assert(!has_any(held, KeyModifier::ALT));
    EXPECT_EQ(to_string(held), "LEFT_SHIFT | RIGHT_CONTROL");
    EXPECT_EQ(to_string(KeyModifier::CONTROL), "LEFT_CONTROL | RIGHT_CONTROL");
    EXPECT_EQ(gfx::from_string<WindowFlag>("RESIZABLE | HIDDEN"), WindowFlag::RESIZABLE | WindowFlag::HIDDEN);
}

TEST(PlatformEnumsTest, PlatformBackendsAreNamedInErrors)
{
    static_assert(gfx::BackendEnumeration<PlatformBackendKind>);
    const gfx::BackendIdentity sdl = PlatformBackendKind::SDL3;
    EXPECT_EQ(std::format("{}", sdl), "gfx::platform::SDL3");
    EXPECT_FALSE(sdl == PlatformBackendKind::GLFW);
}

} // namespace
