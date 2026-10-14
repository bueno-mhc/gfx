// Must not compile: the numeric value of a handle is private.
import gfx;

auto main() -> int {
    const gfx::BufferHandle handle;
    return static_cast<int>(handle.m_value);
}
