// A dock builder edits its context's dock tree: it is made by UiContext::dock_builder() only.
import std;
import gfx.ui;

auto make() -> gfx::ui::UiDockBuilder
{
    return gfx::ui::UiDockBuilder{};
}

auto main() -> int
{
    return 0;
}
