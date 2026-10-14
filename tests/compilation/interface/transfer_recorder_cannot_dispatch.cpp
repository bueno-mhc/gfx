// Must not compile: transfer queues run copies only.
import gfx;

void record(gfx::CommandBuffer& command_buffer)
{
    gfx::TransferRecorder recorder{command_buffer};
    recorder.dispatch(8, 8, 1);
}

auto main() -> int
{
    return 0;
}
