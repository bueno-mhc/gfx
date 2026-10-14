// <windows.h> defines macros such as ERROR, OPAQUE, DELETE, INFINITE, min and max. Through
// <gfx/windows_headers.hpp> it must coexist with `import gfx;`, and no gfx name may be broken by it.
#include <gfx/windows_headers.hpp>

#include <gtest/gtest.h>

import std;
import gfx;

namespace {

TEST(PlatformHeadersTest, LibraryNamesSurviveWindowsMacros) 
{
    EXPECT_EQ(gfx::to_string(gfx::BlendOperation::MINIMUM), "MINIMUM");
    EXPECT_EQ(gfx::to_string(gfx::BlendOperation::MAXIMUM), "MAXIMUM");
    EXPECT_EQ(gfx::to_string(gfx::DebugMessageSeverity::CRITICAL), "CRITICAL");
    EXPECT_EQ(gfx::to_string(gfx::BorderColor::OPAQUE_BLACK_FLOAT), "OPAQUE_BLACK_FLOAT");
    EXPECT_EQ(gfx::to_string(gfx::BackendKind::NULL_BACKEND), "NULL_BACKEND");
    EXPECT_EQ(gfx::WAIT_INDEFINITELY, std::chrono::nanoseconds::max());
    EXPECT_FALSE(gfx::BufferHandle{}.is_valid());
}

TEST(PlatformHeadersTest, WindowsMacrosRemainUsable) 
{
    // gfx defines no macro of its own and leaves the platform's untouched.
    EXPECT_EQ(ERROR, 0);
    EXPECT_EQ(INFINITE, 0xFFFF'FFFFU);
}

} // namespace
