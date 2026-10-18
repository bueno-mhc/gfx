/// @file text_editor.cppm
/// @brief gfx.ui.text:text_editor: TextEditor, the editing model of the UI's text fields — a UTF-8 text with a
///        cursor and a selection, moved by grapheme clusters, words and lines, edited by inserting and erasing,
///        with undo and redo, a capacity and single-line mode. It draws nothing: widgets lay the text out and
///        map positions to offsets (caret_position(), hit_test()).
export module gfx.ui.text:text_editor;

import std;
import gfx.ui.enums;
import :boundaries;

export namespace gfx::ui
{

/// @brief How a TextEditor edits.
struct TextEditorDescription
{
    std::size_t capacity = 0;                        ///< Largest text, bytes (0: no limit); inserts are cut at a character.
    std::uint32_t undo_levels = DEFAULT_UNDO_LEVELS; ///< Edits undo() can revert.
    bool single_line = false;                        ///< Inserted line breaks become spaces.
};

/// @brief A text being edited: cursor and selection (byte offsets on grapheme boundaries, the anchor being where
///        the selection started), motions, edits, undo and redo. Inserted text is made valid UTF-8 (invalid bytes
///        become U+FFFD) without control characters other than tabs and line breaks.
///
/// @code
/// gfx::ui::TextEditor editor;
/// editor.set_text("hello world");
/// editor.move(gfx::ui::TextMotion::TEXT_START);
/// editor.move(gfx::ui::TextMotion::NEXT_WORD, true);   // selects "hello "
/// editor.insert("goodbye ");                         // "goodbye world"
/// editor.undo();                                     // "hello world"
/// @endcode
class TextEditor
{
public:
    /// @brief An editor of an empty text.
    explicit TextEditor(const TextEditorDescription& description = {});

    /// @brief Replaces the text (cut to the capacity), puts the cursor at its end, forgets the undo history.
    void set_text(std::string_view text);

    /// @brief The text.
    [[nodiscard]] auto text() const noexcept -> std::string_view
    {
        return m_text;
    }

    /// @brief The cursor's byte offset.
    [[nodiscard]] auto cursor() const noexcept -> std::uint32_t
    {
        return m_cursor;
    }

    /// @brief Where the selection started (the cursor when nothing is selected).
    [[nodiscard]] auto anchor() const noexcept -> std::uint32_t
    {
        return m_anchor;
    }

    /// @brief The selection as [begin, end) (empty when nothing is selected).
    [[nodiscard]] auto selection() const noexcept -> std::pair<std::uint32_t, std::uint32_t>
    {
        return {std::min(m_cursor, m_anchor), std::max(m_cursor, m_anchor)};
    }

    /// @brief Whether something is selected.
    [[nodiscard]] auto has_selection() const noexcept -> bool
    {
        return m_cursor != m_anchor;
    }

    /// @brief The selected text.
    [[nodiscard]] auto selected_text() const noexcept -> std::string_view;

    /// @brief The grapheme and word boundaries of the text.
    [[nodiscard]] auto boundaries() const noexcept -> const TextBoundaries&
    {
        return m_boundaries;
    }

    /// @brief A counter increased by every change of the text.
    [[nodiscard]] auto revision() const noexcept -> std::uint64_t
    {
        return m_revision;
    }

    /// @brief Largest text in bytes (0: no limit).
    [[nodiscard]] auto capacity() const noexcept -> std::size_t
    {
        return m_description.capacity;
    }

    /// @brief Changes the capacity (the text is not cut: only later inserts are).
    void set_capacity(std::size_t capacity) noexcept
    {
        m_description.capacity = capacity;
    }

    /// @brief Whether typing replaces the grapheme after the cursor.
    [[nodiscard]] auto overwrite() const noexcept -> bool
    {
        return m_overwrite;
    }

    /// @brief Typing replaces (true) or inserts (false).
    void set_overwrite(bool overwrite) noexcept
    {
        m_overwrite = overwrite;
    }

    /// @brief Moves the cursor to @p offset (the grapheme boundary at or before it); @p extend keeps the anchor,
    ///        extending the selection.
    void set_cursor(std::uint32_t offset,
                    bool extend = false) noexcept;

    /// @brief Selects from @p anchor to @p cursor.
    void set_selection(std::uint32_t anchor,
                       std::uint32_t cursor) noexcept;

    /// @brief Moves the cursor by @p motion (@p extend: extending the selection). Without @p extend, a selection
    ///        collapses to its start (backward motions) or end (forward motions) first.
    void move(TextMotion motion,
              bool extend = false) noexcept;

    /// @brief Selects everything.
    void select_all() noexcept;

    /// @brief Selects the word (or run of spaces) at @p offset.
    void select_word(std::uint32_t offset) noexcept;

    /// @brief Replaces the selection with @p text (in overwrite mode without a selection, the graphemes after the
    ///        cursor). Returns whether the text changed.
    auto insert(std::string_view text) -> bool;

    /// @brief Erases the selection, or from the cursor to where @p motion would move it (Backspace:
    ///        PREVIOUS_GRAPHEME; Delete: NEXT_GRAPHEME; with Control: the words). Returns whether the text changed.
    auto erase(TextMotion motion) -> bool;

    /// @brief Erases the selection. Returns whether the text changed.
    auto erase_selection() -> bool;

    /// @brief Reverts the last edit (consecutive typing and erasing are one edit). Returns whether there was one.
    auto undo() -> bool;

    /// @brief Applies again the last edit undone. Returns whether there was one.
    auto redo() -> bool;

    /// @brief Whether undo() can revert something.
    [[nodiscard]] auto can_undo() const noexcept -> bool
    {
        return !m_undo.empty();
    }

    /// @brief Whether redo() can apply something.
    [[nodiscard]] auto can_redo() const noexcept -> bool
    {
        return !m_redo.empty();
    }

private:
    /// An edit: @p removed at @p offset was replaced with @p inserted.
    struct Edit
    {
        std::uint32_t offset = 0;
        std::string removed{};
        std::string inserted{};
        std::uint32_t cursor_before = 0;
        std::uint32_t anchor_before = 0;
        bool typing = false;
        bool erasing = false;
    };

    /// Replaces [@p begin, @p end) with @p text (already clean), records the edit, puts the cursor after it.
    void replace(std::uint32_t begin,
                 std::uint32_t end,
                 std::string_view text,
                 bool typing,
                 bool erasing);

    /// Replaces the text range of an undo or redo step without recording it.
    void apply(std::uint32_t offset,
               std::size_t length,
               std::string_view text);

    /// @p text made valid UTF-8 without unwanted control characters (into m_clean).
    auto clean(std::string_view text) -> std::string_view;

    TextEditorDescription m_description{};
    std::string m_text{};
    std::string m_clean{};
    TextBoundaries m_boundaries{};
    std::uint32_t m_cursor = 0;
    std::uint32_t m_anchor = 0;
    std::uint64_t m_revision = 0;
    bool m_overwrite = false;
    bool m_merge = false;
    std::vector<Edit> m_undo{};
    std::vector<Edit> m_redo{};
};

} // namespace gfx::ui
