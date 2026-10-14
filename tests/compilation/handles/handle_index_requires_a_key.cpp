// Must not compile: reading the slot index of a handle requires a HandleKey.
import gfx;

auto main() -> int {
    const gfx::BufferHandle handle;
    return static_cast<int>(handle.index());
}
