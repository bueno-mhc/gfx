// Composites combine pressed-or-released inputs; a gamepad axis is not one.
import gfx.platform;

using namespace gfx::platform;

auto main() -> int
{
    constexpr Binding binding = composite_axis(GamepadAxis::LEFT_X, Key::E);
    return static_cast<int>(binding.source_count());
}
