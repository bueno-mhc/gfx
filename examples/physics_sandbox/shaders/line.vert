#version 460
// Debug lines, pulled from a bindless storage buffer: four words per vertex (world position, packed color), two vertices
// per segment, from the word the push constants name.
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 3) readonly buffer GfxStorageBuffer
{
    uint words[];
} gfx_storage_buffers[];

layout(push_constant) uniform Push
{
    mat4 view_projection; // world space to clip space
    uint line_buffer;     // bindless index of the buffer holding the frame's line vertices
    uint line_word;       // first word of the vertices in it
    uint flags;           // bit 0: the target is sRGB (colors are converted to linear)
} push;

layout(location = 0) flat out vec4 color;

vec3 to_linear(vec3 encoded)
{
    return mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), encoded));
}

void main()
{
    const uint first = push.line_word + uint(gl_VertexIndex) * 4u;
    const vec3 position = vec3(uintBitsToFloat(gfx_storage_buffers[push.line_buffer].words[first]),
                               uintBitsToFloat(gfx_storage_buffers[push.line_buffer].words[first + 1u]),
                               uintBitsToFloat(gfx_storage_buffers[push.line_buffer].words[first + 2u]));
    color = unpackUnorm4x8(gfx_storage_buffers[push.line_buffer].words[first + 3u]);
    if ((push.flags & 1u) != 0u)
    {
        color.rgb = to_linear(color.rgb);
    }
    gl_Position = push.view_projection * vec4(position, 1.0);
}
