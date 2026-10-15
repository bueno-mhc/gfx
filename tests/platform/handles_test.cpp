#include <gtest/gtest.h>

import std;
import gfx.platform;

namespace
{

using namespace gfx::platform;
using namespace gfx::platform::literals;

TEST(PlatformHandlesTest, ActionsAndContextsAreNamedAtCompileTime)
{
    constexpr ActionIdentifier jump = "jump"_action;
    static_assert(jump == ActionIdentifier{"jump"});
    static_assert(jump.value() == gfx::name_hash("jump"));
    static_assert("gameplay"_context == BindingContextIdentifier{"gameplay"});
    static_assert(InputIdentifier<ActionIdentifier> && InputIdentifier<BindingContextIdentifier>);
    static_assert(!std::is_convertible_v<ActionIdentifier, BindingContextIdentifier>);
    EXPECT_EQ(std::format("{}", "a"_action), "Action(0xE40C292C)");
}

TEST(PlatformHandlesTest, PlatformHandlesAreDistinctKinds)
{
    static_assert(PlatformHandle<WindowHandle> && PlatformHandle<MonitorHandle> && PlatformHandle<CursorHandle>);
    static_assert(InputDeviceHandle<GamepadHandle> && InputDeviceHandle<TouchDeviceHandle>);
    static_assert(!InputDeviceHandle<WindowHandle>);
    static_assert(!std::is_convertible_v<WindowHandle, CursorHandle>);
    EXPECT_EQ(std::format("{}", WindowHandle{}), "Window(null)");
}

TEST(PlatformHandlesTest, PlayersAreSlots)
{
    constexpr PlayerIdentifier first;
    constexpr PlayerIdentifier second{1};
    static_assert(first.index() == 0 && second.index() == 1 && first < second);
    EXPECT_EQ(std::format("{}", second), "Player(1)");
}

} // namespace
