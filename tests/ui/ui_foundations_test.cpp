// The foundations of the UI library: its enumerations (described, verified at compile time, spelled and
// parsed), its handle and identifier kinds (distinct, hashed at compile time, formatted) and its errors
// (the core hierarchy, with the UI subsystem as the backend).
#include <gtest/gtest.h>

import std;
import gfx.ui;

namespace
{

using namespace gfx::ui;
using namespace gfx::ui::literals;

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

TEST(UiEnumerationsTest, EveryEnumeratorIsSpelledAndParsed)
{
    EXPECT_TRUE(round_trips<UiSubsystem>());
    EXPECT_TRUE(round_trips<FontFormat>());
    EXPECT_TRUE(round_trips<FontDecoderKind>());
    EXPECT_TRUE(round_trips<SdfKind>());
    EXPECT_TRUE(round_trips<AtlasTexelFormat>());
    EXPECT_TRUE(round_trips<AtlasCompression>());
    EXPECT_TRUE(round_trips<AtlasSectionKind>());
    EXPECT_TRUE(round_trips<AtlasPackerKind>());
    EXPECT_TRUE(round_trips<TextDirection>());
    EXPECT_EQ(std::format("{}", SdfKind::MTSDF), "MTSDF");
    EXPECT_FALSE(gfx::from_string<SdfKind>("BITMAP").has_value());
}

TEST(UiEnumerationsTest, AtlasFormatValuesAreStable)
{
    // These values are stored in .uiatlas files: changing them would break every atlas built.
    static_assert(std::to_underlying(AtlasSectionKind::FACE_METRICS) == 0 && std::to_underlying(AtlasSectionKind::DESCRIPTOR) == 7);
    static_assert(std::to_underlying(SdfKind::SDF) == 0 && std::to_underlying(SdfKind::MTSDF) == 3);
    static_assert(std::to_underlying(AtlasTexelFormat::R8_UNORM) == 0 && std::to_underlying(AtlasTexelFormat::R16G16B16A16_SFLOAT) == 3);
    static_assert(std::to_underlying(AtlasCompression::ZSTD) == 1);
    static_assert(std::string_view{UI_ATLAS_MAGIC.data(), UI_ATLAS_MAGIC.size()} == "GFXUIATL");
    static_assert(UI_ATLAS_HEADER_SIZE % UI_ATLAS_ALIGNMENT == 0);
    SUCCEED();
}

TEST(UiHandlesTest, HandleKindsAreDistinct)
{
    static_assert(UiHandle<FontHandle> && UiHandle<FontAtlasHandle> && UiHandle<ThemeHandle> && UiHandle<DockNodeHandle>);
    static_assert(FontResourceHandle<FontHandle> && FontResourceHandle<FontAtlasHandle> && !FontResourceHandle<ThemeHandle>);
    static_assert(!std::is_convertible_v<FontHandle, FontAtlasHandle>);
    static_assert(!UiHandle<int>);
    EXPECT_EQ(std::format("{}", FontAtlasHandle{}), "FontAtlas(null)");
    EXPECT_FALSE(DockNodeHandle{}.is_valid());
}

TEST(UiHandlesTest, IdentifiersAreNamedAtCompileTime)
{
    constexpr WindowIdentifier inspector = "Inspector"_window;
    static_assert(inspector == WindowIdentifier{"Inspector"});
    static_assert(inspector.value() == gfx::name_hash("Inspector"));
    static_assert("editor"_dock_space == DockSpaceIdentifier{"editor"});
    static_assert("confirm"_popup.value() == "confirm"_table.value(), "same name, same hash: the tag keeps the kinds apart");
    static_assert(UiIdentifier<WindowIdentifier> && UiIdentifier<PopupIdentifier> && UiIdentifier<WidgetIdentifier>);
    static_assert(!std::is_convertible_v<PopupIdentifier, TableIdentifier>);

    // Names known at run time hash the same way.
    const std::string name = "Insp" + std::string{"ector"};
    EXPECT_EQ(WindowIdentifier::from_name(name), inspector);
    EXPECT_EQ(std::format("{}", "a"_menu), "Menu(0xE40C292C)");
}

TEST(UiHandlesTest, GlyphsAreIndicesNotCodePoints)
{
    constexpr GlyphIndex notdef;
    constexpr GlyphIndex glyph{36};
    static_assert(notdef.is_notdef() && !glyph.is_notdef() && notdef < glyph);
    static_assert(!std::is_convertible_v<std::uint32_t, GlyphIndex>);
    EXPECT_EQ(std::format("{}", glyph), "Glyph(36)");
}

TEST(UiErrorsTest, CapabilityErrorsNameTheirSubsystem)
{
    // Thrown as the core error, with the subsystem as the backend.
    try
    {
        throw_unsupported(UiSubsystem::FONT, "FontSource::open", "WOFF2 decoding", "the font decoder was built without Brotli",
                          "use a TrueType or OpenType file");
        FAIL() << "throw_unsupported must throw";
    }
    catch (const UiCapabilityError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::ui::FONT] FontSource::open")) << message;
        EXPECT_NE(message.find("WOFF2 decoding"), std::string_view::npos) << message;
        EXPECT_NE(message.find("use a TrueType or OpenType file"), std::string_view::npos) << message;
        ASSERT_TRUE(error.backend().has_value());
        EXPECT_TRUE(*error.backend() == UiSubsystem::FONT);
        EXPECT_FALSE(*error.backend() == UiSubsystem::GRAPHICS);
        EXPECT_EQ(error.capability(), "WOFF2 decoding");
    }

    // One catch for every library of gfx, and for std::runtime_error.
    EXPECT_THROW(throw_unsupported(UiSubsystem::PLATFORM, "UiPlatformServices::clipboard_text", "clipboard", "no platform", "attach one"),
                 gfx::BackendCapabilityError);
    EXPECT_THROW(throw_unsupported(UiSubsystem::INPUT, "a", "b", "c", "d"), std::runtime_error);
}

} // namespace
