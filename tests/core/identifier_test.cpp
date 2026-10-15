#include <gtest/gtest.h>

import std;
import gfx.core;

namespace
{

using namespace gfx;

struct ActionTag
{
    static constexpr std::string_view NAME = "Action";
};

struct ContextTag
{
    static constexpr std::string_view NAME = "Context";
};

using ActionIdentifier = Identifier<ActionTag>;
using ContextIdentifier = Identifier<ContextTag>;

TEST(IdentifierTest, NamesHashAtCompileTime)
{
    // FNV-1a test vectors (32 bits).
    static_assert(name_hash("a") == 0xE40C292CU);
    static_assert(name_hash("foobar") == 0xBF9CF968U);
    static_assert(name_hash("") == 0U, "the empty name is the null identifier");

    constexpr ActionIdentifier jump{"jump"};
    static_assert(jump.value() == name_hash("jump"));
    static_assert(jump.is_valid());
    EXPECT_EQ(jump, ActionIdentifier::from_name("jump")) << "names known only at run time give the same identifier";
}

TEST(IdentifierTest, IdentifiersCompareByName)
{
    constexpr ActionIdentifier jump{"jump"};
    constexpr ActionIdentifier fire{"fire"};
    EXPECT_NE(jump, fire);
    EXPECT_EQ(jump, ActionIdentifier{"jump"});
    EXPECT_FALSE(ActionIdentifier{}.is_valid());
    static_assert(TaggedIdentifier<ActionIdentifier>);
    static_assert(TaggedIdentifier<ContextIdentifier>);
    static_assert(!std::is_convertible_v<ActionIdentifier, ContextIdentifier>, "identifiers of different kinds do not mix");
    static_assert(std::is_trivially_copyable_v<ActionIdentifier> && sizeof(ActionIdentifier) == 4);
}

TEST(IdentifierTest, IdentifiersKeyUnorderedContainersAndFormat)
{
    std::unordered_set<ActionIdentifier> actions{ActionIdentifier{"jump"}, ActionIdentifier{"fire"}, ActionIdentifier{"jump"}};
    EXPECT_EQ(actions.size(), 2U);
    EXPECT_EQ(std::format("{}", ActionIdentifier{"a"}), "Action(0xE40C292C)");
    EXPECT_EQ(std::format("{}", ActionIdentifier{}), "Action(null)");
}

} // namespace
