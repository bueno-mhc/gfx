// Must not compile: ray tracing needs a queue that runs compute work.
import gfx;

void record(gfx::CommandBuffer& command_buffer, const gfx::ShaderBindingTable& table)
{
    gfx::TransferRecorder recorder{command_buffer};
    recorder.trace_rays(table, 1920, 1080);
}

auto main() -> int
{
    return 0;
}
