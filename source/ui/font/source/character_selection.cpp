/// @file character_selection.cpp
/// @brief CharacterSelection: named sets, ranges, code points and UTF-8 text.
module gfx.ui.atlas_tools;

import std;
import :implementations;

namespace gfx::ui
{

namespace
{

/// Largest Unicode code point.
constexpr char32_t LAST_CODE_POINT = 0x10FFFF;

/// Whether @p code_point is a surrogate (not a scalar value).
constexpr auto
is_surrogate(char32_t code_point) noexcept -> bool
{
    return code_point >= 0xD800 && code_point <= 0xDFFF;
}

/// The code point of "U+XXXX" (1 to 6 hexadecimal digits), or nullopt.
auto
parse_code_point(std::string_view token) -> std::optional<char32_t>
{
    if (token.size() < 3 || (token[0] != 'U' && token[0] != 'u') || token[1] != '+' || token.size() > 8)
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(token.data() + 2, token.data() + token.size(), value, 16);
    if (error != std::errc{} || end != token.data() + token.size())
    {
        return std::nullopt;
    }
    return static_cast<char32_t>(value);
}

} // namespace

auto
format_code_point(char32_t code_point) -> std::string
{
    return std::format("U+{:04X}", static_cast<std::uint32_t>(code_point));
}

auto
CharacterSelection::parse(std::string_view specification) -> CharacterSelection
{
    CharacterSelection selection;
    std::size_t position = 0;
    while (position < specification.size())
    {
        // The next token, between separators.
        const auto is_separator = [](char character) { return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == ','; };
        while (position < specification.size() && is_separator(specification[position]))
        {
            ++position;
        }
        std::size_t end = position;
        while (end < specification.size() && !is_separator(specification[end]))
        {
            ++end;
        }
        const std::string_view token = specification.substr(position, end - position);
        position = end;
        if (token.empty())
        {
            continue;
        }

        // A named set, a code point or a range.
        if (const std::optional<CharacterSet> set = from_string<CharacterSet>(token); set.has_value())
        {
            selection.add(*set);
            continue;
        }
        const std::size_t dash = token.find('-', 2);
        if (dash == std::string_view::npos)
        {
            if (const std::optional<char32_t> code_point = parse_code_point(token); code_point.has_value())
            {
                selection.add(*code_point);
                continue;
            }
        }
        else
        {
            const std::optional<char32_t> first = parse_code_point(token.substr(0, dash));
            const std::optional<char32_t> last = parse_code_point(token.substr(dash + 1));
            if (first.has_value() && last.has_value())
            {
                selection.add_range(*first, *last);
                continue;
            }
        }
        std::string sets;
        for (const auto& entry : EnumerationTraits<CharacterSet>::NAMES)
        {
            sets += std::format("{}{}", sets.empty() ? "" : " ", entry.name);
        }
        throw_font_usage_error("CharacterSelection::parse",
                               std::format("'{}' is neither a character set, a code point (U+00B1) nor a range (U+2190-U+21FF)", token),
                               std::format("name characters with: {}, U+XXXX or U+XXXX-U+YYYY", sets));
    }
    return selection;
}

auto
CharacterSelection::add(char32_t code_point) -> CharacterSelection&
{
    if (code_point > LAST_CODE_POINT || is_surrogate(code_point))
    {
        throw_font_usage_error("CharacterSelection::add",
                               std::format("{} is not a Unicode scalar value", format_code_point(code_point)),
                               "use code points up to U+10FFFF, surrogates (U+D800 to U+DFFF) excluded");
    }
    const auto position = std::ranges::lower_bound(m_code_points, code_point);
    if (position == m_code_points.end() || *position != code_point)
    {
        m_code_points.insert(position, code_point);
    }
    return *this;
}

auto
CharacterSelection::add(CharacterSet set) -> CharacterSelection&
{
    const CharacterRange range = CHARACTER_SET_RANGES[std::to_underlying(set)];
    return add_range(range.first, range.last);
}

auto
CharacterSelection::add(const CharacterSelection& other) -> CharacterSelection&
{
    m_code_points.insert(m_code_points.end(), other.m_code_points.begin(), other.m_code_points.end());
    normalize();
    return *this;
}

auto
CharacterSelection::add_range(char32_t first,
                              char32_t last) -> CharacterSelection&
{
    if (first > last || last > LAST_CODE_POINT)
    {
        throw_font_usage_error("CharacterSelection::add_range",
                               std::format("{}-{} is not a range of Unicode code points", format_code_point(first), format_code_point(last)),
                               "give the first code point, then the last, both up to U+10FFFF");
    }
    for (char32_t code_point = first; code_point <= last; ++code_point)
    {
        if (!is_surrogate(code_point))
        {
            m_code_points.push_back(code_point);
        }
    }
    normalize();
    return *this;
}

auto
CharacterSelection::add_text(std::string_view text) -> CharacterSelection&
{
    // Decode UTF-8, rejecting overlong forms, surrogates and values beyond U+10FFFF.
    std::size_t position = 0;
    while (position < text.size())
    {
        const auto byte = [&](std::size_t index) { return static_cast<std::uint8_t>(text[index]); };
        const std::uint8_t lead = byte(position);
        std::size_t length = 0;
        char32_t code_point = 0;
        char32_t minimum = 0;
        if (lead < 0x80)
        {
            length = 1;
            code_point = lead;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            code_point = lead & 0x1FU;
            minimum = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            code_point = lead & 0x0FU;
            minimum = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            code_point = lead & 0x07U;
            minimum = 0x10000;
        }
        bool valid = length != 0 && position + length <= text.size();
        for (std::size_t index = 1; valid && index < length; ++index)
        {
            valid = (byte(position + index) & 0xC0) == 0x80;
            code_point = (code_point << 6) | (byte(position + index) & 0x3FU);
        }
        if (!valid || code_point < minimum || code_point > LAST_CODE_POINT || is_surrogate(code_point))
        {
            throw_font_usage_error("CharacterSelection::add_text", std::format("the text is not valid UTF-8 at byte {}", position),
                                   "save text files of characters as UTF-8");
        }
        // Control characters (line breaks, tabs) have no glyph.
        if (code_point >= 0x20 && (code_point < 0x7F || code_point > 0x9F))
        {
            m_code_points.push_back(code_point);
        }
        position += length;
    }
    normalize();
    return *this;
}

void
CharacterSelection::normalize()
{
    std::ranges::sort(m_code_points);
    const auto duplicates = std::ranges::unique(m_code_points);
    m_code_points.erase(duplicates.begin(), duplicates.end());
}

} // namespace gfx::ui
