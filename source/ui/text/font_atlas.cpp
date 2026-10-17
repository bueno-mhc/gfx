/// @file font_atlas.cpp
/// @brief FontAtlas: validation of a blob, decompression of its pages and shaping font (zstd).
module;

#include <zstd.h>

module gfx.ui.text;

import std;
import gfx.ui.errors;
import gfx.ui.atlas_format;

namespace gfx::ui
{

namespace
{

/// Throws the error of an invalid atlas: a capability error for another major version, else invalid usage.
[[noreturn]] void
throw_atlas_error(const AtlasReadError& error)
{
    if (error.problem == AtlasProblem::UNSUPPORTED_VERSION)
    {
        throw_unsupported(UiSubsystem::FONT, "FontAtlas::load", std::format("atlas format version {}", error.actual),
                          describe_atlas_error(error), "rebuild the atlas with this version's gfx_ui_atlas_builder (gfx_add_ui_atlas())");
    }
    throw InvalidUsageError({.backend = UiSubsystem::FONT,
                             .operation = "FontAtlas::load",
                             .problem = std::format("the blob is not a valid atlas: {}", describe_atlas_error(error)),
                             .suggestion = "load a file that gfx_ui_atlas_builder wrote, whole and unmodified"});
}

/// The bytes of @p section, decompressed.
auto
decompress(const AtlasView& view,
           const AtlasSection& section) -> std::vector<std::byte>
{
    const std::span<const std::byte> stored = view.section_bytes(section);
    if (section.compression == AtlasCompression::NONE)
    {
        return {stored.begin(), stored.end()};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(section.uncompressed_size));
    const std::size_t size = ZSTD_decompress(bytes.data(), bytes.size(), stored.data(), stored.size());
    if (ZSTD_isError(size) != 0 || size != bytes.size())
    {
        throw InvalidUsageError({.backend = UiSubsystem::FONT,
                                 .operation = "FontAtlas::load",
                                 .problem = std::format("section {} at byte {} does not decompress ({})",
                                                        section.kind().has_value() ? to_string(*section.kind()) : std::string_view{"?"}, section.offset,
                                                        ZSTD_isError(size) != 0 ? ZSTD_getErrorName(size) : "wrong size"),
                                 .suggestion = "load a file that gfx_ui_atlas_builder wrote, whole and unmodified"});
    }
    return bytes;
}

} // namespace

FontAtlas::~FontAtlas() = default;

void
FontAtlas::initialize(std::span<const std::byte> blob,
                      AtlasVerification verification)
{
    // The structure (and checksums), then every page and the shaping font, decompressed once.
    const std::expected<AtlasView, AtlasReadError> parsed = AtlasView::parse(blob, verification);
    if (!parsed.has_value())
    {
        throw_atlas_error(parsed.error());
    }
    m_view = *parsed;
    for (std::size_t page = 0; page < m_view.descriptor().page_count; ++page)
    {
        m_pages.push_back(decompress(m_view, m_view.page(page)));
    }
    if (const std::optional<AtlasSection> shaping = m_view.shaping_font(); shaping.has_value())
    {
        m_shaping_font = decompress(m_view, *shaping);
    }
}

auto
FontAtlas::load(std::span<const std::byte> blob,
                AtlasVerification verification) -> std::shared_ptr<const FontAtlas>
{
    const std::shared_ptr<FontAtlas> atlas{new FontAtlas};
    atlas->initialize(blob, verification);
    return atlas;
}

auto
FontAtlas::load(std::vector<std::byte>&& blob,
                AtlasVerification verification) -> std::shared_ptr<const FontAtlas>
{
    const std::shared_ptr<FontAtlas> atlas{new FontAtlas};
    atlas->m_owned = std::move(blob);
    atlas->initialize(atlas->m_owned, verification);
    return atlas;
}

auto
FontAtlas::load_file(const std::filesystem::path& path) -> std::shared_ptr<const FontAtlas>
{
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file)
    {
        throw InvalidUsageError({.backend = UiSubsystem::FONT,
                                 .operation = "FontAtlas::load_file",
                                 .problem = std::format("the atlas file '{}' cannot be opened", path.string()),
                                 .suggestion = "check the path, and that the file is readable"});
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return load(std::move(bytes));
}

auto
FontAtlas::name() const -> std::string
{
    const AtlasFaceMetrics face = m_view.face_metrics();
    return std::format("{} {}", face.family.to_string(), face.style.to_string());
}

} // namespace gfx::ui
