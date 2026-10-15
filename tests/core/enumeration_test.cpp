#include <gtest/gtest.h>

import std;
import gfx.core;

namespace
{

using namespace gfx;

TEST(EnumerationTest, SequentialEnumeratorsAreParsedFromTheirSpelling)
{
    // Every enumerator of every core enumeration round-trips through its spelling.
    for (const auto& entry : EnumerationTraits<NativeWindowKind>::NAMES)
    {
        EXPECT_EQ(from_string<NativeWindowKind>(to_string(entry.value)), entry.value) << entry.name;
    }
    for (const auto& entry : EnumerationTraits<ValidationLevel>::NAMES)
    {
        EXPECT_EQ(from_string<ValidationLevel>(to_string(entry.value)), entry.value) << entry.name;
    }
    EXPECT_EQ(from_string<MemoryDomain>("DEVICE"), MemoryDomain::DEVICE);
}

TEST(EnumerationTest, UnknownSpellingsAreNotParsed)
{
    EXPECT_EQ(from_string<NativeWindowKind>("WINDOWS "), std::nullopt) << "spellings are exact";
    EXPECT_EQ(from_string<NativeWindowKind>("windows"), std::nullopt) << "spellings are case sensitive";
    EXPECT_EQ(from_string<NativeWindowKind>(""), std::nullopt);
}

TEST(EnumerationTest, ParsingWorksAtCompileTime)
{
    static_assert(from_string<ValidationLevel>("COMMANDS") == ValidationLevel::COMMANDS);
    static_assert(!from_string<ValidationLevel>("EVERYTHING").has_value());
}

TEST(EnumerationTest, SharedConstantsDescribeTheBuild)
{
    EXPECT_EQ(DEFAULT_VALIDATION_LEVEL, DEBUG_BUILD ? ValidationLevel::COMMANDS : ValidationLevel::CREATION);
    EXPECT_EQ(WAIT_INDEFINITELY, std::chrono::nanoseconds::max());
    EXPECT_EQ(std::format("{}", NativeWindowKind::WEB_CANVAS), "WEB_CANVAS");
}

} // namespace
