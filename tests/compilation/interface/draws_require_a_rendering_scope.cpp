// Must not compile: draws are recorded on the RenderingScope returned by begin_rendering().
import gfx;

void record(gfx::CommandBuffer& command_buffer)
{
    gfx::GraphicsRecorder recorder{command_buffer};
    recorder.draw({.vertex_count = 3});
}

auto main() -> int
{
    return 0;
}
