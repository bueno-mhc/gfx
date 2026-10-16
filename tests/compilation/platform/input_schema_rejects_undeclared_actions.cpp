// A binding naming an action the schema does not declare does not compile.
import gfx.platform;

using namespace gfx::platform;
using namespace gfx::platform::literals;

constexpr auto SCHEMA = make_input_schema(actions(action("jump")),
                                          context("gameplay", bind("fly"_action, Key::SPACE)));

auto main() -> int
{
    return static_cast<int>(SCHEMA.bindings.size());
}
