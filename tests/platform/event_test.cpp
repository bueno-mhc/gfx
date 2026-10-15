#include <gtest/gtest.h>

import std;
import gfx.platform;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;

template<class... Handlers>
struct Overloaded : Handlers...
{
    using Handlers::operator()...;
};

TEST(EventTest, EventsAreSmallTriviallyCopyableValues)
{
    static_assert(sizeof(Event) <= MAXIMUM_EVENT_SIZE);
    static_assert(std::is_trivially_copyable_v<Event>);
    static_assert(std::same_as<EventPayload<EventType::KEY_DOWN>, KeyboardEvent>);
    static_assert(std::same_as<EventPayload<EventType::KEY_UP>, KeyboardEvent>);
    static_assert(std::same_as<EventPayload<EventType::WINDOW_FRAMEBUFFER_RESIZED>, WindowEvent>);
    static_assert(std::same_as<EventPayload<EventType::TOUCH_CANCELED>, TouchEvent>);
    static_assert(std::same_as<EventPayload<EventType::NONE>, std::monostate>);

    // Every event type has exactly one payload (the mapping is computed at compile time).
    for (const auto& entry : gfx::EnumerationTraits<EventType>::NAMES)
    {
        EXPECT_LT(std::to_underlying(entry.value), gfx::ENUMERATOR_COUNT<EventType>) << entry.name;
    }
}

TEST(EventTest, PayloadsAreReadAsWhatTheyAre)
{
    const Event key = Event::make<EventType::KEY_DOWN>(WindowHandle{}, 5ms,
                                                       KeyboardEvent{.key = Key::W, .key_code = KeyCode::W, .modifiers = KeyModifier::LEFT_SHIFT,
                                                                     .native_scancode = 17, .repeat = false});
    EXPECT_EQ(key.type(), EventType::KEY_DOWN);
    EXPECT_EQ(key.timestamp(), 5ms);
    ASSERT_NE(key.get_if<KeyboardEvent>(), nullptr);
    EXPECT_EQ(key.get_if<KeyboardEvent>()->key, Key::W);
    EXPECT_EQ(key.get_if<MouseMotionEvent>(), nullptr);
    EXPECT_EQ(key.as<KeyboardEvent>().modifiers, KeyModifier::LEFT_SHIFT);
    EXPECT_THROW((void)key.as<MouseButtonEvent>(), gfx::InvalidUsageError);
    EXPECT_FALSE(key.text().has_value());

    // Visiting dispatches on the payload held.
    const std::string_view kind = key.visit(Overloaded{
        [](const KeyboardEvent&) { return std::string_view{"keyboard"}; },
        [](const auto&) { return std::string_view{"other"}; },
    });
    EXPECT_EQ(kind, "keyboard");
}

TEST(EventQueueTest, QueuesStoreTextAndKeepTheirMemory)
{
    EventQueue queue{4, 16};
    const TextInputEvent input{};
    queue.push(Event::make<EventType::TEXT_INPUT>(WindowHandle{}, 1ms, input), "héllo");
    queue.push(Event::make<EventType::QUIT_REQUESTED>(2ms, ApplicationEvent{}));

    // The text is stored in the queue and referenced by the event.
    const EventSpan events = queue.view();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events.text(events[0]), "héllo");
    EXPECT_EQ(events.text(events[1]), "");

    // UTF-32 code points without allocation.
    const std::vector<char32_t> points(std::from_range, code_points(events.text(events[0])));
    EXPECT_EQ(points, (std::vector<char32_t>{U'h', U'é', U'l', U'l', U'o'}));

    // Clearing keeps the memory: no allocation for the next pump of the same size.
    const std::size_t event_capacity = queue.event_capacity();
    const std::size_t text_capacity = queue.text_capacity();
    queue.clear();
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.event_capacity(), event_capacity);
    EXPECT_EQ(queue.text_capacity(), text_capacity);
}

TEST(EventQueueTest, EventsKeepTheirTextWhenMovedBetweenQueues)
{
    EventQueue first;
    first.push(Event::make<EventType::DROP_FILE>(WindowHandle{}, 1ms, DropEvent{}), "C:/data/level.map");
    EventQueue second;
    (void)second.store_text("something else first");
    second.push(first.view());
    const EventSpan moved = second.view();
    ASSERT_EQ(moved.size(), 1U);
    EXPECT_EQ(moved.text(moved[0]), "C:/data/level.map");
}

TEST(TextTest, MalformedUtf8IsDetectedAndReplaced)
{
    static_assert(is_valid_utf8("plain ASCII"));
    static_assert(is_valid_utf8("\xE2\x82\xAC"), "U+20AC (euro sign)");
    static_assert(is_valid_utf8("\xEF\xBF\xBD"), "U+FFFD itself is valid");
    static_assert(!is_valid_utf8("\xC3"), "truncated sequence");
    static_assert(!is_valid_utf8("\xC0\xAF"), "overlong encoding");
    static_assert(!is_valid_utf8("\xED\xA0\x80"), "surrogate");
    const std::vector<char32_t> points(std::from_range, code_points("a\xFF" "b"));
    EXPECT_EQ(points, (std::vector<char32_t>{U'a', U'\uFFFD', U'b'}));
}

} // namespace
