// Verifies the toolchain contract: GoogleTest (textual headers) and `import std` mixed in one
// translation unit, plus every C++23 feature the library relies on.
#include <gtest/gtest.h>

import std;
import gfx.tests.toolchain_probe;

namespace {

using namespace gfx::tests;

TEST(ToolchainTest, ClassTypeTemplateArgumentIsEvaluatedAtCompileTime) {
    static_assert(name_length<"uniform_buffer">() == 14);
    EXPECT_EQ(name_length<"camera">(), 6U);
}

TEST(ToolchainTest, IfConstevalSelectsTheEvaluationBranch) {
    static_assert(evaluation_context() == "compile-time");
    EXPECT_EQ(evaluation_context(), "run-time");
}

TEST(ToolchainTest, DeducingThisBuilderChainsInConstantExpressions) {
    constexpr Accumulator accumulator = Accumulator{}.add(1).add(2).add(3);
    static_assert(accumulator.total == 6);
    EXPECT_EQ(Accumulator{}.add(40).add(2).total, 42);
}

TEST(ToolchainTest, ExpectedCarriesValueOrError) {
    const auto parsed = parse_positive_integer("17");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, 17);

    const auto negative = parse_positive_integer("-3");
    ASSERT_FALSE(negative.has_value());
    EXPECT_EQ(negative.error(), "-3 is not strictly positive");

    EXPECT_FALSE(parse_positive_integer("vertex").has_value());
}

TEST(ToolchainTest, MdspanViewsContiguousStorage) {
    const std::array<int, 6> storage{1, 2, 3, 4, 5, 6};
    EXPECT_EQ(sum_grid(std::mdspan(storage.data(), 2, 3)), 21);
}

TEST(ToolchainTest, MoveOnlyFunctionAcceptsMoveOnlyCallables) {
    auto owned = std::make_unique<int>(7);
    EXPECT_EQ(invoke_move_only([value = std::move(owned)] { return *value * 6; }), 42);
}

TEST(ToolchainTest, SourceLocationReportsTheCaller) {
    const auto expected_line = std::source_location::current().line() + 1;
    EXPECT_EQ(caller_line(), expected_line);
}

TEST(ToolchainTest, FormatAndPrintLink) {
    EXPECT_EQ(format_and_print(stdout, 7), "value=0007");
}

TEST(ToolchainTest, DefaultedThreeWayComparisonWorksAcrossModuleBoundary) {
    static_assert(OrderedValue{1} < OrderedValue{2});
    EXPECT_EQ(OrderedValue{3}, OrderedValue{3});
    EXPECT_NE(OrderedValue{3}, OrderedValue{4});
}

TEST(ToolchainTest, StandardContainersFromHeadersAndModuleAreTheSameTypes) {
    // gtest pulled <string>/<vector> textually; `import std` provides the same entities.
    const std::vector<std::string> names{"vertex", "fragment", "compute"};
    EXPECT_EQ(std::ranges::max(names), "vertex");
}

} // namespace
