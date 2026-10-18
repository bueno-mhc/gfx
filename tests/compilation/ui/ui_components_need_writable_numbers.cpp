// A multi-component drag edits its numbers in place: constant ones are not EditableNumbers.
import std;
import gfx.ui;

auto check(gfx::ui::UiFrame& frame) -> bool
{
    const std::array<float, 3> position{1, 2, 3};
    return frame.drag("Position", position);
}

auto main() -> int
{
    return 0;
}
