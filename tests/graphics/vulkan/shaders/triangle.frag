#version 460
// Writes the interpolated color.

layout(location = 0) in vec4 color;
layout(location = 0) out vec4 target;

void main()
{
    target = color;
}
