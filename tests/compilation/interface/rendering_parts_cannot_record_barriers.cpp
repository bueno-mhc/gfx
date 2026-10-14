// Must not compile: a part of a parallel rendering pass only records commands allowed inside a
// rendering pass; barriers belong outside the pass, on the primary command buffer.
import gfx;

void record(gfx::CommandBuffer& secondary, const gfx::RenderingInheritance& inheritance)
{
    gfx::RenderingPartRecorder part{secondary, inheritance};
    part.barrier({});
}

auto main() -> int
{
    return 0;
}
