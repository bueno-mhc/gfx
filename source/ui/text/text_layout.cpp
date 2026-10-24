/// @file text_layout.cpp
/// @brief TextLayoutEngine: paragraphs → bidi and scripts → atlas per character → runs → shaping → line
///        breaking → visual reordering → alignment; the layout cache (fixed capacity; layouts used in one frame
///        only replaced first, the one whose storage fits the new layout best, then the least recently used;
///        nothing allocated on a hit, nor on a miss once storage of its size circulates); caret positions and hit
///        testing.
module gfx.ui.text;

import std;
import gfx.ui.atlas_format;
import gfx.ui.unicode;
import :engine;

namespace gfx::ui
{

namespace
{

using namespace text_engine;

/// Marks the end of a chain of entries.
constexpr std::uint32_t NO_ENTRY = std::numeric_limits<std::uint32_t>::max();

/// Marks a character no atlas of the chain has (drawn as the first atlas's `.notdef`).
constexpr std::uint8_t NO_ATLAS = std::numeric_limits<std::uint8_t>::max();

/// Frames after which a layout used in one frame only is replaced before a free entry is taken: a text used in two
/// consecutive frames is always found again, and texts that change every frame reuse each other's entries.
constexpr std::uint64_t PROBATION_FRAMES = 2;

/// Replaceable layouts (used in one frame only) a miss compares, from the oldest: the one whose storage holds the new
/// layout with the least to spare is replaced, so that a long text takes the storage of a long text of an earlier frame
/// rather than growing a short one's.
constexpr std::uint32_t FIT_CANDIDATES = 16;

/// Frames after which a layout used in several frames, then no more, is replaceable: a text that stood still for a few
/// frames and then changed leaves its entry in the retained list, whose storage a miss compares with the replaceable
/// layouts' of the probation list (the oldest FIT_CANDIDATES of each) and which is replaced before a free entry is taken,
/// rather than a short entry's or a free entry's storage grown.
constexpr std::uint64_t RETAINED_FRAMES = 30;

/// Whether @p code_point ends a paragraph (bidi class B, and the line separator).
constexpr auto
is_paragraph_separator(std::uint32_t code_point) noexcept -> bool
{
    return code_point == U'\n' || code_point == U'\r' || code_point == 0x1C || code_point == 0x1D || code_point == 0x1E || code_point == 0x85
           || code_point == 0x2028 || code_point == 0x2029;
}

/// Whether @p code_point is whitespace that hangs at the end of a line.
constexpr auto
is_whitespace(std::uint32_t code_point) noexcept -> bool
{
    return code_point == U' ' || code_point == U'\t' || code_point == 0x1680 || (code_point >= 0x2000 && code_point <= 0x200A)
           || code_point == 0x205F || code_point == 0x3000;
}

/// What a layout needs, or what storage holds: bytes of text, glyphs and lines.
struct StorageSize
{
    std::size_t text = 0;
    std::size_t glyphs = 0;
    std::size_t lines = 0;
};

/// Makes room for @p count elements in @p storage. Storage that grows is rounded up to a power of two (a size class),
/// so that the layouts of texts of similar lengths fit in each other's storage.
template<class Storage>
void
reserve_size_class(Storage& storage,
                   std::size_t count)
{
    if (storage.capacity() < count)
    {
        storage.reserve(std::bit_ceil(count));
    }
}

/// A cached layout and its key.
struct Entry
{
    std::string text{};
    FontHandle font{};
    TextLayoutDescription description{};
    std::uint64_t hash = 0;
    std::vector<LaidOutGlyph> glyphs{};
    std::vector<LaidOutLine> lines{};
    float width = 0;
    float height = 0;
    std::uint32_t missing_glyph_count = 0;
    TextLayoutProblem problems = TextLayoutProblem::NONE;
    float font_size = 0;
    std::uint64_t frame = 0;
    bool retained = false; // Used in more than one frame: in the retained list, else in the probation list.
    std::uint32_t newer = NO_ENTRY;
    std::uint32_t older = NO_ENTRY;
    std::uint32_t next_in_bucket = NO_ENTRY;

    [[nodiscard]] auto view() const noexcept -> TextLayout
    {
        return {.glyphs = glyphs, .lines = lines, .width = width, .height = height, .missing_glyph_count = missing_glyph_count,
                .problems = problems, .font_size = font_size};
    }

    /// Whether the storage of this entry holds a layout of @p size without growing.
    [[nodiscard]] auto holds(const StorageSize& size) const noexcept -> bool
    {
        return text.capacity() >= size.text && glyphs.capacity() >= size.glyphs && lines.capacity() >= size.lines;
    }

    /// The bytes of storage this entry holds.
    [[nodiscard]] auto storage_bytes() const noexcept -> std::size_t
    {
        return text.capacity() + glyphs.capacity() * sizeof(LaidOutGlyph) + lines.capacity() * sizeof(LaidOutLine);
    }

    /// Copies the layout of @p other (not its key) into this entry's storage, which grows to a size class if it is too small.
    void copy_layout(const Entry& other)
    {
        reserve_size_class(glyphs, other.glyphs.size());
        reserve_size_class(lines, other.lines.size());
        glyphs.assign(other.glyphs.begin(), other.glyphs.end());
        lines.assign(other.lines.begin(), other.lines.end());
        width = other.width;
        height = other.height;
        missing_glyph_count = other.missing_glyph_count;
        problems = other.problems;
        font_size = other.font_size;
    }
};

/// Entries linked from the most to the least recently used.
struct EntryList
{
    std::uint32_t newest = NO_ENTRY;
    std::uint32_t oldest = NO_ENTRY;
};

/// The key of a layout, hashed.
auto
hash_of(std::string_view text,
        FontHandle font,
        const TextLayoutDescription& description) noexcept -> std::uint64_t
{
    std::uint64_t hash = xxhash64(std::as_bytes(std::span{text}));
    const auto combine = [&](std::uint64_t value) { hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6) + (hash >> 2); };
    combine(std::hash<FontHandle>{}(font));
    combine(std::bit_cast<std::uint32_t>(description.wrap_width));
    combine(std::bit_cast<std::uint32_t>(description.line_spacing));
    combine(std::to_underlying(description.alignment) | (std::uint64_t{std::to_underlying(description.direction)} << 8));
    return hash;
}

/// A run of characters of one level, script and atlas, and its glyphs in the paragraph's glyph list.
struct Item
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint8_t level = 0;
    std::uint32_t script = 0;
    std::uint8_t atlas = 0;
    std::uint32_t glyph_begin = 0;
    std::uint32_t glyph_end = 0;
};

/// An atlas of the font's chain.
struct ChainAtlas
{
    FontAtlasHandle handle{};
    const std::shared_ptr<const FontAtlas>* atlas = nullptr;
};

/// What a layout needs from its font.
struct LayoutContext
{
    const FontDescription* font = nullptr;
    const TextLayoutDescription* description = nullptr;
    float ascender = 0;
    float line_height = 0;
};

} // namespace

struct TextLayoutEngine::State
{
    State(const FontLibrary& library,
          std::uint32_t capacity)
        : fonts{library}
        , entries(std::max(capacity, 1U))
        , buckets(std::bit_ceil(std::max(capacity, 1U) * 2), NO_ENTRY)
    {
    }

    const FontLibrary& fonts;
    std::vector<Entry> entries;
    std::uint32_t used = 0;
    std::vector<std::uint32_t> buckets;
    EntryList probation{}; // Layouts used in one frame only.
    EntryList retained{};  // Layouts used in several frames.
    std::deque<Entry> overflow{};  // Layouts made while every entry was in use this frame; their storage is kept.
    std::size_t overflow_used = 0; // Overflow entries holding a layout of this frame.
    Entry scratch{};               // Where a miss is laid out before it is copied into its entry (its storage only grows).
    std::uint64_t frame = 1;
    TextLayoutStatistics statistics{};

    // Atlases resolved from the library (kept alive while the engine uses them) and the shapers.
    std::unordered_map<FontAtlasHandle, std::shared_ptr<const FontAtlas>> atlases{};
    HarfBuzzShaper harfbuzz{};
    BidiParagraph bidi{};

    // Scratch storage, reused from layout to layout.
    std::vector<ChainAtlas> chain{};
    std::vector<std::uint32_t> code_points{};
    std::vector<std::uint32_t> offsets{};
    std::vector<std::uint8_t> atlas_indices{};
    std::vector<Item> items{};
    std::vector<ShapedGlyph> shaped{};
    std::vector<float> advances{};
    std::vector<std::uint8_t> cluster_starts{};
    std::vector<char> line_breaks{};
    std::vector<char> grapheme_breaks{};
    std::vector<VisualRun> runs{};
    std::vector<float> line_shifts{};

    // ---------------------------------------------------------------------------------------- Cache

    [[nodiscard]] auto mask() const noexcept -> std::uint64_t
    {
        return buckets.size() - 1;
    }

    [[nodiscard]] auto find(std::uint64_t hash,
                            std::string_view text,
                            FontHandle font,
                            const TextLayoutDescription& description) const noexcept -> std::uint32_t
    {
        for (std::uint32_t index = buckets[hash & mask()]; index != NO_ENTRY; index = entries[index].next_in_bucket)
        {
            const Entry& entry = entries[index];
            if (entry.hash == hash && entry.font == font && entry.description == description && entry.text == text)
            {
                return index;
            }
        }
        return NO_ENTRY;
    }

    [[nodiscard]] auto list_of(const Entry& entry) noexcept -> EntryList&
    {
        return entry.retained ? retained : probation;
    }

    void unlink(std::uint32_t index) noexcept
    {
        Entry& entry = entries[index];
        EntryList& list = list_of(entry);
        (entry.newer != NO_ENTRY ? entries[entry.newer].older : list.newest) = entry.older;
        (entry.older != NO_ENTRY ? entries[entry.older].newer : list.oldest) = entry.newer;
        entry.newer = NO_ENTRY;
        entry.older = NO_ENTRY;
    }

    void make_newest(std::uint32_t index) noexcept
    {
        Entry& entry = entries[index];
        EntryList& list = list_of(entry);
        entry.older = list.newest;
        entry.newer = NO_ENTRY;
        (list.newest != NO_ENTRY ? entries[list.newest].newer : list.oldest) = index;
        list.newest = index;
        entry.frame = frame;
    }

    /// Marks the entry @p index used this frame; a layout used again in a later frame than before is retained.
    void use(std::uint32_t index) noexcept
    {
        unlink(index);
        entries[index].retained = entries[index].retained || entries[index].frame != frame;
        make_newest(index);
    }

    void remove_from_bucket(std::uint32_t index) noexcept
    {
        std::uint32_t* link = &buckets[entries[index].hash & mask()];
        while (*link != index)
        {
            link = &entries[*link].next_in_bucket;
        }
        *link = entries[index].next_in_bucket;
        entries[index].next_in_bucket = NO_ENTRY;
    }

    /// An entry for a new layout of @p size: of the replaceable ones (used in one frame only, PROBATION_FRAMES frames ago
    /// or more, and used in several frames, RETAINED_FRAMES frames ago or more; the oldest FIT_CANDIDATES of each), the
    /// one whose storage holds the new layout with the least to spare; else the oldest replaceable one (whose storage
    /// grows), a free one, the least recently used one not used this frame, or none (NO_ENTRY).
    [[nodiscard]] auto acquire(const StorageSize& size) noexcept -> std::uint32_t
    {
        // The replaceable layouts, from the oldest of each list: the one whose storage holds the new layout with the least
        // to spare.
        std::uint32_t best = NO_ENTRY;
        const auto compare = [this, &size, &best](const EntryList& list,
                                                  std::uint64_t unused_frames)
        {
            std::uint32_t candidate = list.oldest;
            for (std::uint32_t compared = 0; compared < FIT_CANDIDATES && candidate != NO_ENTRY && entries[candidate].frame + unused_frames <= frame;
                 ++compared)
            {
                const Entry& entry = entries[candidate];
                if (entry.holds(size) && (best == NO_ENTRY || entry.storage_bytes() < entries[best].storage_bytes()))
                {
                    best = candidate;
                }
                candidate = entry.newer;
            }
        };
        compare(probation, PROBATION_FRAMES);
        compare(retained, RETAINED_FRAMES);
        if (best != NO_ENTRY)
        {
            return evict(best);
        }
        if (probation.oldest != NO_ENTRY && entries[probation.oldest].frame + PROBATION_FRAMES <= frame)
        {
            return evict(probation.oldest);
        }
        if (retained.oldest != NO_ENTRY && entries[retained.oldest].frame + RETAINED_FRAMES <= frame)
        {
            return evict(retained.oldest);
        }
        if (used < entries.size())
        {
            return used++;
        }

        // Every entry holds a layout: the least recently used of both lists (the probation one on a tie).
        std::uint32_t index = probation.oldest;
        if (index == NO_ENTRY || (retained.oldest != NO_ENTRY && entries[retained.oldest].frame < entries[index].frame))
        {
            index = retained.oldest;
        }
        if (entries[index].frame == frame)
        {
            return NO_ENTRY;
        }
        return evict(index);
    }

    /// Removes the layout of the entry @p index from the cache, for a new one.
    [[nodiscard]] auto evict(std::uint32_t index) noexcept -> std::uint32_t
    {
        remove_from_bucket(index);
        unlink(index);
        entries[index].retained = false;
        ++statistics.evictions;
        return index;
    }

    // --------------------------------------------------------------------------------------- Layout

    /// The atlas of @p handle, resolved from the library once.
    [[nodiscard]] auto resolve(FontAtlasHandle handle) -> const std::shared_ptr<const FontAtlas>*
    {
        auto found = atlases.find(handle);
        if (found == atlases.end())
        {
            std::shared_ptr<const FontAtlas> atlas = fonts.atlas(handle);
            if (atlas == nullptr)
            {
                return nullptr;
            }
            found = atlases.emplace(handle, std::move(atlas)).first;
        }
        return &found->second;
    }

    /// Lays @p text out in @p font into @p entry.
    void make(Entry& entry,
              std::string_view text,
              const FontDescription& font,
              const TextLayoutDescription& description)
    {
        entry.glyphs.clear();
        entry.lines.clear();
        entry.width = 0;
        entry.height = 0;
        entry.missing_glyph_count = 0;
        entry.problems = TextLayoutProblem::NONE;
        entry.font_size = font.size;
        line_shifts.clear();

        // The chain of atlases.
        chain.clear();
        const auto add_to_chain = [&](FontAtlasHandle handle)
        {
            if (const std::shared_ptr<const FontAtlas>* atlas = resolve(handle); atlas != nullptr)
            {
                chain.push_back({.handle = handle, .atlas = atlas});
            }
        };
        add_to_chain(font.atlas);
        std::ranges::for_each(font.fallbacks, add_to_chain);
        if (chain.empty())
        {
            entry.problems |= TextLayoutProblem::UNKNOWN_FONT;
            return;
        }

        // Code points and their byte offsets (invalid bytes become U+FFFD; tabs lay out as spaces).
        code_points.clear();
        offsets.clear();
        for (const Utf8CodePoint point : Utf8CodePoints{text})
        {
            if (!point.valid)
            {
                entry.problems |= TextLayoutProblem::INVALID_UTF8;
            }
            code_points.push_back(point.code_point == U'\t' ? std::uint32_t{U' '} : static_cast<std::uint32_t>(point.code_point));
            offsets.push_back(point.offset);
        }
        offsets.push_back(static_cast<std::uint32_t>(text.size()));

        // Line metrics of the first atlas.
        const AtlasFaceMetrics metrics = (*chain.front().atlas)->face_metrics();
        const float em = font.size / static_cast<float>(metrics.units_per_em);
        LayoutContext context{.font = &font,
                              .description = &description,
                              .ascender = static_cast<float>(metrics.ascender) * em,
                              .line_height = static_cast<float>(metrics.ascender - metrics.descender + metrics.line_gap) * em * description.line_spacing};

        // Paragraphs, separated by line and paragraph separators (CR LF is one).
        const std::uint32_t count = static_cast<std::uint32_t>(code_points.size());
        std::uint32_t begin = 0;
        while (true)
        {
            std::uint32_t end = begin;
            while (end < count && !is_paragraph_separator(code_points[end]))
            {
                ++end;
            }
            lay_out_paragraph(entry, begin, end, context);
            if (end == count)
            {
                break;
            }
            begin = end + (code_points[end] == U'\r' && end + 1 < count && code_points[end + 1] == U'\n' ? 2U : 1U);
        }

        // Alignment in the wrap width, or in the widest line.
        for (const LaidOutLine& line : entry.lines)
        {
            entry.width = std::max(entry.width, line.width);
        }
        const float box = description.wrap_width > 0 ? description.wrap_width : entry.width;
        for (std::size_t index = 0; index < entry.lines.size(); ++index)
        {
            LaidOutLine& line = entry.lines[index];
            const bool start_is_right = line.right_to_left;
            float left = 0;
            switch (description.alignment)
            {
                case TextAlignment::START:  left = start_is_right ? box - line.width : 0; break;
                case TextAlignment::END:    left = start_is_right ? 0 : box - line.width; break;
                case TextAlignment::LEFT:   left = 0; break;
                case TextAlignment::CENTER: left = (box - line.width) / 2; break;
                case TextAlignment::RIGHT:  left = box - line.width; break;
            }
            line.left = left;
            for (std::uint32_t glyph = line.first_glyph; glyph < line.first_glyph + line.glyph_count; ++glyph)
            {
                entry.glyphs[glyph].x += left - line_shifts[index];
            }
        }
        entry.height = static_cast<float>(entry.lines.size()) * context.line_height;
    }

    /// Lays out the paragraph of code points [@p first, @p last).
    void lay_out_paragraph(Entry& entry,
                           std::uint32_t first,
                           std::uint32_t last,
                           const LayoutContext& context)
    {
        const std::uint32_t length = last - first;
        const std::span<const std::uint32_t> paragraph = std::span{code_points}.subspan(first, length);
        const TextLayoutDescription& description = *context.description;
        if (length == 0)
        {
            add_line(entry, context, first, static_cast<std::uint32_t>(entry.glyphs.size()), 0,
                     description.direction == ParagraphDirection::RIGHT_TO_LEFT);
            return;
        }

        // Levels and scripts, then the atlas of every character.
        bidi.analyze(paragraph, description.direction);
        const std::span<const std::uint8_t> levels = bidi.levels();
        const std::span<const std::uint32_t> scripts = bidi.scripts();
        const auto covers = [&](std::uint8_t atlas, std::uint32_t code_point)
        {
            return !is_drawn(static_cast<char32_t>(code_point)) || (*chain[atlas].atlas)->find_character(static_cast<char32_t>(code_point)).has_value();
        };
        atlas_indices.assign(length, 0);
        for (std::uint32_t index = 0; index < length; ++index)
        {
            const std::uint32_t code_point = paragraph[index];
            const bool sticky = index > 0 && (is_common_or_inherited(static_cast<char32_t>(code_point)) || !is_drawn(static_cast<char32_t>(code_point)));
            if (sticky && atlas_indices[index - 1] != NO_ATLAS && covers(atlas_indices[index - 1], code_point))
            {
                atlas_indices[index] = atlas_indices[index - 1];
                continue;
            }
            atlas_indices[index] = NO_ATLAS;
            for (std::uint8_t atlas = 0; atlas < chain.size(); ++atlas)
            {
                if (covers(atlas, code_point))
                {
                    atlas_indices[index] = atlas;
                    break;
                }
            }
        }

        // Runs of one level, script and atlas, each shaped.
        items.clear();
        shaped.clear();
        for (std::uint32_t index = 0; index < length; ++index)
        {
            const std::uint8_t atlas = atlas_indices[index] == NO_ATLAS ? 0 : atlas_indices[index];
            if (items.empty() || items.back().level != levels[index] || items.back().script != scripts[index] || items.back().atlas != atlas)
            {
                items.push_back({.begin = index, .end = index, .level = levels[index], .script = scripts[index], .atlas = atlas});
            }
            items.back().end = index + 1;
        }
        for (Item& item : items)
        {
            item.glyph_begin = static_cast<std::uint32_t>(shaped.size());
            shape(entry, item, paragraph, context);
            item.glyph_end = static_cast<std::uint32_t>(shaped.size());
        }

        // Logical advance of every character (a cluster's advance goes to its first character).
        advances.assign(length, 0.0F);
        cluster_starts.assign(length, 0);
        for (const ShapedGlyph& glyph : shaped)
        {
            advances[glyph.cluster] += glyph.advance;
            cluster_starts[glyph.cluster] = 1;
        }
        find_line_breaks(paragraph, context.font->language, line_breaks);
        find_grapheme_breaks(paragraph, context.font->language, grapheme_breaks);
        const auto boundary_after = [&](std::uint32_t index) { return index + 1 == length || cluster_starts[index + 1] != 0; };

        // Lines: greedy, at the last break opportunity before the width is exceeded, else at a grapheme
        // boundary (a word wider than the line), never inside a cluster.
        const bool wrapping = description.wrap_width > 0;
        std::uint32_t line_begin = 0;
        std::uint32_t index = 0;
        float width = 0;
        float trailing = 0;
        std::uint32_t opportunity = 0;
        while (index < length)
        {
            width += advances[index];
            trailing = is_whitespace(paragraph[index]) ? trailing + advances[index] : 0.0F;
            if (wrapping && width - trailing > description.wrap_width && index > line_begin)
            {
                std::uint32_t end = opportunity;
                if (end <= line_begin)
                {
                    // The last grapheme boundary before this character, keeping at least one grapheme.
                    end = index;
                    while (end > line_begin + 1 && !(grapheme_breaks[end - 1] == GRAPHEME_BOUNDARY && boundary_after(end - 1)))
                    {
                        --end;
                    }
                }
                emit_line(entry, context, paragraph, first, line_begin, end);
                line_begin = end;
                index = end;
                width = 0;
                trailing = 0;
                opportunity = 0;
                continue;
            }
            if (boundary_after(index) && index + 1 < length)
            {
                if (line_breaks[index] == LINE_BREAK_REQUIRED)
                {
                    emit_line(entry, context, paragraph, first, line_begin, index + 1);
                    line_begin = index + 1;
                    width = 0;
                    trailing = 0;
                    opportunity = 0;
                }
                else if (line_breaks[index] == LINE_BREAK_ALLOWED)
                {
                    opportunity = index + 1;
                }
            }
            ++index;
        }
        emit_line(entry, context, paragraph, first, line_begin, length);
    }

    /// Shapes @p item into the paragraph's glyph list.
    void shape(Entry& entry,
               const Item& item,
               std::span<const std::uint32_t> paragraph,
               const LayoutContext& context)
    {
        const std::shared_ptr<const FontAtlas>& atlas = *chain[item.atlas].atlas;
        const bool right_to_left = (item.level & 1U) != 0;
        bool use_harfbuzz = false;
        switch (context.font->shaping)
        {
            case TextShaping::BASIC:
                break;
            case TextShaping::HARFBUZZ:
                use_harfbuzz = true;
                break;
            case TextShaping::AUTOMATIC:
            {
                // Simple runs: left to right, a script of the basic shaper, no combining mark.
                bool simple = !right_to_left && is_basic_script(item.script);
                for (std::uint32_t index = item.begin; simple && index < item.end; ++index)
                {
                    simple = !is_mark(static_cast<char32_t>(paragraph[index]));
                }
                use_harfbuzz = !simple;
                break;
            }
        }
        if (use_harfbuzz && atlas->shaping_font().empty())
        {
            entry.problems |= TextLayoutProblem::NO_SHAPING_FONT;
            use_harfbuzz = false;
        }
        if (use_harfbuzz)
        {
            harfbuzz.shape(atlas, paragraph, item.begin, item.end, right_to_left, item.script, context.font->language, context.font->size, shaped);
        }
        else
        {
            shape_basic(*atlas, paragraph, item.begin, item.end, right_to_left, context.font->size, shaped);
        }
    }

    /// Adds a line of @p glyph_count glyphs (already in the entry) for the text at code point @p first_code_point.
    void add_line(Entry& entry,
                  const LayoutContext& context,
                  std::uint32_t first_code_point,
                  std::uint32_t first_glyph,
                  std::uint32_t glyph_count,
                  bool right_to_left)
    {
        const float top = static_cast<float>(entry.lines.size()) * context.line_height;
        entry.lines.push_back({.first_glyph = first_glyph,
                               .glyph_count = glyph_count,
                               .text_begin = offsets[first_code_point],
                               .text_end = offsets[first_code_point],
                               .top = top,
                               .baseline = top + context.ascender,
                               .height = context.line_height,
                               .right_to_left = right_to_left});
        line_shifts.push_back(0);
    }

    /// Places the glyphs of the line [@p begin, @p end) of the paragraph starting at code point @p first.
    void emit_line(Entry& entry,
                   const LayoutContext& context,
                   std::span<const std::uint32_t> paragraph,
                   std::uint32_t first,
                   std::uint32_t begin,
                   std::uint32_t end)
    {
        const std::uint32_t first_glyph = static_cast<std::uint32_t>(entry.glyphs.size());
        const bool right_to_left = (bidi.base_level() & 1U) != 0;
        const float baseline = static_cast<float>(entry.lines.size()) * context.line_height + context.ascender;

        // Runs in visual order; in a right-to-left run, its items from the last to the first.
        runs.clear();
        bidi.line_runs(begin, end, runs);
        float pen = 0;
        for (const VisualRun& run : runs)
        {
            const std::uint32_t run_begin = std::max(run.begin, begin);
            const std::uint32_t run_end = std::min(run.end, end);
            const auto place = [&](const Item& item)
            {
                if (item.end <= run_begin || item.begin >= run_end)
                {
                    return;
                }
                for (std::uint32_t index = item.glyph_begin; index < item.glyph_end; ++index)
                {
                    const ShapedGlyph& glyph = shaped[index];
                    const std::uint32_t code_point = paragraph[glyph.cluster];
                    if (glyph.cluster < run_begin || glyph.cluster >= run_end || !is_drawn(static_cast<char32_t>(code_point)))
                    {
                        continue;
                    }
                    if (glyph.glyph.is_notdef())
                    {
                        ++entry.missing_glyph_count;
                        entry.problems |= TextLayoutProblem::MISSING_GLYPHS;
                    }
                    entry.glyphs.push_back({.atlas = chain[item.atlas].handle,
                                            .glyph = glyph.glyph,
                                            .x = pen + glyph.x_offset,
                                            .y = baseline - glyph.y_offset,
                                            .advance = glyph.advance,
                                            .cluster = offsets[first + glyph.cluster],
                                            .right_to_left = (item.level & 1U) != 0});
                    pen += glyph.advance;
                }
            };
            if ((run.level & 1U) != 0)
            {
                std::ranges::for_each(items | std::views::reverse, place);
            }
            else
            {
                std::ranges::for_each(items, place);
            }
        }

        // Width without the trailing whitespace, which hangs: right of a left-to-right line, left of a
        // right-to-left one (the line is shifted so that its visible glyphs start at its left).
        float trailing = 0;
        for (std::uint32_t index = end; index > begin && is_whitespace(paragraph[index - 1]); --index)
        {
            trailing += advances[index - 1];
        }
        const float top = static_cast<float>(entry.lines.size()) * context.line_height;
        entry.lines.push_back({.first_glyph = first_glyph,
                               .glyph_count = static_cast<std::uint32_t>(entry.glyphs.size()) - first_glyph,
                               .text_begin = offsets[first + begin],
                               .text_end = offsets[first + end],
                               .width = pen - trailing,
                               .top = top,
                               .baseline = baseline,
                               .height = context.line_height,
                               .right_to_left = right_to_left});
        line_shifts.push_back(right_to_left ? trailing : 0.0F);
    }
};

TextLayoutEngine::TextLayoutEngine(const FontLibrary& fonts,
                                   TextLayoutEngineDescription description)
    : m_state{std::make_unique<State>(fonts, description.cache_capacity)}
{
}

TextLayoutEngine::~TextLayoutEngine() = default;

auto
TextLayoutEngine::layout(std::string_view text,
                         FontHandle font,
                         const TextLayoutDescription& description) noexcept -> TextLayout
{
    State& state = *m_state;
    const FontDescription* font_description = state.fonts.font(font);
    if (font_description == nullptr)
    {
        return {.problems = TextLayoutProblem::UNKNOWN_FONT};
    }

    // A hit: the cached layout, now the newest.
    const std::uint64_t hash = hash_of(text, font, description);
    if (const std::uint32_t index = state.find(hash, text, font, description); index != NO_ENTRY)
    {
        ++state.statistics.hits;
        state.use(index);
        return state.entries[index].view();
    }

    // A miss: made in the scratch entry, whose size then chooses the replaced or free entry it is copied into, or, when
    // every entry is in use this frame, copied into an overflow entry until the next frame (whose storage the next
    // frames reuse).
    ++state.statistics.misses;
    state.make(state.scratch, text, *font_description, description);
    const std::uint32_t index = state.acquire({.text = text.size(), .glyphs = state.scratch.glyphs.size(), .lines = state.scratch.lines.size()});
    if (index == NO_ENTRY)
    {
        ++state.statistics.overflows;
        if (state.overflow_used == state.overflow.size())
        {
            state.overflow.emplace_back();
        }
        Entry& overflow = state.overflow[state.overflow_used++];
        overflow.copy_layout(state.scratch);
        overflow.problems |= TextLayoutProblem::CACHE_FULL;
        return overflow.view();
    }
    Entry& entry = state.entries[index];
    reserve_size_class(entry.text, text.size());
    entry.text.assign(text);
    entry.font = font;
    entry.description = description;
    entry.hash = hash;
    entry.copy_layout(state.scratch);
    entry.next_in_bucket = state.buckets[hash & state.mask()];
    state.buckets[hash & state.mask()] = index;
    state.make_newest(index);
    return entry.view();
}

void
TextLayoutEngine::new_frame() noexcept
{
    ++m_state->frame;
    m_state->overflow_used = 0;
}

void
TextLayoutEngine::clear() noexcept
{
    // Every entry is forgotten; their storage is kept for the next layouts.
    State& state = *m_state;
    std::ranges::fill(state.buckets, NO_ENTRY);
    for (Entry& entry : state.entries)
    {
        entry.retained = false;
        entry.newer = NO_ENTRY;
        entry.older = NO_ENTRY;
        entry.next_in_bucket = NO_ENTRY;
    }
    state.used = 0;
    state.probation = {};
    state.retained = {};
    state.overflow_used = 0;
}

auto
TextLayoutEngine::statistics() const noexcept -> TextLayoutStatistics
{
    TextLayoutStatistics statistics = m_state->statistics;
    statistics.cached = m_state->used;
    return statistics;
}

// ================================================================================================= Carets

namespace
{

/// The line of @p layout (one at least) holding byte @p offset: the last line starting at or before it (the first
/// line otherwise). Lines are in text order: a binary search.
auto
line_of(const TextLayout& layout,
        std::uint32_t offset) noexcept -> std::size_t
{
    const std::span<const LaidOutLine> following = layout.lines.subspan(1);
    return static_cast<std::size_t>(std::ranges::upper_bound(following, offset, {}, &LaidOutLine::text_begin) - following.begin());
}

/// The glyphs of @p line, in visual order.
auto
glyphs_of(const TextLayout& layout,
          const LaidOutLine& line) noexcept -> std::span<const LaidOutGlyph>
{
    return layout.glyphs.subspan(line.first_glyph, line.glyph_count);
}

/// The byte where the cluster starting at @p cluster ends in @p glyphs (a line's): where the next larger cluster
/// starts, or @p text_end.
auto
cluster_end(std::span<const LaidOutGlyph> glyphs,
            std::uint32_t cluster,
            std::uint32_t text_end) noexcept -> std::uint32_t
{
    std::uint32_t end = text_end;
    for (const LaidOutGlyph& glyph : glyphs)
    {
        if (glyph.cluster > cluster && glyph.cluster < end)
        {
            end = glyph.cluster;
        }
    }
    return end;
}

} // namespace

auto
caret_position(const TextLayout& layout,
               std::uint32_t offset) noexcept -> CaretPosition
{
    if (layout.lines.empty())
    {
        return {};
    }
    const std::size_t line_index = line_of(layout, offset);
    const LaidOutLine& line = layout.lines[line_index];
    CaretPosition caret{.line = static_cast<std::uint32_t>(line_index), .top = line.top, .height = line.height};
    const std::span<const LaidOutGlyph> glyphs = glyphs_of(layout, line);

    // The cluster holding the offset (one pass): the largest cluster starting at or before it, which ends where the
    // smallest cluster starting after it starts (no cluster starts between the two), or at the line's end.
    std::optional<std::uint32_t> start{};
    std::uint32_t end = line.text_end;
    for (const LaidOutGlyph& glyph : glyphs)
    {
        if (glyph.cluster <= offset)
        {
            start = std::max(start.value_or(glyph.cluster), glyph.cluster);
        }
        else if (glyph.cluster < end)
        {
            end = glyph.cluster;
        }
    }

    // Inside a cluster: its share of the cluster's width (all its glyphs), from the side its direction starts on.
    if (start && offset < end)
    {
        float left = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        std::optional<bool> right_to_left{};
        for (const LaidOutGlyph& glyph : glyphs)
        {
            if (glyph.cluster == *start)
            {
                right_to_left = right_to_left.value_or(glyph.right_to_left);
                left = std::min(left, glyph.x);
                right = std::max(right, glyph.x + glyph.advance);
            }
        }
        const float fraction = static_cast<float>(offset - *start) / static_cast<float>(end - *start);
        caret.x = *right_to_left ? right - fraction * (right - left) : left + fraction * (right - left);
        return caret;
    }

    // After the line's last character: where its direction ends.
    caret.x = line.right_to_left ? line.left : line.left + line.width;
    return caret;
}

auto
hit_test(const TextLayout& layout,
         float x,
         float y) noexcept -> std::uint32_t
{
    if (layout.lines.empty())
    {
        return 0;
    }

    // The line under y, then the cluster under x: its start or its end, by the half x falls in.
    std::size_t line_index = 0;
    while (line_index + 1 < layout.lines.size() && y >= layout.lines[line_index + 1].top)
    {
        ++line_index;
    }
    const LaidOutLine& line = layout.lines[line_index];
    const std::span<const LaidOutGlyph> glyphs = glyphs_of(layout, line);

    // The cluster edge nearest x, each cluster measured once: a cluster's glyphs follow each other in the line's
    // visual order (clusters are monotonic within a run, and a cluster belongs to one run).
    const LaidOutGlyph* nearest = nullptr;
    bool nearest_right = false;
    float nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < glyphs.size();)
    {
        const LaidOutGlyph& first = glyphs[index];
        float left = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        for (; index < glyphs.size() && glyphs[index].cluster == first.cluster; ++index)
        {
            left = std::min(left, glyphs[index].x);
            right = std::max(right, glyphs[index].x + glyphs[index].advance);
        }
        for (const auto& [edge, is_right] : {std::pair{left, false}, std::pair{right, true}})
        {
            if (std::abs(edge - x) < nearest_distance)
            {
                nearest_distance = std::abs(edge - x);
                nearest = &first;
                nearest_right = is_right;
            }
        }
    }
    if (nearest == nullptr)
    {
        return line.right_to_left ? line.text_end : line.text_begin;
    }

    // The edge's offset: the cluster's start on the side its direction starts on, its end on the other.
    return nearest_right != nearest->right_to_left ? cluster_end(glyphs, nearest->cluster, line.text_end) : nearest->cluster;
}

} // namespace gfx::ui
