// Must not compile: a compute pipeline cannot be used where graphics commands expect a handle.
import gfx;

template<gfx::GraphicsHandle Type>
auto bind_for_graphics(Type /*handle*/) -> int {
    return 0;
}

auto main() -> int {
    return bind_for_graphics(gfx::ComputePipelineHandle{});
}
