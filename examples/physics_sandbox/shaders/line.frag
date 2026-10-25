#version 460
// Debug lines: their vertices' color.

layout(location = 0) flat in vec4 color;
layout(location = 0) out vec4 result;

void main()
{
    result = color;
}
