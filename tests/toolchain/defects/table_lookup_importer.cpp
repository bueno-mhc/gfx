// Uses the class whose module file GCC 16 cannot read back (see table_lookup_holder.cppm).
import gfx.defects.table_lookup;

auto main() -> int
{
    const defects::Holder holder;
    return holder.contains({}) ? 1 : 0;
}
