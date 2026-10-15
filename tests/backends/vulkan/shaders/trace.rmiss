#version 460
// Rays that hit nothing report 2.
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT uint payload;

void main()
{
    payload = 2;
}
