// The bindless table as gfx declares it: set BINDLESS_SET (0), binding = BindlessResourceKind.
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D gfx_sampled_images[];
layout(set = 0, binding = 1, rgba8) uniform image2D gfx_storage_images[];
layout(set = 0, binding = 2) uniform sampler gfx_samplers[];
layout(set = 0, binding = 3) buffer GfxStorageBuffer
{
    uint words[];
} gfx_storage_buffers[];
