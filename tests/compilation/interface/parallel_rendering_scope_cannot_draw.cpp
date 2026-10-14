// Must not compile: inside a parallel rendering pass the primary command buffer only executes parts;
// draws are recorded by RenderingPartRecorder on secondary command buffers.
import gfx;

void record(gfx::CommandBuffer& command_buffer)
{
    gfx::GraphicsRecorder recorder{command_buffer};
    gfx::ParallelRenderingScope pass = recorder.begin_parallel_rendering({});
    pass.draw({.vertex_count = 3});
}

auto main() -> int
{
    return 0;
}
