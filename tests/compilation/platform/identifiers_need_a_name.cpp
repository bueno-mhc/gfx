// Identifiers are hashed at compile time; an empty name is a compilation error.
import gfx.platform;

using namespace gfx::platform::literals;

auto main() -> int
{
    constexpr gfx::platform::ActionIdentifier nothing = ""_action;
    return nothing.is_valid() ? 0 : 1;
}
