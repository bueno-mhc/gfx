// A slider edits numbers: a bool is not an ArithmeticValue.
import gfx.ui;

auto check(gfx::ui::UiFrame& frame) -> bool
{
    bool value = false;
    return frame.slider("Flag", value, false, true);
}

auto main() -> int
{
    return 0;
}
