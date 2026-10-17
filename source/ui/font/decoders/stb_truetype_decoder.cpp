/// @file stb_truetype_decoder.cpp
/// @brief The stb_truetype decoder: TrueType, OpenType CFF and collections (no WOFF, WOFF2 or CFF2).
///        stb_truetype reads the default instance of a variable font; the atlas builder reaches other
///        instances through the static subset hb-subset makes, which this decoder then reads. Tables
///        stb_truetype does not expose (OS/2, post, fvar, COLR) are read here.
module;

#include <stb_truetype.h>

module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import :implementations;

namespace gfx::ui
{

namespace
{

/// Reads the big-endian tables of an sfnt font.
class TableReader
{
public:
    TableReader(std::span<const std::byte> font,
                std::size_t font_start)
        : m_font{font}
        , m_start{font_start}
    {
    }

    /// Unsigned big-endian value of @p size bytes at @p offset of the file (0 beyond its end).
    [[nodiscard]] auto read(std::size_t offset,
                            std::size_t size) const noexcept -> std::uint32_t
    {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < size; ++index)
        {
            const std::size_t at = offset + index;
            value = (value << 8) | (at < m_font.size() ? std::to_integer<std::uint32_t>(m_font[at]) : 0U);
        }
        return value;
    }

    /// Signed 16-bit value at @p offset.
    [[nodiscard]] auto read_signed(std::size_t offset) const noexcept -> std::int32_t
    {
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(read(offset, 2)));
    }

    /// Offset of table @p tag in the file, or nullopt.
    [[nodiscard]] auto find(std::string_view tag) const noexcept -> std::optional<std::size_t>
    {
        const std::size_t count = read(m_start + 4, 2);
        for (std::size_t table = 0; table < count; ++table)
        {
            const std::size_t record = m_start + 12 + table * 16;
            if (read(record, 4) == font_tag(tag))
            {
                return read(record + 8, 4);
            }
        }
        return std::nullopt;
    }

private:
    std::span<const std::byte> m_font{};
    std::size_t m_start = 0;
};

/// UTF-8 of UTF-16BE text.
auto
utf8_of_utf16_big_endian(const char* bytes,
                         int size) -> std::string
{
    std::string text;
    const auto unit = [&](int index) { return static_cast<char32_t>((static_cast<unsigned char>(bytes[index]) << 8) | static_cast<unsigned char>(bytes[index + 1])); };
    for (int index = 0; index + 1 < size; index += 2)
    {
        char32_t code_point = unit(index);
        if (code_point >= 0xD800 && code_point < 0xDC00 && index + 3 < size && unit(index + 2) >= 0xDC00 && unit(index + 2) < 0xE000)
        {
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (unit(index + 2) - 0xDC00);
            index += 2;
        }
        else if (code_point >= 0xD800 && code_point < 0xE000)
        {
            code_point = 0xFFFD;
        }
        // Encode it.
        if (code_point < 0x80)
        {
            text += static_cast<char>(code_point);
        }
        else if (code_point < 0x800)
        {
            text += static_cast<char>(0xC0 | (code_point >> 6));
            text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
        else if (code_point < 0x10000)
        {
            text += static_cast<char>(0xE0 | (code_point >> 12));
            text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
        else
        {
            text += static_cast<char>(0xF0 | (code_point >> 18));
            text += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
            text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
    }
    return text;
}

/// A face opened by stb_truetype.
class StbTruetypeFace final : public FontFace
{
public:
    StbTruetypeFace(FontSource source,
                    std::uint32_t face_index,
                    int font_start,
                    FontFormat format)
        : m_source{std::move(source)}
        , m_face_index{face_index}
        , m_format{format}
        , m_tables{m_source.bytes(), static_cast<std::size_t>(font_start)}
    {
        const auto* data = reinterpret_cast<const unsigned char*>(m_source.bytes().data());
        if (stbtt_InitFont(&m_font, data, font_start) == 0)
        {
            throw_font_usage_error("FontDecoder::open", std::format("stb_truetype cannot read face {} of '{}'", face_index, m_source.name()),
                                   "check that the file is a complete, valid font, or decode it with FontDecoderKind::FREETYPE");
        }
    }

    [[nodiscard]] auto decoder() const noexcept -> FontDecoderKind override
    {
        return FontDecoderKind::STB_TRUETYPE;
    }

    [[nodiscard]] auto format() const noexcept -> FontFormat override
    {
        return m_format;
    }

    [[nodiscard]] auto metrics() const -> FontFaceMetrics override
    {
        // hhea, then OS/2 (typographic metrics when USE_TYPO_METRICS is set, x-height, cap height), then post.
        int ascender = 0;
        int descender = 0;
        int line_gap = 0;
        stbtt_GetFontVMetrics(&m_font, &ascender, &descender, &line_gap);
        FontFaceMetrics metrics{.units_per_em = m_tables.read(static_cast<std::size_t>(m_font.head) + 18, 2),
                                .ascender = ascender,
                                .descender = descender,
                                .line_gap = line_gap,
                                .family = name(16, 1),
                                .style = name(17, 2)};
        if (const std::optional<std::size_t> os2 = m_tables.find("OS/2"); os2.has_value())
        {
            const std::uint32_t version = m_tables.read(*os2, 2);
            if (version >= 2)
            {
                metrics.x_height = m_tables.read_signed(*os2 + 86);
                metrics.cap_height = m_tables.read_signed(*os2 + 88);
            }
            if ((m_tables.read(*os2 + 62, 2) & (1U << 7)) != 0)
            {
                metrics.ascender = m_tables.read_signed(*os2 + 68);
                metrics.descender = m_tables.read_signed(*os2 + 70);
                metrics.line_gap = m_tables.read_signed(*os2 + 72);
            }
        }
        if (const std::optional<std::size_t> post = m_tables.find("post"); post.has_value())
        {
            metrics.underline_position = m_tables.read_signed(*post + 8);
            metrics.underline_thickness = m_tables.read_signed(*post + 10);
        }
        return metrics;
    }

    [[nodiscard]] auto glyph_count() const noexcept -> std::uint32_t override
    {
        return static_cast<std::uint32_t>(m_font.numGlyphs);
    }

    [[nodiscard]] auto character_map() const -> std::vector<CharacterMapping> override
    {
        // stb_truetype only looks code points up: every scalar value is looked up.
        std::vector<CharacterMapping> map;
        for (char32_t code_point = 0; code_point <= 0x10FFFF; ++code_point)
        {
            if (code_point >= 0xD800 && code_point <= 0xDFFF)
            {
                continue;
            }
            if (const int glyph = stbtt_FindGlyphIndex(&m_font, static_cast<int>(code_point)); glyph != 0)
            {
                map.push_back({.code_point = code_point, .glyph = GlyphIndex{static_cast<std::uint32_t>(glyph)}});
            }
        }
        return map;
    }

    [[nodiscard]] auto advance(GlyphIndex glyph) const -> double override
    {
        int advance = 0;
        int left_side_bearing = 0;
        stbtt_GetGlyphHMetrics(&m_font, static_cast<int>(glyph.value()), &advance, &left_side_bearing);
        return advance;
    }

    [[nodiscard]] auto outline(GlyphIndex glyph) const -> GlyphOutline override
    {
        stbtt_vertex* vertices = nullptr;
        const int count = stbtt_GetGlyphShape(&m_font, static_cast<int>(glyph.value()), &vertices);
        OutlineBuilder builder;
        for (int index = 0; index < count; ++index)
        {
            const stbtt_vertex& vertex = vertices[index];
            const OutlinePoint point{static_cast<double>(vertex.x), static_cast<double>(vertex.y)};
            switch (vertex.type)
            {
                case STBTT_vmove:
                    builder.move_to(point);
                    break;
                case STBTT_vline:
                    builder.line_to(point);
                    break;
                case STBTT_vcurve:
                    builder.quadratic_to({static_cast<double>(vertex.cx), static_cast<double>(vertex.cy)}, point);
                    break;
                case STBTT_vcubic:
                    builder.cubic_to({static_cast<double>(vertex.cx), static_cast<double>(vertex.cy)},
                                     {static_cast<double>(vertex.cx1), static_cast<double>(vertex.cy1)}, point);
                    break;
                default:
                    break;
            }
        }
        stbtt_FreeShape(&m_font, vertices);
        return builder.finish();
    }

    [[nodiscard]] auto glyph_flags(GlyphIndex glyph) const -> AtlasGlyphFlag override
    {
        return has_color_layers(glyph.value()) ? AtlasGlyphFlag::COLOR_LAYERS : AtlasGlyphFlag::NONE;
    }

    [[nodiscard]] auto variation_axes() const -> std::vector<VariationAxis> override
    {
        // fvar: axis records after the header.
        std::vector<VariationAxis> axes;
        const std::optional<std::size_t> fvar = m_tables.find("fvar");
        if (!fvar.has_value())
        {
            return axes;
        }
        const std::size_t first = *fvar + m_tables.read(*fvar + 4, 2);
        const std::size_t count = m_tables.read(*fvar + 8, 2);
        const std::size_t size = m_tables.read(*fvar + 10, 2);
        const auto fixed = [&](std::size_t offset) { return static_cast<std::int32_t>(m_tables.read(offset, 4)) / 65536.0; };
        for (std::size_t axis = 0; axis < count; ++axis)
        {
            const std::size_t record = first + axis * size;
            axes.push_back({.tag = m_tables.read(record, 4),
                            .name = name(static_cast<int>(m_tables.read(record + 18, 2)), -1),
                            .minimum = fixed(record + 4),
                            .default_value = fixed(record + 8),
                            .maximum = fixed(record + 12)});
        }
        return axes;
    }

    [[nodiscard]] auto named_instances() const -> std::vector<NamedInstance> override
    {
        // fvar: instance records after the axis records.
        std::vector<NamedInstance> instances;
        const std::optional<std::size_t> fvar = m_tables.find("fvar");
        if (!fvar.has_value())
        {
            return instances;
        }
        const std::vector<VariationAxis> axes = variation_axes();
        const std::size_t first_axis = *fvar + m_tables.read(*fvar + 4, 2);
        const std::size_t axis_size = m_tables.read(*fvar + 10, 2);
        const std::size_t count = m_tables.read(*fvar + 12, 2);
        const std::size_t size = m_tables.read(*fvar + 14, 2);
        const std::size_t first = first_axis + axes.size() * axis_size;
        for (std::size_t instance = 0; instance < count; ++instance)
        {
            const std::size_t record = first + instance * size;
            NamedInstance named{.name = name(static_cast<int>(m_tables.read(record, 2)), -1)};
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
            {
                const double value = static_cast<std::int32_t>(m_tables.read(record + 4 + axis * 4, 4)) / 65536.0;
                named.coordinates.push_back({.tag = axes[axis].tag, .value = static_cast<float>(value)});
            }
            instances.push_back(std::move(named));
        }
        return instances;
    }

    void set_variation(std::span<const FontAxisValue> coordinates) override
    {
        // Only the default instance: other instances come from a static subset.
        if (!coordinates.empty())
        {
            throw_unsupported(UiSubsystem::FONT, "FontFace::set_variation", "font variations",
                              "stb_truetype reads only the default instance of a variable font",
                              "build the atlas with a shaping font (hb-subset makes the instance static), or decode with "
                              "FontDecoderKind::FREETYPE");
        }
    }

    [[nodiscard]] auto sfnt() const -> std::span<const std::byte> override
    {
        return m_source.bytes();
    }

    [[nodiscard]] auto sfnt_face_index() const noexcept -> std::uint32_t override
    {
        return m_face_index;
    }

private:
    /// English name @p name_id (or @p fallback_id when absent), Windows Unicode then Macintosh Roman.
    [[nodiscard]] auto name(int name_id,
                            int fallback_id) const -> std::string
    {
        for (const int id : {name_id, fallback_id})
        {
            if (id < 0)
            {
                continue;
            }
            int length = 0;
            if (const char* text = stbtt_GetFontNameString(&m_font, &length, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                           STBTT_MS_LANG_ENGLISH, id);
                text != nullptr)
            {
                return utf8_of_utf16_big_endian(text, length);
            }
            if (const char* text = stbtt_GetFontNameString(&m_font, &length, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                                           STBTT_MAC_LANG_ENGLISH, id);
                text != nullptr)
            {
                return std::string{text, static_cast<std::size_t>(length)};
            }
        }
        return {};
    }

    /// Whether COLR (version 0 layers or a version 1 paint) has @p glyph.
    [[nodiscard]] auto has_color_layers(std::uint32_t glyph) const noexcept -> bool
    {
        const std::optional<std::size_t> colr = m_tables.find("COLR");
        if (!colr.has_value())
        {
            return false;
        }
        // Version 0: base glyph records (glyph, first layer, layer count), sorted by glyph.
        const std::size_t base_count = m_tables.read(*colr + 2, 2);
        const std::size_t base_records = *colr + m_tables.read(*colr + 4, 4);
        for (std::size_t record = 0; record < base_count; ++record)
        {
            if (m_tables.read(base_records + record * 6, 2) == glyph)
            {
                return true;
            }
        }
        // Version 1: base glyph paint records (glyph, paint offset).
        if (m_tables.read(*colr, 2) >= 1)
        {
            const std::uint32_t list_offset = m_tables.read(*colr + 14, 4);
            if (list_offset != 0)
            {
                const std::size_t list = *colr + list_offset;
                const std::size_t count = m_tables.read(list, 4);
                for (std::size_t record = 0; record < count; ++record)
                {
                    if (m_tables.read(list + 4 + record * 6, 2) == glyph)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    FontSource m_source;
    std::uint32_t m_face_index = 0;
    FontFormat m_format = FontFormat::TRUETYPE;
    TableReader m_tables;
    stbtt_fontinfo m_font{};
};

/// The stb_truetype decoder.
class StbTruetypeDecoder final : public FontDecoder
{
public:
    [[nodiscard]] auto kind() const noexcept -> FontDecoderKind override
    {
        return FontDecoderKind::STB_TRUETYPE;
    }

    [[nodiscard]] auto face_count(const FontSource& source) const -> std::uint32_t override
    {
        require_readable_format(source, "FontDecoder::face_count");
        const int count = stbtt_GetNumberOfFonts(reinterpret_cast<const unsigned char*>(source.bytes().data()));
        if (count <= 0)
        {
            throw_font_usage_error("FontDecoder::face_count", std::format("stb_truetype finds no face in '{}'", source.name()),
                                   "check that the file is a complete, valid font");
        }
        return static_cast<std::uint32_t>(count);
    }

    [[nodiscard]] auto open(const FontSource& source,
                            std::uint32_t face_index) const -> std::unique_ptr<FontFace> override
    {
        const FontFormat format = require_readable_format(source, "FontDecoder::open");
        const std::uint32_t count = face_count(source);
        if (face_index >= count)
        {
            throw_font_usage_error("FontDecoder::open", std::format("'{}' has {} face(s): there is no face {}", source.name(), count, face_index),
                                   std::format("use a face index below {}", count));
        }
        const int start = stbtt_GetFontOffsetForIndex(reinterpret_cast<const unsigned char*>(source.bytes().data()), static_cast<int>(face_index));
        // The format of the face itself (a collection holds TrueType or CFF faces).
        FontFormat face_format = format;
        if (format == FontFormat::COLLECTION)
        {
            const TableReader tables{source.bytes(), static_cast<std::size_t>(start)};
            face_format = tables.find("CFF2").has_value() ? FontFormat::OPENTYPE_CFF2
                          : tables.find("CFF ").has_value() ? FontFormat::OPENTYPE_CFF
                                                            : FontFormat::TRUETYPE;
        }
        if (face_format == FontFormat::OPENTYPE_CFF2)
        {
            throw_cff2_unsupported(source);
        }
        return std::make_unique<StbTruetypeFace>(source, face_index, start, format);
    }

private:
    /// The format of @p source; throws for the formats stb_truetype cannot read.
    static auto require_readable_format(const FontSource& source,
                                        std::string_view operation) -> FontFormat
    {
        const std::optional<FontFormat> format = source.format();
        if (!format.has_value())
        {
            throw_unsupported(UiSubsystem::FONT, operation, "font format",
                              std::format("'{}' is not a TrueType, OpenType or collection font", source.name()),
                              "convert the font to OpenType (Type 1 and bitmap fonts are not supported)");
        }
        switch (*format)
        {
            case FontFormat::WOFF:
            case FontFormat::WOFF2:
                throw_unsupported(UiSubsystem::FONT, operation, std::format("{} fonts", to_string(*format)),
                                  std::format("stb_truetype cannot decompress '{}' ({})", source.name(), to_string(*format)),
                                  "decode WOFF and WOFF2 fonts with FontDecoderKind::FREETYPE");
            case FontFormat::OPENTYPE_CFF2:
                throw_cff2_unsupported(source);
            default:
                return *format;
        }
    }

    [[noreturn]] static void throw_cff2_unsupported(const FontSource& source)
    {
        throw_unsupported(UiSubsystem::FONT, "FontDecoder::open", "CFF2 outlines",
                          std::format("stb_truetype cannot read the CFF2 outlines of '{}'", source.name()),
                          "decode CFF2 (variable OpenType) fonts with FontDecoderKind::FREETYPE");
    }
};

} // namespace

auto
make_stb_truetype_decoder() -> std::unique_ptr<FontDecoder>
{
    return std::make_unique<StbTruetypeDecoder>();
}

} // namespace gfx::ui
