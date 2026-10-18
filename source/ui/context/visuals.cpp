/// @file visuals.cpp
/// @brief UiFrame's visualization widgets: splitters, line plots and histograms, progress bars, images and image
///        buttons.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import :state;

namespace gfx::ui
{

namespace
{

/// Whether @p rectangle contains @p point.
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// The extent of an item on one axis: @p asked when positive, the room left minus it when negative, else @p fit.
auto
item_extent(float asked,
            float room,
            float fit) noexcept -> float
{
    return asked > 0 ? asked : asked < 0 ? std::max(4.0F, room + asked) : fit;
}

/// @p value within 0 and 1.
auto
saturate(float value) noexcept -> float
{
    return std::clamp(value, 0.0F, 1.0F);
}

} // namespace

auto
UiFrame::splitter(std::string_view id,
                  bool vertical,
                  float thickness,
                  float& first,
                  float& second,
                  float first_minimum,
                  float second_minimum) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("splitter");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    if (!(thickness > 0))
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "splitter", "a splitter needs a positive thickness", id);
        return false;
    }

    // The bar after the first area, as long as the space available; the cursor stays where it is.
    const std::uint32_t identifier = state.widget_id(id);
    const Vector2 origin = window->cursor;
    const float length = std::max(1.0F, vertical ? state.content_bottom(*window) - origin.y : state.content_right(*window) - origin.x);
    const auto bar_at = [&](float size)
    {
        return vertical ? UiRectangle{.left = origin.x + size, .top = origin.y, .right = origin.x + size + thickness, .bottom = origin.y + length}
                        : UiRectangle{.left = origin.x, .top = origin.y + size, .right = origin.x + length, .bottom = origin.y + size + thickness};
    };
    UiRectangle bar = bar_at(first);
    if (!state.item_add(*window, bar, identifier, false, "splitter", id))
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, bar, identifier);
    if (state.active_id == identifier && state.mouse[0].clicked)
    {
        state.active_offset = {.x = state.mouse_position.x - bar.left, .y = state.mouse_position.y - bar.top};
    }

    // Dragged: the first area takes what the bar moved, the second gives it, each kept at its minimum.
    if (button.held && !state.active_by_keyboard)
    {
        const float total = first + second;
        const float wanted = vertical ? state.mouse_position.x - state.active_offset.x - origin.x : state.mouse_position.y - state.active_offset.y - origin.y;
        const float resized = std::clamp(wanted, first_minimum, std::max(first_minimum, total - second_minimum));
        if (resized != first)
        {
            first = resized;
            second = total - resized;
            state.mark_edited(identifier);
            bar = bar_at(first);
        }
    }
    if (button.hovered || button.held)
    {
        state.cursor = vertical ? CursorShape::RESIZE_EAST_WEST : CursorShape::RESIZE_NORTH_SOUTH;
        state.list_of(*window).add_rectangle_filled(bar, state.color(button.held ? StyleColor::SEPARATOR_ACTIVE : StyleColor::SEPARATOR_HOVERED));
    }
    return button.held;
}

void
UiFrame::plot(std::string_view label,
              std::span<const float> values,
              const PlotOptions& options,
              bool histogram,
              std::string_view operation) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window(operation);
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // The graph (the item width, a frame's height), its label after it.
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const TextLayout text = state.layout_text(ContextState::display_text(label), 0, operation);
    const float line = state.current_font().line_height;
    const Vector2 room{.x = state.content_right(*window) - window->cursor.x, .y = state.content_bottom(*window) - window->cursor.y};
    const float width = options.size.x == 0 ? state.take_item_width(*window) : item_extent(options.size.x, room.x, 0.0F);
    const float height = item_extent(options.size.y, room.y, line + style.frame_padding.y * 2);
    const Vector2 position = window->cursor;
    const UiRectangle frame{.left = position.x, .top = position.y, .right = position.x + width, .bottom = position.y + height};
    const UiRectangle inner{.left = frame.left + style.frame_padding.x, .top = frame.top + style.frame_padding.y, .right = frame.right - style.frame_padding.x,
                            .bottom = frame.bottom - style.frame_padding.y};
    const float total = width + (text.width > 0 ? style.item_inner_spacing.x + text.width : 0.0F);
    state.item_size(*window, {.x = total, .y = height}, style.frame_padding.y);
    if (!state.item_add(*window, {.left = frame.left, .top = frame.top, .right = frame.left + total, .bottom = frame.bottom}, 0, false, operation, label))
    {
        return;
    }
    const bool hovered = state.item_hoverable(*window, frame, id) && contains(inner, state.mouse_position);
    DrawList& list = state.list_of(*window);
    state.draw_frame(list, frame, state.color(StyleColor::FRAME_BACKGROUND), style.frame_rounding);

    // The scale: as given, else the values' extent (NaN values are skipped).
    float minimum = options.minimum;
    float maximum = options.maximum;
    if (std::isnan(minimum) || std::isnan(maximum))
    {
        float smallest = std::numeric_limits<float>::max();
        float largest = std::numeric_limits<float>::lowest();
        for (const float value : values)
        {
            if (!std::isnan(value))
            {
                smallest = std::min(smallest, value);
                largest = std::max(largest, value);
            }
        }
        minimum = std::isnan(minimum) ? smallest : minimum;
        maximum = std::isnan(maximum) ? largest : maximum;
    }
    const std::size_t needed = histogram ? 1 : 2;
    if (values.size() >= needed && inner.width() > 0 && minimum <= maximum)
    {
        // One sample per pixel at most (Dear ImGui's sampling): point n + 1 shows the value at n / resolution.
        const auto count = static_cast<std::int64_t>(values.size());
        const std::int64_t items = histogram ? count : count - 1;
        const std::int64_t resolution = std::max<std::int64_t>(
            1, std::min(static_cast<std::int64_t>(inner.width() * state.pixels_per_unit), count) - (histogram ? 0 : 1));
        const float inverse = maximum > minimum ? 1.0F / (maximum - minimum) : 0.0F;
        const auto value_at = [&](std::int64_t index) { return values[static_cast<std::size_t>(std::clamp<std::int64_t>(index, 0, count - 1))]; };
        const auto y_of = [&](float value)
        {
            const float shown = std::isnan(value) ? minimum : value;
            return inner.bottom - saturate((shown - minimum) * inverse) * inner.height();
        };
        const auto sample = [&](std::int64_t n) { return static_cast<std::int64_t>(static_cast<float>(n) / static_cast<float>(resolution) * static_cast<float>(items) + 0.5F); };

        // The value under the mouse, in a tooltip.
        std::int64_t hovered_index = -1;
        if (hovered)
        {
            const float t = std::clamp((state.mouse_position.x - inner.left) / inner.width(), 0.0F, 0.9999F);
            hovered_index = static_cast<std::int64_t>(t * static_cast<float>(items));
            if (histogram)
            {
                tooltip(format_text("{}: {:.4g}", hovered_index, value_at(hovered_index)));
            }
            else
            {
                tooltip(format_text("{}: {:.4g}\n{}: {:.4g}", hovered_index, value_at(hovered_index), hovered_index + 1, value_at(hovered_index + 1)));
            }
        }
        const Color base = state.color(histogram ? StyleColor::PLOT_HISTOGRAM : StyleColor::PLOT_LINES);
        const Color highlighted = state.color(histogram ? StyleColor::PLOT_HISTOGRAM_HOVERED : StyleColor::PLOT_LINES_HOVERED);
        const float step = inner.width() / static_cast<float>(resolution);
        if (histogram)
        {
            // Bars from the zero line (or the scale's edge), a pixel apart when they are wide enough.
            const float zero = minimum * maximum < 0 ? inner.bottom - saturate(-minimum * inverse) * inner.height() : (minimum < 0 ? inner.top : inner.bottom);
            float value = value_at(0);
            for (std::int64_t n = 0; n < resolution; ++n)
            {
                const std::int64_t index = sample(n);
                const float left = inner.left + step * static_cast<float>(n);
                float right = left + step;
                if (right >= left + 2.0F / state.pixels_per_unit)
                {
                    right -= 1.0F / state.pixels_per_unit;
                }
                const float top = y_of(value);
                list.add_rectangle_filled({.left = left, .top = std::min(top, zero), .right = right, .bottom = std::max(top, zero)},
                                          hovered_index == index ? highlighted : base);
                value = value_at(index + 1);
            }
        }
        else
        {
            // A polyline through the samples (the frame arena holds them); the hovered segment over it.
            std::span<Vector2> points{};
            try
            {
                const std::size_t bytes = sizeof(Vector2) * static_cast<std::size_t>(resolution + 1);
                points = {static_cast<Vector2*>(state.arena.allocate(bytes, alignof(Vector2))), static_cast<std::size_t>(resolution + 1)};
                state.arena_used += bytes;
            }
            catch (...)
            {
                state.report(UiFrameProblem::CAPACITY_EXCEEDED, operation, "the frame arena could not hold the plot's points: it is not drawn", label);
            }
            if (!points.empty())
            {
                points[0] = {.x = inner.left, .y = y_of(value_at(0))};
                std::int64_t hovered_segment = -1;
                for (std::int64_t n = 0; n < resolution; ++n)
                {
                    const std::int64_t index = sample(n);
                    points[static_cast<std::size_t>(n + 1)] = {.x = inner.left + step * static_cast<float>(n + 1), .y = y_of(value_at(index + 1))};
                    hovered_segment = index == hovered_index && hovered_segment < 0 ? n : hovered_segment;
                }
                list.push_clip_rectangle(frame);
                list.add_polyline(points, base);
                if (hovered_segment >= 0)
                {
                    list.add_line(points[static_cast<std::size_t>(hovered_segment)], points[static_cast<std::size_t>(hovered_segment + 1)], highlighted);
                }
                list.pop_clip_rectangle();
            }
        }
    }

    // The overlay centered at the top, the label after the graph.
    if (!options.overlay.empty())
    {
        const TextLayout overlay = state.layout_text(options.overlay, 0, operation);
        list.push_clip_rectangle(frame);
        state.draw_text(list, overlay, {.x = frame.left + std::max(0.0F, frame.width() - overlay.width) * 0.5F, .y = frame.top + style.frame_padding.y},
                        state.color(StyleColor::TEXT_NORMAL));
        list.pop_clip_rectangle();
    }
    if (text.width > 0)
    {
        state.draw_text(list, text, {.x = frame.right + style.item_inner_spacing.x, .y = inner.top}, state.color(StyleColor::TEXT_NORMAL));
    }
}

void
UiFrame::plot_lines(std::string_view label,
                    std::span<const float> values,
                    const PlotOptions& options) noexcept
{
    plot(label, values, options, false, "plot_lines");
}

void
UiFrame::plot_histogram(std::string_view label,
                        std::span<const float> values,
                        const PlotOptions& options) noexcept
{
    plot(label, values, options, true, "plot_histogram");
}

void
UiFrame::progress_bar(float fraction,
                      Vector2 size,
                      std::string_view overlay) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("progress_bar");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // A frame (the item width, a frame's height) filled from the left.
    const Style& style = state.style;
    const float line = state.current_font().line_height;
    const Vector2 room{.x = state.content_right(*window) - window->cursor.x, .y = state.content_bottom(*window) - window->cursor.y};
    const float width = size.x == 0 ? state.take_item_width(*window) : item_extent(size.x, room.x, 0.0F);
    const float height = item_extent(size.y, room.y, line + style.frame_padding.y * 2);
    const Vector2 position = window->cursor;
    const UiRectangle frame{.left = position.x, .top = position.y, .right = position.x + width, .bottom = position.y + height};
    state.item_size(*window, {.x = width, .y = height}, style.frame_padding.y);
    if (!state.item_add(*window, frame, 0, false, "progress_bar", overlay))
    {
        return;
    }
    DrawList& list = state.list_of(*window);
    state.draw_frame(list, frame, state.color(StyleColor::FRAME_BACKGROUND), style.frame_rounding);
    const float border = style.frame_border_size;
    const UiRectangle inner{.left = frame.left + border, .top = frame.top + border, .right = frame.right - border, .bottom = frame.bottom - border};

    // The filled part (indeterminate: a fifth of the bar sweeping across), clipped out of the whole rounded bar.
    const bool indeterminate = fraction < 0;
    float start = 0;
    float end = saturate(fraction);
    if (indeterminate)
    {
        constexpr float SWEEP = 0.2F;
        const double phase = std::fmod(state.time / INDETERMINATE_PROGRESS_SECONDS, 1.0);
        const float head = static_cast<float>(phase) * (1.0F + SWEEP) - SWEEP;
        start = saturate(head);
        end = saturate(head + SWEEP);
    }
    const float fill_left = inner.left + inner.width() * start;
    const float fill_right = inner.left + inner.width() * end;
    if (fill_right > fill_left)
    {
        list.push_clip_rectangle({.left = fill_left, .top = inner.top, .right = fill_right, .bottom = inner.bottom});
        list.add_rectangle_filled(inner, state.color(StyleColor::PLOT_HISTOGRAM), std::max(0.0F, style.frame_rounding - border));
        list.pop_clip_rectangle();
    }

    // The overlay (the percentage by default) after the filled part, kept inside the bar; centered when indeterminate.
    const std::string_view shown = !overlay.empty() ? overlay : indeterminate ? std::string_view{} : format_text("{:.0f}%", saturate(fraction) * 100.0F);
    if (shown.empty())
    {
        return;
    }
    const TextLayout text = state.layout_text(shown, 0, "progress_bar");
    const float text_x = indeterminate ? frame.left + (frame.width() - text.width) * 0.5F : fill_right + style.item_spacing.x;
    const float x = std::clamp(text_x, frame.left, std::max(frame.left, frame.right - text.width - style.item_inner_spacing.x));
    list.push_clip_rectangle(frame);
    state.draw_text(list, text, {.x = x, .y = frame.top + std::max(0.0F, frame.height() - text.height) * 0.5F}, state.color(StyleColor::TEXT_NORMAL));
    list.pop_clip_rectangle();
}

void
UiFrame::image(std::uint32_t image,
               Vector2 size,
               const UiRectangle& texture_coordinates,
               Color tint) noexcept
{
    image_rounded(image, size, 0.0F, texture_coordinates, tint);
}

void
UiFrame::image_rounded(std::uint32_t image,
                       Vector2 size,
                       float rounding,
                       const UiRectangle& texture_coordinates,
                       Color tint) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window(rounding > 0 ? "image_rounded" : "image");
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    const Vector2 position = window->cursor;
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + std::max(size.x, 0.0F), .bottom = position.y + std::max(size.y, 0.0F)};
    state.item_size(*window, {.x = rectangle.width(), .y = rectangle.height()});
    if (!state.item_add(*window, rectangle, 0, false, "image", {}))
    {
        return;
    }
    DrawList& list = state.list_of(*window);
    if (rounding > 0)
    {
        list.add_image_rounded(image, rectangle, rounding, state.faded(tint), texture_coordinates);
    }
    else
    {
        list.add_image(image, rectangle, state.faded(tint), texture_coordinates);
    }
}

auto
UiFrame::image_button(std::string_view id,
                      std::uint32_t image,
                      Vector2 size,
                      const UiRectangle& texture_coordinates) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("image_button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // A button's frame around the image (the frame padding between).
    const Style& style = state.style;
    const std::uint32_t identifier = state.widget_id(id);
    const Vector2 padding = style.frame_padding;
    const Vector2 position = window->cursor;
    const UiRectangle rectangle{.left = position.x,
                                .top = position.y,
                                .right = position.x + std::max(size.x, 0.0F) + padding.x * 2,
                                .bottom = position.y + std::max(size.y, 0.0F) + padding.y * 2};
    state.item_size(*window, {.x = rectangle.width(), .y = rectangle.height()}, padding.y);
    if (!state.item_add(*window, rectangle, identifier, true, "image_button", id))
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, rectangle, identifier);
    DrawList& list = state.list_of(*window);
    const StyleColor fill = button.held && button.hovered ? StyleColor::BUTTON_ACTIVE : button.hovered ? StyleColor::BUTTON_HOVERED : StyleColor::BUTTON;
    state.draw_frame(list, rectangle, state.color(fill), style.frame_rounding);
    state.draw_focus(list, rectangle, identifier, style.frame_rounding);
    list.add_image(image,
                   {.left = rectangle.left + padding.x, .top = rectangle.top + padding.y, .right = rectangle.right - padding.x, .bottom = rectangle.bottom - padding.y},
                   state.faded({.red = 1, .green = 1, .blue = 1, .alpha = 1}), texture_coordinates);
    return button.pressed;
}

} // namespace gfx::ui
