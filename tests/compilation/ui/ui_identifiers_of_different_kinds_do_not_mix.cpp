// A popup identifier cannot name a table, even with the same name.
import gfx.ui;

using namespace gfx::ui::literals;

auto main() -> int
{
    const gfx::ui::TableIdentifier table = "objects"_popup;
    return table.is_valid() ? 0 : 1;
}
