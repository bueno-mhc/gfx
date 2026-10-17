/// @file atlas_test_support.cppm
/// @brief Helpers of the atlas tests: the fetched test fonts, font collections assembled from them,
///        decompressed sections, HarfBuzz shaping with an atlas's shaping font, and a small atlas
///        description.
module;

#include <hb.h>
#include <zstd.h>

export module gfx.tests.ui_atlas_support;

import std;
export import gfx.ui.atlas_tools;

export namespace gfx::tests
{

/// The test font @p name (in <build>/ui-test-fonts).
[[nodiscard]] inline auto
font_path(std::string_view name) -> std::filesystem::path
{
    return std::filesystem::path{GFX_UI_TEST_FONT_DIRECTORY} / name;
}

/// The test font @p name as a source.
[[nodiscard]] inline auto
load_font(std::string_view name) -> ui::FontSource
{
    return ui::FontSource::from_file(font_path(name));
}

/// A TrueType collection ('ttcf') of @p fonts (each an sfnt with one face): the faces' table directories
/// followed by every table, offsets rewritten.
[[nodiscard]] inline auto
make_collection(std::span<const ui::FontSource> fonts) -> ui::FontSource
{
    const auto read = [](std::span<const std::byte> bytes, std::size_t offset, std::size_t size)
    {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < size; ++index)
        {
            value = (value << 8) | std::to_integer<std::uint32_t>(bytes[offset + index]);
        }
        return value;
    };
    const auto write = [](std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            bytes[offset + index] = static_cast<std::byte>((value >> (8 * (size - 1 - index))) & 0xFFU);
        }
    };

    // Header, then one table directory per face, then the tables (4-byte aligned).
    std::vector<std::byte> collection(12 + 4 * fonts.size());
    write(collection, 0, 0x74746366U, 4);
    write(collection, 4, 0x00010000U, 4);
    write(collection, 8, static_cast<std::uint32_t>(fonts.size()), 4);
    std::vector<std::size_t> directories;
    for (std::size_t font = 0; font < fonts.size(); ++font)
    {
        const std::span<const std::byte> bytes = fonts[font].bytes();
        const std::size_t table_count = read(bytes, 4, 2);
        directories.push_back(collection.size());
        write(collection, 12 + 4 * font, static_cast<std::uint32_t>(collection.size()), 4);
        collection.insert(collection.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(12 + 16 * table_count));
    }
    for (std::size_t font = 0; font < fonts.size(); ++font)
    {
        const std::span<const std::byte> bytes = fonts[font].bytes();
        const std::size_t table_count = read(bytes, 4, 2);
        for (std::size_t table = 0; table < table_count; ++table)
        {
            const std::size_t record = 12 + 16 * table;
            const std::size_t offset = read(bytes, record + 8, 4);
            const std::size_t length = read(bytes, record + 12, 4);
            collection.resize((collection.size() + 3) / 4 * 4);
            write(collection, directories[font] + record + 8, static_cast<std::uint32_t>(collection.size()), 4);
            collection.insert(collection.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                              bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        }
    }
    return ui::FontSource::from_memory(std::move(collection), "collection.ttc");
}

/// The bytes of @p section of @p atlas, decompressed.
[[nodiscard]] inline auto
decompress(const ui::AtlasView& atlas,
           const ui::AtlasSection& section) -> std::vector<std::byte>
{
    const std::span<const std::byte> stored = atlas.section_bytes(section);
    if (section.compression == ui::AtlasCompression::NONE)
    {
        return {stored.begin(), stored.end()};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(section.uncompressed_size));
    const std::size_t size = ZSTD_decompress(bytes.data(), bytes.size(), stored.data(), stored.size());
    if (ZSTD_isError(size) != 0 || size != bytes.size())
    {
        throw std::runtime_error("the section does not decompress");
    }
    return bytes;
}

/// The glyphs HarfBuzz shapes the UTF-8 @p text into with the OpenType font @p font (direction, script
/// and language guessed from the text).
[[nodiscard]] inline auto
shape_glyphs(std::span<const std::byte> font,
             std::string_view text) -> std::vector<std::uint32_t>
{
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(font.data()), static_cast<unsigned int>(font.size()), HB_MEMORY_MODE_READONLY,
                                     nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_font_t* shaping = hb_font_create(face);
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(shaping, buffer, nullptr, 0);
    unsigned int count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    std::vector<std::uint32_t> glyphs;
    for (unsigned int index = 0; index < count; ++index)
    {
        glyphs.push_back(infos[index].codepoint);
    }
    hb_buffer_destroy(buffer);
    hb_font_destroy(shaping);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return glyphs;
}

/// Printable ASCII as an MTSDF atlas of 32 texels per em (the tests' usual atlas).
[[nodiscard]] inline auto
latin_description() -> ui::AtlasBuildDescription
{
    ui::AtlasBuildDescription description{.characters = ui::CharacterSelection::parse("BASIC_LATIN")};
    description.field.em_size = 32;
    description.field.distance_range = 4;
    return description;
}

/// The view of @p atlas, which must be valid (and outlive the view).
[[nodiscard]] inline auto
view_of(std::span<const std::byte> atlas) -> ui::AtlasView
{
    const std::expected<ui::AtlasView, ui::AtlasReadError> view = ui::AtlasView::parse(atlas);
    if (!view.has_value())
    {
        throw std::runtime_error(ui::describe_atlas_error(view.error()));
    }
    return *view;
}

/// Refused: the view would outlive the temporary atlas.
auto view_of(std::vector<std::byte>&& atlas) -> ui::AtlasView = delete;

} // namespace gfx::tests
