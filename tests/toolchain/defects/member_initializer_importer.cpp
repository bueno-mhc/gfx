// Uses the module whose implementation unit GCC 16 cannot compile (see member_initializer_definition.cpp).
import gfx.defects.member_initializer;

auto main() -> int
{
    return defects::thing_value(5) == 5 ? 0 : 1;
}
