// Test support for the platform backends: pumping until an event arrives, keeping events beyond the
// pump that delivered them.
export module gfx.tests.backend_support;

import std;
import gfx.platform;

export namespace gfx::tests
{

/// An event and its text, kept beyond the pump that delivered it.
struct ReceivedEvent
{
    gfx::platform::Event event{};
    std::string text{};
};

/// The events of one pump of @p platform, kept with their text.
inline auto
pump(gfx::platform::Platform& platform) -> std::vector<ReceivedEvent>
{
    std::vector<ReceivedEvent> received;
    const gfx::platform::EventSpan events = platform.poll_events();
    for (const gfx::platform::Event& event : events)
    {
        received.push_back(ReceivedEvent{.event = event, .text = std::string{events.text(event)}});
    }
    return received;
}

/// Pumps @p platform until an event satisfies @p wanted (or @p timeout passes); every event received
/// meanwhile is appended to @p received.
/// @return Whether a wanted event arrived.
template<class Predicate>
auto
pump_until(gfx::platform::Platform& platform,
           Predicate wanted,
           std::vector<ReceivedEvent>& received,
           std::chrono::milliseconds timeout = std::chrono::milliseconds{3000}) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        bool found = false;
        const gfx::platform::EventSpan events = platform.wait_events_timeout(std::chrono::milliseconds{10});
        for (const gfx::platform::Event& event : events)
        {
            received.push_back(ReceivedEvent{.event = event, .text = std::string{events.text(event)}});
            found = found || wanted(event);
        }
        if (found)
        {
            return true;
        }
    }
    return false;
}

/// Pumps @p platform until an event of type @p type arrives; returns it (std::nullopt on timeout).
inline auto
pump_until_type(gfx::platform::Platform& platform,
                gfx::platform::EventType type,
                std::chrono::milliseconds timeout = std::chrono::milliseconds{3000}) -> std::optional<ReceivedEvent>
{
    std::vector<ReceivedEvent> received;
    if (!pump_until(platform, [type](const gfx::platform::Event& event) { return event.type() == type; }, received, timeout))
    {
        return std::nullopt;
    }
    for (const ReceivedEvent& event : received)
    {
        if (event.event.type() == type)
        {
            return event;
        }
    }
    return std::nullopt;
}

/// The first event of type @p type in @p events (null when none).
inline auto
first_of(const std::vector<ReceivedEvent>& events,
         gfx::platform::EventType type) -> const ReceivedEvent*
{
    const auto found = std::ranges::find_if(events, [type](const ReceivedEvent& event) { return event.event.type() == type; });
    return found != events.end() ? &*found : nullptr;
}

} // namespace gfx::tests
