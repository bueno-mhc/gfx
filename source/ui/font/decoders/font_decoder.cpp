/// @file font_decoder.cpp
/// @brief What FontFace does for faces without variations or OpenType data, and create_font_decoder().
module gfx.ui.atlas_tools;

import std;
import gfx.ui.errors;
import :implementations;

namespace gfx::ui
{

auto
FontFace::variation_axes() const -> std::vector<VariationAxis>
{
    return {};
}

auto
FontFace::named_instances() const -> std::vector<NamedInstance>
{
    return {};
}

void
FontFace::set_variation(std::span<const FontAxisValue> coordinates)
{
    // The default instance is the only one a face without variations has.
    if (!coordinates.empty())
    {
        throw_unsupported(UiSubsystem::FONT, "FontFace::set_variation", "font variations",
                          std::format("the {} decoder cannot apply variation coordinates", to_string(decoder())),
                          "decode variable fonts with FontDecoderKind::FREETYPE, or build at the default instance");
    }
}

auto
FontFace::sfnt() const -> std::span<const std::byte>
{
    return {};
}

auto
FontFace::sfnt_face_index() const noexcept -> std::uint32_t
{
    return 0;
}

auto
create_font_decoder(FontDecoderKind kind) -> std::unique_ptr<FontDecoder>
{
    switch (kind)
    {
        case FontDecoderKind::FREETYPE:
            return make_freetype_decoder();
        case FontDecoderKind::STB_TRUETYPE:
            return make_stb_truetype_decoder();
        case FontDecoderKind::CUSTOM:
            break;
    }
    throw_font_usage_error("create_font_decoder", "FontDecoderKind::CUSTOM names a decoder of the application",
                           "implement gfx::ui::FontDecoder and give it to AtlasBuilder::use_decoder()");
}

} // namespace gfx::ui
