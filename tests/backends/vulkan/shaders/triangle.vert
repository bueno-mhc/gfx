#version 460
// One triangle covering the whole target, without vertex buffers (vertex pulling from
// gl_VertexIndex), colored by push constants.

layout(location = 0) out vec4 color;

layout(push_constant) uniform Push
{
    vec4 tint;
} push;

void main()
{
    const vec2 corners[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(corners[gl_VertexIndex % 3], 0.0, 1.0);
    color = push.tint;
}
