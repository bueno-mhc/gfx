/// @file shaping_font.cpp
/// @brief The HarfBuzz steps of the atlas builder: subsetting (and instancing) the font that HarfBuzz will
///        shape with at run time, and extracting the pair kerning and standard ligatures the basic shaper
///        applies without HarfBuzz.
module;

#include <hb-ot.h>
#include <hb-subset.h>
#include <hb.h>

module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import :implementations;

namespace gfx::ui
{

namespace
{

/// Releases HarfBuzz objects.
struct HarfBuzzDeleter
{
    void operator()(hb_blob_t* blob) const noexcept
    {
        hb_blob_destroy(blob);
    }

    void operator()(hb_face_t* face) const noexcept
    {
        hb_face_destroy(face);
    }

    void operator()(hb_font_t* font) const noexcept
    {
        hb_font_destroy(font);
    }

    void operator()(hb_buffer_t* buffer) const noexcept
    {
        hb_buffer_destroy(buffer);
    }

    void operator()(hb_subset_input_t* input) const noexcept
    {
        hb_subset_input_destroy(input);
    }
};

template<class Object>
using HarfBuzzPointer = std::unique_ptr<Object, HarfBuzzDeleter>;

/// The HarfBuzz face of @p bytes (which must outlive it).
auto
face_of(std::span<const std::byte> bytes,
        std::uint32_t face_index) -> HarfBuzzPointer<hb_face_t>
{
    const HarfBuzzPointer<hb_blob_t> blob{hb_blob_create(reinterpret_cast<const char*>(bytes.data()), static_cast<unsigned int>(bytes.size()),
                                                         HB_MEMORY_MODE_READONLY, nullptr, nullptr)};
    return HarfBuzzPointer<hb_face_t>{hb_face_create(blob.get(), face_index)};
}

/// Whether the basic shaper handles @p script.
auto
is_basic_script(hb_script_t script) noexcept -> bool
{
    return script == HB_SCRIPT_LATIN || script == HB_SCRIPT_GREEK || script == HB_SCRIPT_CYRILLIC || script == HB_SCRIPT_COMMON;
}

/// Shapes code points with a reused buffer, left to right in one script, with the default language
/// system (no locale: the result is the same on every machine).
class PairShaper
{
public:
    explicit PairShaper(hb_font_t* font)
        : m_font{font}
        , m_buffer{hb_buffer_create()}
    {
    }

    /// The glyphs and advances of @p code_points.
    auto shape(std::span<const hb_codepoint_t> code_points,
               hb_script_t script) -> std::span<const hb_glyph_info_t>
    {
        hb_buffer_clear_contents(m_buffer.get());
        hb_buffer_add_codepoints(m_buffer.get(), code_points.data(), static_cast<int>(code_points.size()), 0, static_cast<int>(code_points.size()));
        hb_buffer_set_direction(m_buffer.get(), HB_DIRECTION_LTR);
        hb_buffer_set_script(m_buffer.get(), script);
        hb_shape(m_font, m_buffer.get(), nullptr, 0);
        unsigned int count = 0;
        const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(m_buffer.get(), &count);
        return {infos, count};
    }

    /// Horizontal advance of output glyph @p index of the last shaping.
    [[nodiscard]] auto advance(std::size_t index) const noexcept -> hb_position_t
    {
        unsigned int count = 0;
        return hb_buffer_get_glyph_positions(m_buffer.get(), &count)[index].x_advance;
    }

private:
    hb_font_t* m_font = nullptr;
    HarfBuzzPointer<hb_buffer_t> m_buffer{};
};

} // namespace

auto
subset_shaping_font(std::span<const std::byte> sfnt,
                    std::uint32_t face_index,
                    std::span<const char32_t> code_points,
                    std::span<const FontAxisValue> axes,
                    std::string_view font_name) -> std::vector<std::byte>
{
    const HarfBuzzPointer<hb_face_t> face = face_of(sfnt, face_index);
    if (hb_face_get_glyph_count(face.get()) == 0)
    {
        throw_unsupported(UiSubsystem::FONT, "AtlasBuilder::build", "shaping font",
                          std::format("HarfBuzz cannot read the OpenType tables of '{}'", font_name),
                          "build the atlas without a shaping font (AtlasBuildDescription::shaping_font = false)");
    }

    // The characters, their layout closure (hb-subset's default features), the outline of `.notdef` (drawn
    // for characters the atlas lacks), no hinting.
    const HarfBuzzPointer<hb_subset_input_t> input{hb_subset_input_create_or_fail()};
    for (const char32_t code_point : code_points)
    {
        hb_set_add(hb_subset_input_unicode_set(input.get()), static_cast<hb_codepoint_t>(code_point));
    }
    hb_subset_input_set_flags(input.get(), HB_SUBSET_FLAGS_NO_HINTING | HB_SUBSET_FLAGS_NOTDEF_OUTLINE);

    // A static instance: every axis pinned (defaults first, then the values given).
    if (!axes.empty())
    {
        hb_subset_input_pin_all_axes_to_default(input.get(), face.get());
        for (const FontAxisValue& axis : axes)
        {
            if (hb_subset_input_pin_axis_location(input.get(), face.get(), axis.tag, axis.value) == 0)
            {
                const std::array<char, 4> tag = font_tag_name(axis.tag);
                throw_font_usage_error("AtlasBuilder::build",
                                       std::format("HarfBuzz cannot pin axis '{}' of '{}' at {}", std::string_view{tag.data(), tag.size()},
                                                   font_name, axis.value),
                                       "give values of the font's own axes");
            }
        }
    }
    const HarfBuzzPointer<hb_face_t> subset{hb_subset_or_fail(face.get(), input.get())};
    if (subset == nullptr)
    {
        throw_unsupported(UiSubsystem::FONT, "AtlasBuilder::build", "shaping font",
                          std::format("hb-subset cannot subset{} '{}'", axes.empty() ? "" : " and instance", font_name),
                          "build the atlas without a shaping font (AtlasBuildDescription::shaping_font = false)");
    }
    const HarfBuzzPointer<hb_blob_t> blob{hb_face_reference_blob(subset.get())};
    unsigned int size = 0;
    const char* data = hb_blob_get_data(blob.get(), &size);
    std::vector<std::byte> bytes(size);
    std::memcpy(bytes.data(), data, size);
    return bytes;
}

auto
extract_pair_tables(std::span<const std::byte> shaping_font,
                    std::span<const CharacterMapping> characters,
                    bool kerning,
                    bool ligatures) -> PairTables
{
    PairTables tables;
    if (!kerning && !ligatures)
    {
        return tables;
    }
    const HarfBuzzPointer<hb_face_t> face = face_of(shaping_font, 0);
    const HarfBuzzPointer<hb_font_t> font{hb_font_create(face.get())};
    const double units_per_em = hb_face_get_upem(face.get());
    PairShaper shaper{font.get()};

    // The characters of the basic shaper's scripts.
    hb_unicode_funcs_t* unicode = hb_unicode_funcs_get_default();
    std::vector<CharacterMapping> basic;
    for (const CharacterMapping& mapping : characters)
    {
        if (is_basic_script(hb_unicode_script(unicode, static_cast<hb_codepoint_t>(mapping.code_point))))
        {
            basic.push_back(mapping);
        }
    }
    const auto script_of = [&](std::span<const hb_codepoint_t> code_points)
    {
        for (const hb_codepoint_t code_point : code_points)
        {
            const hb_script_t script = hb_unicode_script(unicode, code_point);
            if (script != HB_SCRIPT_COMMON)
            {
                return script;
            }
        }
        return HB_SCRIPT_LATIN;
    };

    // Every pair: one glyph out is a ligature; the same two glyphs out, a kerning adjustment.
    std::vector<std::vector<std::size_t>> ligature_sequences;
    std::set<std::pair<GlyphIndex, GlyphIndex>> kerned;
    for (std::size_t first = 0; first < basic.size(); ++first)
    {
        for (std::size_t second = 0; second < basic.size(); ++second)
        {
            const std::array<hb_codepoint_t, 2> pair{static_cast<hb_codepoint_t>(basic[first].code_point),
                                                     static_cast<hb_codepoint_t>(basic[second].code_point)};
            const std::span<const hb_glyph_info_t> glyphs = shaper.shape(pair, script_of(pair));
            if (glyphs.size() == 1 && ligatures)
            {
                ligature_sequences.push_back({first, second});
            }
            else if (glyphs.size() == 2 && kerning && glyphs[0].codepoint == basic[first].glyph.value()
                     && glyphs[1].codepoint == basic[second].glyph.value())
            {
                const hb_position_t adjustment = shaper.advance(0) - hb_font_get_glyph_h_advance(font.get(), glyphs[0].codepoint);
                const std::pair key{basic[first].glyph, basic[second].glyph};
                if (adjustment != 0 && kerned.insert(key).second)
                {
                    tables.kerning.push_back({.left = key.first, .right = key.second, .adjustment = static_cast<float>(adjustment / units_per_em)});
                }
            }
        }
    }

    // Ligatures of three and four characters extend those of two and three.
    std::set<std::vector<GlyphIndex>> ligated;
    for (std::size_t length = 2; length <= MAXIMUM_LIGATURE_COMPONENTS && !ligature_sequences.empty(); ++length)
    {
        std::vector<std::vector<std::size_t>> longer;
        for (const std::vector<std::size_t>& sequence : ligature_sequences)
        {
            std::vector<hb_codepoint_t> code_points;
            Ligature ligature{.component_count = static_cast<std::uint32_t>(sequence.size())};
            std::vector<GlyphIndex> components;
            for (std::size_t index = 0; index < sequence.size(); ++index)
            {
                code_points.push_back(static_cast<hb_codepoint_t>(basic[sequence[index]].code_point));
                ligature.components[index] = basic[sequence[index]].glyph;
                components.push_back(basic[sequence[index]].glyph);
            }
            const std::span<const hb_glyph_info_t> glyphs = shaper.shape(code_points, script_of(code_points));
            if (glyphs.size() != 1)
            {
                continue;
            }
            ligature.glyph = GlyphIndex{glyphs[0].codepoint};
            if (ligated.insert(components).second)
            {
                tables.ligatures.push_back(ligature);
            }
            if (length < MAXIMUM_LIGATURE_COMPONENTS)
            {
                for (std::size_t next = 0; next < basic.size(); ++next)
                {
                    std::vector<std::size_t> extended = sequence;
                    extended.push_back(next);
                    longer.push_back(std::move(extended));
                }
            }
        }
        ligature_sequences = std::move(longer);
    }

    // The orders of the format.
    std::ranges::sort(tables.kerning, {}, [](const KerningPair& pair) { return std::pair{pair.left, pair.right}; });
    std::ranges::sort(tables.ligatures, [](const Ligature& left, const Ligature& right)
    {
        if (left.components[0] != right.components[0])
        {
            return left.components[0] < right.components[0];
        }
        if (left.component_count != right.component_count)
        {
            return left.component_count > right.component_count;
        }
        return left.components < right.components;
    });
    return tables;
}

} // namespace gfx::ui
