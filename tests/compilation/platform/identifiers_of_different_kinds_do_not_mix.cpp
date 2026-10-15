// An action identifier cannot name a binding context.
import gfx.platform;

using namespace gfx::platform::literals;

auto main() -> int
{
    const gfx::platform::ActionIdentifier action = "gameplay"_context;
    return action.is_valid() ? 0 : 1;
}
