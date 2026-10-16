// EventChannel: a ring of event batches handed from the producer to the consumer with one atomic flag each.
module gfx.platform.input;

import std;
import gfx.core;
import gfx.platform.interface;

namespace gfx::platform
{

namespace
{

/// @p slot_count, checked.
[[nodiscard]] auto
checked_slot_count(std::size_t slot_count,
                   std::source_location location) -> std::size_t
{
    if (slot_count >= 2)
    {
        return slot_count;
    }
    throw InvalidUsageError(
        {
            .backend = std::nullopt,
            .operation = "EventChannel::EventChannel",
            .problem = std::format("an event channel needs at least 2 slots, not {}", slot_count),
            .suggestion = "keep the default (4 slots)",
        },
        location);
}

} // namespace

EventChannel::EventChannel(std::size_t slot_count,
                           std::source_location location)
    : m_slots(checked_slot_count(slot_count, location))
{
}

void
EventChannel::send(const EventSpan& events)
{
    // Events wait on the producer side until the next slot is free.
    m_pending.push(events);
    Slot& slot = m_slots[m_producer_slot];
    if (m_pending.empty() || slot.full.load(std::memory_order_acquire))
    {
        return;
    }

    // Hand the batch over: the slot's emptied queue becomes the pending queue (no allocation).
    std::swap(slot.events, m_pending);
    slot.full.store(true, std::memory_order_release);
    m_producer_slot = (m_producer_slot + 1) % m_slots.size();
}

auto
EventChannel::receive() -> EventSpan
{
    // Gather every full slot, oldest first, and give each back emptied.
    m_received.clear();
    for (std::size_t visited = 0; visited < m_slots.size(); ++visited)
    {
        Slot& slot = m_slots[m_consumer_slot];
        if (!slot.full.load(std::memory_order_acquire))
        {
            break;
        }
        m_received.push(slot.events.view());
        slot.events.clear();
        slot.full.store(false, std::memory_order_release);
        m_consumer_slot = (m_consumer_slot + 1) % m_slots.size();
    }
    return m_received.view();
}

} // namespace gfx::platform
