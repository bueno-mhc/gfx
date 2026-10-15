#include <gtest/gtest.h>

import std;
import gfx.core;

// Two libraries' backend enumerations, as the graphics and platform libraries declare theirs.
namespace first
{
enum class Backend : std::uint8_t
{
    ALPHA,
    BETA,
};
} // namespace first

namespace second
{
enum class Backend : std::uint8_t
{
    ALPHA,
};
} // namespace second

template<>
struct gfx::EnumerationTraits<first::Backend>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "first";
    static constexpr std::array NAMES{
        EnumeratorName{first::Backend::ALPHA, "ALPHA"},
        EnumeratorName{first::Backend::BETA, "BETA"},
    };
};

template<>
struct gfx::EnumerationTraits<second::Backend>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "second";
    static constexpr std::array NAMES{
        EnumeratorName{second::Backend::ALPHA, "ALPHA"},
    };
};

namespace
{

using namespace gfx;

TEST(BackendIdentityTest, IdentitiesCompareWithTheEnumeratorTheyCameFrom)
{
    static_assert(BackendEnumeration<first::Backend>);
    static_assert(!BackendEnumeration<NativeWindowKind>, "only enumerations with a BACKEND_LIBRARY name backends");

    constexpr BackendIdentity beta = first::Backend::BETA;
    static_assert(beta == first::Backend::BETA);
    static_assert(!(beta == first::Backend::ALPHA));
    static_assert(!(BackendIdentity{first::Backend::ALPHA} == second::Backend::ALPHA), "same name, other library");
    EXPECT_EQ(beta.library(), "first");
    EXPECT_EQ(beta.name(), "BETA");
    EXPECT_EQ(std::format("{}", beta), "first::BETA");
}

TEST(BackendIdentityTest, ErrorsNameTheLibraryAndTheBackend)
{
    try
    {
        throw BackendCapabilityError({.backend = second::Backend::ALPHA,
                                      .operation = "Thing::do",
                                      .capability = "doing",
                                      .reason = "it cannot",
                                      .suggestion = "do something else"});
    }
    catch (const Error& error)
    {
        EXPECT_EQ(error.backend(), second::Backend::ALPHA);
        EXPECT_EQ(error.operation(), "Thing::do");
        EXPECT_TRUE(std::string_view{error.what()}.starts_with("[second::ALPHA] Thing::do: capability 'doing' is not supported."))
            << error.what();
    }
}

TEST(BackendIdentityTest, ErrorsWithoutABackendNameTheLibraries)
{
    const InvalidUsageError error({.backend = std::nullopt, .operation = "Thing::use", .problem = "wrong", .suggestion = ""});
    EXPECT_FALSE(error.backend().has_value());
    EXPECT_TRUE(std::string_view{error.what()}.starts_with("[gfx] Thing::use: wrong")) << error.what();
}

} // namespace
