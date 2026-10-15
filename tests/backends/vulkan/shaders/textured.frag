#version 460
// Samples a bindless texture with a bindless sampler, both named by push constants.
#extension GL_GOOGLE_include_directive : require
#include "bindless.glsl"

layout(location = 0) in vec4 color;
layout(location = 0) out vec4 target;

layout(push_constant) uniform Push
{
    layout(offset = 16) uint texture_index;
    uint sampler_index;
} push;

void main()
{
    target = texture(nonuniformEXT(sampler2D(gfx_sampled_images[push.texture_index], gfx_samplers[push.sampler_index])),
                     vec2(0.5, 0.5));
}
