#version 460
// The UI's primitives (gfx::ui::UiShaderMode): solid triangles, rounded boxes evaluated as signed distances,
// glyphs of SDF/MSDF/MTSDF atlases (with outlines and shadows), images. Distances are measured in screen
// pixels with derivatives, so edges stay one pixel wide under any scale or rotation: a shape's distance by
// its gradient (known in the shape's frame) mapped to the screen through the derivatives of its frame's
// coordinates (exact for any affine transform; finite differences of the distance itself are wrong at
// corners, where the distance changes direction).
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D gfx_sampled_images[];
layout(set = 0, binding = 2) uniform sampler gfx_samplers[];

layout(push_constant) uniform Push
{
    vec2 scale;
    vec2 translation;
    uint vertex_buffer;
    uint vertex_word;
    uint texture_index;
    uint sampler_index;
    uint flags;
} push;

layout(location = 0) in vec2 local;
layout(location = 1) flat in vec4 shape;
layout(location = 2) in vec4 color;
layout(location = 3) flat in vec4 secondary_color;
layout(location = 4) flat in uint mode;

layout(location = 0) out vec4 target;

const uint SOLID = 0u;
const uint SHAPE = 1u;
const uint GLYPH = 2u;
const uint GLYPH_SHADOW = 3u;
const uint IMAGE = 4u;

const uint SDF_MSDF = 2u;
const uint SDF_MTSDF = 3u;

float median(vec3 values)
{
    return max(min(values.r, values.g), min(max(values.r, values.g), values.b));
}

// Signed distance from p to a box of half size half_size whose corners are rounded by radius, and the
// direction it grows in (a unit vector of the box's frame, up to its sign).
float rounded_box(vec2 p, vec2 half_size, float radius, out vec2 direction)
{
    const vec2 q = abs(p) - half_size + radius;
    const vec2 outside = max(q, vec2(0.0));
    const float outside_length = length(outside);
    direction = outside_length > 0.0 ? outside / outside_length : (q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    return min(max(q.x, q.y), 0.0) + outside_length - radius;
}

// Coverage of a pixel by the inside of a signed distance (negative inside) growing along direction in the
// frame of local: the edge is one screen pixel wide.
float coverage(float distance, vec2 direction)
{
    const vec2 screen_gradient = vec2(dot(direction, dFdx(local)), dot(direction, dFdy(local)));
    const float width = max(length(screen_gradient), 1e-6);
    return clamp(0.5 - distance / width, 0.0, 1.0);
}

void main()
{
    const uint kind = mode & 0xFFu;
    if (kind == SOLID)
    {
        target = color;
        return;
    }
    if (kind == SHAPE)
    {
        // A filled box, or its border shape.w pixels wide inside its edge (the same direction, up to its sign).
        vec2 direction;
        float distance = rounded_box(local, shape.xy, shape.z, direction);
        if (shape.w > 0.0)
        {
            distance = abs(distance + shape.w * 0.5) - shape.w * 0.5;
        }
        target = vec4(color.rgb, color.a * coverage(distance, direction));
        return;
    }
    const vec4 texel = texture(sampler2D(gfx_sampled_images[push.texture_index], gfx_samplers[push.sampler_index]), local);
    if (kind == IMAGE)
    {
        target = texel * color;
        return;
    }

    // Glyphs: the field's distance in screen pixels (the atlas's range over the texels a pixel covers).
    const uint sdf = (mode >> 8) & 0xFFu;
    const float fill = sdf >= SDF_MSDF ? median(texel.rgb) : texel.r;
    const float true_distance = sdf == SDF_MTSDF ? texel.a : fill;
    const vec2 texture_size = vec2(textureSize(sampler2D(gfx_sampled_images[push.texture_index], gfx_samplers[push.sampler_index]), 0));
    const vec2 unit_range = vec2(shape.x) / texture_size;
    const vec2 screen_texture_size = vec2(1.0) / fwidth(local);
    const float screen_range = max(0.5 * dot(unit_range, screen_texture_size), 1.0);
    if (kind == GLYPH_SHADOW)
    {
        // The true distance, its edge softened over shape.z pixels.
        const float pixels = screen_range * (true_distance - 0.5);
        const float softness = shape.z + 0.5;
        target = vec4(color.rgb, color.a * clamp((pixels + softness) / (2.0 * softness), 0.0, 1.0));
        return;
    }
    const float fill_alpha = clamp(screen_range * (fill - 0.5) + 0.5, 0.0, 1.0);
    if (shape.y > 0.0)
    {
        // The outline: shape.y pixels outside the edge, under the fill.
        const float outline_alpha = clamp(screen_range * (true_distance - 0.5) + shape.y + 0.5, 0.0, 1.0);
        const vec4 outline = vec4(secondary_color.rgb, secondary_color.a * outline_alpha);
        target = vec4(mix(outline.rgb, color.rgb, fill_alpha), mix(outline.a, color.a, fill_alpha));
        return;
    }
    target = vec4(color.rgb, color.a * fill_alpha);
}
