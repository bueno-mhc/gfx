// A second module that instantiates the handle (as gfx.physics.errors does): an error context of optional handles.
export module gfx.defects.handle_instantiation.context;

import std;
import gfx.defects.handle_instantiation.handles;

export namespace defects
{

struct Context
{
    std::optional<ThingHandle> thing{};
};

} // namespace defects