/// @file types.cppm
/// @brief Small value types shared by the gfx libraries: versions, extents, offsets, rectangles and
///        two-dimensional vectors.
export module gfx.core.types;

import std;

export namespace gfx
{

/// @brief Version number (major.minor.patch).
struct Version
{
    std::uint32_t major = 0; ///< Incompatible changes.
    std::uint32_t minor = 0; ///< Compatible additions.
    std::uint32_t patch = 0; ///< Fixes.

    constexpr auto operator<=>(const Version&) const = default;
};

/// @brief Size of an image, a region or a window (depth is 1 for one- and two-dimensional sizes).
struct Extent
{
    std::uint32_t width = 0;  ///< Size along x.
    std::uint32_t height = 1; ///< Size along y.
    std::uint32_t depth = 1;  ///< Size along z.

    constexpr bool operator==(const Extent&) const = default;
};

/// @brief Position of a texel in an image, or of a window on the desktop.
struct Offset
{
    std::int32_t x = 0; ///< Position along x.
    std::int32_t y = 0; ///< Position along y.
    std::int32_t z = 0; ///< Position along z.

    constexpr bool operator==(const Offset&) const = default;
};

/// @brief Axis-aligned rectangle (framebuffer, window or monitor coordinates).
struct Rectangle
{
    std::int32_t x = 0;       ///< Left edge.
    std::int32_t y = 0;       ///< Top edge.
    std::uint32_t width = 0;  ///< Width.
    std::uint32_t height = 0; ///< Height.

    constexpr bool operator==(const Rectangle&) const = default;
};

/// @brief Two-dimensional vector of floats (cursor positions, wheel offsets, stick values).
struct Vector2
{
    float x = 0.0F; ///< Component along x.
    float y = 0.0F; ///< Component along y.

    constexpr bool operator==(const Vector2&) const = default;
};

} // namespace gfx
