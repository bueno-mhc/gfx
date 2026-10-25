// Uses the module whose partition GCC 16 cannot read back (see handle_instantiation_key.cppm).
import gfx.defects.handle_instantiation;

auto main() -> int
{
    const defects::Context context;
    return defects::Key{} == defects::Key{} && !context.thing ? 0 : 1;
}
