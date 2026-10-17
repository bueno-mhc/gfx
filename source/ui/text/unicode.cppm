/// @file unicode.cppm
/// @brief UTF-8 decoding for the UI library, usable in constant expressions: the code points of a text
///        with their byte offsets (invalid bytes decode as U+FFFD, one byte at a time), and whether a text
///        is valid UTF-8.
export module gfx.ui.unicode;

import std;
import gfx.ui.enums;

export namespace gfx::ui
{

/// @brief A code point of a UTF-8 text: its value, where it starts and how many bytes it takes.
struct Utf8CodePoint
{
    char32_t code_point = 0; ///< The scalar value, or REPLACEMENT_CHARACTER for an invalid byte.
    std::uint32_t offset = 0; ///< Byte offset of its first byte in the text.
    std::uint8_t size = 0;    ///< Bytes it takes (1 for an invalid byte).
    bool valid = true;        ///< Whether the bytes were valid UTF-8.
};

/// @brief The code point that starts at byte @p offset of @p text (below its size): an invalid, truncated
///        or overlong sequence, a surrogate or a value beyond U+10FFFF decodes as one invalid byte.
template<class Character>
    requires std::same_as<Character, char> || std::same_as<Character, char8_t>
[[nodiscard]] constexpr auto
decode_utf8(std::basic_string_view<Character> text,
            std::size_t offset) noexcept -> Utf8CodePoint
{
    const auto byte = [&](std::size_t index) constexpr { return static_cast<std::uint8_t>(text[index]); };
    const std::uint8_t lead = byte(offset);
    const auto invalid = Utf8CodePoint{.code_point = REPLACEMENT_CHARACTER, .offset = static_cast<std::uint32_t>(offset), .size = 1, .valid = false};
    std::size_t size = 0;
    char32_t code_point = 0;
    char32_t minimum = 0;
    if (lead < 0x80U)
    {
        return {.code_point = lead, .offset = static_cast<std::uint32_t>(offset), .size = 1};
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        size = 2;
        code_point = lead & 0x1FU;
        minimum = 0x80;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        size = 3;
        code_point = lead & 0x0FU;
        minimum = 0x800;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        size = 4;
        code_point = lead & 0x07U;
        minimum = 0x10000;
    }
    else
    {
        return invalid;
    }
    if (offset + size > text.size())
    {
        return invalid;
    }
    for (std::size_t index = 1; index < size; ++index)
    {
        const std::uint8_t continuation = byte(offset + index);
        if ((continuation & 0xC0U) != 0x80U)
        {
            return invalid;
        }
        code_point = (code_point << 6) | (continuation & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
    {
        return invalid;
    }
    return {.code_point = code_point, .offset = static_cast<std::uint32_t>(offset), .size = static_cast<std::uint8_t>(size)};
}

/// @brief The code points of a UTF-8 text, in order: `for (const Utf8CodePoint point : Utf8CodePoints{text})`.
template<class Character>
    requires std::same_as<Character, char> || std::same_as<Character, char8_t>
class Utf8CodePoints
{
public:
    /// @brief Iterates the code points.
    class Iterator
    {
    public:
        using value_type = Utf8CodePoint;
        using difference_type = std::ptrdiff_t;

        constexpr Iterator() noexcept = default;

        constexpr Iterator(std::basic_string_view<Character> text,
                           std::size_t offset) noexcept
            : m_text{text}
            , m_offset{offset}
        {
        }

        [[nodiscard]] constexpr auto operator*() const noexcept -> Utf8CodePoint
        {
            return decode_utf8(m_text, m_offset);
        }

        constexpr auto operator++() noexcept -> Iterator&
        {
            m_offset += decode_utf8(m_text, m_offset).size;
            return *this;
        }

        constexpr auto operator++(int) noexcept -> Iterator
        {
            Iterator previous = *this;
            ++*this;
            return previous;
        }

        [[nodiscard]] constexpr auto operator==(const Iterator& other) const noexcept -> bool
        {
            return m_offset == other.m_offset;
        }

    private:
        std::basic_string_view<Character> m_text{};
        std::size_t m_offset = 0;
    };

    /// @brief The code points of @p text.
    constexpr explicit Utf8CodePoints(std::basic_string_view<Character> text) noexcept
        : m_text{text}
    {
    }

    [[nodiscard]] constexpr auto begin() const noexcept -> Iterator
    {
        return {m_text, 0};
    }

    [[nodiscard]] constexpr auto end() const noexcept -> Iterator
    {
        return {m_text, m_text.size()};
    }

private:
    std::basic_string_view<Character> m_text{};
};

Utf8CodePoints(std::string_view) -> Utf8CodePoints<char>;
Utf8CodePoints(std::u8string_view) -> Utf8CodePoints<char8_t>;

/// @brief Whether @p text is valid UTF-8.
template<class Character>
    requires std::same_as<Character, char> || std::same_as<Character, char8_t>
[[nodiscard]] constexpr auto
is_valid_utf8(std::basic_string_view<Character> text) noexcept -> bool
{
    for (const Utf8CodePoint point : Utf8CodePoints<Character>{text})
    {
        if (!point.valid)
        {
            return false;
        }
    }
    return true;
}

/// @brief Whether @p code_point is drawn: not a control character (C0, DEL, C1), whose layout is a break or
///        nothing.
[[nodiscard]] constexpr auto
is_drawn(char32_t code_point) noexcept -> bool
{
    return code_point >= 0x20 && (code_point < 0x7F || code_point > 0x9F);
}

} // namespace gfx::ui
