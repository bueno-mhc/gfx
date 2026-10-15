// An event's payload type follows from its event type: a key press cannot carry a mouse button.
import std;
import gfx.platform;

auto main() -> int
{
    using namespace gfx::platform;
    const Event event = Event::make<EventType::KEY_DOWN>(WindowHandle{}, std::chrono::nanoseconds{0}, MouseButtonEvent{});
    return static_cast<int>(event.type());
}
