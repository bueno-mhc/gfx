/// @file atlas_writer.cpp
/// @brief write_atlas(): the `.uiatlas` serializer. Sections are written in a fixed order (DESCRIPTOR,
///        FACE_METRICS, GLYPHS, CHARACTER_MAP, KERNING, LIGATURES, SHAPING_FONT, PIXELS per page), each
///        at an 8-byte boundary; the result is parsed back with AtlasView before it is returned.
module;

#include <zstd.h>

module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import gfx.ui.atlas_format;
import :implementations;

namespace gfx::ui
{

namespace
{

using atlas_layout::store;

/// A section before it is placed in the blob.
struct PendingSection
{
    AtlasSectionKind kind = AtlasSectionKind::DESCRIPTOR;
    AtlasCompression compression = AtlasCompression::NONE;
    std::vector<std::byte> stored{};
    std::uint64_t uncompressed_size = 0;
};

/// Frees a zstd compression context.
struct CompressionContextDeleter
{
    void operator()(ZSTD_CCtx* context) const noexcept
    {
        ZSTD_freeCCtx(context);
    }
};

/// @p bytes in one zstd frame at @p level (single-threaded, no checksum: the format has its own).
auto
compress(std::span<const std::byte> bytes,
         int level) -> std::vector<std::byte>
{
    const std::unique_ptr<ZSTD_CCtx, CompressionContextDeleter> context{ZSTD_createCCtx()};
    ZSTD_CCtx_setParameter(context.get(), ZSTD_c_compressionLevel, level);
    ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 0);
    ZSTD_CCtx_setParameter(context.get(), ZSTD_c_contentSizeFlag, 1);
    std::vector<std::byte> compressed(ZSTD_compressBound(bytes.size()));
    const std::size_t size = ZSTD_compress2(context.get(), compressed.data(), compressed.size(), bytes.data(), bytes.size());
    if (ZSTD_isError(size) != 0)
    {
        throw BackendApiError({.backend = UiSubsystem::FONT,
                               .operation = "write_atlas",
                               .api_function = "ZSTD_compress2",
                               .result_code = static_cast<std::int64_t>(size),
                               .result_name = ZSTD_getErrorName(size)});
    }
    compressed.resize(size);
    return compressed;
}

/// A section stored as is.
auto
uncompressed(AtlasSectionKind kind,
             std::vector<std::byte> bytes) -> PendingSection
{
    const std::uint64_t size = bytes.size();
    return {.kind = kind, .compression = AtlasCompression::NONE, .stored = std::move(bytes), .uncompressed_size = size};
}

/// A section compressed as @p compression asks.
auto
compressed(AtlasSectionKind kind,
           std::span<const std::byte> bytes,
           AtlasCompression compression,
           int level) -> PendingSection
{
    if (compression == AtlasCompression::ZSTD)
    {
        return {.kind = kind, .compression = compression, .stored = compress(bytes, level), .uncompressed_size = bytes.size()};
    }
    return uncompressed(kind, {bytes.begin(), bytes.end()});
}

/// A table section: count, record size, then @p count records written by @p write_record(bytes, offset, index).
template<class WriteRecord>
auto
table(AtlasSectionKind kind,
      std::size_t count,
      std::size_t record_size,
      WriteRecord write_record) -> PendingSection
{
    std::vector<std::byte> bytes(atlas_layout::Table::RECORDS + count * record_size);
    store(std::span{bytes}, atlas_layout::Table::COUNT, static_cast<std::uint32_t>(count));
    store(std::span{bytes}, atlas_layout::Table::RECORD_SIZE, static_cast<std::uint32_t>(record_size));
    for (std::size_t index = 0; index < count; ++index)
    {
        write_record(std::span{bytes}, atlas_layout::Table::RECORDS + index * record_size, index);
    }
    return uncompressed(kind, std::move(bytes));
}

/// Throws for inconsistent content.
[[noreturn]] void
throw_content_error(std::string problem,
                    std::string suggestion = "")
{
    throw_font_usage_error("write_atlas", std::move(problem), std::move(suggestion));
}

/// Checks the content's tables, references and pages.
void
validate(const AtlasContent& content)
{
    const AtlasDescriptor& descriptor = content.descriptor;
    if (!atlas_layout::texel_format_fits(descriptor.kind, descriptor.texel_format))
    {
        throw_content_error(std::format("{} pages cannot store {} fields", to_string(descriptor.texel_format), to_string(descriptor.kind)),
                            "store SDF and PSDF in R8_UNORM or R16_SFLOAT, MSDF and MTSDF in R8G8B8A8_UNORM or R16G16B16A16_SFLOAT");
    }
    if (content.face.units_per_em == 0)
    {
        throw_content_error("the face has 0 units per em");
    }
    const std::size_t page_size = std::size_t{descriptor.page_width} * descriptor.page_height * atlas_layout::texel_size(descriptor.texel_format);
    for (std::size_t page = 0; page < content.pages.size(); ++page)
    {
        if (content.pages[page].size() != page_size)
        {
            throw_content_error(std::format("page {} has {} bytes, not the {} its dimensions need", page, content.pages[page].size(), page_size));
        }
    }
    const auto has_glyph = [&](GlyphIndex glyph)
    {
        return std::ranges::binary_search(content.glyphs, glyph, {}, &AtlasGlyph::glyph);
    };
    for (std::size_t index = 0; index < content.glyphs.size(); ++index)
    {
        const AtlasGlyph& glyph = content.glyphs[index];
        if (index > 0 && content.glyphs[index - 1].glyph >= glyph.glyph)
        {
            throw_content_error(std::format("glyphs are not sorted by index (or repeat) at {}", glyph.glyph));
        }
        if (glyph.has_box() && glyph.page >= content.pages.size())
        {
            throw_content_error(std::format("{} is on page {} of {}", glyph.glyph, glyph.page, content.pages.size()));
        }
    }
    for (std::size_t index = 0; index < content.characters.size(); ++index)
    {
        const CharacterMapping& mapping = content.characters[index];
        if ((index > 0 && content.characters[index - 1].code_point >= mapping.code_point) || !has_glyph(mapping.glyph))
        {
            throw_content_error(std::format("character {} is out of order or maps to a glyph the atlas lacks", format_code_point(mapping.code_point)));
        }
    }
    for (std::size_t index = 0; index < content.kerning.size(); ++index)
    {
        const KerningPair& pair = content.kerning[index];
        const bool ordered = index == 0 || std::pair{content.kerning[index - 1].left, content.kerning[index - 1].right} < std::pair{pair.left, pair.right};
        if (!ordered || !has_glyph(pair.left) || !has_glyph(pair.right))
        {
            throw_content_error(std::format("kerning pair {} {} is out of order or names a glyph the atlas lacks", pair.left, pair.right));
        }
    }
    for (std::size_t index = 0; index < content.ligatures.size(); ++index)
    {
        const Ligature& ligature = content.ligatures[index];
        bool valid = ligature.component_count >= 2 && ligature.component_count <= MAXIMUM_LIGATURE_COMPONENTS && has_glyph(ligature.glyph);
        for (std::size_t component = 0; valid && component < ligature.component_count; ++component)
        {
            valid = has_glyph(ligature.components[component]);
        }
        if (valid && index > 0)
        {
            const Ligature& previous = content.ligatures[index - 1];
            valid = previous.components[0] < ligature.components[0]
                    || (previous.components[0] == ligature.components[0] && previous.component_count >= ligature.component_count);
        }
        if (!valid)
        {
            throw_content_error(std::format("ligature {} is out of order, has 0, 1 or more than {} components, or names a glyph the atlas lacks",
                                            ligature.glyph, MAXIMUM_LIGATURE_COMPONENTS));
        }
    }
}

/// The DESCRIPTOR section.
auto
descriptor_section(const AtlasContent& content) -> PendingSection
{
    using atlas_layout::Descriptor;
    const AtlasDescriptor& descriptor = content.descriptor;
    std::vector<std::byte> bytes(Descriptor::SIZE);
    const std::span<std::byte> view{bytes};
    store(view, Descriptor::KIND, static_cast<std::uint8_t>(descriptor.kind));
    store(view, Descriptor::TEXEL_FORMAT, static_cast<std::uint8_t>(descriptor.texel_format));
    store(view, Descriptor::PACKER, static_cast<std::uint8_t>(descriptor.packer));
    store(view, Descriptor::DECODER, static_cast<std::uint8_t>(descriptor.decoder));
    store(view, Descriptor::GENERATOR, static_cast<std::uint8_t>(descriptor.generator));
    store(view, Descriptor::COLORING, static_cast<std::uint8_t>(descriptor.coloring));
    store(view, Descriptor::OPTIONS, static_cast<std::uint8_t>((descriptor.overlap_support ? 1U : 0U) | (descriptor.scanline_pass ? 2U : 0U)));
    store(view, Descriptor::PAGE_WIDTH, descriptor.page_width);
    store(view, Descriptor::PAGE_HEIGHT, descriptor.page_height);
    store(view, Descriptor::PAGE_COUNT, static_cast<std::uint32_t>(content.pages.size()));
    store(view, Descriptor::GLYPH_COUNT, static_cast<std::uint32_t>(content.glyphs.size()));
    store(view, Descriptor::EM_SIZE, descriptor.em_size);
    store(view, Descriptor::DISTANCE_RANGE, descriptor.distance_range);
    store(view, Descriptor::PADDING, descriptor.padding);
    store(view, Descriptor::ANGLE_THRESHOLD, descriptor.angle_threshold);
    store(view, Descriptor::MITER_LIMIT, descriptor.miter_limit);
    store(view, Descriptor::FACE_INDEX, descriptor.face_index);
    store(view, Descriptor::SPACING, descriptor.spacing);
    return uncompressed(AtlasSectionKind::DESCRIPTOR, std::move(bytes));
}

/// The FACE_METRICS section.
auto
face_metrics_section(const AtlasFaceContent& face) -> PendingSection
{
    using atlas_layout::AxisRecord;
    using atlas_layout::FaceMetrics;
    const std::size_t axes = FaceMetrics::SIZE;
    const std::size_t family = axes + face.axes.size() * AxisRecord::SIZE;
    const std::size_t style = family + face.family.size();
    std::vector<std::byte> bytes(style + face.style.size());
    const std::span<std::byte> view{bytes};
    store(view, FaceMetrics::UNITS_PER_EM, face.units_per_em);
    store(view, FaceMetrics::ASCENDER, face.ascender);
    store(view, FaceMetrics::DESCENDER, face.descender);
    store(view, FaceMetrics::LINE_GAP, face.line_gap);
    store(view, FaceMetrics::X_HEIGHT, face.x_height);
    store(view, FaceMetrics::CAP_HEIGHT, face.cap_height);
    store(view, FaceMetrics::UNDERLINE_POSITION, face.underline_position);
    store(view, FaceMetrics::UNDERLINE_THICKNESS, face.underline_thickness);
    store(view, FaceMetrics::AXIS_COUNT, static_cast<std::uint32_t>(face.axes.size()));
    store(view, FaceMetrics::FAMILY_SIZE, static_cast<std::uint32_t>(face.family.size()));
    store(view, FaceMetrics::STYLE_SIZE, static_cast<std::uint32_t>(face.style.size()));
    for (std::size_t index = 0; index < face.axes.size(); ++index)
    {
        store(view, axes + index * AxisRecord::SIZE + AxisRecord::TAG, face.axes[index].tag);
        store(view, axes + index * AxisRecord::SIZE + AxisRecord::VALUE, face.axes[index].value);
    }
    std::ranges::transform(face.family, bytes.begin() + static_cast<std::ptrdiff_t>(family), [](char character) { return static_cast<std::byte>(character); });
    std::ranges::transform(face.style, bytes.begin() + static_cast<std::ptrdiff_t>(style), [](char character) { return static_cast<std::byte>(character); });
    return uncompressed(AtlasSectionKind::FACE_METRICS, std::move(bytes));
}

} // namespace

auto
write_atlas(const AtlasContent& content) -> std::vector<std::byte>
{
    validate(content);

    // Every section, in the format's order.
    std::vector<PendingSection> sections;
    sections.push_back(descriptor_section(content));
    sections.push_back(face_metrics_section(content.face));
    sections.push_back(table(AtlasSectionKind::GLYPHS, content.glyphs.size(), atlas_layout::GlyphRecord::SIZE,
                             [&](std::span<std::byte> bytes, std::size_t at, std::size_t index)
                             {
                                 using atlas_layout::GlyphRecord;
                                 const AtlasGlyph& glyph = content.glyphs[index];
                                 store(bytes, at + GlyphRecord::GLYPH, glyph.glyph.value());
                                 store(bytes, at + GlyphRecord::FLAGS, std::to_underlying(glyph.flags));
                                 store(bytes, at + GlyphRecord::PAGE, glyph.page);
                                 store(bytes, at + GlyphRecord::ADVANCE, glyph.advance);
                                 store(bytes, at + GlyphRecord::PLANE_LEFT, glyph.plane.left);
                                 store(bytes, at + GlyphRecord::PLANE_BOTTOM, glyph.plane.bottom);
                                 store(bytes, at + GlyphRecord::PLANE_RIGHT, glyph.plane.right);
                                 store(bytes, at + GlyphRecord::PLANE_TOP, glyph.plane.top);
                                 store(bytes, at + GlyphRecord::TEXEL_LEFT, glyph.texels.left);
                                 store(bytes, at + GlyphRecord::TEXEL_TOP, glyph.texels.top);
                                 store(bytes, at + GlyphRecord::TEXEL_RIGHT, glyph.texels.right);
                                 store(bytes, at + GlyphRecord::TEXEL_BOTTOM, glyph.texels.bottom);
                             }));
    sections.push_back(table(AtlasSectionKind::CHARACTER_MAP, content.characters.size(), atlas_layout::CharacterRecord::SIZE,
                             [&](std::span<std::byte> bytes, std::size_t at, std::size_t index)
                             {
                                 const CharacterMapping& mapping = content.characters[index];
                                 store(bytes, at + atlas_layout::CharacterRecord::CODE_POINT, static_cast<std::uint32_t>(mapping.code_point));
                                 store(bytes, at + atlas_layout::CharacterRecord::GLYPH, mapping.glyph.value());
                             }));
    if (!content.kerning.empty())
    {
        sections.push_back(table(AtlasSectionKind::KERNING, content.kerning.size(), atlas_layout::KerningRecord::SIZE,
                                 [&](std::span<std::byte> bytes, std::size_t at, std::size_t index)
                                 {
                                     const KerningPair& pair = content.kerning[index];
                                     store(bytes, at + atlas_layout::KerningRecord::LEFT, pair.left.value());
                                     store(bytes, at + atlas_layout::KerningRecord::RIGHT, pair.right.value());
                                     store(bytes, at + atlas_layout::KerningRecord::ADJUSTMENT, pair.adjustment);
                                 }));
    }
    if (!content.ligatures.empty())
    {
        sections.push_back(table(AtlasSectionKind::LIGATURES, content.ligatures.size(), atlas_layout::LigatureRecord::SIZE,
                                 [&](std::span<std::byte> bytes, std::size_t at, std::size_t index)
                                 {
                                     using atlas_layout::LigatureRecord;
                                     const Ligature& ligature = content.ligatures[index];
                                     store(bytes, at + LigatureRecord::COMPONENT_COUNT, ligature.component_count);
                                     for (std::size_t component = 0; component < MAXIMUM_LIGATURE_COMPONENTS; ++component)
                                     {
                                         const std::uint32_t glyph = component < ligature.component_count ? ligature.components[component].value() : 0;
                                         store(bytes, at + LigatureRecord::COMPONENTS + 4 * component, glyph);
                                     }
                                     store(bytes, at + LigatureRecord::GLYPH, ligature.glyph.value());
                                 }));
    }
    if (!content.shaping_font.empty())
    {
        sections.push_back(compressed(AtlasSectionKind::SHAPING_FONT, content.shaping_font, content.compression, content.compression_level));
    }
    for (const std::vector<std::byte>& page : content.pages)
    {
        sections.push_back(compressed(AtlasSectionKind::PIXELS, page, content.compression, content.compression_level));
    }

    // Placement: header, section table, then each section at an aligned offset.
    using atlas_layout::aligned;
    std::uint64_t offset = aligned(atlas_layout::Header::SIZE + sections.size() * atlas_layout::SectionEntry::SIZE);
    std::vector<std::uint64_t> offsets;
    for (const PendingSection& section : sections)
    {
        offsets.push_back(offset);
        offset = aligned(offset + section.stored.size());
    }
    std::vector<std::byte> blob(static_cast<std::size_t>(offset));
    const std::span<std::byte> view{blob};

    // Sections and their table entries.
    for (std::size_t index = 0; index < sections.size(); ++index)
    {
        using atlas_layout::SectionEntry;
        const PendingSection& section = sections[index];
        std::ranges::copy(section.stored, blob.begin() + static_cast<std::ptrdiff_t>(offsets[index]));
        const std::size_t entry = atlas_layout::Header::SIZE + index * SectionEntry::SIZE;
        store(view, entry + SectionEntry::KIND, static_cast<std::uint32_t>(section.kind));
        store(view, entry + SectionEntry::COMPRESSION, static_cast<std::uint32_t>(section.compression));
        store(view, entry + SectionEntry::OFFSET, offsets[index]);
        store(view, entry + SectionEntry::STORED_SIZE, static_cast<std::uint64_t>(section.stored.size()));
        store(view, entry + SectionEntry::UNCOMPRESSED_SIZE, section.uncompressed_size);
        store(view, entry + SectionEntry::CHECKSUM, xxhash64(section.stored));
    }

    // Header, its checksum covering everything after it.
    using atlas_layout::Header;
    for (std::size_t index = 0; index < UI_ATLAS_MAGIC.size(); ++index)
    {
        blob[Header::MAGIC + index] = static_cast<std::byte>(UI_ATLAS_MAGIC[index]);
    }
    store(view, Header::VERSION_MAJOR, UI_ATLAS_VERSION_MAJOR);
    store(view, Header::VERSION_MINOR, UI_ATLAS_VERSION_MINOR);
    store(view, Header::ENDIANNESS, UI_ATLAS_ENDIANNESS_TAG);
    store(view, Header::FLAGS, std::uint32_t{0});
    store(view, Header::SECTION_COUNT, static_cast<std::uint32_t>(sections.size()));
    store(view, Header::TOTAL_SIZE, static_cast<std::uint64_t>(blob.size()));
    store(view, Header::SOURCE_FONT_HASH, content.source_font_hash);
    store(view, Header::GENERATOR_VERSION, UI_ATLAS_GENERATOR_VERSION);
    store(view, Header::SECTION_TABLE_OFFSET, static_cast<std::uint32_t>(Header::SIZE));
    store(view, Header::CHECKSUM, xxhash64(std::span<const std::byte>{blob}.subspan(Header::SIZE)));

    // What the readers will see.
    if (const auto parsed = AtlasView::parse(blob); !parsed.has_value())
    {
        throw std::logic_error(std::format("write_atlas produced an invalid atlas: {}", describe_atlas_error(parsed.error())));
    }
    return blob;
}

} // namespace gfx::ui
