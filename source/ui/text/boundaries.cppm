/// @file boundaries.cppm
/// @brief gfx.ui.text:boundaries: grapheme cluster and word boundaries of a UTF-8 text (UAX #29, by
///        libunibreak), by byte offset — what text editing moves and deletes by.
export module gfx.ui.text:boundaries;

import std;

export namespace gfx::ui
{

/// @brief The grapheme clusters (user-perceived characters: "é" as e + U+0301, an emoji with its modifiers, a
///        flag) and words of a UTF-8 text. Offsets are bytes; analyze() again after the text changes.
class TextBoundaries
{
public:
    /// @brief Finds the boundaries of @p text, which must outlive the queries. Reuses its buffers: analyzing
    ///        texts no longer than before allocates nothing.
    void analyze(std::string_view text);

    /// @brief The text analyzed.
    [[nodiscard]] auto text() const noexcept -> std::string_view
    {
        return m_text;
    }

    /// @brief Whether a grapheme cluster starts (or the text ends) at @p offset.
    [[nodiscard]] auto is_grapheme_boundary(std::uint32_t offset) const noexcept -> bool;

    /// @brief The first grapheme boundary after @p offset (the text's size at its end).
    [[nodiscard]] auto next_grapheme(std::uint32_t offset) const noexcept -> std::uint32_t;

    /// @brief The last grapheme boundary before @p offset (0 at the start).
    [[nodiscard]] auto previous_grapheme(std::uint32_t offset) const noexcept -> std::uint32_t;

    /// @brief The start of the word after @p offset: past the rest of the word or punctuation at @p offset and
    ///        the spaces after it (Control+Right).
    [[nodiscard]] auto next_word(std::uint32_t offset) const noexcept -> std::uint32_t;

    /// @brief The start of the word before @p offset: back over spaces, then to the start of the word or
    ///        punctuation before them (Control+Left).
    [[nodiscard]] auto previous_word(std::uint32_t offset) const noexcept -> std::uint32_t;

    /// @brief The word (or run of spaces or punctuation) containing @p offset, as [begin, end) (a double click).
    [[nodiscard]] auto word_at(std::uint32_t offset) const noexcept -> std::pair<std::uint32_t, std::uint32_t>;

    /// @brief Grapheme clusters of the text.
    [[nodiscard]] auto grapheme_count() const noexcept -> std::uint32_t;

private:
    /// Whether a word boundary follows the byte at @p offset - 1 (or @p offset is 0 or the end).
    [[nodiscard]] auto is_word_boundary(std::uint32_t offset) const noexcept -> bool;

    /// Whether the word segment starting at @p offset is made of spaces only.
    [[nodiscard]] auto segment_is_space(std::uint32_t offset) const noexcept -> bool;

    std::string_view m_text{};
    std::vector<char> m_graphemes{};
    std::vector<char> m_words{};
};

} // namespace gfx::ui
