/// @file line_breaking.cpp
/// @brief Line break opportunities (UAX #14), grapheme and word boundaries (UAX #29), with libunibreak:
///        for the layout engine, and TextBoundaries for text editing.
module;

#include <graphemebreak.h>
#include <linebreak.h>
#include <wordbreak.h>

module gfx.ui.text;

import std;
import gfx.ui.unicode;
import :engine;
import :boundaries;

namespace gfx::ui::text_engine
{

namespace
{

/// libunibreak's tables, initialized once.
void
initialize_libunibreak()
{
    static std::once_flag initialized;
    std::call_once(initialized, []
    {
        init_linebreak();
        init_graphemebreak();
        init_wordbreak();
    });
}

} // namespace

void
initialize_libunibreak_for_boundaries()
{
    initialize_libunibreak();
}

void
find_line_breaks(std::span<const std::uint32_t> code_points,
                 const std::string& language,
                 std::vector<char>& breaks)
{
    initialize_libunibreak();
    breaks.resize(code_points.size());
    set_linebreaks_utf32(code_points.data(), code_points.size(), language.empty() ? nullptr : language.c_str(), breaks.data());
}

void
find_grapheme_breaks(std::span<const std::uint32_t> code_points,
                     const std::string& language,
                     std::vector<char>& breaks)
{
    initialize_libunibreak();
    breaks.resize(code_points.size());
    set_graphemebreaks_utf32(code_points.data(), code_points.size(), language.empty() ? nullptr : language.c_str(), breaks.data());
}

} // namespace gfx::ui::text_engine

namespace gfx::ui
{

namespace
{

/// Whether @p code_point is White_Space (Unicode).
constexpr auto
is_white_space(char32_t code_point) noexcept -> bool
{
    return (code_point >= 0x09 && code_point <= 0x0D) || code_point == 0x20 || code_point == 0x85 || code_point == 0xA0 || code_point == 0x1680
           || (code_point >= 0x2000 && code_point <= 0x200A) || code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202F
           || code_point == 0x205F || code_point == 0x3000;
}

} // namespace

void
TextBoundaries::analyze(std::string_view text)
{
    // One status per byte from libunibreak: a boundary follows the byte when it ends a cluster (or word).
    text_engine::initialize_libunibreak_for_boundaries();
    m_text = text;
    m_graphemes.resize(text.size());
    m_words.resize(text.size());
    if (text.empty())
    {
        return;
    }
    const auto* bytes = reinterpret_cast<const utf8_t*>(text.data());
    set_graphemebreaks_utf8(bytes, text.size(), nullptr, m_graphemes.data());
    set_wordbreaks_utf8(bytes, text.size(), nullptr, m_words.data());
    for (std::size_t byte = 0; byte < text.size(); ++byte)
    {
        m_graphemes[byte] = m_graphemes[byte] == GRAPHEMEBREAK_BREAK ? 1 : 0;
        m_words[byte] = m_words[byte] == WORDBREAK_BREAK ? 1 : 0;
    }
}

auto
TextBoundaries::is_grapheme_boundary(std::uint32_t offset) const noexcept -> bool
{
    return offset == 0 || offset >= m_text.size() || m_graphemes[offset - 1] != 0;
}

auto
TextBoundaries::is_word_boundary(std::uint32_t offset) const noexcept -> bool
{
    return offset == 0 || offset >= m_text.size() || m_words[offset - 1] != 0;
}

auto
TextBoundaries::next_grapheme(std::uint32_t offset) const noexcept -> std::uint32_t
{
    const auto size = static_cast<std::uint32_t>(m_text.size());
    if (offset >= size)
    {
        return size;
    }
    do
    {
        ++offset;
    } while (!is_grapheme_boundary(offset));
    return offset;
}

auto
TextBoundaries::previous_grapheme(std::uint32_t offset) const noexcept -> std::uint32_t
{
    offset = std::min(offset, static_cast<std::uint32_t>(m_text.size()));
    if (offset == 0)
    {
        return 0;
    }
    do
    {
        --offset;
    } while (!is_grapheme_boundary(offset));
    return offset;
}

auto
TextBoundaries::segment_is_space(std::uint32_t offset) const noexcept -> bool
{
    // The segment's code points, up to the next word boundary.
    std::uint32_t position = offset;
    do
    {
        const Utf8CodePoint code_point = decode_utf8(m_text, position);
        if (!is_white_space(code_point.code_point))
        {
            return false;
        }
        position += code_point.size;
    } while (position < m_text.size() && !is_word_boundary(position));
    return true;
}

auto
TextBoundaries::next_word(std::uint32_t offset) const noexcept -> std::uint32_t
{
    // Past the segment at the offset, then past the spaces after it.
    const auto size = static_cast<std::uint32_t>(m_text.size());
    if (offset >= size)
    {
        return size;
    }
    const auto segment_end = [&](std::uint32_t position)
    {
        do
        {
            ++position;
        } while (!is_word_boundary(position));
        return position;
    };
    std::uint32_t position = segment_end(offset);
    while (position < size && segment_is_space(position))
    {
        position = segment_end(position);
    }
    return position;
}

auto
TextBoundaries::previous_word(std::uint32_t offset) const noexcept -> std::uint32_t
{
    // Back over spaces, then to the start of the segment before them.
    offset = std::min(offset, static_cast<std::uint32_t>(m_text.size()));
    const auto segment_start = [&](std::uint32_t position)
    {
        do
        {
            --position;
        } while (!is_word_boundary(position));
        return position;
    };
    while (offset > 0)
    {
        const std::uint32_t start = segment_start(offset);
        if (!segment_is_space(start))
        {
            return start;
        }
        offset = start;
    }
    return 0;
}

auto
TextBoundaries::word_at(std::uint32_t offset) const noexcept -> std::pair<std::uint32_t, std::uint32_t>
{
    const auto size = static_cast<std::uint32_t>(m_text.size());
    if (size == 0)
    {
        return {0, 0};
    }
    offset = std::min(offset, size - 1);
    std::uint32_t begin = offset;
    while (!is_word_boundary(begin))
    {
        --begin;
    }
    std::uint32_t end = offset + 1;
    while (!is_word_boundary(end))
    {
        ++end;
    }
    return {begin, end};
}

auto
TextBoundaries::grapheme_count() const noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(std::ranges::count(m_graphemes, 1));
}

} // namespace gfx::ui
