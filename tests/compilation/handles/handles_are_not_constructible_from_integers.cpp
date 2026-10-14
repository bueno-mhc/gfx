// Must not compile: a handle cannot be forged from an integer.
import gfx;

auto main() -> int {
    const gfx::BufferHandle handle(42U);
    return handle.is_valid() ? 0 : 1;
}
