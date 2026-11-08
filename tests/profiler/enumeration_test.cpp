// The profiler's enumerations and constants: described, verified at compile time, spelled and parsed; its errors name
// the subsystem ("[gfx::profiler::EXPORT] ..."); its handles do not mix.
#include <gtest/gtest.h>

import std;
import gfx.profiler;

namespace
{

using namespace gfx::profiler;

/// Whether every enumerator of @p Enumeration is spelled by to_string() and parsed back by from_string().
template<class Enumeration>
auto
round_trips() -> bool
{
    return std::ranges::all_of(gfx::EnumerationTraits<Enumeration>::NAMES, [](const auto& entry)
    {
        return gfx::to_string(entry.value) == entry.name && gfx::from_string<Enumeration>(entry.name) == entry.value;
    });
}

TEST(ProfilerEnumerationsTest, EveryEnumeratorIsSpelledAndParsed)
{
    EXPECT_TRUE(round_trips<ProfilerSubsystem>());
    EXPECT_TRUE(round_trips<ProfilerCategory>());
    EXPECT_TRUE(round_trips<RecordKind>());
    EXPECT_TRUE(round_trips<TraceFormat>());
    EXPECT_TRUE(round_trips<SessionState>());
    EXPECT_EQ(gfx::to_string(ProfilerCategory::PHYSICS | ProfilerCategory::GPU), "PHYSICS | GPU");
    EXPECT_TRUE(has_all(ALL_PROFILER_CATEGORIES, ProfilerCategory::GENERAL | ProfilerCategory::APPLICATION));
    EXPECT_EQ(ALL_PROFILER_CATEGORIES & ~(ProfilerCategory::GENERAL | ProfilerCategory::PHYSICS | ProfilerCategory::GRAPHICS | ProfilerCategory::GPU
                                          | ProfilerCategory::UI | ProfilerCategory::PLATFORM | ProfilerCategory::APPLICATION),
              ProfilerCategory::NONE);
    EXPECT_EQ(PROFILER_TRACE_MAGIC.size(), 8U);
    EXPECT_TRUE(std::has_single_bit(DEFAULT_PROFILER_RING_CAPACITY));
    EXPECT_EQ(PROFILER_ENABLED, GFX_PROFILER_ENABLED != 0);
}

TEST(ProfilerEnumerationsTest, ErrorsNameTheirSubsystem)
{
    const ProfilerError error{{.subsystem = ProfilerSubsystem::EXPORT,
                               .operation = "export_trace",
                               .problem = "cannot open 'x' for writing",
                               .suggestion = "check the directory"}};
    const std::string_view message = error.what();
    EXPECT_TRUE(message.starts_with("[gfx::profiler::EXPORT] export_trace: cannot open 'x' for writing")) << message;
    EXPECT_NE(message.find("Suggestion: check the directory"), std::string_view::npos) << message;
    EXPECT_EQ(error.subsystem(), ProfilerSubsystem::EXPORT);
    EXPECT_EQ(error.problem(), "cannot open 'x' for writing");
    EXPECT_EQ(error.suggestion(), "check the directory");
    const gfx::Error& base = error;
    EXPECT_TRUE(base.backend().has_value());
}

TEST(ProfilerEnumerationsTest, SitesAreMadeAtCompileTime)
{
    static constexpr ProfileSite SITE{"compile time", ProfilerCategory::UI};
    static_assert(SITE.name() == "compile time");
    static_assert(SITE.category() == ProfilerCategory::UI);
    static_assert(SITE.line() != 0U);
    static_assert(SITE.name_identifier() == profile_hash("compile time"));
    static_assert(profile_hash("") == 14'695'981'039'346'656'037ULL);
    static_assert(profile_hash("a") == 0xAF63DC4C8601EC8CULL); // FNV-1a 64 of "a"
    static constexpr ProfileSite OTHER_LINE{"compile time", ProfilerCategory::UI};
    static_assert(OTHER_LINE.identifier() != SITE.identifier());
    static_assert(OTHER_LINE.name_identifier() == SITE.name_identifier());
    EXPECT_TRUE(SITE.file().ends_with("enumeration_test.cpp")) << SITE.file();
    EXPECT_FALSE((std::is_convertible_v<SessionHandle, SinkHandle>));
}

} // namespace
