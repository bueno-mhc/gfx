// A partition that imports the module that instantiated the handle (in std::optional) and instantiates the handle's
// members itself: a braced default initializer and a defaulted comparison. GCC 16 writes a module file for it that the
// primary interface cannot read back ("failed to read compiled module cluster N: Bad file data"); without the
// initializer and the comparison, without importing that module, or with the handle explicitly instantiated in its own
// module, it is fine.
export module gfx.defects.handle_instantiation:key;

import gfx.defects.handle_instantiation.context;
import gfx.defects.handle_instantiation.handles;

export namespace defects
{

struct Key
{
    ThingHandle thing{};

    [[nodiscard]] constexpr auto operator==(const Key&) const noexcept -> bool = default;
};

} // namespace defects
