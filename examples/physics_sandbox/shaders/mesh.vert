#version 460
// Instanced shape meshes, pulled from bindless storage buffers: six words per vertex (position, normal in the shape's
// frame) in the mesh buffer, nine words per instance (translation, orientation quaternion x y z w, packed color, flags) in
// the frame's instance buffer, from the word the push constants name.
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 3) readonly buffer GfxStorageBuffer
{
    uint words[];
} gfx_storage_buffers[];

layout(push_constant) uniform Push
{
    mat4 view_projection; // world space to clip space
    vec4 light;           // xyz: unit direction toward the light; w: its strength
    uint vertex_buffer;   // bindless index of the buffer holding every mesh's vertices
    uint instance_buffer; // bindless index of the buffer holding the frame's instances
    uint instance_word;   // first word of the instances in it
    uint flags;           // bit 0: the target is sRGB (colors are converted to linear)
} push;

layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) flat out vec4 color;
layout(location = 3) flat out uint instance_flags;

float word_float(uint buffer_index, uint word)
{
    return uintBitsToFloat(gfx_storage_buffers[buffer_index].words[word]);
}

// v turned by the unit quaternion q.
vec3 rotate(vec4 q, vec3 v)
{
    const vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// sRGB-encoded channels to linear ones.
vec3 to_linear(vec3 encoded)
{
    return mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), encoded));
}

void main()
{
    // The vertex (gl_VertexIndex counts from the mesh's first vertex in the buffer) and the instance (gl_InstanceIndex
    // counts from the draw's first instance).
    const uint vertex = uint(gl_VertexIndex) * 6u;
    const vec3 position = vec3(word_float(push.vertex_buffer, vertex), word_float(push.vertex_buffer, vertex + 1u),
                               word_float(push.vertex_buffer, vertex + 2u));
    const vec3 normal = vec3(word_float(push.vertex_buffer, vertex + 3u), word_float(push.vertex_buffer, vertex + 4u),
                             word_float(push.vertex_buffer, vertex + 5u));
    const uint instance = push.instance_word + uint(gl_InstanceIndex) * 9u;
    const vec3 translation = vec3(word_float(push.instance_buffer, instance), word_float(push.instance_buffer, instance + 1u),
                                  word_float(push.instance_buffer, instance + 2u));
    const vec4 orientation = vec4(word_float(push.instance_buffer, instance + 3u), word_float(push.instance_buffer, instance + 4u),
                                  word_float(push.instance_buffer, instance + 5u), word_float(push.instance_buffer, instance + 6u));
    color = unpackUnorm4x8(gfx_storage_buffers[push.instance_buffer].words[instance + 7u]);
    instance_flags = gfx_storage_buffers[push.instance_buffer].words[instance + 8u];
    if ((push.flags & 1u) != 0u)
    {
        color.rgb = to_linear(color.rgb);
    }

    world_position = translation + rotate(orientation, position);
    world_normal = rotate(orientation, normal);
    gl_Position = push.view_projection * vec4(world_position, 1.0);
}
