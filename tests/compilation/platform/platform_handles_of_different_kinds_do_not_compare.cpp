// A window handle and a cursor handle are different kinds: they cannot be compared or mixed.
import gfx.platform;

auto main() -> int
{
    const gfx::platform::WindowHandle window{};
    const gfx::platform::CursorHandle cursor{};
    return window == cursor ? 0 : 1;
}
