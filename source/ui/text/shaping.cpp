/// @file shaping.cpp
/// @brief The two shapers: HarfBuzz (with the atlas's shaping font, a reused buffer and one HarfBuzz font
///        per atlas) and the basic shaper (the atlas's character map, standard ligatures and pair kerning).
module;

#include <hb.h>

module gfx.ui.text;

import std;
import gfx.ui.atlas_format;
import :engine;

namespace gfx::ui::text_engine
{

namespace
{

/// The HarfBuzz font of an atlas's shaping font (font units: scale = units per em).
class HarfBuzzFont
{
public:
    explicit HarfBuzzFont(std::shared_ptr<const FontAtlas> atlas)
        : m_atlas{std::move(atlas)}
    {
        const std::span<const std::byte> font = m_atlas->shaping_font();
        m_blob = hb_blob_create(reinterpret_cast<const char*>(font.data()), static_cast<unsigned int>(font.size()), HB_MEMORY_MODE_READONLY,
                                nullptr, nullptr);
        m_face = hb_face_create(m_blob, 0);
        m_font = hb_font_create(m_face);
        m_units_per_em = hb_face_get_upem(m_face);
    }

    HarfBuzzFont(const HarfBuzzFont&) = delete;
    auto operator=(const HarfBuzzFont&) -> HarfBuzzFont& = delete;

    ~HarfBuzzFont()
    {
        hb_font_destroy(m_font);
        hb_face_destroy(m_face);
        hb_blob_destroy(m_blob);
    }

    [[nodiscard]] auto font() const noexcept -> hb_font_t*
    {
        return m_font;
    }

    [[nodiscard]] auto units_per_em() const noexcept -> unsigned int
    {
        return m_units_per_em;
    }

private:
    std::shared_ptr<const FontAtlas> m_atlas{};
    hb_blob_t* m_blob = nullptr;
    hb_face_t* m_face = nullptr;
    hb_font_t* m_font = nullptr;
    unsigned int m_units_per_em = 0;
};

} // namespace

struct HarfBuzzShaper::State
{
    hb_buffer_t* buffer = hb_buffer_create();
    std::unordered_map<const FontAtlas*, std::unique_ptr<HarfBuzzFont>> fonts{};

    State() = default;
    State(const State&) = delete;
    auto operator=(const State&) -> State& = delete;

    ~State()
    {
        hb_buffer_destroy(buffer);
    }
};

HarfBuzzShaper::HarfBuzzShaper()
    : m_state{std::make_unique<State>()}
{
}

HarfBuzzShaper::~HarfBuzzShaper() = default;

void
HarfBuzzShaper::shape(const std::shared_ptr<const FontAtlas>& atlas,
                      std::span<const std::uint32_t> paragraph,
                      std::uint32_t begin,
                      std::uint32_t end,
                      bool right_to_left,
                      std::uint32_t script,
                      const std::string& language,
                      float size,
                      std::vector<ShapedGlyph>& glyphs)
{
    // The atlas's HarfBuzz font, made on first use (it keeps the atlas alive).
    std::unique_ptr<HarfBuzzFont>& font = m_state->fonts[atlas.get()];
    if (font == nullptr)
    {
        font = std::make_unique<HarfBuzzFont>(atlas);
    }

    // The run, with the whole paragraph as context; clusters are indices of code points in the paragraph.
    hb_buffer_t* buffer = m_state->buffer;
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_codepoints(buffer, paragraph.data(), static_cast<int>(paragraph.size()), begin, static_cast<int>(end - begin));
    hb_buffer_set_direction(buffer, right_to_left ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, hb_script_from_iso15924_tag(script));
    if (!language.empty())
    {
        hb_buffer_set_language(buffer, hb_language_from_string(language.c_str(), -1));
    }
    hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_shape(font->font(), buffer, nullptr, 0);

    // Glyphs in visual order, in pixels.
    unsigned int count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    const float scale = size / static_cast<float>(font->units_per_em());
    for (unsigned int index = 0; index < count; ++index)
    {
        glyphs.push_back({.glyph = GlyphIndex{infos[index].codepoint},
                          .cluster = infos[index].cluster,
                          .advance = static_cast<float>(positions[index].x_advance) * scale,
                          .x_offset = static_cast<float>(positions[index].x_offset) * scale,
                          .y_offset = static_cast<float>(positions[index].y_offset) * scale});
    }
}

void
shape_basic(const FontAtlas& atlas,
            std::span<const std::uint32_t> paragraph,
            std::uint32_t begin,
            std::uint32_t end,
            bool right_to_left,
            float size,
            std::vector<ShapedGlyph>& glyphs)
{
    const AtlasView& view = atlas.view();
    const auto glyph_of = [&](std::uint32_t index)
    {
        return atlas.find_character(static_cast<char32_t>(paragraph[index])).value_or(GlyphIndex{});
    };

    // Characters to glyphs, standard ligatures replacing their components (longest first).
    const std::size_t first = glyphs.size();
    std::uint32_t index = begin;
    while (index < end)
    {
        const GlyphIndex glyph = glyph_of(index);
        std::size_t candidate = 0;
        std::size_t high = view.ligature_count();
        while (candidate < high)
        {
            const std::size_t middle = candidate + (high - candidate) / 2;
            if (view.ligature_at(middle).components[0] < glyph)
            {
                candidate = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        std::uint32_t consumed = 1;
        GlyphIndex result = glyph;
        for (; candidate < view.ligature_count() && consumed == 1; ++candidate)
        {
            const Ligature ligature = view.ligature_at(candidate);
            if (ligature.components[0] != glyph)
            {
                break;
            }
            bool matches = index + ligature.component_count <= end;
            for (std::uint32_t component = 1; matches && component < ligature.component_count; ++component)
            {
                matches = glyph_of(index + component) == ligature.components[component];
            }
            if (matches)
            {
                consumed = ligature.component_count;
                result = ligature.glyph;
            }
        }
        const std::optional<AtlasGlyph> record = view.find_glyph(result);
        glyphs.push_back({.glyph = result, .cluster = index, .advance = record.has_value() ? record->advance * size : 0.0F});
        index += consumed;
    }

    // Pair kerning, then the visual order of a right-to-left run.
    for (std::size_t glyph = first; glyph + 1 < glyphs.size(); ++glyph)
    {
        glyphs[glyph].advance += view.find_kerning(glyphs[glyph].glyph, glyphs[glyph + 1].glyph) * size;
    }
    if (right_to_left)
    {
        std::reverse(glyphs.begin() + static_cast<std::ptrdiff_t>(first), glyphs.end());
    }
}

} // namespace gfx::ui::text_engine
