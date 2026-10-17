/// @file character_selection.cppm
/// @brief CharacterSelection: the characters an atlas must hold, from named sets, ranges, code points
///        and text, and the text form atlas descriptions use ("BASIC_LATIN ARROWS U+00B1 U+2190-U+21FF").
export module gfx.ui.atlas_tools:character_selection;

import std;
import gfx.ui.enums;

export namespace gfx::ui
{

/// @brief A set of Unicode scalar values (code points without surrogates), kept sorted.
class CharacterSelection
{
public:
    /// @brief An empty selection.
    CharacterSelection() = default;

    /// @brief The characters of @p specification: whitespace- or comma-separated CharacterSet names
    ///        (BASIC_LATIN), code points (U+00B1) and ranges (U+2190-U+21FF).
    /// @throws gfx::InvalidUsageError naming the first token that is none of them.
    [[nodiscard]] static auto parse(std::string_view specification) -> CharacterSelection;

    /// @brief Adds @p code_point.
    /// @throws gfx::InvalidUsageError when it is a surrogate or beyond U+10FFFF.
    auto add(char32_t code_point) -> CharacterSelection&;

    /// @brief Adds the characters of @p set (CHARACTER_SET_RANGES).
    auto add(CharacterSet set) -> CharacterSelection&;

    /// @brief Adds every character of @p other.
    auto add(const CharacterSelection& other) -> CharacterSelection&;

    /// @brief Adds @p first to @p last, both included (surrogates are skipped).
    /// @throws gfx::InvalidUsageError when the range is reversed or goes beyond U+10FFFF.
    auto add_range(char32_t first,
                   char32_t last) -> CharacterSelection&;

    /// @brief Adds every character of the UTF-8 text @p text, but control characters (line breaks, tabs).
    /// @throws gfx::InvalidUsageError at the first byte that is not valid UTF-8.
    auto add_text(std::string_view text) -> CharacterSelection&;

    /// @brief The characters, ascending.
    [[nodiscard]] auto code_points() const noexcept -> std::span<const char32_t>
    {
        return m_code_points;
    }

    /// @brief Whether @p code_point is selected.
    [[nodiscard]] auto contains(char32_t code_point) const noexcept -> bool
    {
        return std::ranges::binary_search(m_code_points, code_point);
    }

    /// @brief Number of characters.
    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return m_code_points.size();
    }

    /// @brief Whether no character is selected.
    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return m_code_points.empty();
    }

private:
    /// @brief Sorts the characters and removes duplicates.
    void normalize();

    std::vector<char32_t> m_code_points{};
};

/// @brief "U+0041" for @p code_point (four digits at least), for messages and reports.
[[nodiscard]] auto format_code_point(char32_t code_point) -> std::string;

} // namespace gfx::ui
