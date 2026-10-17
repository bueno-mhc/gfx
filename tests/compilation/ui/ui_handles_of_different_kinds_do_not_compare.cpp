// A font handle and a font atlas handle are different kinds: they cannot be compared or mixed.
import gfx.ui;

auto main() -> int
{
    const gfx::ui::FontHandle font{};
    const gfx::ui::FontAtlasHandle atlas{};
    return font == atlas ? 0 : 1;
}
