/// @file event_channel.cppm
/// @brief EventChannel: hands the events the platform thread pumps to a game thread, without locks
///        (single producer, single consumer). The platform thread sends each pump; the game thread
///        receives everything sent since its previous receive, in order. Sending never blocks and never
///        drops events: when the consumer lags, events wait on the producer side for a free slot.
///
/// @code
/// // Platform (main) thread                       // Game thread
/// channel.send(platform.poll_events());           const InputFrame& input = input_system.update(channel.receive());
/// @endcode
export module gfx.platform.input:event_channel;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.interface;

export namespace gfx::platform
{

/// @brief Lock-free single-producer, single-consumer channel of events (platform thread → game thread).
///        Memory is reused: in steady state neither side allocates.
class EventChannel
{
public:
    /// @brief A channel of @p slot_count slots (at least 2): batches in flight between the threads.
    /// @throws InvalidUsageError when @p slot_count is below 2.
    explicit EventChannel(std::size_t slot_count = 4,
                          std::source_location location = std::source_location::current());

    EventChannel(const EventChannel&) = delete;
    auto operator=(const EventChannel&) -> EventChannel& = delete;

    /// @brief Sends @p events and their text (producer thread only). Never blocks; keeps the events on
    ///        the producer side while every slot is full, and they leave with a later call: call it
    ///        after every pump, even one without events.
    void send(const EventSpan& events);

    /// @brief Everything sent since the previous receive, in order (consumer thread only).
    /// @return The events, valid until the next receive().
    [[nodiscard]] auto receive() -> EventSpan;

private:
    /// A batch in flight: filled by the producer, emptied by the consumer.
    struct Slot
    {
        EventQueue events{};
        std::atomic<bool> full{false};
    };

private:
    std::vector<Slot> m_slots{};
    EventQueue m_pending{};
    std::size_t m_producer_slot = 0;
    EventQueue m_received{};
    std::size_t m_consumer_slot = 0;
};

} // namespace gfx::platform
