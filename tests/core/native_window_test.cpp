#include <gtest/gtest.h>

import std;
import gfx.core;

namespace
{

using namespace gfx;

TEST(NativeWindowHandleTest, HandlesKnowTheirWindowingSystem)
{
    constexpr NativeWindowHandle none;
    static_assert(none.kind() == NativeWindowKind::NONE && !none.is_valid());

    constexpr NativeWindowHandle win32{Win32WindowHandle{.instance = 0x1000, .window = 0x2000}};
    static_assert(win32.kind() == NativeWindowKind::WINDOWS && win32.is_valid());
    const NativeWindowHandle wayland{WaylandWindowHandle{.display = 1, .surface = 2}};
    const NativeWindowHandle cocoa{CocoaWindowHandle{.window = 1, .metal_layer = 2}};
    const NativeWindowHandle xcb{XcbWindowHandle{.connection = 1, .window = 2}};
    EXPECT_EQ(wayland.kind(), NativeWindowKind::WAYLAND);
    EXPECT_EQ(cocoa.kind(), NativeWindowKind::COCOA);
    EXPECT_EQ(xcb.kind(), NativeWindowKind::XCB);
}

TEST(NativeWindowHandleTest, AlternativesAreReadOnlyAsWhatTheyAre)
{
    const NativeWindowHandle window{XlibWindowHandle{.display = 7, .window = 9}};
    ASSERT_NE(window.get_if<XlibWindowHandle>(), nullptr);
    EXPECT_EQ(window.get_if<XlibWindowHandle>()->window, 9U);
    EXPECT_EQ(window.get_if<Win32WindowHandle>(), nullptr);

    // Visiting dispatches on the alternative held.
    const std::string_view visited = window.visit([]<class Alternative>(const Alternative&) -> std::string_view
    {
        return std::same_as<Alternative, XlibWindowHandle> ? "xlib" : "other";
    });
    EXPECT_EQ(visited, "xlib");
}

TEST(NativeWindowHandleTest, HandlesAreComparedByValue)
{
    const NativeWindowHandle first{Win32WindowHandle{.instance = 1, .window = 2}};
    const NativeWindowHandle same{Win32WindowHandle{.instance = 1, .window = 2}};
    const NativeWindowHandle other{Win32WindowHandle{.instance = 1, .window = 3}};
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
    EXPECT_NE(first, NativeWindowHandle{});
    static_assert(std::is_trivially_copyable_v<NativeWindowHandle>);
}

} // namespace
