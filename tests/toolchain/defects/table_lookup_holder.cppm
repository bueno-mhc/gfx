// A module interface whose inline function instantiates HandleTable::find() (gfx.core.handle_table) for a
// tag of a third module (gfx::platform::WindowTag) with a record that points to a class of this module.
// GCC 16 writes a module file that importers cannot read ("failed to read compiled module cluster N:
// Bad file data"). With a tag of this module, or a record without the pointer, the module file is fine.
export module gfx.defects.table_lookup;

import std;
import gfx.platform;
import gfx.core.handle_table;

export namespace defects
{

/// Any class of this module.
class Thing
{
};

/// The table's record points to it.
struct Record
{
    Thing* thing = nullptr;
};

/// Looks handles up inline, in the module interface.
class Holder
{
public:
    [[nodiscard]] auto contains(gfx::platform::WindowHandle window) const noexcept -> bool
    {
        return m_windows.find(window) != nullptr;
    }

private:
    gfx::backends::HandleTable<gfx::platform::WindowTag, Record> m_windows{gfx::platform::PlatformBackendKind::SDL3};
};

} // namespace defects
