/// @file draw_list.cpp
/// @brief DrawList: quads of shapes, images and glyphs, triangles of polylines and polygons, and the
///        commands they go to (a new one when the clip rectangle or the texture changes).
module gfx.ui.draw;

import std;
import gfx.ui.atlas_format;
import gfx.ui.handles;
import gfx.ui.text;

namespace gfx::ui
{

namespace
{

/// The clip of a list nobody clipped: everything.
constexpr UiRectangle UNCLIPPED{.left = -1.0e9F, .top = -1.0e9F, .right = 1.0e9F, .bottom = 1.0e9F};

/// Largest length of a miter, in line half widths.
constexpr float MITER_LIMIT = 4.0F;

constexpr auto
plus(Vector2 left,
     Vector2 right) noexcept -> Vector2
{
    return {.x = left.x + right.x, .y = left.y + right.y};
}

constexpr auto
minus(Vector2 left,
      Vector2 right) noexcept -> Vector2
{
    return {.x = left.x - right.x, .y = left.y - right.y};
}

constexpr auto
times(Vector2 vector,
      float factor) noexcept -> Vector2
{
    return {.x = vector.x * factor, .y = vector.y * factor};
}

constexpr auto
dot(Vector2 left,
    Vector2 right) noexcept -> float
{
    return left.x * right.x + left.y * right.y;
}

auto
normalized(Vector2 vector) noexcept -> Vector2
{
    const float length = std::hypot(vector.x, vector.y);
    return length > 0 ? times(vector, 1.0F / length) : Vector2{};
}

/// The direction of @p vector turned a quarter clockwise on screen (y down).
constexpr auto
perpendicular(Vector2 vector) noexcept -> Vector2
{
    return {.x = -vector.y, .y = vector.x};
}

/// The mode word of @p mode for an atlas of @p kind.
constexpr auto
mode_of(UiShaderMode mode,
        SdfKind kind = SdfKind::SDF) noexcept -> std::uint32_t
{
    return std::uint32_t{std::to_underlying(mode)} | (std::uint32_t{std::to_underlying(kind)} << 8);
}

} // namespace

DrawList::DrawList()
{
    m_clips.reserve(8);
    m_transforms.reserve(8);
    clear();
}

void
DrawList::clear() noexcept
{
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    m_clips.resize(1);
    m_clips.front() = UNCLIPPED;
    m_transforms.resize(1);
    m_transforms.front() = Transform2D{};
}

void
DrawList::push_clip_rectangle(const UiRectangle& clip,
                              bool within_current)
{
    m_clips.push_back(within_current ? clip.intersection(m_clips.back()) : clip);
}

void
DrawList::pop_clip_rectangle() noexcept
{
    if (m_clips.size() > 1)
    {
        m_clips.pop_back();
    }
}

void
DrawList::push_transform(const Transform2D& transform)
{
    m_transforms.push_back(m_transforms.back() * transform);
}

void
DrawList::pop_transform() noexcept
{
    if (m_transforms.size() > 1)
    {
        m_transforms.pop_back();
    }
}

auto
DrawList::pixel_size() const noexcept -> float
{
    const Transform2D& transform = m_transforms.back();
    const float determinant = std::abs(transform.xx * transform.yy - transform.xy * transform.yx);
    return (determinant > 0 ? 1.0F / std::sqrt(determinant) : 1.0F) / m_pixels_per_unit;
}

auto
DrawList::command_for(const DrawTexture& texture) -> DrawCommand&
{
    // The current command when the clip is the same and the textures agree (shapes sample nothing).
    const UiRectangle& clip = m_clips.back();
    if (!m_commands.empty())
    {
        DrawCommand& last = m_commands.back();
        const bool textures_agree = texture.kind == DrawTextureKind::NONE || last.texture.kind == DrawTextureKind::NONE || last.texture == texture;
        if (last.clip == clip && textures_agree)
        {
            if (last.texture.kind == DrawTextureKind::NONE)
            {
                last.texture = texture;
            }
            return last;
        }
    }
    m_commands.push_back({.clip = clip, .texture = texture, .first_index = static_cast<std::uint32_t>(m_indices.size())});
    return m_commands.back();
}

void
DrawList::add_quad(const std::array<Vector2, 4>& positions,
                   const std::array<Vector2, 4>& locals,
                   const std::array<float, 4>& shape,
                   std::uint32_t color,
                   std::uint32_t secondary_color,
                   std::uint32_t mode,
                   const DrawTexture& texture)
{
    DrawCommand& command = command_for(texture);
    const auto base = static_cast<std::uint32_t>(m_vertices.size());
    const Transform2D& transform = m_transforms.back();
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        m_vertices.push_back({.position = transform.apply(positions[corner]),
                              .local = locals[corner],
                              .shape = shape,
                              .color = color,
                              .secondary_color = secondary_color,
                              .mode = mode});
    }
    for (const std::uint32_t index : {0U, 1U, 2U, 0U, 2U, 3U})
    {
        m_indices.push_back(base + index);
    }
    command.index_count += 6;
}

void
DrawList::add_box(Vector2 center,
                  Vector2 axis_x,
                  Vector2 axis_y,
                  float half_width,
                  float half_height,
                  float corner_radius,
                  float border,
                  Color color)
{
    if (!(half_width > 0 && half_height > 0) || color.alpha <= 0)
    {
        return;
    }
    // The quad grows by a pixel past the shape (its anti-aliased edge), in the shape's own units.
    const float margin = pixel_size();
    const float extent_x = half_width + margin;
    const float extent_y = half_height + margin;
    const float smallest = std::min(half_width, half_height);
    const std::array<Vector2, 4> locals{Vector2{.x = -extent_x, .y = -extent_y}, Vector2{.x = extent_x, .y = -extent_y},
                                        Vector2{.x = extent_x, .y = extent_y}, Vector2{.x = -extent_x, .y = extent_y}};
    std::array<Vector2, 4> positions{};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        positions[corner] = plus(center, plus(times(axis_x, locals[corner].x), times(axis_y, locals[corner].y)));
    }
    add_quad(positions, locals, {half_width, half_height, std::clamp(corner_radius, 0.0F, smallest), std::clamp(border, 0.0F, smallest)},
             color.packed(), 0, mode_of(UiShaderMode::SHAPE), DrawTexture{});
}

void
DrawList::add_rectangle_filled(const UiRectangle& rectangle,
                               Color color,
                               float corner_radius)
{
    const Vector2 center{.x = (rectangle.left + rectangle.right) / 2, .y = (rectangle.top + rectangle.bottom) / 2};
    add_box(center, {.x = 1}, {.y = 1}, rectangle.width() / 2, rectangle.height() / 2, corner_radius, 0, color);
}

void
DrawList::add_rectangle(const UiRectangle& rectangle,
                        Color color,
                        float thickness,
                        float corner_radius)
{
    if (thickness <= 0)
    {
        return;
    }
    const Vector2 center{.x = (rectangle.left + rectangle.right) / 2, .y = (rectangle.top + rectangle.bottom) / 2};
    add_box(center, {.x = 1}, {.y = 1}, rectangle.width() / 2, rectangle.height() / 2, corner_radius, thickness, color);
}

void
DrawList::add_circle_filled(Vector2 center,
                            float radius,
                            Color color)
{
    add_box(center, {.x = 1}, {.y = 1}, radius, radius, radius, 0, color);
}

void
DrawList::add_circle(Vector2 center,
                     float radius,
                     Color color,
                     float thickness)
{
    if (thickness <= 0)
    {
        return;
    }
    add_box(center, {.x = 1}, {.y = 1}, radius, radius, radius, thickness, color);
}

void
DrawList::add_line(Vector2 from,
                   Vector2 to,
                   Color color,
                   float thickness)
{
    // A box along the line: its x axis from one end to the other.
    const Vector2 direction = minus(to, from);
    const float length = std::hypot(direction.x, direction.y);
    if (length <= 0 || thickness <= 0)
    {
        return;
    }
    const Vector2 axis = times(direction, 1.0F / length);
    add_box(times(plus(from, to), 0.5F), axis, perpendicular(axis), length / 2, thickness / 2, 0, 0, color);
}

void
DrawList::add_polyline(std::span<const Vector2> points,
                       Color color,
                       float thickness,
                       bool closed)
{
    const std::size_t count = points.size();
    if (count < 2 || thickness <= 0 || color.alpha <= 0)
    {
        return;
    }
    // Four vertices per point: outer edge (transparent), core edge, core edge, outer edge (transparent). The
    // fringe is a pixel wide, centered on the line's edge, so the line covers its thickness; a line thinner
    // than a pixel has no core and fades instead.
    const float fringe = pixel_size();
    const float core = std::max(thickness / 2 - fringe / 2, 0.0F);
    const float outer = core + fringe;
    const float fade = std::min(thickness / fringe, 1.0F);
    const std::size_t segments = closed ? count : count - 1;
    const auto segment_normal = [&](std::size_t segment)
    {
        return perpendicular(normalized(minus(points[(segment + 1) % count], points[segment])));
    };
    DrawCommand& command = command_for(DrawTexture{});
    const auto base = static_cast<std::uint32_t>(m_vertices.size());
    const Transform2D& transform = m_transforms.back();
    const std::uint32_t opaque = color.with_alpha(color.alpha * fade).packed();
    const std::uint32_t transparent = color.with_alpha(0).packed();
    for (std::size_t point = 0; point < count; ++point)
    {
        // The miter of the two segments meeting at the point (one at an open end), limited.
        const bool has_previous = closed || point > 0;
        const bool has_next = closed || point + 1 < count;
        const Vector2 previous = segment_normal(has_previous ? (point + count - 1) % count : 0);
        const Vector2 next = segment_normal(has_next ? point % segments : segments - 1);
        Vector2 miter = has_previous && has_next ? normalized(plus(previous, next)) : (has_next ? next : previous);
        const float cosine = dot(miter, has_next ? next : previous);
        miter = times(miter, 1.0F / std::max(cosine, 1.0F / MITER_LIMIT));
        for (const auto& [offset, packed] : {std::pair{outer, transparent}, std::pair{core, opaque}, std::pair{-core, opaque},
                                             std::pair{-outer, transparent}})
        {
            m_vertices.push_back({.position = transform.apply(plus(points[point], times(miter, offset))),
                                  .color = packed,
                                  .mode = mode_of(UiShaderMode::SOLID)});
        }
    }
    // Three quads per segment: outer fringe, line, outer fringe.
    for (std::size_t segment = 0; segment < segments; ++segment)
    {
        const std::uint32_t first = base + static_cast<std::uint32_t>(segment * 4);
        const std::uint32_t second = base + static_cast<std::uint32_t>(((segment + 1) % count) * 4);
        for (std::uint32_t strip = 0; strip < 3; ++strip)
        {
            for (const std::uint32_t index : {first + strip, first + strip + 1, second + strip + 1, first + strip, second + strip + 1, second + strip})
            {
                m_indices.push_back(index);
            }
        }
        command.index_count += 18;
    }
}

void
DrawList::add_convex_polygon_filled(std::span<const Vector2> points,
                                    Color color)
{
    const std::size_t count = points.size();
    if (count < 3 || color.alpha <= 0)
    {
        return;
    }
    // Outward normals whatever the winding.
    float area = 0;
    for (std::size_t point = 0; point < count; ++point)
    {
        const Vector2 current = points[point];
        const Vector2 next = points[(point + 1) % count];
        area += current.x * next.y - next.x * current.y;
    }
    const float outward = area > 0 ? -1.0F : 1.0F;
    const float fringe = pixel_size() / 2;
    DrawCommand& command = command_for(DrawTexture{});
    const auto base = static_cast<std::uint32_t>(m_vertices.size());
    const Transform2D& transform = m_transforms.back();
    const std::uint32_t opaque = color.packed();
    const std::uint32_t transparent = color.with_alpha(0).packed();
    for (std::size_t point = 0; point < count; ++point)
    {
        // Inner vertex (opaque) and outer vertex (transparent), half a pixel each side of the edge.
        const Vector2 previous = perpendicular(normalized(minus(points[point], points[(point + count - 1) % count])));
        const Vector2 next = perpendicular(normalized(minus(points[(point + 1) % count], points[point])));
        Vector2 normal = normalized(plus(previous, next));
        normal = times(normal, outward / std::max(dot(normal, next), 1.0F / MITER_LIMIT));
        m_vertices.push_back({.position = transform.apply(minus(points[point], times(normal, fringe))), .color = opaque,
                              .mode = mode_of(UiShaderMode::SOLID)});
        m_vertices.push_back({.position = transform.apply(plus(points[point], times(normal, fringe))), .color = transparent,
                              .mode = mode_of(UiShaderMode::SOLID)});
    }
    // A fan of the inner vertices, then a fringe quad per edge.
    for (std::uint32_t point = 1; point + 1 < count; ++point)
    {
        for (const std::uint32_t index : {base, base + point * 2, base + (point + 1) * 2})
        {
            m_indices.push_back(index);
        }
        command.index_count += 3;
    }
    for (std::uint32_t point = 0; point < count; ++point)
    {
        const std::uint32_t inner = base + point * 2;
        const std::uint32_t next = base + static_cast<std::uint32_t>(((point + 1) % count) * 2);
        for (const std::uint32_t index : {inner, inner + 1, next + 1, inner, next + 1, next})
        {
            m_indices.push_back(index);
        }
        command.index_count += 6;
    }
}

void
DrawList::add_rectangle_filled_gradient(const UiRectangle& rectangle,
                                        Color top_left,
                                        Color top_right,
                                        Color bottom_right,
                                        Color bottom_left)
{
    if (rectangle.empty())
    {
        return;
    }

    // Four solid vertices, their colors interpolated by the rasterizer.
    DrawCommand& command = command_for(DrawTexture{});
    const auto base = static_cast<std::uint32_t>(m_vertices.size());
    const Transform2D& transform = m_transforms.back();
    const std::array corners{Vector2{.x = rectangle.left, .y = rectangle.top}, Vector2{.x = rectangle.right, .y = rectangle.top},
                             Vector2{.x = rectangle.right, .y = rectangle.bottom}, Vector2{.x = rectangle.left, .y = rectangle.bottom}};
    const std::array colors{top_left, top_right, bottom_right, bottom_left};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        m_vertices.push_back({.position = transform.apply(corners[corner]), .color = colors[corner].packed(), .mode = mode_of(UiShaderMode::SOLID)});
    }
    for (const std::uint32_t index : {0U, 1U, 2U, 0U, 2U, 3U})
    {
        m_indices.push_back(base + index);
    }
    command.index_count += 6;
}

void
DrawList::add_triangle_filled(const std::array<Vector2, 3>& points,
                              const std::array<Color, 3>& colors)
{
    DrawCommand& command = command_for(DrawTexture{});
    const auto base = static_cast<std::uint32_t>(m_vertices.size());
    const Transform2D& transform = m_transforms.back();
    for (std::size_t point = 0; point < 3; ++point)
    {
        m_vertices.push_back({.position = transform.apply(points[point]), .color = colors[point].packed(), .mode = mode_of(UiShaderMode::SOLID)});
        m_indices.push_back(base + static_cast<std::uint32_t>(point));
    }
    command.index_count += 3;
}

void
DrawList::add_image(std::uint32_t image,
                    const UiRectangle& rectangle,
                    Color tint,
                    const UiRectangle& texture_coordinates)
{
    if (rectangle.empty())
    {
        return;
    }
    const UiRectangle& uv = texture_coordinates;
    add_quad({Vector2{.x = rectangle.left, .y = rectangle.top}, Vector2{.x = rectangle.right, .y = rectangle.top},
              Vector2{.x = rectangle.right, .y = rectangle.bottom}, Vector2{.x = rectangle.left, .y = rectangle.bottom}},
             {Vector2{.x = uv.left, .y = uv.top}, Vector2{.x = uv.right, .y = uv.top}, Vector2{.x = uv.right, .y = uv.bottom},
              Vector2{.x = uv.left, .y = uv.bottom}},
             {}, tint.packed(), 0, mode_of(UiShaderMode::IMAGE), DrawTexture::from_image(image));
}

void
DrawList::add_text(const FontLibrary& fonts,
                   const TextLayout& layout,
                   Vector2 origin,
                   const TextStyle& style)
{
    // The shadow first (under the text), then the text.
    const bool shadow = style.shadow_color.alpha > 0;
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass)
    {
        const bool shadow_pass = pass == 0;
        const Vector2 offset = shadow_pass ? plus(origin, style.shadow_offset) : origin;
        FontAtlasHandle current{};
        std::shared_ptr<const FontAtlas> atlas;
        for (const LaidOutGlyph& glyph : layout.glyphs)
        {
            // The glyph's atlas (looked up again only when it changes), then its box.
            if (glyph.atlas != current || atlas == nullptr)
            {
                current = glyph.atlas;
                atlas = fonts.atlas(glyph.atlas);
            }
            if (atlas == nullptr)
            {
                continue;
            }
            const std::optional<AtlasGlyph> record = atlas->view().find_glyph(glyph.glyph);
            if (!record.has_value() || !record->has_box())
            {
                continue;
            }
            const AtlasDescriptor& descriptor = atlas->descriptor();
            const float size = layout.font_size;
            const float left = offset.x + glyph.x + record->plane.left * size;
            const float right = offset.x + glyph.x + record->plane.right * size;
            const float top = offset.y + glyph.y - record->plane.top * size;
            const float bottom = offset.y + glyph.y - record->plane.bottom * size;
            const float width = static_cast<float>(descriptor.page_width);
            const float height = static_cast<float>(descriptor.page_height);
            const UiRectangle uv{.left = record->texels.left / width,
                                 .top = record->texels.top / height,
                                 .right = record->texels.right / width,
                                 .bottom = record->texels.bottom / height};
            const std::array<float, 4> shape = shadow_pass ? std::array{descriptor.distance_range, 0.0F, style.shadow_softness, 0.0F}
                                                           : std::array{descriptor.distance_range, style.outline_width, 0.0F, 0.0F};
            add_quad({Vector2{.x = left, .y = top}, Vector2{.x = right, .y = top}, Vector2{.x = right, .y = bottom}, Vector2{.x = left, .y = bottom}},
                     {Vector2{.x = uv.left, .y = uv.top}, Vector2{.x = uv.right, .y = uv.top}, Vector2{.x = uv.right, .y = uv.bottom},
                      Vector2{.x = uv.left, .y = uv.bottom}},
                     shape, shadow_pass ? style.shadow_color.packed() : style.color.packed(), style.outline_color.packed(),
                     mode_of(shadow_pass ? UiShaderMode::GLYPH_SHADOW : UiShaderMode::GLYPH, descriptor.kind),
                     DrawTexture::font_page(glyph.atlas, record->page));
        }
    }
}

} // namespace gfx::ui
