// A two-dimensional binding (WASD) on a BUTTON action does not compile.
import gfx.platform;

using namespace gfx::platform;
using namespace gfx::platform::literals;

constexpr auto SCHEMA = make_input_schema(actions(action("move")),
                                          context("gameplay", bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D))));

auto main() -> int
{
    return static_cast<int>(SCHEMA.bindings.size());
}
