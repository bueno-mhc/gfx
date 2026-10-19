// Uses the function whose module GCC 16 cannot compile (see using_declaration_function.cppm).
import gfx.defects.using_declaration;

auto main() -> int
{
    constexpr defects::Bindings bindings = defects::bindings();
    return bindings.first.button == bindings.second.button ? 1 : 0;
}
