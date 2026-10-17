/// @file freetype_decoder.cpp
/// @brief The FreeType decoder: TrueType, OpenType CFF and CFF2, collections, WOFF, WOFF2 and variable
///        fonts, through freetype_library.hpp. Its classes are declared here: no other unit needs them.
module;

#include "freetype_library.hpp"

module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import :implementations;

namespace gfx::ui
{

namespace
{

/// A copy of FreeType's text (empty for none).
auto
text_of(freetype::Text text) -> std::string
{
    return text.data == nullptr ? std::string{} : std::string{text.data, static_cast<std::size_t>(text.size)};
}

/// Throws the error of a failed FreeType call made by @p operation on @p font.
[[noreturn]] void
throw_freetype_error(std::string_view operation,
                     std::string_view font,
                     const freetype::Status& status,
                     std::source_location location = std::source_location::current())
{
    throw BackendApiError({.backend = UiSubsystem::FONT,
                           .operation = std::format("{} ({})", operation, font),
                           .api_function = text_of(status.function),
                           .result_code = status.error,
                           .result_name = text_of(status.message)},
                          location);
}

/// The names of every axis of @p axes and their ranges ("wght 100 to 900, wdth 62.5 to 100").
auto
describe_axes(std::span<const VariationAxis> axes) -> std::string
{
    std::string text;
    for (const VariationAxis& axis : axes)
    {
        const std::array<char, 4> tag = font_tag_name(axis.tag);
        text += std::format("{}{} {} to {}", text.empty() ? "" : ", ", std::string_view{tag.data(), tag.size()}, axis.minimum, axis.maximum);
    }
    return text.empty() ? std::string{"none"} : text;
}

/// Builds a GlyphOutline from FreeType's drawing commands.
class OutlineCollector final : public freetype::OutlineSink
{
public:
    void move_to(double x,
                 double y) override
    {
        m_builder.move_to({x, y});
    }

    void line_to(double x,
                 double y) override
    {
        m_builder.line_to({x, y});
    }

    void quadratic_to(double control_x,
                      double control_y,
                      double x,
                      double y) override
    {
        m_builder.quadratic_to({control_x, control_y}, {x, y});
    }

    void cubic_to(double first_control_x,
                  double first_control_y,
                  double second_control_x,
                  double second_control_y,
                  double x,
                  double y) override
    {
        m_builder.cubic_to({first_control_x, first_control_y}, {second_control_x, second_control_y}, {x, y});
    }

    [[nodiscard]] auto finish() -> GlyphOutline
    {
        return m_builder.finish();
    }

private:
    OutlineBuilder m_builder{};
};

/// A face opened by FreeType.
class FreetypeFace final : public FontFace
{
public:
    FreetypeFace(FontSource source,
                 freetype::Face* face,
                 FontFormat format)
        : m_source{std::move(source)}
        , m_face{face}
        , m_format{format}
        , m_information{freetype::information(*face)}
    {
    }

    FreetypeFace(const FreetypeFace&) = delete;
    auto operator=(const FreetypeFace&) -> FreetypeFace& = delete;

    ~FreetypeFace() override
    {
        freetype::close(m_face);
    }

    [[nodiscard]] auto decoder() const noexcept -> FontDecoderKind override
    {
        return FontDecoderKind::FREETYPE;
    }

    [[nodiscard]] auto format() const noexcept -> FontFormat override
    {
        return m_format;
    }

    [[nodiscard]] auto metrics() const -> FontFaceMetrics override
    {
        const freetype::FaceInformation information = freetype::information(*m_face);
        return {.units_per_em = information.units_per_em,
                .ascender = information.ascender,
                .descender = information.descender,
                .line_gap = information.line_gap,
                .x_height = information.x_height,
                .cap_height = information.cap_height,
                .underline_position = information.underline_position,
                .underline_thickness = information.underline_thickness,
                .family = text_of(information.family),
                .style = text_of(information.style)};
    }

    [[nodiscard]] auto glyph_count() const noexcept -> std::uint32_t override
    {
        return static_cast<std::uint32_t>(m_information.glyph_count);
    }

    [[nodiscard]] auto character_map() const -> std::vector<CharacterMapping> override
    {
        // FreeType walks the Unicode map by ascending code point.
        const auto count = static_cast<std::size_t>(freetype::character_count(*m_face));
        std::vector<unsigned> code_points(count);
        std::vector<unsigned> glyphs(count);
        freetype::character_map(*m_face, code_points.data(), glyphs.data());
        std::vector<CharacterMapping> map(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            map[index] = {.code_point = static_cast<char32_t>(code_points[index]), .glyph = GlyphIndex{glyphs[index]}};
        }
        return map;
    }

    [[nodiscard]] auto advance(GlyphIndex glyph) const -> double override
    {
        double value = 0;
        if (const freetype::Status status = freetype::advance(*m_face, glyph.value(), value); !status.success)
        {
            throw_freetype_error("FontFace::advance", m_source.name(), status);
        }
        return value;
    }

    [[nodiscard]] auto outline(GlyphIndex glyph) const -> GlyphOutline override
    {
        OutlineCollector collector;
        bool has_outline = false;
        if (const freetype::Status status = freetype::outline(*m_face, glyph.value(), collector, has_outline); !status.success)
        {
            throw_freetype_error("FontFace::outline", m_source.name(), status);
        }
        return collector.finish();
    }

    [[nodiscard]] auto glyph_flags(GlyphIndex glyph) const -> AtlasGlyphFlag override
    {
        AtlasGlyphFlag flags = AtlasGlyphFlag::NONE;
        if (freetype::has_color_layers(*m_face, glyph.value()))
        {
            flags |= AtlasGlyphFlag::COLOR_LAYERS;
        }
        OutlineCollector collector;
        bool has_outline = false;
        if (const freetype::Status status = freetype::outline(*m_face, glyph.value(), collector, has_outline); status.success && !has_outline)
        {
            flags |= AtlasGlyphFlag::NO_OUTLINE;
        }
        return flags;
    }

    [[nodiscard]] auto variation_axes() const -> std::vector<VariationAxis> override
    {
        std::vector<VariationAxis> axes;
        for (unsigned index = 0; index < m_information.axis_count; ++index)
        {
            const freetype::Axis axis = freetype::axis(*m_face, index);
            axes.push_back({.tag = static_cast<std::uint32_t>(axis.tag),
                            .name = text_of(axis.name),
                            .minimum = axis.minimum,
                            .default_value = axis.default_value,
                            .maximum = axis.maximum});
        }
        return axes;
    }

    [[nodiscard]] auto named_instances() const -> std::vector<NamedInstance> override
    {
        const std::vector<VariationAxis> axes = variation_axes();
        std::vector<NamedInstance> instances;
        std::vector<double> coordinates(axes.size());
        for (unsigned index = 0; index < m_information.named_instance_count; ++index)
        {
            NamedInstance instance{.name = text_of(freetype::named_instance(*m_face, index, coordinates.data()))};
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
            {
                instance.coordinates.push_back({.tag = axes[axis].tag, .value = static_cast<float>(coordinates[axis])});
            }
            instances.push_back(std::move(instance));
        }
        return instances;
    }

    void set_variation(std::span<const FontAxisValue> coordinates) override
    {
        // Every axis at its default, then the values given.
        const std::vector<VariationAxis> axes = variation_axes();
        if (axes.empty())
        {
            if (!coordinates.empty())
            {
                throw_font_usage_error("FontFace::set_variation", std::format("'{}' is not a variable font", m_source.name()),
                                       "give no variation for a static font");
            }
            return;
        }
        std::vector<double> values;
        for (const VariationAxis& axis : axes)
        {
            values.push_back(axis.default_value);
        }
        for (const FontAxisValue& coordinate : coordinates)
        {
            const auto axis = std::ranges::find(axes, coordinate.tag, &VariationAxis::tag);
            const std::array<char, 4> tag = font_tag_name(coordinate.tag);
            if (axis == axes.end())
            {
                throw_font_usage_error("FontFace::set_variation",
                                       std::format("'{}' has no axis '{}'", m_source.name(), std::string_view{tag.data(), tag.size()}),
                                       std::format("use one of its axes: {}", describe_axes(axes)));
            }
            if (coordinate.value < axis->minimum || coordinate.value > axis->maximum)
            {
                throw_font_usage_error("FontFace::set_variation",
                                       std::format("{} is outside the range of axis '{}' of '{}'", coordinate.value,
                                                   std::string_view{tag.data(), tag.size()}, m_source.name()),
                                       std::format("its axes: {}", describe_axes(axes)));
            }
            values[static_cast<std::size_t>(axis - axes.begin())] = coordinate.value;
        }
        if (const freetype::Status status = freetype::set_variation(*m_face, values.data(), static_cast<unsigned>(values.size()));
            !status.success)
        {
            throw_freetype_error("FontFace::set_variation", m_source.name(), status);
        }
    }

    [[nodiscard]] auto sfnt() const -> std::span<const std::byte> override
    {
        // The tables FreeType reads: the file itself, or the ones it decoded from WOFF and WOFF2.
        if (!m_information.is_sfnt)
        {
            return {};
        }
        if (m_sfnt.empty())
        {
            unsigned long long size = 0;
            freetype::Status status = freetype::sfnt(*m_face, nullptr, size);
            if (status.success)
            {
                m_sfnt.resize(static_cast<std::size_t>(size));
                status = freetype::sfnt(*m_face, reinterpret_cast<unsigned char*>(m_sfnt.data()), size);
            }
            if (!status.success)
            {
                throw_freetype_error("FontFace::sfnt", m_source.name(), status);
            }
        }
        return m_sfnt;
    }

    [[nodiscard]] auto sfnt_face_index() const noexcept -> std::uint32_t override
    {
        return m_information.sfnt_face_index;
    }

private:
    FontSource m_source;
    freetype::Face* m_face = nullptr;
    FontFormat m_format = FontFormat::TRUETYPE;
    freetype::FaceInformation m_information{};
    mutable std::vector<std::byte> m_sfnt{};
};

/// The FreeType decoder.
class FreetypeDecoder final : public FontDecoder
{
public:
    [[nodiscard]] auto kind() const noexcept -> FontDecoderKind override
    {
        return FontDecoderKind::FREETYPE;
    }

    [[nodiscard]] auto face_count(const FontSource& source) const -> std::uint32_t override
    {
        require_known_format(source, "FontDecoder::face_count");
        long count = 0;
        const freetype::Status status = freetype::face_count(bytes_of(source), source.bytes().size(), count);
        if (!status.success)
        {
            throw_open_error("FontDecoder::face_count", source, status);
        }
        return static_cast<std::uint32_t>(count);
    }

    [[nodiscard]] auto open(const FontSource& source,
                            std::uint32_t face_index) const -> std::unique_ptr<FontFace> override
    {
        const FontFormat format = require_known_format(source, "FontDecoder::open");
        const std::uint32_t count = face_count(source);
        if (face_index >= count)
        {
            throw_font_usage_error("FontDecoder::open", std::format("'{}' has {} face(s): there is no face {}", source.name(), count, face_index),
                                   std::format("use a face index below {}", count));
        }
        freetype::Face* face = nullptr;
        const freetype::Status status = freetype::open(bytes_of(source), source.bytes().size(), static_cast<long>(face_index), face);
        if (!status.success)
        {
            throw_open_error("FontDecoder::open", source, status);
        }
        return std::make_unique<FreetypeFace>(source, face, format);
    }

private:
    [[nodiscard]] static auto bytes_of(const FontSource& source) noexcept -> const unsigned char*
    {
        return reinterpret_cast<const unsigned char*>(source.bytes().data());
    }

    /// The format of @p source; throws for anything FontFormat does not name.
    static auto require_known_format(const FontSource& source,
                                     std::string_view operation) -> FontFormat
    {
        const std::optional<FontFormat> format = source.format();
        if (!format.has_value())
        {
            throw_unsupported(UiSubsystem::FONT, operation, "font format",
                              std::format("'{}' is not a TrueType, OpenType, collection, WOFF or WOFF2 font", source.name()),
                              "convert the font to OpenType (Type 1 and bitmap fonts are not supported)");
        }
        return *format;
    }

    /// Throws for a font FreeType cannot open.
    [[noreturn]] static void throw_open_error(std::string_view operation,
                                              const FontSource& source,
                                              const freetype::Status& status)
    {
        throw_font_usage_error(operation,
                               std::format("FreeType cannot read '{}': {} reported \"{}\"", source.name(), text_of(status.function),
                                           text_of(status.message)),
                               "check that the file is a complete, valid font");
    }
};

} // namespace

auto
make_freetype_decoder() -> std::unique_ptr<FontDecoder>
{
    return std::make_unique<FreetypeDecoder>();
}

} // namespace gfx::ui
