/// @file text_editor.cpp
/// @brief TextEditor: motions over TextBoundaries, edits recorded for undo (typing and erasing merged into one
///        edit while they continue), clean UTF-8 input, the capacity.
module gfx.ui.text;

import std;
import gfx.ui.enums;
import gfx.ui.unicode;
import :boundaries;
import :text_editor;

namespace gfx::ui
{

namespace
{

/// Whether @p character is a space or a tab.
constexpr auto
is_blank(char character) noexcept -> bool
{
    return character == ' ' || character == '\t';
}

/// The UTF-8 bytes of @p code_point appended to @p text.
void
append_utf8(std::string& text,
            char32_t code_point)
{
    if (code_point < 0x80)
    {
        text.push_back(static_cast<char>(code_point));
    }
    else if (code_point < 0x800)
    {
        text.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else if (code_point < 0x10000)
    {
        text.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else
    {
        text.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

} // namespace

TextEditor::TextEditor(const TextEditorDescription& description)
    : m_description{description}
{
    m_boundaries.analyze(m_text);
}

void
TextEditor::set_text(std::string_view text)
{
    // Cleaned, cut to the capacity at a character, the cursor at the end, no history.
    std::string_view value = clean(text);
    if (m_description.capacity != 0 && value.size() > m_description.capacity)
    {
        std::size_t size = m_description.capacity;
        while (size > 0 && (static_cast<std::uint8_t>(value[size]) & 0xC0U) == 0x80U)
        {
            --size;
        }
        value = value.substr(0, size);
    }
    m_text.assign(value);
    m_boundaries.analyze(m_text);
    m_cursor = static_cast<std::uint32_t>(m_text.size());
    m_anchor = m_cursor;
    m_undo.clear();
    m_redo.clear();
    m_merge = false;
    ++m_revision;
}

void
TextEditor::reset(const TextEditorDescription& description)
{
    m_description = description;
    m_overwrite = false;
    set_text({});
}

auto
TextEditor::selected_text() const noexcept -> std::string_view
{
    const auto [begin, end] = selection();
    return std::string_view{m_text}.substr(begin, end - begin);
}

void
TextEditor::set_cursor(std::uint32_t offset,
                       bool extend) noexcept
{
    // On the grapheme boundary at or before the offset.
    offset = std::min(offset, static_cast<std::uint32_t>(m_text.size()));
    while (offset > 0 && !m_boundaries.is_grapheme_boundary(offset))
    {
        --offset;
    }
    m_cursor = offset;
    if (!extend)
    {
        m_anchor = offset;
    }
    m_merge = false;
}

void
TextEditor::set_selection(std::uint32_t anchor,
                          std::uint32_t cursor) noexcept
{
    set_cursor(anchor);
    set_cursor(cursor, true);
}

void
TextEditor::move(TextMotion motion,
                 bool extend) noexcept
{
    // Without extending, a selection collapses toward the motion first (one step).
    const bool backward = motion == TextMotion::PREVIOUS_GRAPHEME || motion == TextMotion::PREVIOUS_WORD;
    if (!extend && has_selection() && (motion == TextMotion::PREVIOUS_GRAPHEME || motion == TextMotion::NEXT_GRAPHEME))
    {
        const auto [begin, end] = selection();
        set_cursor(backward ? begin : end);
        return;
    }

    // The target of the motion; lines are separated by line breaks.
    const std::string_view text = m_text;
    std::uint32_t target = m_cursor;
    switch (motion)
    {
        case TextMotion::PREVIOUS_GRAPHEME:
            target = m_boundaries.previous_grapheme(m_cursor);
            break;
        case TextMotion::NEXT_GRAPHEME:
            target = m_boundaries.next_grapheme(m_cursor);
            break;
        case TextMotion::PREVIOUS_WORD:
            target = m_boundaries.previous_word(m_cursor);
            break;
        case TextMotion::NEXT_WORD:
            target = m_boundaries.next_word(m_cursor);
            break;
        case TextMotion::LINE_START:
        {
            const std::size_t line_break = m_cursor == 0 ? std::string_view::npos : text.rfind('\n', m_cursor - 1);
            target = line_break == std::string_view::npos ? 0U : static_cast<std::uint32_t>(line_break + 1);
            break;
        }
        case TextMotion::LINE_END:
        {
            const std::size_t line_break = text.find('\n', m_cursor);
            target = line_break == std::string_view::npos ? static_cast<std::uint32_t>(text.size()) : static_cast<std::uint32_t>(line_break);
            break;
        }
        case TextMotion::TEXT_START:
            target = 0;
            break;
        case TextMotion::TEXT_END:
            target = static_cast<std::uint32_t>(text.size());
            break;
    }
    set_cursor(target, extend);
}

void
TextEditor::select_all() noexcept
{
    m_anchor = 0;
    m_cursor = static_cast<std::uint32_t>(m_text.size());
    m_merge = false;
}

void
TextEditor::select_word(std::uint32_t offset) noexcept
{
    const auto [begin, end] = m_boundaries.word_at(offset);
    m_anchor = begin;
    m_cursor = end;
    m_merge = false;
}

auto
TextEditor::clean(std::string_view text) -> std::string_view
{
    // Valid UTF-8 without control characters other than tabs and line breaks (a CR LF pair is one break);
    // single-line editors turn line breaks into spaces.
    m_clean.clear();
    for (std::size_t offset = 0; offset < text.size();)
    {
        const Utf8CodePoint code_point = decode_utf8(text, offset);
        offset += code_point.size;
        char32_t value = code_point.code_point;
        if (value == U'\r')
        {
            if (offset < text.size() && text[offset] == '\n')
            {
                continue;
            }
            value = U'\n';
        }
        if (value == U'\n' && m_description.single_line)
        {
            value = U' ';
        }
        if ((value < 0x20 && value != U'\t' && value != U'\n') || value == 0x7F)
        {
            continue;
        }
        append_utf8(m_clean, value);
    }
    return m_clean;
}

void
TextEditor::apply(std::uint32_t offset,
                  std::size_t length,
                  std::string_view text)
{
    m_text.replace(offset, length, text);
    m_boundaries.analyze(m_text);
    ++m_revision;
}

void
TextEditor::replace(std::uint32_t begin,
                    std::uint32_t end,
                    std::string_view text,
                    bool typing,
                    bool erasing)
{
    // Cut to the capacity at a character boundary.
    if (m_description.capacity != 0)
    {
        const std::size_t room = m_description.capacity - std::min(m_description.capacity, m_text.size() - (end - begin));
        if (text.size() > room)
        {
            std::size_t size = room;
            while (size > 0 && (static_cast<std::uint8_t>(text[size]) & 0xC0U) == 0x80U)
            {
                --size;
            }
            text = text.substr(0, size);
        }
    }
    if (text.empty() && begin == end)
    {
        return;
    }

    // Recorded for undo: merged with the previous edit while typing (or erasing) continues contiguously and a
    // word does not start after a space.
    const std::string_view removed = std::string_view{m_text}.substr(begin, end - begin);
    Edit* last = m_undo.empty() ? nullptr : &m_undo.back();
    const bool merge_typing = typing && m_merge && last != nullptr && last->typing && last->removed.empty() && !last->inserted.empty()
                              && removed.empty() && begin == last->offset + last->inserted.size()
                              && !(is_blank(last->inserted.back()) && !text.empty() && !is_blank(text.front()));
    const bool merge_erasing = erasing && m_merge && last != nullptr && last->erasing && last->inserted.empty() && text.empty()
                               && (end == last->offset || begin == last->offset);
    if (merge_typing)
    {
        last->inserted.append(text);
    }
    else if (merge_erasing)
    {
        if (end == last->offset)
        {
            last->removed.insert(0, removed);
            last->offset = begin;
        }
        else
        {
            last->removed.append(removed);
        }
    }
    else
    {
        m_undo.push_back({.offset = begin,
                          .removed = std::string{removed},
                          .inserted = std::string{text},
                          .cursor_before = m_cursor,
                          .anchor_before = m_anchor,
                          .typing = typing,
                          .erasing = erasing});
        if (m_undo.size() > m_description.undo_levels)
        {
            m_undo.erase(m_undo.begin());
        }
    }
    m_redo.clear();

    // The change, the cursor after the inserted text.
    apply(begin, end - begin, text);
    m_cursor = begin + static_cast<std::uint32_t>(text.size());
    m_anchor = m_cursor;
    m_merge = typing || erasing;
}

auto
TextEditor::insert(std::string_view text) -> bool
{
    const std::string_view value = clean(text);
    auto [begin, end] = selection();
    if (m_overwrite && begin == end)
    {
        // Overwriting: as many graphemes after the cursor as the text has.
        TextBoundaries inserted;
        inserted.analyze(value);
        for (std::uint32_t grapheme = 0; grapheme < inserted.grapheme_count() && end < m_text.size(); ++grapheme)
        {
            end = m_boundaries.next_grapheme(end);
        }
    }
    const std::uint64_t revision = m_revision;
    replace(begin, end, value, true, false);
    return m_revision != revision;
}

auto
TextEditor::erase_selection() -> bool
{
    if (!has_selection())
    {
        return false;
    }
    const auto [begin, end] = selection();
    m_merge = false;
    replace(begin, end, {}, false, false);
    return true;
}

auto
TextEditor::erase(TextMotion motion) -> bool
{
    if (has_selection())
    {
        return erase_selection();
    }

    // From the cursor to where the motion goes, in either direction.
    const std::uint32_t cursor = m_cursor;
    const std::uint32_t anchor = m_anchor;
    const bool merge = m_merge;
    move(motion);
    const std::uint32_t target = m_cursor;
    m_cursor = cursor;
    m_anchor = anchor;
    m_merge = merge;
    if (target == cursor)
    {
        return false;
    }
    const bool single = motion == TextMotion::PREVIOUS_GRAPHEME || motion == TextMotion::NEXT_GRAPHEME;
    if (!single)
    {
        m_merge = false;
    }
    replace(std::min(target, cursor), std::max(target, cursor), {}, false, single);
    return true;
}

auto
TextEditor::undo() -> bool
{
    if (m_undo.empty())
    {
        return false;
    }
    Edit edit = std::move(m_undo.back());
    m_undo.pop_back();
    apply(edit.offset, edit.inserted.size(), edit.removed);
    m_cursor = edit.cursor_before;
    m_anchor = edit.anchor_before;
    m_merge = false;
    m_redo.push_back(std::move(edit));
    return true;
}

auto
TextEditor::redo() -> bool
{
    if (m_redo.empty())
    {
        return false;
    }
    Edit edit = std::move(m_redo.back());
    m_redo.pop_back();
    apply(edit.offset, edit.removed.size(), edit.inserted);
    m_cursor = edit.offset + static_cast<std::uint32_t>(edit.inserted.size());
    m_anchor = m_cursor;
    m_merge = false;
    m_undo.push_back(std::move(edit));
    return true;
}

} // namespace gfx::ui
