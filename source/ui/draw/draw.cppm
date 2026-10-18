/// @file draw.cppm
/// @brief gfx.ui.draw: what the UI draws, as data a renderer turns into one indexed draw per command.
///        Shapes (rectangles, borders, circles, lines) are single quads whose signed distance the shader
///        evaluates (crisp at any scale); polylines and polygons are triangles with an anti-aliased fringe;
///        text is one quad per glyph sampling its atlas page, with outlines and shadows. Positions are
///        pixels of the target, y down; a transform stack (rotation, scale) and a clip stack apply.
export module gfx.ui.draw;

import std;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.text;

export namespace gfx::ui
{

/// @brief A color: sRGB-encoded channels from 0 to 1, alpha not premultiplied.
struct Color
{
    float red = 0;   ///< Red.
    float green = 0; ///< Green.
    float blue = 0;  ///< Blue.
    float alpha = 1; ///< Opacity.

    /// @brief The color of 8-bit channels.
    [[nodiscard]] static constexpr auto from_rgba8(std::uint8_t red,
                                                   std::uint8_t green,
                                                   std::uint8_t blue,
                                                   std::uint8_t alpha = 255) noexcept -> Color
    {
        return {.red = static_cast<float>(red) / 255.0F, .green = static_cast<float>(green) / 255.0F, .blue = static_cast<float>(blue) / 255.0F,
                .alpha = static_cast<float>(alpha) / 255.0F};
    }

    /// @brief The same color with opacity @p value.
    [[nodiscard]] constexpr auto with_alpha(float value) const noexcept -> Color
    {
        return {.red = red, .green = green, .blue = blue, .alpha = value};
    }

    /// @brief The color as 8-bit channels in one word: red in the low byte, alpha in the high byte.
    [[nodiscard]] constexpr auto packed() const noexcept -> std::uint32_t
    {
        const auto channel = [](float value) constexpr
        {
            const float clamped = value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
            return static_cast<std::uint32_t>(clamped * 255.0F + 0.5F);
        };
        return channel(red) | (channel(green) << 8) | (channel(blue) << 16) | (channel(alpha) << 24);
    }

    /// @brief Colors compare channel by channel.
    [[nodiscard]] constexpr auto operator==(const Color&) const noexcept -> bool = default;
};

/// @brief A color as hue, saturation and value (each from 0 to 1) and opacity.
struct HsvColor
{
    float hue = 0;        ///< 0 red, 1/3 green, 2/3 blue (1 is red again).
    float saturation = 0; ///< 0 gray, 1 full color.
    float value = 0;      ///< 0 black, 1 full brightness.
    float alpha = 1;      ///< Opacity.

    /// @brief Colors compare channel by channel.
    [[nodiscard]] constexpr auto operator==(const HsvColor&) const noexcept -> bool = default;
};

/// @brief @p color as hue, saturation and value (of its sRGB-encoded channels, as color pickers show them).
[[nodiscard]] constexpr auto
to_hsv(Color color) noexcept -> HsvColor
{
    const float maximum = std::max({color.red, color.green, color.blue});
    const float minimum = std::min({color.red, color.green, color.blue});
    const float range = maximum - minimum;
    float hue = 0;
    if (range > 0)
    {
        if (maximum == color.red)
        {
            hue = (color.green - color.blue) / range;
            hue = hue < 0 ? hue + 6.0F : hue;
        }
        else if (maximum == color.green)
        {
            hue = (color.blue - color.red) / range + 2.0F;
        }
        else
        {
            hue = (color.red - color.green) / range + 4.0F;
        }
        hue /= 6.0F;
    }
    return {.hue = hue, .saturation = maximum > 0 ? range / maximum : 0.0F, .value = maximum, .alpha = color.alpha};
}

/// @brief The color of @p hsv (hue wraps around).
[[nodiscard]] constexpr auto
from_hsv(HsvColor hsv) noexcept -> Color
{
    // The hue's sector of six, and where in it.
    float hue = hsv.hue - static_cast<float>(static_cast<long long>(hsv.hue));
    hue = hue < 0 ? hue + 1.0F : hue;
    const float scaled = hue * 6.0F;
    const auto sector = static_cast<int>(scaled) % 6;
    const float fraction = scaled - static_cast<float>(static_cast<int>(scaled));
    const float value = hsv.value;
    const float low = value * (1.0F - hsv.saturation);
    const float falling = value * (1.0F - hsv.saturation * fraction);
    const float rising = value * (1.0F - hsv.saturation * (1.0F - fraction));
    switch (sector)
    {
        case 0:  return {.red = value, .green = rising, .blue = low, .alpha = hsv.alpha};
        case 1:  return {.red = falling, .green = value, .blue = low, .alpha = hsv.alpha};
        case 2:  return {.red = low, .green = value, .blue = rising, .alpha = hsv.alpha};
        case 3:  return {.red = low, .green = falling, .blue = value, .alpha = hsv.alpha};
        case 4:  return {.red = rising, .green = low, .blue = value, .alpha = hsv.alpha};
        default: return {.red = value, .green = low, .blue = falling, .alpha = hsv.alpha};
    }
}

/// @brief A color written as "#RRGGBB" or "#RRGGBBAA".
struct HexadecimalColor
{
    std::array<char, 9> characters{}; ///< The characters.
    std::uint8_t size = 0;            ///< How many are used.

    /// @brief The text.
    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return {characters.data(), size};
    }
};

/// @brief @p color as "#RRGGBB", or "#RRGGBBAA" with @p alpha.
[[nodiscard]] constexpr auto
to_hexadecimal(Color color,
               bool alpha = true) noexcept -> HexadecimalColor
{
    constexpr std::string_view DIGITS = "0123456789ABCDEF";
    const std::uint32_t packed = color.packed();
    HexadecimalColor result{.size = static_cast<std::uint8_t>(alpha ? 9 : 7)};
    result.characters[0] = '#';
    for (std::uint32_t channel = 0; channel < (alpha ? 4U : 3U); ++channel)
    {
        const std::uint32_t byte = (packed >> (8U * channel)) & 0xFFU;
        result.characters[1 + channel * 2] = DIGITS[byte >> 4U];
        result.characters[2 + channel * 2] = DIGITS[byte & 0xFU];
    }
    return result;
}

/// @brief The color "#RRGGBB" or "#RRGGBBAA" (the '#' optional, digits in either case) names; nothing for other
///        text.
[[nodiscard]] constexpr auto
from_hexadecimal(std::string_view text) noexcept -> std::optional<Color>
{
    if (!text.empty() && text.front() == '#')
    {
        text.remove_prefix(1);
    }
    if (text.size() != 6 && text.size() != 8)
    {
        return std::nullopt;
    }
    const auto digit = [](char character) constexpr -> int
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f')
        {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F')
        {
            return character - 'A' + 10;
        }
        return -1;
    };
    std::array<std::uint8_t, 4> channels{0, 0, 0, 255};
    for (std::size_t channel = 0; channel < text.size() / 2; ++channel)
    {
        const int high = digit(text[channel * 2]);
        const int low = digit(text[channel * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }
        channels[channel] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Color::from_rgba8(channels[0], channels[1], channels[2], channels[3]);
}

/// @brief An axis-aligned rectangle in pixels (y down).
struct UiRectangle
{
    float left = 0;   ///< Left edge.
    float top = 0;    ///< Top edge.
    float right = 0;  ///< Right edge.
    float bottom = 0; ///< Bottom edge.

    /// @brief Width (0 when right is left of left).
    [[nodiscard]] constexpr auto width() const noexcept -> float
    {
        return right > left ? right - left : 0.0F;
    }

    /// @brief Height (0 when bottom is above top).
    [[nodiscard]] constexpr auto height() const noexcept -> float
    {
        return bottom > top ? bottom - top : 0.0F;
    }

    /// @brief Whether it covers no area.
    [[nodiscard]] constexpr auto empty() const noexcept -> bool
    {
        return !(right > left && bottom > top);
    }

    /// @brief The part of it inside @p other (empty when they do not overlap).
    [[nodiscard]] constexpr auto intersection(const UiRectangle& other) const noexcept -> UiRectangle
    {
        return {.left = left > other.left ? left : other.left,
                .top = top > other.top ? top : other.top,
                .right = right < other.right ? right : other.right,
                .bottom = bottom < other.bottom ? bottom : other.bottom};
    }

    /// @brief Rectangles compare edge by edge.
    [[nodiscard]] constexpr auto operator==(const UiRectangle&) const noexcept -> bool = default;
};

/// @brief An affine transform of the plane: p' = (xx·x + xy·y + x_offset, yx·x + yy·y + y_offset).
struct Transform2D
{
    float xx = 1;       ///< x' per x.
    float yx = 0;       ///< y' per x.
    float xy = 0;       ///< x' per y.
    float yy = 1;       ///< y' per y.
    float x_offset = 0; ///< Added to x'.
    float y_offset = 0; ///< Added to y'.

    /// @brief A translation by @p offset.
    [[nodiscard]] static constexpr auto translation(Vector2 offset) noexcept -> Transform2D
    {
        return {.x_offset = offset.x, .y_offset = offset.y};
    }

    /// @brief A scaling by @p x and @p y about the origin.
    [[nodiscard]] static constexpr auto scaling(float x,
                                                float y) noexcept -> Transform2D
    {
        return {.xx = x, .yy = y};
    }

    /// @brief A rotation by @p radians about the origin, clockwise on screen (y points down).
    [[nodiscard]] static auto rotation(float radians) noexcept -> Transform2D
    {
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return {.xx = cosine, .yx = sine, .xy = -sine, .yy = cosine};
    }

    /// @brief @p transform applied about @p pivot (moved to the origin, transformed, moved back).
    [[nodiscard]] static constexpr auto about(Vector2 pivot,
                                              const Transform2D& transform) noexcept -> Transform2D
    {
        return translation(pivot) * transform * translation({.x = -pivot.x, .y = -pivot.y});
    }

    /// @brief The transformed @p point.
    [[nodiscard]] constexpr auto apply(Vector2 point) const noexcept -> Vector2
    {
        return {.x = xx * point.x + xy * point.y + x_offset, .y = yx * point.x + yy * point.y + y_offset};
    }

    /// @brief The transformed direction @p vector (no translation).
    [[nodiscard]] constexpr auto apply_to_vector(Vector2 vector) const noexcept -> Vector2
    {
        return {.x = xx * vector.x + xy * vector.y, .y = yx * vector.x + yy * vector.y};
    }

    /// @brief The transform applying @p right, then @p left.
    [[nodiscard]] friend constexpr auto operator*(const Transform2D& left,
                                                  const Transform2D& right) noexcept -> Transform2D
    {
        return {.xx = left.xx * right.xx + left.xy * right.yx,
                .yx = left.yx * right.xx + left.yy * right.yx,
                .xy = left.xx * right.xy + left.xy * right.yy,
                .yy = left.yx * right.xy + left.yy * right.yy,
                .x_offset = left.xx * right.x_offset + left.xy * right.y_offset + left.x_offset,
                .y_offset = left.yx * right.x_offset + left.yy * right.y_offset + left.y_offset};
    }

    /// @brief Transforms compare member by member.
    [[nodiscard]] constexpr auto operator==(const Transform2D&) const noexcept -> bool = default;
};

/// @brief What a draw command samples.
struct DrawTexture
{
    DrawTextureKind kind = DrawTextureKind::NONE; ///< Nothing, a font atlas page or an image.
    FontAtlasHandle atlas{};                      ///< FONT_PAGE: the atlas.
    std::uint32_t page = 0;                       ///< FONT_PAGE: the page.
    std::uint32_t image = 0;                      ///< IMAGE: the image's bindless sampled-image index.

    /// @brief Page @p page of @p atlas.
    [[nodiscard]] static constexpr auto font_page(FontAtlasHandle atlas,
                                                  std::uint32_t page) noexcept -> DrawTexture
    {
        return {.kind = DrawTextureKind::FONT_PAGE, .atlas = atlas, .page = page};
    }

    /// @brief The image of bindless sampled-image index @p index.
    [[nodiscard]] static constexpr auto from_image(std::uint32_t index) noexcept -> DrawTexture
    {
        return {.kind = DrawTextureKind::IMAGE, .image = index};
    }

    /// @brief Textures compare member by member.
    [[nodiscard]] constexpr auto operator==(const DrawTexture&) const noexcept -> bool = default;
};

/// @brief A vertex as the UI shader reads it (std430 layout, 48 bytes).
struct UiVertex
{
    Vector2 position{};              ///< Pixels of the target.
    Vector2 local{};                 ///< SHAPE: pixels from the shape's center in its frame; GLYPH, IMAGE: texture coordinates.
    std::array<float, 4> shape{};    ///< SHAPE: half width, half height, corner radius, border width (0: filled);
                                     ///< GLYPH: distance range (texels), outline width, 0, 0; GLYPH_SHADOW: range, 0, softness, 0.
    std::uint32_t color = 0;         ///< Color::packed().
    std::uint32_t secondary_color = 0; ///< GLYPH: outline color.
    std::uint32_t mode = 0;          ///< UiShaderMode in bits 0 to 7, the atlas's SdfKind in bits 8 to 15.
    std::uint32_t reserved = 0;      ///< 0.
};

static_assert(sizeof(UiVertex) == 48, "UiVertex matches the shader's std430 struct");

/// @brief Triangles of one clip rectangle and one texture: one indexed draw.
struct DrawCommand
{
    UiRectangle clip{};          ///< Pixels drawn into (the renderer clamps it to its viewport).
    DrawTexture texture{};       ///< What the triangles sample.
    std::uint32_t first_index = 0; ///< First index of the list's index array.
    std::uint32_t index_count = 0; ///< Indices.
};

/// @brief How text is drawn.
struct TextStyle
{
    Color color{.red = 1, .green = 1, .blue = 1, .alpha = 1}; ///< Fill.
    Color outline_color{};                                    ///< Outline, when outline_width > 0.
    float outline_width = 0;                                  ///< Pixels around the glyphs (within the atlas's distance range).
    Color shadow_color{.alpha = 0};                           ///< Shadow or glow, drawn when not transparent.
    Vector2 shadow_offset{};                                  ///< Pixels from the text (0, 0: a glow).
    float shadow_softness = 0;                                ///< Pixels of blur at its edge.
};

/// @brief Primitives to draw, in order, with their commands. clear() keeps the arrays' capacity: a list
///        refilled every frame allocates nothing once it has grown.
class DrawList
{
public:
    /// @brief An empty list, unclipped, untransformed.
    DrawList();

    /// @brief Removes every primitive and resets the clip and transform stacks.
    void clear() noexcept;

    /// @brief Pixels per unit of this list's coordinates (UiDrawData::scale): anti-aliased edges and fringes stay
    ///        one pixel wide (1 by default; kept by clear()).
    void set_pixels_per_unit(float pixels_per_unit) noexcept
    {
        m_pixels_per_unit = pixels_per_unit > 0 ? pixels_per_unit : 1.0F;
    }

    /// @brief Clips what follows to @p clip (pixels of the target), within the current clip unless
    ///        @p within_current is false (then @p clip alone: a window's menu bar, outside its content's clip).
    void push_clip_rectangle(const UiRectangle& clip,
                             bool within_current = true);

    /// @brief Restores the clip of before the last push_clip_rectangle().
    void pop_clip_rectangle() noexcept;

    /// @brief Transforms what follows by @p transform, after the current transform.
    void push_transform(const Transform2D& transform);

    /// @brief Restores the transform of before the last push_transform().
    void pop_transform() noexcept;

    /// @brief A filled rectangle, its corners rounded by @p corner_radius pixels.
    void add_rectangle_filled(const UiRectangle& rectangle,
                              Color color,
                              float corner_radius = 0);

    /// @brief A rectangle's border, @p thickness pixels wide inside its edge.
    void add_rectangle(const UiRectangle& rectangle,
                       Color color,
                       float thickness = 1,
                       float corner_radius = 0);

    /// @brief A filled circle.
    void add_circle_filled(Vector2 center,
                           float radius,
                           Color color);

    /// @brief A circle's ring, @p thickness pixels wide inside its radius.
    void add_circle(Vector2 center,
                    float radius,
                    Color color,
                    float thickness = 1);

    /// @brief A line from @p from to @p to, @p thickness pixels wide, with square ends.
    void add_line(Vector2 from,
                  Vector2 to,
                  Color color,
                  float thickness = 1);

    /// @brief Lines through @p points, joined, @p thickness pixels wide, with an anti-aliased fringe.
    void add_polyline(std::span<const Vector2> points,
                      Color color,
                      float thickness = 1,
                      bool closed = false);

    /// @brief A filled convex polygon of @p points, with an anti-aliased fringe.
    void add_convex_polygon_filled(std::span<const Vector2> points,
                                   Color color);

    /// @brief A rectangle whose corners have their own colors, blended across it (no anti-aliased edge: the inside
    ///        of a widget, such as a color picker's square).
    void add_rectangle_filled_gradient(const UiRectangle& rectangle,
                                       Color top_left,
                                       Color top_right,
                                       Color bottom_right,
                                       Color bottom_left);

    /// @brief A triangle whose vertices have their own colors, blended across it (no anti-aliased edge).
    void add_triangle_filled(const std::array<Vector2, 3>& points,
                             const std::array<Color, 3>& colors);

    /// @brief The image of bindless sampled-image index @p image over @p rectangle, its @p texture_coordinates
    ///        (0 to 1) tinted by @p tint.
    void add_image(std::uint32_t image,
                   const UiRectangle& rectangle,
                   Color tint = {.red = 1, .green = 1, .blue = 1, .alpha = 1},
                   const UiRectangle& texture_coordinates = {.left = 0, .top = 0, .right = 1, .bottom = 1});

    /// @brief The glyphs of @p layout, its top-left corner at @p origin, with @p style; atlases from @p fonts.
    void add_text(const FontLibrary& fonts,
                  const TextLayout& layout,
                  Vector2 origin,
                  const TextStyle& style = {});

    /// @brief Vertices (indices refer to them).
    [[nodiscard]] auto vertices() const noexcept -> std::span<const UiVertex>
    {
        return m_vertices;
    }

    /// @brief Indices, three per triangle.
    [[nodiscard]] auto indices() const noexcept -> std::span<const std::uint32_t>
    {
        return m_indices;
    }

    /// @brief Commands, in drawing order.
    [[nodiscard]] auto commands() const noexcept -> std::span<const DrawCommand>
    {
        return m_commands;
    }

private:
    /// @brief A rounded box of half size (@p half_width, @p half_height) about @p center, with axes @p axis_x
    ///        and @p axis_y (unit vectors of its frame, before the transform).
    void add_box(Vector2 center,
                 Vector2 axis_x,
                 Vector2 axis_y,
                 float half_width,
                 float half_height,
                 float corner_radius,
                 float border,
                 Color color);

    /// @brief A quad of corners @p positions (before the transform, clockwise from the top left).
    void add_quad(const std::array<Vector2, 4>& positions,
                  const std::array<Vector2, 4>& locals,
                  const std::array<float, 4>& shape,
                  std::uint32_t color,
                  std::uint32_t secondary_color,
                  std::uint32_t mode,
                  const DrawTexture& texture);

    /// @brief The command the next triangles go to: the current one when its clip and texture fit.
    auto command_for(const DrawTexture& texture) -> DrawCommand&;

    /// @brief Units before the transform per target pixel (anti-aliasing margins and fringes).
    [[nodiscard]] auto pixel_size() const noexcept -> float;

    std::vector<UiVertex> m_vertices{};
    std::vector<std::uint32_t> m_indices{};
    std::vector<DrawCommand> m_commands{};
    std::vector<UiRectangle> m_clips{};
    std::vector<Transform2D> m_transforms{};
    float m_pixels_per_unit = 1.0F;
};

/// @brief What a frame of the UI draws: draw lists in order over a target of width × height pixels.
struct UiDrawData
{
    std::span<const DrawList* const> lists{}; ///< Drawn in order.
    float width = 0;                          ///< Pixels of the target area (the viewport).
    float height = 0;                         ///< Pixels of the target area.
    float scale = 1;                          ///< Pixels per unit of the lists' coordinates (a UI in points on a
                                              ///< high-density display: its content scale).
};

} // namespace gfx::ui
