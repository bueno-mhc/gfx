/// @file atlas_format.cppm
/// @brief The `.uiatlas` format (docs/ui-atlas-format.md): its records, their little-endian encoding,
///        xxHash64, and AtlasView, a view of a validated blob that works at run time and in constant
///        expressions (`static_assert` on an atlas embedded at build time). The atlas tools write blobs
///        with the layouts of gfx::ui::atlas_layout; the font atlas of the UI context reads them.
export module gfx.ui.atlas_format;

import std;
import gfx.ui.enums;
import gfx.ui.handles;

export namespace gfx::ui
{

// =================================================================================================
// Byte layout of the format
// =================================================================================================

/// @brief Offsets and sizes of every record of the `.uiatlas` format, and the little-endian loads and
///        stores that read and write them. Writers of atlases (the atlas tools) and AtlasView share them,
///        so the format is defined once.
namespace atlas_layout
{

/// @brief A value the format stores: unsigned and signed integers of 8 to 64 bits, and 32-bit floats.
template<class Value>
concept StoredValue = std::same_as<Value, std::uint8_t> || std::same_as<Value, std::uint16_t>
                      || std::same_as<Value, std::uint32_t> || std::same_as<Value, std::uint64_t>
                      || std::same_as<Value, std::int32_t> || std::same_as<Value, float>;

/// @brief The little-endian value at @p offset of @p bytes (which must hold it).
template<StoredValue Value>
[[nodiscard]] constexpr auto
load(std::span<const std::byte> bytes,
     std::size_t offset) noexcept -> Value
{
    if constexpr (std::same_as<Value, float>)
    {
        return std::bit_cast<float>(load<std::uint32_t>(bytes, offset));
    }
    else if constexpr (std::same_as<Value, std::int32_t>)
    {
        return std::bit_cast<std::int32_t>(load<std::uint32_t>(bytes, offset));
    }
    else
    {
        // Assemble the bytes, least significant first.
        Value value = 0;
        for (std::size_t index = 0; index < sizeof(Value); ++index)
        {
            value |= static_cast<Value>(static_cast<Value>(std::to_integer<std::uint8_t>(bytes[offset + index])) << (8 * index));
        }
        return value;
    }
}

/// @brief Stores @p value little-endian at @p offset of @p bytes (which must have room for it).
template<StoredValue Value>
constexpr void
store(std::span<std::byte> bytes,
      std::size_t offset,
      Value value) noexcept
{
    if constexpr (std::same_as<Value, float>)
    {
        store(bytes, offset, std::bit_cast<std::uint32_t>(value));
    }
    else if constexpr (std::same_as<Value, std::int32_t>)
    {
        store(bytes, offset, std::bit_cast<std::uint32_t>(value));
    }
    else
    {
        // Least significant byte first.
        for (std::size_t index = 0; index < sizeof(Value); ++index)
        {
            bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 0xFFU);
        }
    }
}

/// @brief @p size rounded up to a multiple of UI_ATLAS_ALIGNMENT.
[[nodiscard]] constexpr auto
aligned(std::uint64_t size) noexcept -> std::uint64_t
{
    return (size + UI_ATLAS_ALIGNMENT - 1) / UI_ATLAS_ALIGNMENT * UI_ATLAS_ALIGNMENT;
}

/// @brief The header: the first UI_ATLAS_HEADER_SIZE bytes of a blob.
struct Header
{
    static constexpr std::size_t MAGIC = 0;                 ///< UI_ATLAS_MAGIC (8 bytes).
    static constexpr std::size_t VERSION_MAJOR = 8;         ///< uint16.
    static constexpr std::size_t VERSION_MINOR = 10;        ///< uint16.
    static constexpr std::size_t ENDIANNESS = 12;           ///< uint32 UI_ATLAS_ENDIANNESS_TAG.
    static constexpr std::size_t FLAGS = 16;                ///< uint32, none defined (0).
    static constexpr std::size_t SECTION_COUNT = 20;        ///< uint32.
    static constexpr std::size_t TOTAL_SIZE = 24;           ///< uint64: size of the blob.
    static constexpr std::size_t CHECKSUM = 32;             ///< uint64: xxHash64 of the bytes after the header.
    static constexpr std::size_t SOURCE_FONT_HASH = 40;     ///< uint64: xxHash64 of the font file.
    static constexpr std::size_t GENERATOR_VERSION = 48;    ///< uint32 UI_ATLAS_GENERATOR_VERSION.
    static constexpr std::size_t SECTION_TABLE_OFFSET = 52; ///< uint32: where the section table starts.
    static constexpr std::size_t SIZE = UI_ATLAS_HEADER_SIZE; ///< 56 to 63 are reserved (0).
};

/// @brief An entry of the section table.
struct SectionEntry
{
    static constexpr std::size_t KIND = 0;              ///< uint32 AtlasSectionKind (unknown kinds are skipped).
    static constexpr std::size_t COMPRESSION = 4;       ///< uint32 AtlasCompression.
    static constexpr std::size_t OFFSET = 8;            ///< uint64: from the start of the blob, aligned.
    static constexpr std::size_t STORED_SIZE = 16;      ///< uint64: bytes in the blob.
    static constexpr std::size_t UNCOMPRESSED_SIZE = 24; ///< uint64: bytes once decompressed.
    static constexpr std::size_t CHECKSUM = 32;         ///< uint64: xxHash64 of the stored bytes.
    static constexpr std::size_t SIZE = 40;
};

/// @brief The DESCRIPTOR section: the parameters the atlas was built with.
struct Descriptor
{
    static constexpr std::size_t KIND = 0;            ///< uint8 SdfKind.
    static constexpr std::size_t TEXEL_FORMAT = 1;    ///< uint8 AtlasTexelFormat.
    static constexpr std::size_t PACKER = 2;          ///< uint8 AtlasPackerKind.
    static constexpr std::size_t DECODER = 3;         ///< uint8 FontDecoderKind.
    static constexpr std::size_t GENERATOR = 4;       ///< uint8 SdfGeneratorKind.
    static constexpr std::size_t COLORING = 5;        ///< uint8 EdgeColoring.
    static constexpr std::size_t OPTIONS = 6;         ///< uint8: bit 0 overlap support, bit 1 scanline pass.
    static constexpr std::size_t PAGE_WIDTH = 8;      ///< uint32 texels.
    static constexpr std::size_t PAGE_HEIGHT = 12;    ///< uint32 texels.
    static constexpr std::size_t PAGE_COUNT = 16;     ///< uint32: the number of PIXELS sections.
    static constexpr std::size_t GLYPH_COUNT = 20;    ///< uint32: records of the GLYPHS section.
    static constexpr std::size_t EM_SIZE = 24;        ///< float: texels per em.
    static constexpr std::size_t DISTANCE_RANGE = 28; ///< float: texels between the field's extreme values.
    static constexpr std::size_t PADDING = 32;        ///< float: texels added around every glyph box.
    static constexpr std::size_t ANGLE_THRESHOLD = 36; ///< float: radians, edge coloring corners.
    static constexpr std::size_t MITER_LIMIT = 40;    ///< float.
    static constexpr std::size_t FACE_INDEX = 44;     ///< uint32: face of a collection.
    static constexpr std::size_t SPACING = 48;        ///< uint32 texels between boxes.
    static constexpr std::size_t SIZE = 64;           ///< 52 to 63 are reserved (0).
};

/// @brief The FACE_METRICS section: fixed fields (font units), then axis records, then the names.
struct FaceMetrics
{
    static constexpr std::size_t UNITS_PER_EM = 0;        ///< uint32.
    static constexpr std::size_t ASCENDER = 4;            ///< int32.
    static constexpr std::size_t DESCENDER = 8;           ///< int32 (negative below the baseline).
    static constexpr std::size_t LINE_GAP = 12;           ///< int32.
    static constexpr std::size_t X_HEIGHT = 16;           ///< int32.
    static constexpr std::size_t CAP_HEIGHT = 20;         ///< int32.
    static constexpr std::size_t UNDERLINE_POSITION = 24; ///< int32.
    static constexpr std::size_t UNDERLINE_THICKNESS = 28; ///< int32.
    static constexpr std::size_t AXIS_COUNT = 32;         ///< uint32: axis records after the fixed fields.
    static constexpr std::size_t FAMILY_SIZE = 36;        ///< uint32: bytes of the UTF-8 family name.
    static constexpr std::size_t STYLE_SIZE = 40;         ///< uint32: bytes of the UTF-8 style name.
    static constexpr std::size_t SIZE = 48;               ///< 44 to 47 are reserved (0).
};

/// @brief An axis record of FACE_METRICS: a variation axis and the value the atlas was built at.
struct AxisRecord
{
    static constexpr std::size_t TAG = 0;   ///< uint32 OpenType tag ('wght' = 0x77676874).
    static constexpr std::size_t VALUE = 4; ///< float, in the axis's design units.
    static constexpr std::size_t SIZE = 8;
};

/// @brief The start of every table section (GLYPHS, CHARACTER_MAP, KERNING, LIGATURES).
struct Table
{
    static constexpr std::size_t COUNT = 0;       ///< uint32: records.
    static constexpr std::size_t RECORD_SIZE = 4; ///< uint32: bytes per record (later minor versions may grow it).
    static constexpr std::size_t RECORDS = 8;     ///< The first record.
};

/// @brief A record of GLYPHS, sorted by glyph index.
struct GlyphRecord
{
    static constexpr std::size_t GLYPH = 0;        ///< uint32 glyph index.
    static constexpr std::size_t FLAGS = 4;        ///< uint16 AtlasGlyphFlag.
    static constexpr std::size_t PAGE = 6;         ///< uint16.
    static constexpr std::size_t ADVANCE = 8;      ///< float em.
    static constexpr std::size_t PLANE_LEFT = 12;  ///< float em, from the pen position.
    static constexpr std::size_t PLANE_BOTTOM = 16; ///< float em, y up from the baseline.
    static constexpr std::size_t PLANE_RIGHT = 20; ///< float em.
    static constexpr std::size_t PLANE_TOP = 24;   ///< float em.
    static constexpr std::size_t TEXEL_LEFT = 28;  ///< float texels from the page's left edge.
    static constexpr std::size_t TEXEL_TOP = 32;   ///< float texels down from the page's top edge.
    static constexpr std::size_t TEXEL_RIGHT = 36; ///< float texels.
    static constexpr std::size_t TEXEL_BOTTOM = 40; ///< float texels.
    static constexpr std::size_t SIZE = 48;        ///< 44 to 47 are reserved (0).
};

/// @brief A record of CHARACTER_MAP, sorted by code point.
struct CharacterRecord
{
    static constexpr std::size_t CODE_POINT = 0; ///< uint32.
    static constexpr std::size_t GLYPH = 4;      ///< uint32 glyph index.
    static constexpr std::size_t SIZE = 8;
};

/// @brief A record of KERNING, sorted by left then right glyph.
struct KerningRecord
{
    static constexpr std::size_t LEFT = 0;       ///< uint32 glyph index.
    static constexpr std::size_t RIGHT = 4;      ///< uint32 glyph index.
    static constexpr std::size_t ADJUSTMENT = 8; ///< float em added to the left glyph's advance.
    static constexpr std::size_t SIZE = 12;
};

/// @brief A record of LIGATURES, sorted by first component, then longest first, then the other components.
struct LigatureRecord
{
    static constexpr std::size_t COMPONENT_COUNT = 0; ///< uint32, 2 to MAXIMUM_LIGATURE_COMPONENTS.
    static constexpr std::size_t COMPONENTS = 4;      ///< uint32 glyph indices (unused ones are 0).
    static constexpr std::size_t GLYPH = 4 + 4 * MAXIMUM_LIGATURE_COMPONENTS; ///< uint32: the ligature.
    static constexpr std::size_t SIZE = GLYPH + 4;
};

/// @brief Bytes per texel of @p format.
[[nodiscard]] constexpr auto
texel_size(AtlasTexelFormat format) noexcept -> std::size_t
{
    switch (format)
    {
        case AtlasTexelFormat::R8_UNORM:            return 1;
        case AtlasTexelFormat::R8G8B8A8_UNORM:      return 4;
        case AtlasTexelFormat::R16_SFLOAT:          return 2;
        case AtlasTexelFormat::R16G16B16A16_SFLOAT: return 8;
    }
    return 0;
}

/// @brief Channels of the distance field @p kind (1, 3 or 4).
[[nodiscard]] constexpr auto
channel_count(SdfKind kind) noexcept -> std::size_t
{
    switch (kind)
    {
        case SdfKind::SDF:
        case SdfKind::PSDF:  return 1;
        case SdfKind::MSDF:  return 3;
        case SdfKind::MTSDF: return 4;
    }
    return 0;
}

/// @brief Whether pages of @p format can store a field of @p kind: one channel for SDF and PSDF, four
///        for MSDF (alpha unused) and MTSDF.
[[nodiscard]] constexpr auto
texel_format_fits(SdfKind kind,
                  AtlasTexelFormat format) noexcept -> bool
{
    const bool single_channel = format == AtlasTexelFormat::R8_UNORM || format == AtlasTexelFormat::R16_SFLOAT;
    return single_channel == (channel_count(kind) == 1);
}

} // namespace atlas_layout

// =================================================================================================
// xxHash64
// =================================================================================================

/// @brief XXH64 of @p bytes with @p seed (the xxHash algorithm by Yann Collet), the checksum of the
///        format: computed at run time by the writer and readers, and in constant expressions.
[[nodiscard]] constexpr auto
xxhash64(std::span<const std::byte> bytes,
         std::uint64_t seed = 0) noexcept -> std::uint64_t
{
    constexpr std::uint64_t PRIME_1 = 0x9E3779B185EBCA87ULL;
    constexpr std::uint64_t PRIME_2 = 0xC2B2AE3D27D4EB4FULL;
    constexpr std::uint64_t PRIME_3 = 0x165667B19E3779F9ULL;
    constexpr std::uint64_t PRIME_4 = 0x85EBCA77C2B2AE63ULL;
    constexpr std::uint64_t PRIME_5 = 0x27D4EB2F165667C5ULL;
    const auto round = [](std::uint64_t accumulator, std::uint64_t input) constexpr
    {
        accumulator += input * PRIME_2;
        return std::rotl(accumulator, 31) * PRIME_1;
    };
    const auto merge = [&](std::uint64_t accumulator, std::uint64_t value) constexpr
    {
        accumulator ^= round(0, value);
        return accumulator * PRIME_1 + PRIME_4;
    };
    const std::size_t size = bytes.size();
    std::size_t offset = 0;
    std::uint64_t hash = 0;

    // Stripes of 32 bytes into four lanes.
    if (size >= 32)
    {
        std::uint64_t lane_1 = seed + PRIME_1 + PRIME_2;
        std::uint64_t lane_2 = seed + PRIME_2;
        std::uint64_t lane_3 = seed;
        std::uint64_t lane_4 = seed - PRIME_1;
        for (; offset + 32 <= size; offset += 32)
        {
            lane_1 = round(lane_1, atlas_layout::load<std::uint64_t>(bytes, offset));
            lane_2 = round(lane_2, atlas_layout::load<std::uint64_t>(bytes, offset + 8));
            lane_3 = round(lane_3, atlas_layout::load<std::uint64_t>(bytes, offset + 16));
            lane_4 = round(lane_4, atlas_layout::load<std::uint64_t>(bytes, offset + 24));
        }
        hash = std::rotl(lane_1, 1) + std::rotl(lane_2, 7) + std::rotl(lane_3, 12) + std::rotl(lane_4, 18);
        hash = merge(hash, lane_1);
        hash = merge(hash, lane_2);
        hash = merge(hash, lane_3);
        hash = merge(hash, lane_4);
    }
    else
    {
        hash = seed + PRIME_5;
    }
    hash += size;

    // The remaining bytes: 8, then 4, then 1 at a time.
    for (; offset + 8 <= size; offset += 8)
    {
        hash ^= round(0, atlas_layout::load<std::uint64_t>(bytes, offset));
        hash = std::rotl(hash, 27) * PRIME_1 + PRIME_4;
    }
    if (offset + 4 <= size)
    {
        hash ^= static_cast<std::uint64_t>(atlas_layout::load<std::uint32_t>(bytes, offset)) * PRIME_1;
        hash = std::rotl(hash, 23) * PRIME_2 + PRIME_3;
        offset += 4;
    }
    for (; offset < size; ++offset)
    {
        hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) * PRIME_5;
        hash = std::rotl(hash, 11) * PRIME_1;
    }

    // Avalanche.
    hash ^= hash >> 33;
    hash *= PRIME_2;
    hash ^= hash >> 29;
    hash *= PRIME_3;
    hash ^= hash >> 32;
    return hash;
}

// =================================================================================================
// Records
// =================================================================================================

/// @brief The OpenType tag of four ASCII characters ("wght" → 0x77676874).
[[nodiscard]] constexpr auto
font_tag(std::string_view name) noexcept -> std::uint32_t
{
    std::uint32_t tag = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        const char character = index < name.size() ? name[index] : ' ';
        tag = (tag << 8) | static_cast<std::uint8_t>(character);
    }
    return tag;
}

/// @brief The four characters of the OpenType tag @p tag (0x77676874 → "wght").
[[nodiscard]] constexpr auto
font_tag_name(std::uint32_t tag) noexcept -> std::array<char, 4>
{
    return {static_cast<char>((tag >> 24) & 0xFFU), static_cast<char>((tag >> 16) & 0xFFU), static_cast<char>((tag >> 8) & 0xFFU),
            static_cast<char>(tag & 0xFFU)};
}

/// @brief The header of a blob, decoded.
struct AtlasHeader
{
    std::uint16_t version_major = 0;     ///< UI_ATLAS_VERSION_MAJOR for every blob a reader accepts.
    std::uint16_t version_minor = 0;     ///< Any minor version of the major one is accepted.
    std::uint32_t flags = 0;             ///< None defined.
    std::uint32_t section_count = 0;     ///< Entries of the section table.
    std::uint64_t total_size = 0;        ///< Bytes of the blob.
    std::uint64_t checksum = 0;          ///< xxHash64 of everything after the header.
    std::uint64_t source_font_hash = 0;  ///< xxHash64 of the font file the atlas was built from.
    std::uint32_t generator_version = 0; ///< UI_ATLAS_GENERATOR_VERSION of the tool that built it.
};

/// @brief An entry of the section table.
struct AtlasSection
{
    std::uint32_t kind_value = 0;                         ///< AtlasSectionKind, or a kind this reader does not know.
    AtlasCompression compression = AtlasCompression::NONE; ///< How the bytes are stored.
    std::uint64_t offset = 0;                             ///< From the start of the blob.
    std::uint64_t stored_size = 0;                        ///< Bytes in the blob.
    std::uint64_t uncompressed_size = 0;                  ///< Bytes once decompressed.
    std::uint64_t checksum = 0;                           ///< xxHash64 of the stored bytes.

    /// @brief The kind of the section, or nullopt for a kind of a later minor version.
    [[nodiscard]] constexpr auto kind() const noexcept -> std::optional<AtlasSectionKind>
    {
        if (kind_value < ENUMERATOR_COUNT<AtlasSectionKind>)
        {
            return static_cast<AtlasSectionKind>(kind_value);
        }
        return std::nullopt;
    }
};

/// @brief The parameters an atlas was built with (the DESCRIPTOR section).
struct AtlasDescriptor
{
    SdfKind kind = SdfKind::MTSDF;                                  ///< What the texels store.
    AtlasTexelFormat texel_format = AtlasTexelFormat::R8G8B8A8_UNORM; ///< Format of every page.
    AtlasPackerKind packer = AtlasPackerKind::TIGHT;                ///< How the boxes were placed.
    FontDecoderKind decoder = FontDecoderKind::FREETYPE;            ///< What read the outlines.
    SdfGeneratorKind generator = SdfGeneratorKind::MSDFGEN;         ///< What generated the fields.
    EdgeColoring coloring = EdgeColoring::INK_TRAP;                 ///< Edge coloring (MSDF, MTSDF).
    bool overlap_support = true;                                    ///< Overlapping contours were handled.
    bool scanline_pass = true;                                      ///< Signs were corrected by a scanline pass.
    std::uint32_t page_width = 0;                                   ///< Texels.
    std::uint32_t page_height = 0;                                  ///< Texels.
    std::uint32_t page_count = 0;                                   ///< PIXELS sections.
    std::uint32_t glyph_count = 0;                                  ///< Records of GLYPHS.
    float em_size = 0;                                              ///< Texels per em.
    float distance_range = 0;                                       ///< Texels between the field's extreme values.
    float padding = 0;                                              ///< Texels added around every box.
    float angle_threshold = 0;                                      ///< Radians.
    float miter_limit = 0;                                          ///< Of the boxes' miters.
    std::uint32_t face_index = 0;                                   ///< Face of a collection.
    std::uint32_t spacing = 0;                                      ///< Texels between boxes.
};

/// @brief UTF-8 text stored in a blob (a font name).
struct AtlasText
{
    std::span<const std::byte> bytes{}; ///< The UTF-8 bytes.

    /// @brief Whether the text is @p text.
    [[nodiscard]] constexpr auto operator==(std::string_view text) const noexcept -> bool
    {
        return std::ranges::equal(bytes, text, [](std::byte byte, char character)
        {
            return std::to_integer<std::uint8_t>(byte) == static_cast<std::uint8_t>(character);
        });
    }

    /// @brief A copy of the text.
    [[nodiscard]] auto to_string() const -> std::string
    {
        std::string text(bytes.size(), '\0');
        std::ranges::transform(bytes, text.begin(), [](std::byte byte) { return static_cast<char>(byte); });
        return text;
    }
};

/// @brief A variation axis of a font and the value an atlas was built at.
struct FontAxisValue
{
    std::uint32_t tag = 0; ///< OpenType tag (font_tag("wght")).
    float value = 0;       ///< In the axis's design units (wght: 100 to 900).

    /// @brief Axis values compare member by member.
    [[nodiscard]] constexpr auto operator==(const FontAxisValue&) const noexcept -> bool = default;
};

/// @brief The metrics and names of the face an atlas was built from (font units of FACE_METRICS).
struct AtlasFaceMetrics
{
    std::uint32_t units_per_em = 0;       ///< Font units per em.
    std::int32_t ascender = 0;            ///< Above the baseline.
    std::int32_t descender = 0;           ///< Below the baseline (negative).
    std::int32_t line_gap = 0;            ///< Added between lines.
    std::int32_t x_height = 0;            ///< Height of lowercase letters.
    std::int32_t cap_height = 0;          ///< Height of capital letters.
    std::int32_t underline_position = 0;  ///< Top of the underline (negative below the baseline).
    std::int32_t underline_thickness = 0; ///< Thickness of the underline.
    AtlasText family{};                   ///< Family name ("Noto Sans").
    AtlasText style{};                    ///< Style name ("Bold").
    std::uint32_t axis_count = 0;         ///< Axis values.
    std::span<const std::byte> axis_records{}; ///< The axis records (axis() decodes them).

    /// @brief Axis value @p index (below axis_count).
    [[nodiscard]] constexpr auto axis(std::size_t index) const noexcept -> FontAxisValue
    {
        const std::size_t offset = index * atlas_layout::AxisRecord::SIZE;
        return {.tag = atlas_layout::load<std::uint32_t>(axis_records, offset + atlas_layout::AxisRecord::TAG),
                .value = atlas_layout::load<float>(axis_records, offset + atlas_layout::AxisRecord::VALUE)};
    }
};

/// @brief Where a glyph's quad goes, in em units from the pen position on the baseline (y up).
struct GlyphPlaneBounds
{
    float left = 0;   ///< Left edge.
    float bottom = 0; ///< Bottom edge.
    float right = 0;  ///< Right edge.
    float top = 0;    ///< Top edge.
};

/// @brief What a glyph's quad samples, in texels of its page from the top-left corner (y down).
struct GlyphTexelBounds
{
    float left = 0;   ///< Left edge.
    float top = 0;    ///< Top edge.
    float right = 0;  ///< Right edge.
    float bottom = 0; ///< Bottom edge.
};

/// @brief A glyph of an atlas (a GLYPHS record).
struct AtlasGlyph
{
    GlyphIndex glyph{};                          ///< Index in the atlas's shaping font.
    AtlasGlyphFlag flags = AtlasGlyphFlag::NONE; ///< Whitespace, color layers, no outline.
    std::uint16_t page = 0;                      ///< Page whose texels hold it.
    float advance = 0;                           ///< Horizontal advance, em.
    GlyphPlaneBounds plane{};                    ///< Quad on the baseline, em.
    GlyphTexelBounds texels{};                   ///< Quad in the page, texels.

    /// @brief Whether the glyph has a box in the atlas (whitespace has none).
    [[nodiscard]] constexpr auto has_box() const noexcept -> bool
    {
        return texels.right > texels.left && texels.bottom > texels.top;
    }
};

/// @brief A code point and the glyph the font maps it to (a CHARACTER_MAP record).
struct CharacterMapping
{
    char32_t code_point = 0; ///< Unicode scalar value.
    GlyphIndex glyph{};      ///< Its glyph.

    /// @brief Mappings compare member by member.
    [[nodiscard]] constexpr auto operator==(const CharacterMapping&) const noexcept -> bool = default;
};

/// @brief A kerning adjustment between two glyphs (a KERNING record).
struct KerningPair
{
    GlyphIndex left{};     ///< First glyph in logical order.
    GlyphIndex right{};    ///< Glyph that follows it.
    float adjustment = 0;  ///< Em added to the left glyph's advance (negative: closer).

    /// @brief Pairs compare member by member.
    [[nodiscard]] constexpr auto operator==(const KerningPair&) const noexcept -> bool = default;
};

/// @brief A standard ligature: components replaced by one glyph (a LIGATURES record).
struct Ligature
{
    std::array<GlyphIndex, MAXIMUM_LIGATURE_COMPONENTS> components{}; ///< The glyphs it replaces, in order.
    std::uint32_t component_count = 0;                               ///< 2 to MAXIMUM_LIGATURE_COMPONENTS.
    GlyphIndex glyph{};                                              ///< The ligature glyph.

    /// @brief Ligatures compare member by member.
    [[nodiscard]] constexpr auto operator==(const Ligature&) const noexcept -> bool = default;
};

/// @brief Why a blob is not a valid atlas: the problem, where, and the values that disagree.
struct AtlasReadError
{
    AtlasProblem problem = AtlasProblem::TRUNCATED;  ///< What is wrong.
    std::optional<AtlasSectionKind> section{};       ///< The section concerned, when one is.
    std::uint64_t offset = 0;                        ///< Byte of the blob concerned.
    std::uint64_t expected = 0;                      ///< What a valid blob has there.
    std::uint64_t actual = 0;                        ///< What this blob has.
};

/// @brief An explanation of @p error, for an exception message or a log.
[[nodiscard]] auto describe_atlas_error(const AtlasReadError& error) -> std::string;

// =================================================================================================
// AtlasView
// =================================================================================================

/// @brief A validated `.uiatlas` blob, read in place: header, descriptor, face metrics, glyphs,
///        character map, kerning, ligatures, shaping font and pages. parse() checks the whole structure
///        (and, by default, every checksum), so the lookups never read out of bounds. Usable at run time
///        and in constant expressions; the blob must outlive the view.
class AtlasView
{
public:
    /// @brief An empty view (no blob).
    constexpr AtlasView() noexcept = default;

    /// @brief Validates @p blob: header (magic, version, endianness, size, checksum), section table (bounds,
    ///        alignment, compression, checksums), the required sections (DESCRIPTOR, FACE_METRICS, GLYPHS,
    ///        CHARACTER_MAP; one PIXELS section per page) and their content (sizes, ordering, glyph
    ///        references). Sections of unknown kinds are skipped.
    [[nodiscard]] static constexpr auto parse(std::span<const std::byte> blob,
                                              AtlasVerification verification = AtlasVerification::CHECKSUMS) noexcept
        -> std::expected<AtlasView, AtlasReadError>;

    /// @brief Refused: the view would outlive the temporary vector it reads (keep the blob in a variable).
    static auto parse(std::vector<std::byte>&& blob,
                      AtlasVerification verification = AtlasVerification::CHECKSUMS) -> std::expected<AtlasView, AtlasReadError> = delete;

    /// @brief The whole blob.
    [[nodiscard]] constexpr auto blob() const noexcept -> std::span<const std::byte>
    {
        return m_blob;
    }

    /// @brief The decoded header.
    [[nodiscard]] constexpr auto header() const noexcept -> const AtlasHeader&
    {
        return m_header;
    }

    /// @brief The build parameters.
    [[nodiscard]] constexpr auto descriptor() const noexcept -> const AtlasDescriptor&
    {
        return m_descriptor;
    }

    /// @brief Metrics and names of the face.
    [[nodiscard]] constexpr auto face_metrics() const noexcept -> AtlasFaceMetrics;

    /// @brief Entry @p index of the section table (below header().section_count).
    [[nodiscard]] constexpr auto section(std::size_t index) const noexcept -> AtlasSection
    {
        return read_section(m_blob, index);
    }

    /// @brief The @p occurrence-th section of @p kind (PIXELS has one per page), or nullopt.
    [[nodiscard]] constexpr auto find_section(AtlasSectionKind kind,
                                              std::size_t occurrence = 0) const noexcept -> std::optional<AtlasSection>;

    /// @brief The stored bytes of @p section (compressed when its compression is not NONE).
    [[nodiscard]] constexpr auto section_bytes(const AtlasSection& section) const noexcept -> std::span<const std::byte>
    {
        return m_blob.subspan(static_cast<std::size_t>(section.offset), static_cast<std::size_t>(section.stored_size));
    }

    /// @brief Glyphs of the atlas.
    [[nodiscard]] constexpr auto glyph_count() const noexcept -> std::size_t
    {
        return m_glyphs.count;
    }

    /// @brief Glyph @p index, in glyph order (below glyph_count()).
    [[nodiscard]] constexpr auto glyph_at(std::size_t index) const noexcept -> AtlasGlyph;

    /// @brief The record of @p glyph, or nullopt when the atlas lacks it.
    [[nodiscard]] constexpr auto find_glyph(GlyphIndex glyph) const noexcept -> std::optional<AtlasGlyph>;

    /// @brief Entries of the character map.
    [[nodiscard]] constexpr auto character_count() const noexcept -> std::size_t
    {
        return m_characters.count;
    }

    /// @brief Character map entry @p index, in code point order (below character_count()).
    [[nodiscard]] constexpr auto character_at(std::size_t index) const noexcept -> CharacterMapping;

    /// @brief The glyph of @p code_point, or nullopt when the atlas lacks the character.
    [[nodiscard]] constexpr auto find_character(char32_t code_point) const noexcept -> std::optional<GlyphIndex>;

    /// @brief Kerning pairs.
    [[nodiscard]] constexpr auto kerning_count() const noexcept -> std::size_t
    {
        return m_kerning.count;
    }

    /// @brief Kerning pair @p index (below kerning_count()).
    [[nodiscard]] constexpr auto kerning_at(std::size_t index) const noexcept -> KerningPair;

    /// @brief The adjustment between @p left and @p right, em (0 when the pair has none).
    [[nodiscard]] constexpr auto find_kerning(GlyphIndex left,
                                              GlyphIndex right) const noexcept -> float;

    /// @brief Standard ligatures.
    [[nodiscard]] constexpr auto ligature_count() const noexcept -> std::size_t
    {
        return m_ligatures.count;
    }

    /// @brief Ligature @p index (below ligature_count()).
    [[nodiscard]] constexpr auto ligature_at(std::size_t index) const noexcept -> Ligature;

    /// @brief The PIXELS section of page @p index (below descriptor().page_count).
    [[nodiscard]] constexpr auto page(std::size_t index) const noexcept -> AtlasSection
    {
        return find_section(AtlasSectionKind::PIXELS, index).value_or(AtlasSection{});
    }

    /// @brief The SHAPING_FONT section, or nullopt when the atlas was built without one.
    [[nodiscard]] constexpr auto shaping_font() const noexcept -> std::optional<AtlasSection>
    {
        return find_section(AtlasSectionKind::SHAPING_FONT);
    }

private:
    /// @brief Where a table section's records are.
    struct TableLocation
    {
        std::size_t records = 0;     ///< Offset of the first record in the blob.
        std::size_t count = 0;       ///< Records.
        std::size_t record_size = 0; ///< Bytes per record.
    };

    /// @brief Entry @p index of the section table of @p blob (in bounds).
    [[nodiscard]] static constexpr auto read_section(std::span<const std::byte> blob,
                                                     std::size_t index) noexcept -> AtlasSection;

    std::span<const std::byte> m_blob{};
    AtlasHeader m_header{};
    AtlasDescriptor m_descriptor{};
    std::size_t m_face_metrics = 0;
    std::size_t m_face_metrics_size = 0;
    TableLocation m_glyphs{};
    TableLocation m_characters{};
    TableLocation m_kerning{};
    TableLocation m_ligatures{};
};

// =================================================================================================
// AtlasView: definitions
// =================================================================================================

constexpr auto
AtlasView::read_section(std::span<const std::byte> blob,
                        std::size_t index) noexcept -> AtlasSection
{
    using atlas_layout::load;
    using atlas_layout::SectionEntry;
    const std::size_t entry = atlas_layout::load<std::uint32_t>(blob, atlas_layout::Header::SECTION_TABLE_OFFSET) + index * SectionEntry::SIZE;
    return {.kind_value = load<std::uint32_t>(blob, entry + SectionEntry::KIND),
            .compression = static_cast<AtlasCompression>(load<std::uint32_t>(blob, entry + SectionEntry::COMPRESSION)),
            .offset = load<std::uint64_t>(blob, entry + SectionEntry::OFFSET),
            .stored_size = load<std::uint64_t>(blob, entry + SectionEntry::STORED_SIZE),
            .uncompressed_size = load<std::uint64_t>(blob, entry + SectionEntry::UNCOMPRESSED_SIZE),
            .checksum = load<std::uint64_t>(blob, entry + SectionEntry::CHECKSUM)};
}

constexpr auto
AtlasView::parse(std::span<const std::byte> blob,
                 AtlasVerification verification) noexcept -> std::expected<AtlasView, AtlasReadError>
{
    using atlas_layout::Header;
    using atlas_layout::load;
    using Error = std::unexpected<AtlasReadError>;
    AtlasView view;
    view.m_blob = blob;

    // Header: magic, endianness, version, size, checksum.
    if (blob.size() < Header::SIZE)
    {
        return Error{{.problem = AtlasProblem::TRUNCATED, .expected = Header::SIZE, .actual = blob.size()}};
    }
    for (std::size_t index = 0; index < UI_ATLAS_MAGIC.size(); ++index)
    {
        if (std::to_integer<std::uint8_t>(blob[index]) != static_cast<std::uint8_t>(UI_ATLAS_MAGIC[index]))
        {
            return Error{{.problem = AtlasProblem::BAD_MAGIC, .offset = index}};
        }
    }
    const std::uint32_t endianness = load<std::uint32_t>(blob, Header::ENDIANNESS);
    if (endianness != UI_ATLAS_ENDIANNESS_TAG)
    {
        return Error{{.problem = AtlasProblem::WRONG_ENDIANNESS, .offset = Header::ENDIANNESS, .expected = UI_ATLAS_ENDIANNESS_TAG,
                      .actual = endianness}};
    }
    AtlasHeader& header = view.m_header;
    header = {.version_major = load<std::uint16_t>(blob, Header::VERSION_MAJOR),
              .version_minor = load<std::uint16_t>(blob, Header::VERSION_MINOR),
              .flags = load<std::uint32_t>(blob, Header::FLAGS),
              .section_count = load<std::uint32_t>(blob, Header::SECTION_COUNT),
              .total_size = load<std::uint64_t>(blob, Header::TOTAL_SIZE),
              .checksum = load<std::uint64_t>(blob, Header::CHECKSUM),
              .source_font_hash = load<std::uint64_t>(blob, Header::SOURCE_FONT_HASH),
              .generator_version = load<std::uint32_t>(blob, Header::GENERATOR_VERSION)};
    if (header.version_major != UI_ATLAS_VERSION_MAJOR)
    {
        return Error{{.problem = AtlasProblem::UNSUPPORTED_VERSION, .offset = Header::VERSION_MAJOR, .expected = UI_ATLAS_VERSION_MAJOR,
                      .actual = header.version_major}};
    }
    if (header.total_size > blob.size())
    {
        return Error{{.problem = AtlasProblem::TRUNCATED, .offset = Header::TOTAL_SIZE, .expected = header.total_size, .actual = blob.size()}};
    }
    if (header.total_size != blob.size() || header.total_size % UI_ATLAS_ALIGNMENT != 0)
    {
        return Error{{.problem = AtlasProblem::SIZE_MISMATCH, .offset = Header::TOTAL_SIZE, .expected = blob.size(), .actual = header.total_size}};
    }
    if (verification == AtlasVerification::CHECKSUMS)
    {
        const std::uint64_t checksum = xxhash64(blob.subspan(Header::SIZE));
        if (checksum != header.checksum)
        {
            return Error{{.problem = AtlasProblem::CHECKSUM_MISMATCH, .offset = Header::CHECKSUM, .expected = checksum, .actual = header.checksum}};
        }
    }

    // Section table: bounds of the table and of every section, alignment, compression, checksums.
    const std::uint64_t table = load<std::uint32_t>(blob, Header::SECTION_TABLE_OFFSET);
    const std::uint64_t table_end = table + std::uint64_t{header.section_count} * atlas_layout::SectionEntry::SIZE;
    if (table < Header::SIZE || table % UI_ATLAS_ALIGNMENT != 0 || table_end > blob.size())
    {
        return Error{{.problem = AtlasProblem::SECTION_OUT_OF_BOUNDS, .offset = Header::SECTION_TABLE_OFFSET, .expected = blob.size(),
                      .actual = table_end}};
    }
    std::array<std::size_t, ENUMERATOR_COUNT<AtlasSectionKind>> occurrences{};
    std::array<AtlasSection, ENUMERATOR_COUNT<AtlasSectionKind>> first{};
    for (std::size_t index = 0; index < header.section_count; ++index)
    {
        const AtlasSection section = read_section(blob, index);
        const std::uint64_t entry = table + index * atlas_layout::SectionEntry::SIZE;
        const std::optional<AtlasSectionKind> kind = section.kind();
        if (section.offset % UI_ATLAS_ALIGNMENT != 0)
        {
            return Error{{.problem = AtlasProblem::MISALIGNED_SECTION, .section = kind, .offset = entry, .actual = section.offset}};
        }
        if (section.offset < table_end || section.stored_size > blob.size() || section.offset > blob.size() - section.stored_size)
        {
            return Error{{.problem = AtlasProblem::SECTION_OUT_OF_BOUNDS, .section = kind, .offset = entry, .expected = blob.size(),
                          .actual = section.offset + section.stored_size}};
        }
        if (std::to_underlying(section.compression) >= ENUMERATOR_COUNT<AtlasCompression>)
        {
            return Error{{.problem = AtlasProblem::UNKNOWN_COMPRESSION, .section = kind, .offset = entry,
                          .actual = std::to_underlying(section.compression)}};
        }
        if (verification == AtlasVerification::CHECKSUMS)
        {
            const std::uint64_t checksum = xxhash64(view.section_bytes(section));
            if (checksum != section.checksum)
            {
                return Error{{.problem = AtlasProblem::SECTION_CHECKSUM_MISMATCH, .section = kind, .offset = entry, .expected = checksum,
                              .actual = section.checksum}};
            }
        }
        if (!kind.has_value())
        {
            continue;
        }
        const std::size_t slot = std::to_underlying(*kind);
        if (occurrences[slot]++ == 0)
        {
            first[slot] = section;
        }
        else if (*kind != AtlasSectionKind::PIXELS)
        {
            return Error{{.problem = AtlasProblem::DUPLICATE_SECTION, .section = kind, .offset = entry}};
        }
    }

    // Required sections; tables are never compressed (they are read in place).
    for (const AtlasSectionKind kind : {AtlasSectionKind::DESCRIPTOR, AtlasSectionKind::FACE_METRICS, AtlasSectionKind::GLYPHS,
                                        AtlasSectionKind::CHARACTER_MAP})
    {
        if (occurrences[std::to_underlying(kind)] == 0)
        {
            return Error{{.problem = AtlasProblem::MISSING_SECTION, .section = kind}};
        }
    }
    const auto malformed = [](AtlasSectionKind kind, std::uint64_t offset, std::uint64_t expected, std::uint64_t actual) constexpr
    {
        return Error{{.problem = AtlasProblem::MALFORMED_SECTION, .section = kind, .offset = offset, .expected = expected, .actual = actual}};
    };
    for (const AtlasSectionKind kind : {AtlasSectionKind::DESCRIPTOR, AtlasSectionKind::FACE_METRICS, AtlasSectionKind::GLYPHS,
                                        AtlasSectionKind::CHARACTER_MAP, AtlasSectionKind::KERNING, AtlasSectionKind::LIGATURES})
    {
        const AtlasSection& section = first[std::to_underlying(kind)];
        if (occurrences[std::to_underlying(kind)] != 0
            && (section.compression != AtlasCompression::NONE || section.uncompressed_size != section.stored_size))
        {
            return malformed(kind, section.offset, std::to_underlying(AtlasCompression::NONE), std::to_underlying(section.compression));
        }
    }

    // Descriptor: values in range, texel format fitting the field kind, one PIXELS section per page.
    {
        using atlas_layout::Descriptor;
        const AtlasSection& section = first[std::to_underlying(AtlasSectionKind::DESCRIPTOR)];
        const std::size_t at = static_cast<std::size_t>(section.offset);
        if (section.stored_size < Descriptor::SIZE)
        {
            return malformed(AtlasSectionKind::DESCRIPTOR, section.offset, Descriptor::SIZE, section.stored_size);
        }
        const auto byte = [&](std::size_t field) constexpr { return load<std::uint8_t>(blob, at + field); };
        if (byte(Descriptor::KIND) >= ENUMERATOR_COUNT<SdfKind> || byte(Descriptor::TEXEL_FORMAT) >= ENUMERATOR_COUNT<AtlasTexelFormat>
            || byte(Descriptor::PACKER) >= ENUMERATOR_COUNT<AtlasPackerKind> || byte(Descriptor::DECODER) >= ENUMERATOR_COUNT<FontDecoderKind>
            || byte(Descriptor::GENERATOR) >= ENUMERATOR_COUNT<SdfGeneratorKind> || byte(Descriptor::COLORING) >= ENUMERATOR_COUNT<EdgeColoring>)
        {
            return malformed(AtlasSectionKind::DESCRIPTOR, section.offset, 0, 0);
        }
        AtlasDescriptor& descriptor = view.m_descriptor;
        descriptor = {.kind = static_cast<SdfKind>(byte(Descriptor::KIND)),
                      .texel_format = static_cast<AtlasTexelFormat>(byte(Descriptor::TEXEL_FORMAT)),
                      .packer = static_cast<AtlasPackerKind>(byte(Descriptor::PACKER)),
                      .decoder = static_cast<FontDecoderKind>(byte(Descriptor::DECODER)),
                      .generator = static_cast<SdfGeneratorKind>(byte(Descriptor::GENERATOR)),
                      .coloring = static_cast<EdgeColoring>(byte(Descriptor::COLORING)),
                      .overlap_support = (byte(Descriptor::OPTIONS) & 1U) != 0,
                      .scanline_pass = (byte(Descriptor::OPTIONS) & 2U) != 0,
                      .page_width = load<std::uint32_t>(blob, at + Descriptor::PAGE_WIDTH),
                      .page_height = load<std::uint32_t>(blob, at + Descriptor::PAGE_HEIGHT),
                      .page_count = load<std::uint32_t>(blob, at + Descriptor::PAGE_COUNT),
                      .glyph_count = load<std::uint32_t>(blob, at + Descriptor::GLYPH_COUNT),
                      .em_size = load<float>(blob, at + Descriptor::EM_SIZE),
                      .distance_range = load<float>(blob, at + Descriptor::DISTANCE_RANGE),
                      .padding = load<float>(blob, at + Descriptor::PADDING),
                      .angle_threshold = load<float>(blob, at + Descriptor::ANGLE_THRESHOLD),
                      .miter_limit = load<float>(blob, at + Descriptor::MITER_LIMIT),
                      .face_index = load<std::uint32_t>(blob, at + Descriptor::FACE_INDEX),
                      .spacing = load<std::uint32_t>(blob, at + Descriptor::SPACING)};
        if (!atlas_layout::texel_format_fits(descriptor.kind, descriptor.texel_format))
        {
            return malformed(AtlasSectionKind::DESCRIPTOR, section.offset + Descriptor::TEXEL_FORMAT, 0, std::to_underlying(descriptor.texel_format));
        }
        if (occurrences[std::to_underlying(AtlasSectionKind::PIXELS)] != descriptor.page_count)
        {
            return malformed(AtlasSectionKind::PIXELS, section.offset + Descriptor::PAGE_COUNT, descriptor.page_count,
                             occurrences[std::to_underlying(AtlasSectionKind::PIXELS)]);
        }
        const std::uint64_t page_size = std::uint64_t{descriptor.page_width} * descriptor.page_height
                                        * atlas_layout::texel_size(descriptor.texel_format);
        for (std::size_t page = 0; page < descriptor.page_count; ++page)
        {
            const AtlasSection pixels = view.page(page);
            if (pixels.uncompressed_size != page_size || (pixels.compression == AtlasCompression::NONE && pixels.stored_size != page_size))
            {
                return malformed(AtlasSectionKind::PIXELS, pixels.offset, page_size, pixels.uncompressed_size);
            }
        }
    }

    // Face metrics: fixed fields, axis records and names inside the section.
    {
        using atlas_layout::FaceMetrics;
        const AtlasSection& section = first[std::to_underlying(AtlasSectionKind::FACE_METRICS)];
        const std::size_t at = static_cast<std::size_t>(section.offset);
        if (section.stored_size < FaceMetrics::SIZE)
        {
            return malformed(AtlasSectionKind::FACE_METRICS, section.offset, FaceMetrics::SIZE, section.stored_size);
        }
        const std::uint64_t size = FaceMetrics::SIZE
                                   + std::uint64_t{load<std::uint32_t>(blob, at + FaceMetrics::AXIS_COUNT)} * atlas_layout::AxisRecord::SIZE
                                   + load<std::uint32_t>(blob, at + FaceMetrics::FAMILY_SIZE)
                                   + load<std::uint32_t>(blob, at + FaceMetrics::STYLE_SIZE);
        if (size > section.stored_size || load<std::uint32_t>(blob, at + FaceMetrics::UNITS_PER_EM) == 0)
        {
            return malformed(AtlasSectionKind::FACE_METRICS, section.offset, section.stored_size, size);
        }
        view.m_face_metrics = at;
        view.m_face_metrics_size = static_cast<std::size_t>(section.stored_size);
    }

    // Tables: record sizes and counts inside their sections.
    const auto locate = [&](AtlasSectionKind kind, std::size_t minimum_record_size) constexpr -> std::expected<TableLocation, AtlasReadError>
    {
        using atlas_layout::Table;
        if (occurrences[std::to_underlying(kind)] == 0)
        {
            return TableLocation{};
        }
        const AtlasSection& section = first[std::to_underlying(kind)];
        const std::size_t at = static_cast<std::size_t>(section.offset);
        if (section.stored_size < Table::RECORDS)
        {
            return malformed(kind, section.offset, Table::RECORDS, section.stored_size);
        }
        const std::uint64_t count = load<std::uint32_t>(blob, at + Table::COUNT);
        const std::uint64_t record_size = load<std::uint32_t>(blob, at + Table::RECORD_SIZE);
        if (record_size < minimum_record_size || Table::RECORDS + count * record_size > section.stored_size)
        {
            return malformed(kind, section.offset, section.stored_size, Table::RECORDS + count * record_size);
        }
        return TableLocation{.records = at + Table::RECORDS, .count = static_cast<std::size_t>(count),
                             .record_size = static_cast<std::size_t>(record_size)};
    };
    const auto glyphs = locate(AtlasSectionKind::GLYPHS, atlas_layout::GlyphRecord::SIZE);
    const auto characters = locate(AtlasSectionKind::CHARACTER_MAP, atlas_layout::CharacterRecord::SIZE);
    const auto kerning = locate(AtlasSectionKind::KERNING, atlas_layout::KerningRecord::SIZE);
    const auto ligatures = locate(AtlasSectionKind::LIGATURES, atlas_layout::LigatureRecord::SIZE);
    for (const auto* located : {&glyphs, &characters, &kerning, &ligatures})
    {
        if (!located->has_value())
        {
            return Error{located->error()};
        }
    }
    view.m_glyphs = *glyphs;
    view.m_characters = *characters;
    view.m_kerning = *kerning;
    view.m_ligatures = *ligatures;
    if (view.m_glyphs.count != view.m_descriptor.glyph_count)
    {
        return malformed(AtlasSectionKind::GLYPHS, view.m_glyphs.records, view.m_descriptor.glyph_count, view.m_glyphs.count);
    }

    // Ordering (binary searches rely on it) and references: pages of glyphs, glyphs of the other tables.
    for (std::size_t index = 0; index < view.glyph_count(); ++index)
    {
        const AtlasGlyph glyph = view.glyph_at(index);
        if ((index > 0 && view.glyph_at(index - 1).glyph >= glyph.glyph) || (glyph.has_box() && glyph.page >= view.m_descriptor.page_count))
        {
            return malformed(AtlasSectionKind::GLYPHS, view.m_glyphs.records + index * view.m_glyphs.record_size, 0, glyph.glyph.value());
        }
    }
    for (std::size_t index = 0; index < view.character_count(); ++index)
    {
        const CharacterMapping mapping = view.character_at(index);
        if ((index > 0 && view.character_at(index - 1).code_point >= mapping.code_point) || !view.find_glyph(mapping.glyph).has_value())
        {
            return malformed(AtlasSectionKind::CHARACTER_MAP, view.m_characters.records + index * view.m_characters.record_size, 0,
                             mapping.code_point);
        }
    }
    for (std::size_t index = 0; index < view.kerning_count(); ++index)
    {
        const KerningPair pair = view.kerning_at(index);
        const bool ordered = index == 0 || std::pair{view.kerning_at(index - 1).left, view.kerning_at(index - 1).right} < std::pair{pair.left, pair.right};
        if (!ordered || !view.find_glyph(pair.left).has_value() || !view.find_glyph(pair.right).has_value())
        {
            return malformed(AtlasSectionKind::KERNING, view.m_kerning.records + index * view.m_kerning.record_size, 0, pair.left.value());
        }
    }
    for (std::size_t index = 0; index < view.ligature_count(); ++index)
    {
        const Ligature ligature = view.ligature_at(index);
        bool valid = ligature.component_count >= 2 && ligature.component_count <= MAXIMUM_LIGATURE_COMPONENTS
                     && view.find_glyph(ligature.glyph).has_value();
        for (std::size_t component = 0; valid && component < ligature.component_count; ++component)
        {
            valid = view.find_glyph(ligature.components[component]).has_value();
        }
        if (valid && index > 0)
        {
            // First component ascending, then longest first.
            const Ligature previous = view.ligature_at(index - 1);
            valid = previous.components[0] < ligature.components[0]
                    || (previous.components[0] == ligature.components[0] && previous.component_count >= ligature.component_count);
        }
        if (!valid)
        {
            return malformed(AtlasSectionKind::LIGATURES, view.m_ligatures.records + index * view.m_ligatures.record_size, 0,
                             ligature.glyph.value());
        }
    }
    return view;
}

constexpr auto
AtlasView::face_metrics() const noexcept -> AtlasFaceMetrics
{
    using atlas_layout::FaceMetrics;
    using atlas_layout::load;
    const std::size_t at = m_face_metrics;
    const std::uint32_t axis_count = load<std::uint32_t>(m_blob, at + FaceMetrics::AXIS_COUNT);
    const std::uint32_t family_size = load<std::uint32_t>(m_blob, at + FaceMetrics::FAMILY_SIZE);
    const std::uint32_t style_size = load<std::uint32_t>(m_blob, at + FaceMetrics::STYLE_SIZE);
    const std::size_t axes = at + FaceMetrics::SIZE;
    const std::size_t family = axes + std::size_t{axis_count} * atlas_layout::AxisRecord::SIZE;
    return {.units_per_em = load<std::uint32_t>(m_blob, at + FaceMetrics::UNITS_PER_EM),
            .ascender = load<std::int32_t>(m_blob, at + FaceMetrics::ASCENDER),
            .descender = load<std::int32_t>(m_blob, at + FaceMetrics::DESCENDER),
            .line_gap = load<std::int32_t>(m_blob, at + FaceMetrics::LINE_GAP),
            .x_height = load<std::int32_t>(m_blob, at + FaceMetrics::X_HEIGHT),
            .cap_height = load<std::int32_t>(m_blob, at + FaceMetrics::CAP_HEIGHT),
            .underline_position = load<std::int32_t>(m_blob, at + FaceMetrics::UNDERLINE_POSITION),
            .underline_thickness = load<std::int32_t>(m_blob, at + FaceMetrics::UNDERLINE_THICKNESS),
            .family = {m_blob.subspan(family, family_size)},
            .style = {m_blob.subspan(family + family_size, style_size)},
            .axis_count = axis_count,
            .axis_records = m_blob.subspan(axes, std::size_t{axis_count} * atlas_layout::AxisRecord::SIZE)};
}

constexpr auto
AtlasView::find_section(AtlasSectionKind kind,
                        std::size_t occurrence) const noexcept -> std::optional<AtlasSection>
{
    for (std::size_t index = 0; index < m_header.section_count; ++index)
    {
        const AtlasSection section = read_section(m_blob, index);
        if (section.kind() == kind && occurrence-- == 0)
        {
            return section;
        }
    }
    return std::nullopt;
}

constexpr auto
AtlasView::glyph_at(std::size_t index) const noexcept -> AtlasGlyph
{
    using atlas_layout::GlyphRecord;
    using atlas_layout::load;
    const std::size_t at = m_glyphs.records + index * m_glyphs.record_size;
    return {.glyph = GlyphIndex{load<std::uint32_t>(m_blob, at + GlyphRecord::GLYPH)},
            .flags = static_cast<AtlasGlyphFlag>(load<std::uint16_t>(m_blob, at + GlyphRecord::FLAGS)),
            .page = load<std::uint16_t>(m_blob, at + GlyphRecord::PAGE),
            .advance = load<float>(m_blob, at + GlyphRecord::ADVANCE),
            .plane = {.left = load<float>(m_blob, at + GlyphRecord::PLANE_LEFT),
                      .bottom = load<float>(m_blob, at + GlyphRecord::PLANE_BOTTOM),
                      .right = load<float>(m_blob, at + GlyphRecord::PLANE_RIGHT),
                      .top = load<float>(m_blob, at + GlyphRecord::PLANE_TOP)},
            .texels = {.left = load<float>(m_blob, at + GlyphRecord::TEXEL_LEFT),
                       .top = load<float>(m_blob, at + GlyphRecord::TEXEL_TOP),
                       .right = load<float>(m_blob, at + GlyphRecord::TEXEL_RIGHT),
                       .bottom = load<float>(m_blob, at + GlyphRecord::TEXEL_BOTTOM)}};
}

constexpr auto
AtlasView::find_glyph(GlyphIndex glyph) const noexcept -> std::optional<AtlasGlyph>
{
    // Binary search over the records, sorted by glyph index.
    std::size_t low = 0;
    std::size_t high = m_glyphs.count;
    while (low < high)
    {
        const std::size_t middle = low + (high - low) / 2;
        const std::uint32_t value = atlas_layout::load<std::uint32_t>(m_blob, m_glyphs.records + middle * m_glyphs.record_size);
        if (value < glyph.value())
        {
            low = middle + 1;
        }
        else if (value > glyph.value())
        {
            high = middle;
        }
        else
        {
            return glyph_at(middle);
        }
    }
    return std::nullopt;
}

constexpr auto
AtlasView::character_at(std::size_t index) const noexcept -> CharacterMapping
{
    using atlas_layout::CharacterRecord;
    const std::size_t at = m_characters.records + index * m_characters.record_size;
    return {.code_point = static_cast<char32_t>(atlas_layout::load<std::uint32_t>(m_blob, at + CharacterRecord::CODE_POINT)),
            .glyph = GlyphIndex{atlas_layout::load<std::uint32_t>(m_blob, at + CharacterRecord::GLYPH)}};
}

constexpr auto
AtlasView::find_character(char32_t code_point) const noexcept -> std::optional<GlyphIndex>
{
    // Binary search over the records, sorted by code point.
    std::size_t low = 0;
    std::size_t high = m_characters.count;
    while (low < high)
    {
        const std::size_t middle = low + (high - low) / 2;
        const CharacterMapping mapping = character_at(middle);
        if (mapping.code_point < code_point)
        {
            low = middle + 1;
        }
        else if (mapping.code_point > code_point)
        {
            high = middle;
        }
        else
        {
            return mapping.glyph;
        }
    }
    return std::nullopt;
}

constexpr auto
AtlasView::kerning_at(std::size_t index) const noexcept -> KerningPair
{
    using atlas_layout::KerningRecord;
    using atlas_layout::load;
    const std::size_t at = m_kerning.records + index * m_kerning.record_size;
    return {.left = GlyphIndex{load<std::uint32_t>(m_blob, at + KerningRecord::LEFT)},
            .right = GlyphIndex{load<std::uint32_t>(m_blob, at + KerningRecord::RIGHT)},
            .adjustment = load<float>(m_blob, at + KerningRecord::ADJUSTMENT)};
}

constexpr auto
AtlasView::find_kerning(GlyphIndex left,
                        GlyphIndex right) const noexcept -> float
{
    // Binary search over the pairs, sorted by left then right glyph.
    std::size_t low = 0;
    std::size_t high = m_kerning.count;
    while (low < high)
    {
        const std::size_t middle = low + (high - low) / 2;
        const KerningPair pair = kerning_at(middle);
        if (std::pair{pair.left, pair.right} < std::pair{left, right})
        {
            low = middle + 1;
        }
        else if (std::pair{left, right} < std::pair{pair.left, pair.right})
        {
            high = middle;
        }
        else
        {
            return pair.adjustment;
        }
    }
    return 0.0F;
}

constexpr auto
AtlasView::ligature_at(std::size_t index) const noexcept -> Ligature
{
    using atlas_layout::LigatureRecord;
    using atlas_layout::load;
    const std::size_t at = m_ligatures.records + index * m_ligatures.record_size;
    Ligature ligature{.component_count = load<std::uint32_t>(m_blob, at + LigatureRecord::COMPONENT_COUNT),
                      .glyph = GlyphIndex{load<std::uint32_t>(m_blob, at + LigatureRecord::GLYPH)}};
    for (std::size_t component = 0; component < MAXIMUM_LIGATURE_COMPONENTS; ++component)
    {
        ligature.components[component] = GlyphIndex{load<std::uint32_t>(m_blob, at + LigatureRecord::COMPONENTS + 4 * component)};
    }
    return ligature;
}

} // namespace gfx::ui
