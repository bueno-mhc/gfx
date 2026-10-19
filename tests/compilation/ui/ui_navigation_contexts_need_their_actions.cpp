// The UI's navigation context binds the UI's navigation actions: a schema with the context but without the actions
// (with_ui_navigation_actions()) does not compile.
import std;
import gfx.platform;
import gfx.ui.platform;

constexpr auto SCHEMA = gfx::platform::make_input_schema(gfx::platform::actions(gfx::platform::action("jump")), gfx::ui::ui_navigation_context());

auto main() -> int
{
    return static_cast<int>(SCHEMA.actions.size());
}
