// A handle kind of its own, as gfx.physics.handles declares them (without explicit instantiations).
export module gfx.defects.handle_instantiation.handles;

import std;
export import gfx.core.handles;

export namespace defects
{

struct ThingTag
{
    static constexpr std::string_view NAME = "Thing";
};

using ThingHandle = gfx::Handle<ThingTag>;

} // namespace defects
