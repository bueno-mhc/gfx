// make_input_schema() checks the schema while compiling: two actions with the same name do not compile.
import gfx.platform;

using namespace gfx::platform;
using namespace gfx::platform::literals;

constexpr auto SCHEMA = make_input_schema(actions(action("jump"), action("jump")),
                                          context("gameplay", bind("jump"_action, Key::SPACE)));

auto main() -> int
{
    return static_cast<int>(SCHEMA.actions.size());
}
