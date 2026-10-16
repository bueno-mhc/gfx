#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.input_support;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;
using gfx::tests::Events;

/// A text input event carrying @p text, in its own queue.
auto
text_events(std::string_view text) -> EventQueue
{
    EventQueue queue;
    queue.push(Event::make<EventType::TEXT_INPUT>(0ns, TextInputEvent{}), text);
    return queue;
}

TEST(EventChannelTest, EventsArriveInOrderWithTheirText)
{
    // Two sends before a receive: both batches, in order, text included.
    EventChannel channel;
    channel.send(Events{}.key_down(Key::A).span());
    const EventQueue typed = text_events("héllo");
    channel.send(typed.view());
    const EventSpan received = channel.receive();
    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[0].type(), EventType::KEY_DOWN);
    EXPECT_EQ(received[1].type(), EventType::TEXT_INPUT);
    EXPECT_EQ(received.text(received[1]), "héllo");

    // Nothing new: an empty span.
    EXPECT_TRUE(channel.receive().empty());
}

TEST(EventChannelTest, ALaggingConsumerLosesNothing)
{
    // Twenty sends into four slots: the overflow waits on the producer side and leaves with later sends.
    EventChannel channel{4};
    for (int index = 0; index < 20; ++index)
    {
        channel.send(Events{}.key_down(static_cast<Key>(std::to_underlying(Key::A) + index % 26)).span());
    }
    std::vector<Event> received;
    for (int round = 0; round < 8 && received.size() < 20; ++round)
    {
        const EventSpan batch = channel.receive();
        received.insert(received.end(), batch.begin(), batch.end());
        channel.send(EventSpan{});
    }
    ASSERT_EQ(received.size(), 20U);
    for (std::size_t index = 0; index < received.size(); ++index)
    {
        EXPECT_EQ(received[index].as<KeyboardEvent>().key, static_cast<Key>(std::to_underlying(Key::A) + index % 26));
    }
}

TEST(EventChannelTest, ThreadsExchangeEveryEventInOrder)
{
    // The platform thread sends numbered batches; the game thread receives them all, in order.
    constexpr int BATCHES = 2000;
    EventChannel channel;
    std::atomic<bool> consumer_done{false};
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    std::jthread platform_thread{[&channel, &consumer_done, deadline]
    {
        for (int batch = 0; batch < BATCHES; ++batch)
        {
            MouseMotionEvent motion{};
            motion.delta = gfx::Vector2{.x = static_cast<float>(batch), .y = 0.0F};
            EventQueue queue;
            queue.push(Event::make<EventType::MOUSE_MOTION>(std::chrono::nanoseconds{batch}, motion));
            channel.send(queue.view());
        }

        // Keep pumping (as a platform thread does) so that what waits on the producer side leaves.
        while (!consumer_done.load() && std::chrono::steady_clock::now() < deadline)
        {
            channel.send(EventSpan{});
            std::this_thread::yield();
        }
    }};
    std::vector<float> received;
    while (received.size() < BATCHES && std::chrono::steady_clock::now() < deadline)
    {
        for (const Event& event : channel.receive())
        {
            received.push_back(event.as<MouseMotionEvent>().delta.x);
        }
        std::this_thread::yield();
    }
    consumer_done.store(true);
    platform_thread.join();
    ASSERT_EQ(received.size(), static_cast<std::size_t>(BATCHES));
    for (std::size_t index = 0; index < received.size(); ++index)
    {
        ASSERT_EQ(received[index], static_cast<float>(index));
    }
}

TEST(EventChannelTest, ChannelsNeedTwoSlots)
{
    EXPECT_THROW(EventChannel{1}, gfx::InvalidUsageError);
}

} // namespace
