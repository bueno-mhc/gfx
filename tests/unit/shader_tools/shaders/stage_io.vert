#version 460
// Vertex inputs of several formats and sizes, and outputs read by stage_io.frag.

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texture_coordinate;
layout(location = 2) in uvec4 joints;
layout(location = 3) in mat4 instance_transform;

layout(location = 0) out vec2 out_texture_coordinate;
layout(location = 1) flat out uint out_joint;

void main()
{
    gl_Position = instance_transform * vec4(position, 1.0);
    out_texture_coordinate = texture_coordinate;
    out_joint = joints.x;
}
