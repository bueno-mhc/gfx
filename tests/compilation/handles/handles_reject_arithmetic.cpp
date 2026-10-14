// Must not compile: handles are names, not numbers; they support no arithmetic.
import gfx;

auto main() -> int {
    const gfx::BufferHandle handle;
    const auto next = handle + 1;
    return next.is_valid() ? 0 : 1;
}
