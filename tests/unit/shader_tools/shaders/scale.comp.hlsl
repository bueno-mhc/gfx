// An HLSL compute shader compiled at build time; its workgroup size comes from a specialization constant.

[[vk::constant_id(0)]] const uint GROUP_SIZE = 64;

[[vk::binding(0, 0)]] RWStructuredBuffer<float> values;

struct Parameters
{
    float factor;
    uint count;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

[numthreads(64, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x < parameters.count)
    {
        values[thread.x] *= parameters.factor * float(GROUP_SIZE);
    }
}
