/// @file ui_platform.cpp
/// @brief UiPlatformInput (window events → UiFrameInput) and UiWindowServices (clipboard, cursors, text input).
module gfx.ui.platform;

import std;
import gfx.platform;
import gfx.ui;

namespace gfx::ui
{

namespace
{

/// The mouse button bit of @p button.
constexpr auto
button_bit(MouseButton button) noexcept -> MouseButtonMask
{
    return static_cast<MouseButtonMask>(1U << std::to_underlying(button));
}

} // namespace

UiPlatformInput::UiPlatformInput(const platform::Window& window)
    : m_window{&window}
{
    m_buttons.reserve(32);
    m_keys.reserve(64);
    m_text.reserve(256);
}

void
UiPlatformInput::begin_frame() noexcept
{
    m_buttons.clear();
    m_keys.clear();
    m_text.clear();
    m_wheel = {};
}

auto
UiPlatformInput::pixels_per_unit() const noexcept -> float
{
    const float scale = m_window->content_scale();
    return scale > 0 ? scale : 1.0F;
}

auto
UiPlatformInput::to_units(Vector2 position) const noexcept -> Vector2
{
    // Window coordinates → pixels → UI units.
    const float factor = m_window->pixel_density() / pixels_per_unit();
    return {.x = position.x * factor, .y = position.y * factor};
}

void
UiPlatformInput::add_events(platform::EventSpan events)
{
    using platform::EventType;
    const platform::WindowHandle window = m_window->handle();
    for (const platform::Event& event : events)
    {
        if (event.window() != window)
        {
            continue;
        }
        switch (event.type())
        {
            case EventType::MOUSE_MOTION:
            {
                const auto& motion = event.as<platform::MouseMotionEvent>();
                m_mouse = to_units(motion.position);
                m_mouse_present = true;
                m_held = motion.buttons;
                break;
            }
            case EventType::MOUSE_BUTTON_DOWN:
            case EventType::MOUSE_BUTTON_UP:
            {
                const auto& button = event.as<platform::MouseButtonEvent>();
                const bool down = event.type() == EventType::MOUSE_BUTTON_DOWN;
                m_mouse = to_units(button.position);
                m_mouse_present = true;
                m_buttons.push_back({.button = button.button, .down = down, .position = m_mouse});
                m_held = down ? (m_held | button_bit(button.button)) : (m_held & ~button_bit(button.button));
                break;
            }
            case EventType::MOUSE_WHEEL:
            {
                const auto& wheel = event.as<platform::MouseWheelEvent>();
                m_wheel = {.x = m_wheel.x + wheel.delta.x, .y = m_wheel.y + wheel.delta.y};
                break;
            }
            case EventType::KEY_DOWN:
            case EventType::KEY_UP:
            {
                const auto& key = event.as<platform::KeyboardEvent>();
                m_keys.push_back({.key = key.key, .key_code = key.key_code, .down = event.type() == EventType::KEY_DOWN, .repeat = key.repeat});
                m_modifiers = key.modifiers;
                break;
            }
            case EventType::TEXT_INPUT:
                m_text.append(events.text(event));
                break;
            case EventType::WINDOW_FOCUS_GAINED:
                m_focused = true;
                break;
            case EventType::WINDOW_FOCUS_LOST:
                m_focused = false;
                m_held = MouseButtonMask::NONE;
                m_modifiers = KeyModifier::NONE;
                break;
            case EventType::WINDOW_MOUSE_ENTERED:
                m_mouse_present = true;
                break;
            case EventType::WINDOW_MOUSE_LEFT:
                m_mouse_present = false;
                break;
            default:
                break;
        }
    }
}

auto
UiPlatformInput::frame_input(double time) const noexcept -> UiFrameInput
{
    // The display in UI units: the framebuffer's pixels over the content scale.
    const Extent pixels = m_window->framebuffer_size();
    const float scale = pixels_per_unit();
    return {.time = time,
            .display_size = {.x = static_cast<float>(pixels.width) / scale, .y = static_cast<float>(pixels.height) / scale},
            .pixels_per_unit = scale,
            .focused = m_focused,
            .mouse_present = m_mouse_present,
            .mouse_position = m_mouse,
            .mouse_buttons = m_buttons,
            .mouse_buttons_held = m_held,
            .mouse_wheel = m_wheel,
            .keys = m_keys,
            .modifiers = m_modifiers,
            .text = m_text};
}

UiWindowServices::UiWindowServices(platform::Platform& platform,
                                   platform::Window& window)
    : m_platform{&platform}
    , m_window{&window}
{
}

auto
UiWindowServices::clipboard_text() -> std::string
{
    return m_platform->clipboard_text();
}

void
UiWindowServices::set_clipboard_text(std::string_view text)
{
    m_platform->set_clipboard_text(text);
}

void
UiWindowServices::set_cursor(CursorShape shape)
{
    // One platform cursor per shape, made when first shown.
    std::unique_ptr<platform::Cursor>& cursor = m_cursors[std::to_underlying(shape)];
    if (cursor == nullptr)
    {
        cursor = m_platform->create_cursor(shape);
    }
    m_window->set_cursor(cursor->handle());
}

void
UiWindowServices::set_text_input(bool active,
                                 const UiRectangle& area)
{
    if (!active)
    {
        m_window->stop_text_input();
        return;
    }

    // UI units back to window coordinates.
    const float factor = m_window->content_scale() / std::max(m_window->pixel_density(), 1.0e-3F);
    const Rectangle rectangle{.x = static_cast<std::int32_t>(std::floor(area.left * factor)),
                              .y = static_cast<std::int32_t>(std::floor(area.top * factor)),
                              .width = static_cast<std::uint32_t>(std::ceil(area.width() * factor)),
                              .height = static_cast<std::uint32_t>(std::ceil(area.height() * factor))};
    m_window->start_text_input({.area = rectangle});
}

auto
UiWindowServices::open_url(std::string_view /*url*/) -> bool
{
    return false;
}

} // namespace gfx::ui
