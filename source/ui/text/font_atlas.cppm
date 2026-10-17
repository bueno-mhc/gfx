/// @file font_atlas.cppm
/// @brief FontAtlas: a `.uiatlas` loaded at run time — validated once, its pages and shaping font
///        decompressed once, then immutable (any thread may read it). No glyph is decoded or rasterized.
export module gfx.ui.text:font_atlas;

import std;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.atlas_format;

export namespace gfx::ui
{

/// @brief A page of an atlas, decompressed: width × height texels of the atlas's texel format, rows from
///        the top (what a renderer uploads as one image).
struct FontAtlasPage
{
    std::uint32_t width = 0;                                     ///< Texels.
    std::uint32_t height = 0;                                    ///< Texels.
    AtlasTexelFormat format = AtlasTexelFormat::R8G8B8A8_UNORM;  ///< Of every texel.
    std::span<const std::byte> texels{};                         ///< Row by row from the top.
};

/// @brief A loaded font atlas. Created by load(), owned through std::shared_ptr; immutable.
class FontAtlas
{
public:
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas(FontAtlas&&) = delete;
    auto operator=(const FontAtlas&) -> FontAtlas& = delete;
    auto operator=(FontAtlas&&) -> FontAtlas& = delete;
    ~FontAtlas();

    /// @brief Loads the atlas @p blob, which must outlive the atlas (an embedded constant, a mapped file):
    ///        its tables are read in place, its pages and shaping font decompressed.
    /// @throws gfx::InvalidUsageError for a blob that is not a valid atlas (the message explains why).
    /// @throws gfx::BackendCapabilityError (UiCapabilityError) for an atlas of another major format version.
    [[nodiscard]] static auto load(std::span<const std::byte> blob,
                                   AtlasVerification verification = AtlasVerification::CHECKSUMS) -> std::shared_ptr<const FontAtlas>;

    /// @brief Loads the atlas @p blob, which the atlas keeps.
    [[nodiscard]] static auto load(std::vector<std::byte>&& blob,
                                   AtlasVerification verification = AtlasVerification::CHECKSUMS) -> std::shared_ptr<const FontAtlas>;

    /// @brief Reads and loads the atlas file @p path.
    /// @throws gfx::InvalidUsageError when the file cannot be read or is not a valid atlas.
    [[nodiscard]] static auto load_file(const std::filesystem::path& path) -> std::shared_ptr<const FontAtlas>;

    /// @brief The validated blob.
    [[nodiscard]] auto view() const noexcept -> const AtlasView&
    {
        return m_view;
    }

    /// @brief The parameters it was built with.
    [[nodiscard]] auto descriptor() const noexcept -> const AtlasDescriptor&
    {
        return m_view.descriptor();
    }

    /// @brief "Family Style" ("Noto Sans Regular"), for messages.
    [[nodiscard]] auto name() const -> std::string;

    /// @brief Pages.
    [[nodiscard]] auto page_count() const noexcept -> std::size_t
    {
        return m_pages.size();
    }

    /// @brief Page @p index (below page_count()).
    [[nodiscard]] auto page(std::size_t index) const noexcept -> FontAtlasPage
    {
        const AtlasDescriptor& descriptor = m_view.descriptor();
        return {.width = descriptor.page_width, .height = descriptor.page_height, .format = descriptor.texel_format, .texels = m_pages[index]};
    }

    /// @brief The OpenType font HarfBuzz shapes with (empty when the atlas was built without one).
    [[nodiscard]] auto shaping_font() const noexcept -> std::span<const std::byte>
    {
        return m_shaping_font;
    }

    /// @brief The glyph of @p code_point, or nullopt when the atlas lacks the character.
    [[nodiscard]] auto find_character(char32_t code_point) const noexcept -> std::optional<GlyphIndex>
    {
        return m_view.find_character(code_point);
    }

    /// @brief Units per em, ascender, descender and line gap of the face (font units).
    [[nodiscard]] auto face_metrics() const noexcept -> AtlasFaceMetrics
    {
        return m_view.face_metrics();
    }

private:
    FontAtlas() = default;

    /// @brief Validates the blob and decompresses its pages and shaping font.
    void initialize(std::span<const std::byte> blob,
                    AtlasVerification verification);

    std::vector<std::byte> m_owned{};
    AtlasView m_view{};
    std::vector<std::vector<std::byte>> m_pages{};
    std::vector<std::byte> m_shaping_font{};
};

} // namespace gfx::ui
