/// @file atlas_builder.cpp
/// @brief AtlasBuilder: font → shaping subset → glyphs → boxes → packing → distance fields (in parallel)
///        → pages → pair tables → `.uiatlas`. Glyphs are read on the calling thread (decoders are not
///        thread-safe); boxes and fields are computed on worker threads, each glyph independently, so the
///        bytes do not depend on the number of threads.
module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import gfx.ui.atlas_format;
import :implementations;

namespace gfx::ui
{

namespace
{

/// Everything known about one glyph of the atlas.
struct GlyphWork
{
    GlyphIndex glyph{};
    AtlasGlyphFlag flags = AtlasGlyphFlag::NONE;
    double advance = 0;
    GlyphOutline outline{};
    GlyphBox box{};
};

/// The texel format of @p kind when the description names none.
auto
default_texel_format(SdfKind kind) noexcept -> AtlasTexelFormat
{
    return atlas_layout::channel_count(kind) == 1 ? AtlasTexelFormat::R8_UNORM : AtlasTexelFormat::R8G8B8A8_UNORM;
}

/// IEEE binary16 bits of @p value, rounded to nearest even.
constexpr auto
to_half(float value) noexcept -> std::uint16_t
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000U;
    const std::uint32_t exponent = (bits >> 23) & 0xFFU;
    std::uint32_t mantissa = bits & 0x7FFFFFU;
    if (exponent == 0xFF)
    {
        return static_cast<std::uint16_t>(sign | 0x7C00U | (mantissa != 0 ? 0x200U : 0U));
    }
    const int half_exponent = static_cast<int>(exponent) - 127 + 15;
    if (half_exponent >= 31)
    {
        return static_cast<std::uint16_t>(sign | 0x7C00U);
    }
    if (half_exponent <= 0)
    {
        // Subnormal, or zero below the smallest subnormal.
        if (half_exponent < -10)
        {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000U;
        const auto shift = static_cast<std::uint32_t>(14 - half_exponent);
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1U << shift) - 1);
        const std::uint32_t halfway = 1U << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (half & 1U) != 0))
        {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = sign | (static_cast<std::uint32_t>(half_exponent) << 10) | (mantissa >> 13);
    const std::uint32_t remainder = mantissa & 0x1FFFU;
    if (remainder > 0x1000U || (remainder == 0x1000U && (half & 1U) != 0))
    {
        ++half;
    }
    return static_cast<std::uint16_t>(half);
}

static_assert(to_half(1.0F) == 0x3C00 && to_half(-2.0F) == 0xC000 && to_half(0.5F) == 0x3800 && to_half(65504.0F) == 0x7BFF);

/// Writes @p field at texel (@p x, @p y) of @p page (of @p page_width texels in @p format).
void
write_field(const GlyphField& field,
            std::span<std::byte> page,
            std::uint32_t page_width,
            AtlasTexelFormat format,
            std::uint32_t x,
            std::uint32_t y)
{
    const std::size_t texel_size = atlas_layout::texel_size(format);
    const std::size_t components = format == AtlasTexelFormat::R8_UNORM || format == AtlasTexelFormat::R16_SFLOAT ? 1 : 4;
    for (std::uint32_t row = 0; row < field.height; ++row)
    {
        for (std::uint32_t column = 0; column < field.width; ++column)
        {
            const std::size_t offset = ((std::size_t{y} + row) * page_width + x + column) * texel_size;
            for (std::size_t component = 0; component < components; ++component)
            {
                // A missing alpha (MSDF in four channels) is opaque.
                const float value = component < field.channel_count ? field.at(column, row, static_cast<std::uint32_t>(component)) : 1.0F;
                if (texel_size == components)
                {
                    page[offset + component] = static_cast<std::byte>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
                }
                else
                {
                    atlas_layout::store(page, offset + component * 2, to_half(value));
                }
            }
        }
    }
}

/// Runs @p work(index) for every index below @p count on @p requested threads (0: one per hardware
/// thread); rethrows the first exception.
void
run_parallel(std::size_t count,
             std::uint32_t requested,
             const std::function<void(std::size_t)>& work)
{
    const std::size_t hardware = std::max(1U, std::thread::hardware_concurrency());
    const std::size_t thread_count = std::min(count, requested == 0 ? hardware : std::size_t{requested});
    if (thread_count <= 1)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            work(index);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::mutex mutex;
    std::exception_ptr failure;
    {
        std::vector<std::jthread> threads;
        for (std::size_t thread = 0; thread < thread_count; ++thread)
        {
            threads.emplace_back([&]
            {
                while (!failed.load(std::memory_order_relaxed))
                {
                    const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= count)
                    {
                        return;
                    }
                    try
                    {
                        work(index);
                    }
                    catch (...)
                    {
                        const std::scoped_lock lock{mutex};
                        if (failure == nullptr)
                        {
                            failure = std::current_exception();
                        }
                        failed.store(true, std::memory_order_relaxed);
                    }
                }
            });
        }
    }
    if (failure != nullptr)
    {
        std::rethrow_exception(failure);
    }
}

/// "wght 100 to 900, wdth 62.5 to 100".
auto
describe_axes(std::span<const VariationAxis> axes) -> std::string
{
    std::string text;
    for (const VariationAxis& axis : axes)
    {
        const std::array<char, 4> tag = font_tag_name(axis.tag);
        text += std::format("{}{} {} to {}", text.empty() ? "" : ", ", std::string_view{tag.data(), tag.size()}, axis.minimum, axis.maximum);
    }
    return text;
}

/// Every axis of @p face at the value the description gives (its default otherwise); empty for a static font.
auto
resolve_variation(const FontFace& face,
                  const AtlasBuildDescription& description,
                  std::string_view font_name) -> std::vector<FontAxisValue>
{
    const std::vector<VariationAxis> axes = face.variation_axes();
    if (!description.named_instance.empty() && !description.variation.empty())
    {
        throw_font_usage_error("AtlasBuilder::build", "both a variation and a named instance are given", "give one of them");
    }
    if (axes.empty())
    {
        if (!description.variation.empty() || !description.named_instance.empty())
        {
            throw_font_usage_error("AtlasBuilder::build", std::format("'{}' is not a variable font", font_name),
                                   "give no variation and no named instance for a static font");
        }
        return {};
    }

    // A named instance gives every axis.
    if (!description.named_instance.empty())
    {
        const std::vector<NamedInstance> instances = face.named_instances();
        const auto instance = std::ranges::find(instances, description.named_instance, &NamedInstance::name);
        if (instance == instances.end())
        {
            std::string names;
            for (const NamedInstance& named : instances)
            {
                names += std::format("{}'{}'", names.empty() ? "" : ", ", named.name);
            }
            throw_font_usage_error("AtlasBuilder::build", std::format("'{}' has no instance named '{}'", font_name, description.named_instance),
                                   std::format("use one of its instances: {}", names));
        }
        return instance->coordinates;
    }

    // Defaults, then the values given (checked against the axes).
    std::vector<FontAxisValue> values;
    for (const VariationAxis& axis : axes)
    {
        values.push_back({.tag = axis.tag, .value = static_cast<float>(axis.default_value)});
    }
    for (const FontAxisValue& value : description.variation)
    {
        const auto axis = std::ranges::find(axes, value.tag, &VariationAxis::tag);
        const std::array<char, 4> tag = font_tag_name(value.tag);
        if (axis == axes.end() || value.value < axis->minimum || value.value > axis->maximum)
        {
            throw_font_usage_error("AtlasBuilder::build",
                                   std::format("'{}' has no axis '{}' or {} is outside its range", font_name, std::string_view{tag.data(), tag.size()},
                                               value.value),
                                   std::format("its axes: {}", describe_axes(axes)));
        }
        values[static_cast<std::size_t>(axis - axes.begin())].value = value.value;
    }
    return values;
}

} // namespace

AtlasBuilder::AtlasBuilder(AtlasBuildDescription description)
    : m_description{std::move(description)}
{
}

auto
AtlasBuilder::use_decoder(std::unique_ptr<FontDecoder> decoder) -> AtlasBuilder&
{
    m_decoder = std::move(decoder);
    return *this;
}

auto
AtlasBuilder::use_generator(std::unique_ptr<SdfGenerator> generator) -> AtlasBuilder&
{
    m_generator = std::move(generator);
    return *this;
}

auto
AtlasBuilder::use_packer(std::unique_ptr<AtlasPacker> packer) -> AtlasBuilder&
{
    m_packer = std::move(packer);
    return *this;
}

auto
AtlasBuilder::build(const FontSource& font) const -> AtlasBuildResult
{
    const AtlasBuildDescription& description = m_description;
    const SdfGenerationDescription& field = description.field;

    // The decoder, generator and packer: given ones, else those the description names.
    std::unique_ptr<FontDecoder> created_decoder;
    std::unique_ptr<SdfGenerator> created_generator;
    std::unique_ptr<AtlasPacker> created_packer;
    const FontDecoder* decoder = m_decoder != nullptr ? m_decoder.get() : (created_decoder = create_font_decoder(description.decoder)).get();
    const SdfGenerator* generator = m_generator != nullptr ? m_generator.get()
                                                           : (created_generator = create_sdf_generator(description.generator)).get();
    const AtlasPacker* packer = m_packer != nullptr ? m_packer.get() : (created_packer = create_atlas_packer(description.packer)).get();

    // The description.
    const AtlasTexelFormat texel_format = description.texel_format.value_or(default_texel_format(field.kind));
    if (description.characters.empty())
    {
        throw_font_usage_error("AtlasBuilder::build", "no characters are selected", "select characters (CharacterSelection::parse(\"BASIC_LATIN\"))");
    }
    if (!(field.em_size > 0) || !(field.distance_range > 0) || field.padding < 0)
    {
        throw_font_usage_error("AtlasBuilder::build",
                               std::format("em size {}, distance range {} and padding {} texels are not all usable", field.em_size,
                                           field.distance_range, field.padding),
                               "use a positive em size and distance range, and a padding of 0 or more");
    }
    if (!atlas_layout::texel_format_fits(field.kind, texel_format))
    {
        throw_font_usage_error("AtlasBuilder::build", std::format("{} pages cannot store {} fields", to_string(texel_format), to_string(field.kind)),
                               "store SDF and PSDF in R8_UNORM or R16_SFLOAT, MSDF and MTSDF in R8G8B8A8_UNORM or R16G16B16A16_SFLOAT");
    }
    if (!generator->supports(field.kind))
    {
        throw_unsupported(UiSubsystem::FONT, "AtlasBuilder::build", std::format("{} fields", to_string(field.kind)),
                          std::format("the {} generator does not generate {} fields", to_string(generator->kind()), to_string(field.kind)),
                          "use SdfGeneratorKind::MSDFGEN");
    }
    if (!description.shaping_font && (description.kerning || description.ligatures))
    {
        throw_font_usage_error("AtlasBuilder::build", "kerning and ligatures are read from the shaping font, which is turned off",
                               "turn kerning and ligatures off too, or keep the shaping font");
    }
    if (description.compression == AtlasCompression::ZSTD && (description.compression_level < 1 || description.compression_level > 22))
    {
        throw_font_usage_error("AtlasBuilder::build", std::format("zstd has no level {}", description.compression_level), "use a level from 1 to 22");
    }

    // The face, its variation, and the face glyphs are read from: the shaping subset, or the face itself.
    const std::unique_ptr<FontFace> face = decoder->open(font, description.face_index);
    const FontFaceMetrics names = face->metrics();
    const std::vector<FontAxisValue> axes = resolve_variation(*face, description, font.name());
    std::vector<std::byte> shaping_font;
    std::unique_ptr<FontFace> subset_face;
    FontFace* source_face = face.get();
    if (description.shaping_font)
    {
        const std::span<const std::byte> sfnt = face->sfnt();
        if (sfnt.empty())
        {
            throw_unsupported(UiSubsystem::FONT, "AtlasBuilder::build", "OpenType tables of the font",
                              std::format("the {} decoder provides no OpenType data for '{}'", to_string(face->decoder()), font.name()),
                              "decode with FontDecoderKind::FREETYPE, or build without a shaping font (no kerning or ligatures)");
        }
        shaping_font = subset_shaping_font(sfnt, face->sfnt_face_index(), description.characters.code_points(), axes, font.name());
        subset_face = decoder->open(FontSource::from_memory(std::span<const std::byte>{shaping_font}, std::format("{} (subset)", font.name())), 0);
        source_face = subset_face.get();
    }
    else if (!axes.empty())
    {
        face->set_variation(axes);
    }
    const FontFaceMetrics metrics = source_face->metrics();

    // Characters the font has, and those it lacks.
    std::vector<CharacterMapping> characters;
    AtlasBuildReport report;
    for (const CharacterMapping& mapping : source_face->character_map())
    {
        if (description.characters.contains(mapping.code_point))
        {
            characters.push_back(mapping);
        }
    }
    for (const char32_t code_point : description.characters.code_points())
    {
        if (!std::ranges::binary_search(characters, code_point, {}, &CharacterMapping::code_point))
        {
            report.missing_characters.push_back(code_point);
        }
    }

    // The glyphs: every glyph of the subset (characters and their closure), or `.notdef` and those of the characters.
    std::vector<GlyphIndex> glyph_indices;
    if (subset_face != nullptr)
    {
        for (std::uint32_t glyph = 0; glyph < source_face->glyph_count(); ++glyph)
        {
            glyph_indices.emplace_back(glyph);
        }
    }
    else
    {
        glyph_indices.emplace_back();
        for (const CharacterMapping& mapping : characters)
        {
            glyph_indices.push_back(mapping.glyph);
        }
        std::ranges::sort(glyph_indices);
        const auto duplicates = std::ranges::unique(glyph_indices);
        glyph_indices.erase(duplicates.begin(), duplicates.end());
    }

    // Outlines on this thread, boxes on the workers.
    std::vector<GlyphWork> glyphs(glyph_indices.size());
    for (std::size_t index = 0; index < glyphs.size(); ++index)
    {
        GlyphWork& glyph = glyphs[index];
        glyph.glyph = glyph_indices[index];
        glyph.flags = source_face->glyph_flags(glyph.glyph);
        glyph.advance = source_face->advance(glyph.glyph);
        glyph.outline = source_face->outline(glyph.glyph);
        if (glyph.outline.empty() && !has_any(glyph.flags, AtlasGlyphFlag::NO_OUTLINE))
        {
            glyph.flags |= AtlasGlyphFlag::WHITESPACE;
        }
        if (has_any(glyph.flags, AtlasGlyphFlag::COLOR_LAYERS))
        {
            ++report.color_glyph_count;
        }
    }
    run_parallel(glyphs.size(), description.thread_count,
                 [&](std::size_t index) { glyphs[index].box = generator->layout(glyphs[index].outline, metrics.units_per_em, field); });

    // Packing (a custom packer's result is checked).
    std::vector<AtlasBoxSize> sizes;
    for (const GlyphWork& glyph : glyphs)
    {
        sizes.push_back({.width = glyph.box.width, .height = glyph.box.height});
    }
    const AtlasPacking packing = packer->pack(sizes, description.packing);
    if (packing.placements.size() != glyphs.size())
    {
        throw_font_usage_error("AtlasBuilder::build", std::format("the packer placed {} of {} boxes", packing.placements.size(), glyphs.size()),
                               "return one placement per box from AtlasPacker::pack()");
    }
    for (std::size_t index = 0; index < glyphs.size(); ++index)
    {
        const AtlasPlacement& placement = packing.placements[index];
        if (!glyphs[index].box.empty()
            && (placement.page >= packing.page_count || placement.x + glyphs[index].box.width > packing.page_width
                || placement.y + glyphs[index].box.height > packing.page_height))
        {
            throw_font_usage_error("AtlasBuilder::build", std::format("the packer placed box {} outside its page", index),
                                   "place every box inside a page of the packing's dimensions");
        }
    }

    // Distance fields, each written into its page (disjoint boxes: the workers never write the same bytes).
    const std::size_t page_size = std::size_t{packing.page_width} * packing.page_height * atlas_layout::texel_size(texel_format);
    std::vector<std::vector<std::byte>> pages(packing.page_count, std::vector<std::byte>(page_size));
    run_parallel(glyphs.size(), description.thread_count, [&](std::size_t index)
    {
        const GlyphWork& glyph = glyphs[index];
        if (glyph.box.empty())
        {
            return;
        }
        const GlyphField generated = generator->generate(glyph.outline, glyph.box, field);
        if (generated.width != glyph.box.width || generated.height != glyph.box.height
            || generated.channel_count != atlas_layout::channel_count(field.kind) || generated.values.size() != std::size_t{generated.width} * generated.height * generated.channel_count)
        {
            throw_font_usage_error("AtlasBuilder::build", std::format("the generator returned a field that does not match the box of {}", glyph.glyph),
                                   "return width × height texels of the field kind's channels from SdfGenerator::generate()");
        }
        const AtlasPlacement& placement = packing.placements[index];
        write_field(generated, pages[placement.page], packing.page_width, texel_format, placement.x, placement.y);
    });

    // The content of the atlas.
    AtlasContent content{
        .descriptor = {.kind = field.kind,
                       .texel_format = texel_format,
                       .packer = packer->kind(),
                       .decoder = decoder->kind(),
                       .generator = generator->kind(),
                       .coloring = field.coloring,
                       .overlap_support = field.overlap_support,
                       .scanline_pass = field.scanline_pass,
                       .page_width = packing.page_width,
                       .page_height = packing.page_height,
                       .em_size = static_cast<float>(field.em_size),
                       .distance_range = static_cast<float>(field.distance_range),
                       .padding = static_cast<float>(field.padding),
                       .angle_threshold = static_cast<float>(field.angle_threshold),
                       .miter_limit = static_cast<float>(field.miter_limit),
                       .face_index = description.face_index,
                       .spacing = description.packing.spacing},
        .face = {.units_per_em = metrics.units_per_em,
                 .ascender = metrics.ascender,
                 .descender = metrics.descender,
                 .line_gap = metrics.line_gap,
                 .x_height = metrics.x_height,
                 .cap_height = metrics.cap_height,
                 .underline_position = metrics.underline_position,
                 .underline_thickness = metrics.underline_thickness,
                 .family = names.family,
                 .style = description.named_instance.empty() ? names.style : description.named_instance,
                 .axes = axes},
        .characters = std::move(characters),
        .shaping_font = shaping_font,
        .pages = std::move(pages),
        .source_font_hash = font.hash(),
        .compression = description.compression,
        .compression_level = description.compression_level};
    const double units_per_em = metrics.units_per_em;
    for (std::size_t index = 0; index < glyphs.size(); ++index)
    {
        const GlyphWork& glyph = glyphs[index];
        AtlasGlyph record{.glyph = glyph.glyph, .flags = glyph.flags, .advance = static_cast<float>(glyph.advance / units_per_em)};
        if (!glyph.box.empty())
        {
            const AtlasPlacement& placement = packing.placements[index];
            record.page = static_cast<std::uint16_t>(placement.page);
            record.plane = glyph.box.plane_bounds(metrics.units_per_em);
            record.texels = {.left = static_cast<float>(placement.x) + 0.5F,
                             .top = static_cast<float>(placement.y) + 0.5F,
                             .right = static_cast<float>(placement.x + glyph.box.width) - 0.5F,
                             .bottom = static_cast<float>(placement.y + glyph.box.height) - 0.5F};
        }
        content.glyphs.push_back(record);
    }
    if (!shaping_font.empty())
    {
        PairTables pairs = extract_pair_tables(shaping_font, content.characters, description.kerning, description.ligatures);
        content.kerning = std::move(pairs.kerning);
        content.ligatures = std::move(pairs.ligatures);
    }

    // The blob and its report.
    AtlasBuildResult result{.atlas = write_atlas(content)};
    report.glyph_count = static_cast<std::uint32_t>(content.glyphs.size());
    report.page_count = packing.page_count;
    report.page_width = packing.page_width;
    report.page_height = packing.page_height;
    report.kerning_pair_count = content.kerning.size();
    report.ligature_count = content.ligatures.size();
    report.shaping_font_size = shaping_font.size();
    report.atlas_size = result.atlas.size();
    result.report = std::move(report);
    return result;
}

auto
build_atlas(const FontSource& font,
            const AtlasBuildDescription& description) -> AtlasBuildResult
{
    return AtlasBuilder{description}.build(font);
}

} // namespace gfx::ui
