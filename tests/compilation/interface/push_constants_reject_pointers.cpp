// Must not compile: the bytes of a pointer are an address, not the data it points to.
import gfx;

struct Push
{
    float radius;
};

void record(gfx::CommandBuffer& command_buffer, const gfx::PipelineLayout& layout, const Push& push)
{
    gfx::ComputeRecorder recorder{command_buffer};
    recorder.push_constants(layout, &push);
}

auto main() -> int
{
    return 0;
}
