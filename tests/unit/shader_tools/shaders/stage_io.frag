#version 460
// Fragment inputs from stage_io.vert and two color outputs.

layout(location = 0) in vec2 texture_coordinate;
layout(location = 1) flat in uint joint;

layout(location = 0) out vec4 color;
layout(location = 1) out uint identifier;

void main()
{
    color = vec4(texture_coordinate, 0.0, 1.0);
    identifier = joint;
}
