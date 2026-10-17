/// @file font_source.cppm
/// @brief FontSource: the bytes of a font file, shared by the faces opened from it, and the detection of
///        its container format from its first bytes.
export module gfx.ui.atlas_tools:font_source;

import std;
import gfx.ui.enums;

export namespace gfx::ui
{

/// @brief The container format of the font in @p bytes, from its magic number and, for OpenType with
///        PostScript outlines, its table directory (CFF or CFF2); nullopt for anything else (Type 1, a
///        bitmap font, not a font).
[[nodiscard]] constexpr auto
detect_font_format(std::span<const std::byte> bytes) noexcept -> std::optional<FontFormat>
{
    // Big-endian fields of the sfnt header and table directory.
    const auto big_endian = [&](std::size_t offset, std::size_t size) constexpr -> std::uint32_t
    {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < size; ++index)
        {
            value = (value << 8) | std::to_integer<std::uint32_t>(bytes[offset + index]);
        }
        return value;
    };
    if (bytes.size() < 12)
    {
        return std::nullopt;
    }
    switch (big_endian(0, 4))
    {
        case 0x00010000U:
        case 0x74727565U: // 'true' (Apple TrueType)
            return FontFormat::TRUETYPE;
        case 0x74746366U: // 'ttcf'
            return FontFormat::COLLECTION;
        case 0x774F4646U: // 'wOFF'
            return FontFormat::WOFF;
        case 0x774F4632U: // 'wOF2'
            return FontFormat::WOFF2;
        case 0x4F54544FU: // 'OTTO': CFF2 when its table directory lists 'CFF2'
        {
            const std::size_t table_count = big_endian(4, 2);
            for (std::size_t table = 0; table < table_count && 12 + table * 16 + 4 <= bytes.size(); ++table)
            {
                if (big_endian(12 + table * 16, 4) == 0x43464632U)
                {
                    return FontFormat::OPENTYPE_CFF2;
                }
            }
            return FontFormat::OPENTYPE_CFF;
        }
        default:
            return std::nullopt;
    }
}

/// @brief The bytes of a font file (or of a font in memory) and its name for messages. Copies share the
///        bytes; faces opened from a source keep a copy, so the source may be destroyed before them.
class FontSource
{
public:
    /// @brief Reads the font file @p path.
    /// @throws gfx::InvalidUsageError when the file cannot be read.
    [[nodiscard]] static auto from_file(const std::filesystem::path& path) -> FontSource;

    /// @brief A font held in memory (the bytes are copied); @p name identifies it in messages.
    [[nodiscard]] static auto from_memory(std::span<const std::byte> bytes,
                                          std::string name = "font in memory") -> FontSource;

    /// @brief A font held in memory, taking @p bytes; @p name identifies it in messages.
    [[nodiscard]] static auto from_memory(std::vector<std::byte>&& bytes,
                                          std::string name = "font in memory") -> FontSource;

    /// @brief The bytes of the font.
    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>
    {
        return *m_bytes;
    }

    /// @brief File name (or the name given for memory), for messages.
    [[nodiscard]] auto name() const noexcept -> std::string_view
    {
        return m_name;
    }

    /// @brief The container format detected from the bytes (detect_font_format()).
    [[nodiscard]] auto format() const noexcept -> std::optional<FontFormat>
    {
        return detect_font_format(*m_bytes);
    }

    /// @brief xxHash64 of the bytes (stored in atlases built from this source).
    [[nodiscard]] auto hash() const noexcept -> std::uint64_t
    {
        return m_hash;
    }

private:
    FontSource(std::shared_ptr<const std::vector<std::byte>> bytes,
               std::string name);

    std::shared_ptr<const std::vector<std::byte>> m_bytes{};
    std::string m_name{};
    std::uint64_t m_hash = 0;
};

} // namespace gfx::ui
