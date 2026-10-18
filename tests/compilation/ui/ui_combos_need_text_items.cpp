// A combo box shows its items as text: integers are not TextLike.
import std;
import gfx.ui;

auto check(gfx::ui::UiFrame& frame) -> bool
{
    std::size_t selected = 0;
    const std::array<int, 3> items{1, 2, 3};
    return frame.combo("Numbers", selected, items);
}

auto main() -> int
{
    return 0;
}
