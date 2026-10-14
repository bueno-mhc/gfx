// Imports the module whose partition GCC 16 cannot read back (see lambda_in_constructor_module.cppm).
import gfx.defects.lambda_in_constructor;

auto main() -> int
{
    const defects::Source source;
    const defects::Checked checked{source};
    (void)checked;
    return 0;
}
