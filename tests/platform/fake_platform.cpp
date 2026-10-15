/// @file fake_platform.cpp
/// @brief The fake platform's window table, used here and not in the module interface: instantiations of
///        HandleTable::find() and remove() in a module interface make its module file unreadable for
///        importers (toolchain defect 10).
module gfx.tests.fake_platform;

import std;
import gfx.platform;
import gfx.core.handle_table;

namespace gfx::tests
{

// ------------------------------------------------------------------------------------------------- FakePlatform

auto
FakePlatform::register_window(FakeWindow& window) -> WindowHandle
{
    return m_windows.insert({.window = &window});
}

void
FakePlatform::unregister_window(WindowHandle window) noexcept
{
    (void)m_windows.remove(window);
    ++m_destroyed_windows;
}

auto
FakePlatform::is_window_alive(WindowHandle window) const noexcept -> bool
{
    return m_windows.find(window) != nullptr;
}

} // namespace gfx::tests
