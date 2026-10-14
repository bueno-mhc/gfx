// Must not compile: a buffer handle and an image handle name different kinds of objects.
import gfx;

auto main() -> int {
    const gfx::BufferHandle buffer;
    const gfx::ImageHandle image;
    return buffer == image ? 0 : 1;
}
