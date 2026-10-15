#version 460
// Rays that hit the triangle report 1.
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT uint payload;
hitAttributeEXT vec2 attributes;

void main()
{
    payload = 1;
}
