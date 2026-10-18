// A list clipper keeps where its list is in the frame (it moves the cursor past the items it skips): a copy would
// move it twice, so clippers cannot be copied.
import std;
import gfx.ui;

auto copy(const gfx::ui::UiListClipper& clipper) -> gfx::ui::UiListClipper
{
    return clipper;
}

auto main() -> int
{
    return 0;
}
