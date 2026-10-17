/// @file font_library.cppm
/// @brief FontLibrary: the atlases and fonts of a UI, named by handles. A font is an atlas, the atlases to
///        fall back on for the characters it lacks, a size, a language and a shaping policy. Lookups are
///        lock-free; adding and removing take a lock, so fonts may be added from any thread.
export module gfx.ui.text:font_library;

import std;
import gfx.ui.enums;
import gfx.ui.handles;
import :font_atlas;

export namespace gfx::ui
{

/// @brief A font: what a text is laid out with.
struct FontDescription
{
    FontAtlasHandle atlas{};                      ///< The atlas tried first.
    std::vector<FontAtlasHandle> fallbacks{};     ///< Tried in order for characters the atlas lacks (other scripts, symbols).
    float size = 16.0F;                           ///< Pixels per em.
    std::string language{};                       ///< BCP 47 tag ("en", "ar", "ja"): language-specific shaping and line breaking.
    TextShaping shaping = TextShaping::AUTOMATIC; ///< Which shaper shapes its runs.
};

/// @brief The atlases and fonts of a UI.
class FontLibrary
{
public:
    /// @brief An empty library.
    FontLibrary();

    FontLibrary(const FontLibrary&) = delete;
    auto operator=(const FontLibrary&) -> FontLibrary& = delete;
    ~FontLibrary();

    /// @brief Adds @p atlas and returns its handle.
    /// @throws gfx::InvalidUsageError for a null atlas.
    [[nodiscard]] auto add_atlas(std::shared_ptr<const FontAtlas> atlas) -> FontAtlasHandle;

    /// @brief The atlas of @p handle, or nullptr when it names none.
    [[nodiscard]] auto atlas(FontAtlasHandle handle) const noexcept -> std::shared_ptr<const FontAtlas>;

    /// @brief Removes the atlas of @p handle (atlases are shared: those still in use stay alive).
    /// @throws gfx::InvalidUsageError when a font of the library uses it.
    void remove_atlas(FontAtlasHandle handle);

    /// @brief Adds the font @p description and returns its handle.
    /// @throws gfx::InvalidUsageError for an atlas handle the library does not hold or a size that is not positive.
    [[nodiscard]] auto add_font(FontDescription description) -> FontHandle;

    /// @brief The description of @p handle, or nullptr when it names none (valid until the font is removed).
    [[nodiscard]] auto font(FontHandle handle) const noexcept -> const FontDescription*;

    /// @brief Removes the font of @p handle.
    void remove_font(FontHandle handle) noexcept;

    /// @brief Atlases held.
    [[nodiscard]] auto atlas_count() const noexcept -> std::size_t;

    /// @brief Fonts held.
    [[nodiscard]] auto font_count() const noexcept -> std::size_t;

private:
    struct State;
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::ui
