// HOLD and TAP triggers press BUTTON actions; one on an axis does not compile.
import std;
import gfx.platform;

using namespace gfx::platform;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;

constexpr auto SCHEMA = make_input_schema(actions(action("zoom", ActionValueType::AXIS_1D)),
                                          context("gameplay", bind("zoom"_action, single(Key::Z).held_for(500ms))));

auto main() -> int
{
    return static_cast<int>(SCHEMA.bindings.size());
}
