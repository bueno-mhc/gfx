// Must not compile: rendering passes need a graphics queue.
import gfx;

void record(gfx::CommandBuffer& command_buffer)
{
    gfx::ComputeRecorder recorder{command_buffer};
    auto rendering = recorder.begin_rendering({});
}

auto main() -> int
{
    return 0;
}
