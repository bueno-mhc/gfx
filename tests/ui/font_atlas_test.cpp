// Loaded atlases (embedded, owned, from a file, refused when damaged), the font library (handles,
// validation, removal) and the coverage checks of embedded atlases at compile time.
#include <gtest/gtest.h>

import std;
import gfx.tests.text_support;

namespace
{

using namespace gfx::ui;
using gfx::tests::latin_atlas;
using gfx::tests::latin_basic_atlas;

// Labels checked against an embedded atlas when this file compiles.
static_assert(atlas_covers(latin_atlas(), u8"Ångström — “quoted” …"), "Latin-1 and the punctuation the atlas was built with");
static_assert(atlas_covers(latin_atlas(), "line\nbreaks and\ttabs need no glyph"));
static_assert(!atlas_covers(latin_atlas(), u8"→"), "no arrow was requested");
static_assert(first_missing_character(latin_atlas(), std::u8string_view{u8"abc→"}) == U'→');
static_assert(first_missing_character(latin_basic_atlas(), std::u8string_view{u8"é"}) == U'é');
static_assert(!first_missing_character(latin_basic_atlas(), std::u8string_view{u8"plain ASCII"}).has_value());
static_assert(first_missing_character(latin_atlas(), std::string_view{"\xFF"}) == REPLACEMENT_CHARACTER, "invalid UTF-8");
static_assert(!atlas_covers(std::span<const std::byte>{}, u8"a"), "not an atlas");

TEST(FontAtlasTest, EmbeddedAtlasesLoadInPlace)
{
    const std::shared_ptr<const FontAtlas> atlas = FontAtlas::load(latin_atlas());
    EXPECT_EQ(atlas->view().blob().data(), latin_atlas().data());
    EXPECT_EQ(atlas->name(), "Noto Sans Regular");
    ASSERT_EQ(atlas->page_count(), atlas->descriptor().page_count);
    const FontAtlasPage page = atlas->page(0);
    EXPECT_EQ(page.format, AtlasTexelFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(page.texels.size(), std::size_t{page.width} * page.height * 4);
    EXPECT_FALSE(atlas->shaping_font().empty());
    EXPECT_TRUE(atlas->find_character(U'é').has_value());
    EXPECT_FALSE(atlas->find_character(U'→').has_value());
    EXPECT_EQ(atlas->face_metrics().units_per_em, 1000U);

    // Without a shaping font.
    EXPECT_TRUE(FontAtlas::load(latin_basic_atlas())->shaping_font().empty());
}

TEST(FontAtlasTest, OwnedAndFileAtlasesLoad)
{
    const std::shared_ptr<const FontAtlas> file = FontAtlas::load_file(GFX_TEST_TEXT_LATIN_ATLAS);
    EXPECT_TRUE(std::ranges::equal(file->view().blob(), latin_atlas()));
    std::vector<std::byte> copy(latin_atlas().begin(), latin_atlas().end());
    const std::shared_ptr<const FontAtlas> owned = FontAtlas::load(std::move(copy));
    EXPECT_EQ(owned->name(), "Noto Sans Regular");
    EXPECT_TRUE(std::ranges::equal(owned->page(0).texels, file->page(0).texels));
}

TEST(FontAtlasTest, DamagedAtlasesAreRefused)
{
    // A changed byte: the checksum tells, and the message says what is wrong.
    std::vector<std::byte> damaged(latin_atlas().begin(), latin_atlas().end());
    damaged[damaged.size() / 2] ^= std::byte{0x40};
    try
    {
        static_cast<void>(FontAtlas::load(std::move(damaged)));
        FAIL() << "a damaged atlas must be refused";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("checksum"), std::string_view::npos) << error.what();
    }

    // Another major version: a capability error.
    std::vector<std::byte> newer(latin_atlas().begin(), latin_atlas().end());
    atlas_layout::store(std::span{newer}, atlas_layout::Header::VERSION_MAJOR, std::uint16_t{2});
    EXPECT_THROW(static_cast<void>(FontAtlas::load(std::move(newer))), gfx::BackendCapabilityError);
    EXPECT_THROW(static_cast<void>(FontAtlas::load_file("missing.uiatlas")), gfx::InvalidUsageError);
}

TEST(FontLibraryTest, FontsNameTheirAtlases)
{
    FontLibrary library;
    const FontAtlasHandle latin = library.add_atlas(FontAtlas::load(latin_atlas()));
    const FontHandle font = library.add_font({.atlas = latin, .size = 20, .language = "en"});
    ASSERT_NE(library.font(font), nullptr);
    EXPECT_EQ(library.font(font)->size, 20.0F);
    EXPECT_EQ(library.font(font)->language, "en");
    EXPECT_EQ(library.atlas(latin)->name(), "Noto Sans Regular");
    EXPECT_EQ(library.atlas_count(), 1U);
    EXPECT_EQ(library.font_count(), 1U);

    // Atlases a font uses stay; unknown handles name nothing.
    EXPECT_THROW(library.remove_atlas(latin), gfx::InvalidUsageError);
    library.remove_font(font);
    EXPECT_EQ(library.font(font), nullptr);
    library.remove_atlas(latin);
    EXPECT_EQ(library.atlas(latin), nullptr);
    EXPECT_EQ(library.font(FontHandle{}), nullptr);
}

TEST(FontLibraryTest, InvalidFontsAreRefused)
{
    FontLibrary library;
    const FontAtlasHandle latin = library.add_atlas(FontAtlas::load(latin_atlas()));
    EXPECT_THROW(static_cast<void>(library.add_atlas(nullptr)), gfx::InvalidUsageError);
    EXPECT_THROW(static_cast<void>(library.add_font({.atlas = FontAtlasHandle{}})), gfx::InvalidUsageError);
    EXPECT_THROW(static_cast<void>(library.add_font({.atlas = latin, .fallbacks = {FontAtlasHandle{}}})), gfx::InvalidUsageError);
    EXPECT_THROW(static_cast<void>(library.add_font({.atlas = latin, .size = 0})), gfx::InvalidUsageError);
}

} // namespace
