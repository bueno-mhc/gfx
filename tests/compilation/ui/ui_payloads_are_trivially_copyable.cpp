// A drag and drop payload is copied as bytes: a std::string is not trivially copyable.
import std;
import gfx.ui;

auto check(gfx::ui::UiFrame& frame) -> bool
{
    const std::string name = "asset";
    return frame.set_drag_drop_payload("asset", name);
}

auto main() -> int
{
    return 0;
}
