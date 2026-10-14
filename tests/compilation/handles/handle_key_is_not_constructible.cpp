// Must not compile: only HandleIssuer descendants (backends) can create a HandleKey.
import gfx;

auto main() -> int {
    const gfx::HandleKey key{};
    return gfx::BufferHandle::create(key, 1, 1).is_valid() ? 0 : 1;
}
