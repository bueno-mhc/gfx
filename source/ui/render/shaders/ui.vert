#version 460
// The UI's vertices, pulled from a bindless storage buffer: twelve words per vertex (gfx::ui::UiVertex),
// from the word the push constants name. Positions are pixels of the target, y down.
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 3) readonly buffer GfxStorageBuffer
{
    uint words[];
} gfx_storage_buffers[];

layout(push_constant) uniform Push
{
    vec2 scale;              // 2 / target size: pixels to normalized device coordinates
    vec2 translation;        // -1 - 2 * viewport offset / target size
    uint vertex_buffer;      // bindless index of the buffer holding the vertices
    uint vertex_word;        // first word of the vertices in it
    uint texture_index;      // bindless index of the command's texture
    uint sampler_index;      // bindless index of the sampler
    uint flags;              // bit 0: the target is sRGB (colors are converted to linear)
} push;

layout(location = 0) out vec2 local;
layout(location = 1) flat out vec4 shape;
layout(location = 2) out vec4 color;
layout(location = 3) flat out vec4 secondary_color;
layout(location = 4) flat out uint mode;

// sRGB-encoded channels to linear ones.
vec3 to_linear(vec3 encoded)
{
    return mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), encoded));
}

void main()
{
    // The vertex's twelve words: position, local, shape, color, secondary color, mode.
    const uint first = push.vertex_word + uint(gl_VertexIndex) * 12u;
    const vec2 position = vec2(uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first]),
                               uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 1u]));
    local = vec2(uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 2u]),
                 uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 3u]));
    shape = vec4(uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 4u]),
                 uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 5u]),
                 uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 6u]),
                 uintBitsToFloat(gfx_storage_buffers[push.vertex_buffer].words[first + 7u]));
    color = unpackUnorm4x8(gfx_storage_buffers[push.vertex_buffer].words[first + 8u]);
    secondary_color = unpackUnorm4x8(gfx_storage_buffers[push.vertex_buffer].words[first + 9u]);
    mode = gfx_storage_buffers[push.vertex_buffer].words[first + 10u];
    if ((push.flags & 1u) != 0u)
    {
        color.rgb = to_linear(color.rgb);
        secondary_color.rgb = to_linear(secondary_color.rgb);
    }
    gl_Position = vec4(position * push.scale + push.translation, 0.0, 1.0);
}
