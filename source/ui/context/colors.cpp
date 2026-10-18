/// @file colors.cpp
/// @brief Color widgets: color_edit() (channel drags or a hexadecimal field, a preview opening a picker popup),
///        color_picker() (a saturation/value square and a hue bar, or a hue wheel and a triangle; an alpha bar; a
///        side preview) and color_button() (a swatch over a checkerboard).
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

constexpr float PI = std::numbers::pi_v<float>;
constexpr Color WHITE{.red = 1, .green = 1, .blue = 1, .alpha = 1};
constexpr Color BLACK{.red = 0, .green = 0, .blue = 0, .alpha = 1};
constexpr Color TRANSPARENT_BLACK{.red = 0, .green = 0, .blue = 0, .alpha = 0};
constexpr Color CHECKER_LIGHT = Color::from_rgba8(204, 204, 204);
constexpr Color CHECKER_DARK = Color::from_rgba8(128, 128, 128);

/// A checkerboard of @p cell squares over @p rectangle (behind translucent colors).
void
draw_checkerboard(DrawList& list,
                  const UiRectangle& rectangle,
                  float cell)
{
    list.add_rectangle_filled(rectangle, CHECKER_LIGHT);
    std::uint32_t row = 0;
    for (float top = rectangle.top; top < rectangle.bottom; top += cell, ++row)
    {
        std::uint32_t column = 0;
        for (float left = rectangle.left; left < rectangle.right; left += cell, ++column)
        {
            if (((row + column) & 1U) != 0)
            {
                list.add_rectangle_filled({.left = left, .top = top, .right = std::min(left + cell, rectangle.right), .bottom = std::min(top + cell, rectangle.bottom)},
                                          CHECKER_DARK);
            }
        }
    }
}

/// @p color as HSV, its hue and saturation kept from the last edit where the color leaves them undefined (gray,
/// black).
auto
hsv_of(const ContextState& state,
       Color color) noexcept -> HsvColor
{
    HsvColor hsv = to_hsv(color);
    if (state.saved_hue_color == color.with_alpha(1).packed())
    {
        if (hsv.saturation == 0 || hsv.value == 0)
        {
            hsv.hue = state.saved_hue;
        }
        if (hsv.value == 0)
        {
            hsv.saturation = state.saved_saturation;
        }
    }
    return hsv;
}

/// Remembers the hue and saturation of @p hsv for color @p color.
void
save_hue(ContextState& state,
         const HsvColor& hsv,
         Color color) noexcept
{
    state.saved_hue = hsv.hue;
    state.saved_saturation = hsv.saturation;
    state.saved_hue_color = color.with_alpha(1).packed();
}

/// @p flags with the display, channels and picker the options menu chose where @p flags choose none.
auto
with_options(const ContextState& state,
             ColorEditFlag flags) noexcept -> ColorEditFlag
{
    constexpr ColorEditFlag DISPLAY = ColorEditFlag::DISPLAY_RGB | ColorEditFlag::DISPLAY_HSV | ColorEditFlag::DISPLAY_HEXADECIMAL;
    constexpr ColorEditFlag CHANNELS = ColorEditFlag::INTEGER_CHANNELS | ColorEditFlag::FLOAT_CHANNELS;
    constexpr ColorEditFlag PICKER = ColorEditFlag::PICKER_HUE_BAR | ColorEditFlag::PICKER_HUE_WHEEL;
    for (const ColorEditFlag group : {DISPLAY, CHANNELS, PICKER})
    {
        if (!has_any(flags, group))
        {
            flags = flags | (state.color_options & group);
        }
    }
    return flags;
}

/// Replaces the options' bits of @p group with @p choice.
void
choose_option(ContextState& state,
              ColorEditFlag group,
              ColorEditFlag choice) noexcept
{
    state.color_options = (state.color_options & ~group) | choice;
}

/// Edits @p color holding hue, saturation and value (ColorEditFlag::INPUT_HSV) with @p edit, which edits red, green
/// and blue: true when it changed.
template<class Edit>
auto
edit_hsv(ContextState& state,
         Color& color,
         Edit&& edit) noexcept -> bool
{
    Color rgb = from_hsv({.hue = color.red, .saturation = color.green, .value = color.blue, .alpha = color.alpha});
    if (state.saved_hue_color != rgb.with_alpha(1).packed())
    {
        save_hue(state, {.hue = color.red, .saturation = color.green, .value = color.blue, .alpha = color.alpha}, rgb);
    }
    if (!std::forward<Edit>(edit)(rgb))
    {
        return false;
    }
    const HsvColor hsv = hsv_of(state, rgb);
    color = {.red = hsv.hue, .green = hsv.saturation, .blue = hsv.value, .alpha = rgb.alpha};
    return true;
}

/// A region of the picker the mouse drags: true while it is held (the value follows the mouse).
auto
drag_region(ContextState& state,
            WindowState& window,
            const UiRectangle& rectangle,
            std::uint32_t id,
            bool may_start = true) noexcept -> bool
{
    static_cast<void>(state.item_add(window, rectangle, id, false, "color_picker", {}));
    const bool hovered = state.item_hoverable(window, rectangle, id);
    if (hovered)
    {
        state.hovered_id = id;
    }
    if (hovered && may_start && state.mouse[0].clicked && state.active_id != id)
    {
        state.set_active(id, window);
        state.active_button = MouseButton::LEFT;
    }
    if (state.active_id == id && !state.active_by_keyboard)
    {
        state.active_id_alive = true;
        if (state.mouse[0].down)
        {
            return true;
        }
        state.clear_active();
    }
    return false;
}

/// A marker ring at @p center (white over black, visible on any color).
void
draw_marker(DrawList& list,
            Vector2 center,
            float radius)
{
    list.add_circle(center, radius + 1, BLACK, 1);
    list.add_circle(center, radius, WHITE, 2);
}

/// A horizontal marker across a vertical bar at @p y.
void
draw_bar_marker(DrawList& list,
                const UiRectangle& bar,
                float y)
{
    list.add_rectangle({.left = bar.left - 2, .top = y - 3, .right = bar.right + 2, .bottom = y + 3}, BLACK, 1);
    list.add_rectangle({.left = bar.left - 1, .top = y - 2, .right = bar.right + 1, .bottom = y + 2}, WHITE, 1);
}

/// Barycentric coordinates of @p point in triangle @p a, @p b, @p c.
auto
barycentric(Vector2 a,
            Vector2 b,
            Vector2 c,
            Vector2 point) noexcept -> std::array<float, 3>
{
    const Vector2 v0{.x = b.x - a.x, .y = b.y - a.y};
    const Vector2 v1{.x = c.x - a.x, .y = c.y - a.y};
    const Vector2 v2{.x = point.x - a.x, .y = point.y - a.y};
    const float denominator = v0.x * v1.y - v1.x * v0.y;
    if (denominator == 0)
    {
        return {1, 0, 0};
    }
    const float v = (v2.x * v1.y - v1.x * v2.y) / denominator;
    const float w = (v0.x * v2.y - v2.x * v0.y) / denominator;
    return {1.0F - v - w, v, w};
}

/// @p point moved to the nearest point of triangle @p a, @p b, @p c (unchanged inside it).
auto
closest_in_triangle(Vector2 a,
                    Vector2 b,
                    Vector2 c,
                    Vector2 point) noexcept -> Vector2
{
    const std::array<float, 3> weights = barycentric(a, b, c, point);
    if (weights[0] >= 0 && weights[1] >= 0 && weights[2] >= 0)
    {
        return point;
    }
    const auto closest_on_segment = [point](Vector2 from, Vector2 to)
    {
        const Vector2 direction{.x = to.x - from.x, .y = to.y - from.y};
        const float length = direction.x * direction.x + direction.y * direction.y;
        const float t = length > 0 ? std::clamp(((point.x - from.x) * direction.x + (point.y - from.y) * direction.y) / length, 0.0F, 1.0F) : 0.0F;
        return Vector2{.x = from.x + direction.x * t, .y = from.y + direction.y * t};
    };
    const auto distance = [point](Vector2 other) { return (other.x - point.x) * (other.x - point.x) + (other.y - point.y) * (other.y - point.y); };
    Vector2 best = closest_on_segment(a, b);
    for (const Vector2 candidate : {closest_on_segment(b, c), closest_on_segment(c, a)})
    {
        if (distance(candidate) < distance(best))
        {
            best = candidate;
        }
    }
    return best;
}

/// @p point rotated by @p angle about the origin.
auto
rotated(Vector2 point,
        float angle) noexcept -> Vector2
{
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {.x = point.x * cosine - point.y * sine, .y = point.x * sine + point.y * cosine};
}

} // namespace

auto
UiFrame::color_button(std::string_view id,
                      Color color,
                      ColorEditFlag flags,
                      Vector2 size) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("color_button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // A square of a frame's height unless sized.
    const Style& style = state.style;
    const float frame = state.current_font().line_height + style.frame_padding.y * 2;
    const Vector2 extent{.x = size.x > 0 ? size.x : frame, .y = size.y > 0 ? size.y : frame};
    const std::uint32_t identifier = state.widget_id(id);
    const Vector2 position = window->cursor;
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + extent.x, .bottom = position.y + extent.y};
    state.item_size(*window, extent, style.frame_padding.y);
    if (!state.item_add(*window, rectangle, identifier, true, "color_button", id))
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, rectangle, identifier);

    // The color (opaque without alpha; over a checkerboard where translucent, half of it with ALPHA_PREVIEW_HALF).
    DrawList& list = state.list_of(*window);
    const Color opaque = color.with_alpha(1);
    const bool translucent = !has_any(flags, ColorEditFlag::NO_ALPHA) && color.alpha < 1;
    const float cell = std::max(2.0F, std::floor(frame * 0.5F));
    if (!translucent)
    {
        list.add_rectangle_filled(rectangle, state.faded(opaque), style.frame_rounding);
    }
    else if (has_any(flags, ColorEditFlag::ALPHA_PREVIEW_HALF))
    {
        const float middle = std::floor((rectangle.left + rectangle.right) * 0.5F);
        list.add_rectangle_filled({.left = rectangle.left, .top = rectangle.top, .right = middle, .bottom = rectangle.bottom}, state.faded(opaque));
        const UiRectangle right{.left = middle, .top = rectangle.top, .right = rectangle.right, .bottom = rectangle.bottom};
        draw_checkerboard(list, right, cell);
        list.add_rectangle_filled(right, state.faded(color));
    }
    else
    {
        draw_checkerboard(list, rectangle, cell);
        list.add_rectangle_filled(rectangle, state.faded(color));
    }
    if (!has_any(flags, ColorEditFlag::NO_BORDER))
    {
        list.add_rectangle(rectangle, state.color(button.hovered ? StyleColor::TEXT_NORMAL : StyleColor::BORDER), 1, style.frame_rounding);
    }
    state.draw_focus(list, rectangle, identifier, style.frame_rounding);

    // Dragged, it carries its color (COLOR_PAYLOAD_TYPE); hovered a moment, a tooltip shows it. Both leave the
    // swatch the last item.
    const LastItem swatch = state.last_item;
    const ColorEditFlag preview_flags = (flags & (ColorEditFlag::NO_ALPHA | ColorEditFlag::ALPHA_PREVIEW_HALF)) | ColorEditFlag::NO_TOOLTIP | ColorEditFlag::NO_DRAG_DROP;
    const HexadecimalColor hexadecimal = to_hexadecimal(color, !has_any(flags, ColorEditFlag::NO_ALPHA));
    if (!has_any(flags, ColorEditFlag::NO_DRAG_DROP) && begin_drag_drop_source())
    {
        static_cast<void>(set_drag_drop_payload(COLOR_PAYLOAD_TYPE, color));
        static_cast<void>(color_button("##dragged", color, preview_flags, {.x = frame * 2, .y = frame * 2}));
        same_line();
        text_unformatted(hexadecimal.view());
        end_drag_drop_source();
    }
    else if (!has_any(flags, ColorEditFlag::NO_TOOLTIP) && begin_item_tooltip())
    {
        static_cast<void>(color_button("##shown", color, preview_flags, {.x = frame * 3, .y = frame * 3}));
        same_line();
        begin_group();
        text_unformatted(hexadecimal.view());
        text("R {} G {} B {} A {}", std::lround(color.red * 255.0F), std::lround(color.green * 255.0F), std::lround(color.blue * 255.0F),
             std::lround(color.alpha * 255.0F));
        end_group();
        end_tooltip();
    }
    state.last_item = swatch;
    return button.pressed;
}

auto
UiFrame::color_edit(std::string_view label,
                    Color& color,
                    ColorEditFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("color_edit");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    if (has_any(flags, ColorEditFlag::INPUT_HSV))
    {
        return edit_hsv(state, color, [&](Color& rgb) { return color_edit(label, rgb, flags & ~ColorEditFlag::INPUT_HSV); });
    }
    flags = with_options(state, flags);
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const bool alpha = !has_any(flags, ColorEditFlag::NO_ALPHA);
    const bool preview = !has_any(flags, ColorEditFlag::NO_SMALL_PREVIEW);
    const bool inputs = !has_any(flags, ColorEditFlag::NO_INPUTS);
    const float frame = state.current_font().line_height + style.frame_padding.y * 2;
    const float width = state.take_item_width(*window);
    bool changed = false;
    begin_group();
    push_id(label);

    // The inputs: one drag per channel (RGB or HSV; integers 0 to 255 or numbers 0 to 1), or a hexadecimal field.
    if (inputs)
    {
        const float available = std::max(1.0F, width - (preview ? frame + style.item_inner_spacing.x : 0.0F));
        if (has_any(flags, ColorEditFlag::DISPLAY_HEXADECIMAL))
        {
            std::array<char, 10> buffer{};
            const HexadecimalColor text = to_hexadecimal(color, alpha);
            std::ranges::copy(text.view(), buffer.begin());
            set_next_item_width(available);
            if (input_text("##hexadecimal", buffer, {.flags = InputTextFlag::CHARACTERS_HEXADECIMAL | InputTextFlag::CHARACTERS_UPPERCASE}))
            {
                const std::string_view typed{buffer.data(), static_cast<std::size_t>(std::ranges::find(buffer, '\0') - buffer.begin())};
                if (const std::optional<Color> parsed = from_hexadecimal(typed))
                {
                    color = alpha && typed.size() >= 8 ? *parsed : parsed->with_alpha(color.alpha);
                    changed = true;
                }
            }
        }
        else
        {
            const bool hsv_display = has_any(flags, ColorEditFlag::DISPLAY_HSV);
            const bool floats = has_any(flags, ColorEditFlag::FLOAT_CHANNELS);
            const bool hdr = has_any(flags, ColorEditFlag::HDR) && !hsv_display;
            const std::size_t count = alpha ? 4 : 3;
            const float component = std::max(1.0F, (available - style.item_inner_spacing.x * static_cast<float>(count - 1)) / static_cast<float>(count));
            const HsvColor hsv = hsv_of(state, color);
            std::array<float, 4> values = hsv_display ? std::array{hsv.hue, hsv.saturation, hsv.value, color.alpha}
                                                      : std::array{color.red, color.green, color.blue, color.alpha};
            constexpr std::array<std::string_view, 4> RGB_FLOAT{"R:{:.3f}", "G:{:.3f}", "B:{:.3f}", "A:{:.3f}"};
            constexpr std::array<std::string_view, 4> HSV_FLOAT{"H:{:.3f}", "S:{:.3f}", "V:{:.3f}", "A:{:.3f}"};
            constexpr std::array<std::string_view, 4> RGB_INTEGER{"R:{}", "G:{}", "B:{}", "A:{}"};
            constexpr std::array<std::string_view, 4> HSV_INTEGER{"H:{}", "S:{}", "V:{}", "A:{}"};
            bool edited = false;
            for (std::size_t channel = 0; channel < count; ++channel)
            {
                if (channel > 0)
                {
                    same_line(0, style.item_inner_spacing.x);
                }
                push_id(static_cast<std::int32_t>(channel));
                set_next_item_width(component);
                if (floats)
                {
                    const float maximum = hdr && channel < 3 ? std::numeric_limits<float>::max() : 1.0F;
                    edited = drag("##channel", values[channel], 1.0F / 255.0F, 0.0F, maximum, (hsv_display ? HSV_FLOAT : RGB_FLOAT)[channel]) || edited;
                }
                else
                {
                    std::int32_t value = static_cast<std::int32_t>(std::lround(std::clamp(values[channel], 0.0F, 1.0F) * 255.0F));
                    if (drag("##channel", value, 1.0F, 0, 255, (hsv_display ? HSV_INTEGER : RGB_INTEGER)[channel]))
                    {
                        values[channel] = static_cast<float>(value) / 255.0F;
                        edited = true;
                    }
                }
                pop_id();
            }
            if (edited)
            {
                if (hsv_display)
                {
                    const HsvColor edited_hsv{.hue = values[0], .saturation = values[1], .value = values[2], .alpha = values[3]};
                    color = from_hsv(edited_hsv);
                    save_hue(state, edited_hsv, color);
                }
                else
                {
                    color = {.red = values[0], .green = values[1], .blue = values[2], .alpha = alpha ? values[3] : color.alpha};
                }
                changed = true;
            }
        }
    }

    // The preview: clicking it opens the picker in a popup below it.
    if (preview)
    {
        if (inputs)
        {
            same_line(0, style.item_inner_spacing.x);
        }
        const bool clicked = color_button("##preview", color, flags);
        const UiRectangle swatch = state.last_item.rectangle;
        const std::uint32_t popup = ContextState::hash(id, "#picker");
        if (clicked && !has_any(flags, ColorEditFlag::NO_PICKER) && !std::ranges::contains(state.popups_closed_by_click, popup))
        {
            if (PopupState* opened = state.open_popup(popup, {.x = swatch.left, .y = swatch.bottom + style.item_spacing.y}, 0, 0, swatch.top - style.item_spacing.y))
            {
                opened->opener_item = state.last_item.id;
            }
            save_hue(state, hsv_of(state, color), color);
        }
        if (state.begin_popup(*this, popup, UiWindowFlag::NONE))
        {
            const ColorEditFlag kept = flags & (ColorEditFlag::NO_ALPHA | ColorEditFlag::ALPHA_BAR | ColorEditFlag::ALPHA_PREVIEW_HALF | ColorEditFlag::HDR
                                                | ColorEditFlag::DISPLAY_RGB | ColorEditFlag::DISPLAY_HSV | ColorEditFlag::DISPLAY_HEXADECIMAL
                                                | ColorEditFlag::INTEGER_CHANNELS | ColorEditFlag::FLOAT_CHANNELS | ColorEditFlag::PICKER_HUE_BAR
                                                | ColorEditFlag::PICKER_HUE_WHEEL);
            const float picker_width = std::max(width, frame * 10);
            set_next_item_width(picker_width);
            changed = color_picker("##picker", color, kept | ColorEditFlag::NO_LABEL) || changed;
            state.end_popup(*this);
        }
    }

    // The label after the row.
    const std::string_view text = ContextState::display_text(label);
    if (!has_any(flags, ColorEditFlag::NO_LABEL) && !text.empty())
    {
        same_line(0, style.item_inner_spacing.x);
        const TextLayout layout = state.layout_text(text, 0, "color_edit");
        const UiRectangle rectangle{.left = window->cursor.x, .top = window->cursor.y, .right = window->cursor.x + layout.width, .bottom = window->cursor.y + frame};
        state.item_size(*window, {.x = layout.width, .y = frame}, style.frame_padding.y);
        if (state.item_add(*window, rectangle, 0, false, "color_edit", {}))
        {
            state.draw_text(state.list_of(*window), layout, {.x = rectangle.left, .y = rectangle.top + style.frame_padding.y}, state.color(StyleColor::TEXT_NORMAL));
        }
    }
    pop_id();
    end_group();
    state.last_item.id = id;

    // A right click on the row opens the options (display, channels, copying); a color dropped on it is taken.
    const LastItem row = state.last_item;
    const std::uint32_t options = ContextState::hash(id, "#options");
    if (!has_any(flags, ColorEditFlag::NO_OPTIONS) && state.mouse[std::to_underlying(MouseButton::RIGHT)].released && is_item_hovered())
    {
        if (PopupState* opened = state.open_popup(options, state.mouse_position, 0, 0))
        {
            opened->opener_item = id;
        }
    }
    if (state.begin_popup(*this, options, UiWindowFlag::NONE))
    {
        constexpr ColorEditFlag DISPLAY = ColorEditFlag::DISPLAY_RGB | ColorEditFlag::DISPLAY_HSV | ColorEditFlag::DISPLAY_HEXADECIMAL;
        constexpr ColorEditFlag CHANNELS = ColorEditFlag::INTEGER_CHANNELS | ColorEditFlag::FLOAT_CHANNELS;
        for (const auto& [name, choice, group] : {std::tuple{std::string_view{"RGB"}, ColorEditFlag::DISPLAY_RGB, DISPLAY},
                                                  std::tuple{std::string_view{"HSV"}, ColorEditFlag::DISPLAY_HSV, DISPLAY},
                                                  std::tuple{std::string_view{"Hexadecimal"}, ColorEditFlag::DISPLAY_HEXADECIMAL, DISPLAY},
                                                  std::tuple{std::string_view{"0..255"}, ColorEditFlag::INTEGER_CHANNELS, CHANNELS},
                                                  std::tuple{std::string_view{"0.00..1.00"}, ColorEditFlag::FLOAT_CHANNELS, CHANNELS}})
        {
            if (choice == ColorEditFlag::INTEGER_CHANNELS)
            {
                separator();
            }
            if (selectable(name, has_any(state.color_options, choice)))
            {
                choose_option(state, group, choice);
            }
        }
        separator();
        if (selectable("Copy as hexadecimal"))
        {
            try
            {
                state.services->set_clipboard_text(to_hexadecimal(color, alpha).view());
            }
            catch (...)
            {
                state.report(UiFrameProblem::PLATFORM_SERVICE, "color_edit", "the clipboard could not be written", label);
            }
        }
        state.end_popup(*this);
    }
    state.last_item = row;
    if (!has_any(flags, ColorEditFlag::NO_DRAG_DROP) && begin_drag_drop_target())
    {
        if (const std::optional<UiDragDropPayload> payload = accept_drag_drop_payload(COLOR_PAYLOAD_TYPE); payload && payload->data.size() == sizeof(Color))
        {
            Color dropped{};
            std::memcpy(&dropped, payload->data.data(), sizeof(Color));
            color = alpha ? dropped : dropped.with_alpha(color.alpha);
            changed = true;
        }
        end_drag_drop_target();
    }
    state.last_item = row;
    if (changed)
    {
        state.mark_edited(id);
    }
    return changed;
}

auto
UiFrame::color_picker(std::string_view label,
                      Color& color,
                      ColorEditFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("color_picker");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    if (has_any(flags, ColorEditFlag::INPUT_HSV))
    {
        return edit_hsv(state, color, [&](Color& rgb) { return color_picker(label, rgb, flags & ~ColorEditFlag::INPUT_HSV); });
    }
    flags = with_options(state, flags);

    // Sizes: bars a frame's height wide; the square (or wheel) takes the rest of the item width.
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const bool alpha = !has_any(flags, ColorEditFlag::NO_ALPHA);
    const bool alpha_bar = alpha && has_any(flags, ColorEditFlag::ALPHA_BAR);
    const bool wheel = has_any(flags, ColorEditFlag::PICKER_HUE_WHEEL);
    const bool side_preview = !has_any(flags, ColorEditFlag::NO_SIDE_PREVIEW);
    const float frame = state.current_font().line_height + style.frame_padding.y * 2;
    const float bar_width = frame;
    const float width = state.take_item_width(*window);
    const float preview_width = side_preview ? frame * 3 + style.item_inner_spacing.x : 0.0F;
    const float picker = std::max(bar_width, std::floor(width - preview_width - (wheel ? 0.0F : bar_width + style.item_inner_spacing.x)
                                                        - (alpha_bar ? bar_width + style.item_inner_spacing.x : 0.0F)));
    begin_group();
    push_id(label);
    const Vector2 origin = window->cursor;
    const float bars_left = origin.x + picker + style.item_inner_spacing.x;
    const UiRectangle area{.left = origin.x, .top = origin.y, .right = origin.x + picker, .bottom = origin.y + picker};
    const UiRectangle hue_bar{.left = bars_left, .top = origin.y, .right = bars_left + bar_width, .bottom = origin.y + picker};
    const float alpha_left = wheel ? bars_left : hue_bar.right + style.item_inner_spacing.x;
    const UiRectangle alpha_area{.left = alpha_left, .top = origin.y, .right = alpha_left + bar_width, .bottom = origin.y + picker};
    const float content_right = alpha_bar ? alpha_area.right : wheel ? area.right : hue_bar.right;
    state.item_size(*window, {.x = content_right - origin.x, .y = picker});
    DrawList& list = state.list_of(*window);

    // The color's HSV (hue and saturation kept where the color does not define them).
    HsvColor hsv = hsv_of(state, color);
    bool hsv_changed = false;
    bool alpha_changed = false;
    const Vector2 mouse = state.mouse_position;

    // Wheel geometry: a ring of hues around a triangle whose corners are the hue, black and white.
    const Vector2 center{.x = (area.left + area.right) * 0.5F, .y = (area.top + area.bottom) * 0.5F};
    const float outer = picker * 0.5F;
    const float inner = outer - std::max(4.0F, bar_width * 0.75F);
    const float triangle = inner - 4.0F;

    if (wheel)
    {
        // Pressed in the ring: the hue follows the mouse's angle; in the triangle: saturation and value.
        const std::uint32_t ring_id = ContextState::hash(id, "#ring");
        const std::uint32_t triangle_id = ContextState::hash(id, "#triangle");
        const float dx = mouse.x - center.x;
        const float dy = mouse.y - center.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const bool in_ring = distance >= inner - 1 && distance <= outer + 1;
        const float hue_angle = hsv.hue * 2 * PI;
        const Vector2 hue_point = rotated({.x = triangle, .y = 0}, hue_angle);
        const Vector2 black_point = rotated({.x = triangle * -0.5F, .y = triangle * -0.866025F}, hue_angle);
        const Vector2 white_point = rotated({.x = triangle * -0.5F, .y = triangle * 0.866025F}, hue_angle);
        const auto at = [center](Vector2 offset) { return Vector2{.x = center.x + offset.x, .y = center.y + offset.y}; };
        const std::array<float, 3> weights = barycentric(at(hue_point), at(black_point), at(white_point), mouse);
        const bool in_triangle = weights[0] >= 0 && weights[1] >= 0 && weights[2] >= 0;
        if (drag_region(state, *window, area, ring_id, in_ring))
        {
            float hue = std::atan2(dy, dx) / (2 * PI);
            hsv.hue = hue < 0 ? hue + 1 : hue;
            hsv_changed = true;
        }
        if (drag_region(state, *window, area, triangle_id, in_triangle && !in_ring))
        {
            const Vector2 point = closest_in_triangle(at(hue_point), at(black_point), at(white_point), mouse);
            const std::array<float, 3> clamped = barycentric(at(hue_point), at(black_point), at(white_point), point);
            hsv.value = std::clamp(1.0F - clamped[1], 0.0001F, 1.0F);
            hsv.saturation = std::clamp(clamped[0] / hsv.value, 0.0001F, 1.0F);
            hsv_changed = true;
        }

        // The ring (segments of blended hues), its marker, the triangle and its marker.
        constexpr std::uint32_t SEGMENTS = 96;
        for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
        {
            const float from = static_cast<float>(segment) / SEGMENTS;
            const float to = static_cast<float>(segment + 1) / SEGMENTS;
            const Color from_color = state.faded(from_hsv({.hue = from, .saturation = 1, .value = 1}));
            const Color to_color = state.faded(from_hsv({.hue = to, .saturation = 1, .value = 1}));
            const Vector2 outer_from = at(rotated({.x = outer, .y = 0}, from * 2 * PI));
            const Vector2 outer_to = at(rotated({.x = outer, .y = 0}, to * 2 * PI));
            const Vector2 inner_from = at(rotated({.x = inner, .y = 0}, from * 2 * PI));
            const Vector2 inner_to = at(rotated({.x = inner, .y = 0}, to * 2 * PI));
            list.add_triangle_filled({outer_from, outer_to, inner_to}, {from_color, to_color, to_color});
            list.add_triangle_filled({outer_from, inner_to, inner_from}, {from_color, to_color, from_color});
        }
        const float marker_angle = hsv.hue * 2 * PI;
        draw_marker(list, at(rotated({.x = (inner + outer) * 0.5F, .y = 0}, marker_angle)), (outer - inner) * 0.3F);
        const Vector2 hue_now = at(rotated({.x = triangle, .y = 0}, marker_angle));
        const Vector2 black_now = at(rotated({.x = triangle * -0.5F, .y = triangle * -0.866025F}, marker_angle));
        const Vector2 white_now = at(rotated({.x = triangle * -0.5F, .y = triangle * 0.866025F}, marker_angle));
        list.add_triangle_filled({hue_now, black_now, white_now},
                                 {state.faded(from_hsv({.hue = hsv.hue, .saturation = 1, .value = 1})), state.faded(BLACK), state.faded(WHITE)});
        const auto mix_points = [](Vector2 first, Vector2 second, float t) { return Vector2{.x = first.x + (second.x - first.x) * t, .y = first.y + (second.y - first.y) * t}; };
        draw_marker(list, mix_points(mix_points(white_now, hue_now, hsv.saturation), black_now, 1.0F - hsv.value), 4);
    }
    else
    {
        // Pressed in the square: saturation across, value down; in the bar: the hue down.
        if (drag_region(state, *window, area, ContextState::hash(id, "#square")))
        {
            hsv.saturation = std::clamp((mouse.x - area.left) / std::max(area.width() - 1, 1.0F), 0.0F, 1.0F);
            hsv.value = 1.0F - std::clamp((mouse.y - area.top) / std::max(area.height() - 1, 1.0F), 0.0F, 1.0F);
            hsv_changed = true;
        }
        if (drag_region(state, *window, hue_bar, ContextState::hash(id, "#hue")))
        {
            hsv.hue = std::clamp((mouse.y - hue_bar.top) / std::max(hue_bar.height() - 1, 1.0F), 0.0F, 0.9999F);
            hsv_changed = true;
        }

        // The square: white to the hue across, transparent to black down; its marker.
        const Color hue_color = state.faded(from_hsv({.hue = hsv.hue, .saturation = 1, .value = 1}));
        list.add_rectangle_filled_gradient(area, state.faded(WHITE), hue_color, hue_color, state.faded(WHITE));
        list.add_rectangle_filled_gradient(area, TRANSPARENT_BLACK, TRANSPARENT_BLACK, state.faded(BLACK), state.faded(BLACK));
        draw_marker(list, {.x = area.left + hsv.saturation * area.width(), .y = area.top + (1.0F - hsv.value) * area.height()}, 5);

        // The hue bar: six blended sections; its marker.
        for (std::uint32_t section = 0; section < 6; ++section)
        {
            const float top = hue_bar.top + hue_bar.height() * static_cast<float>(section) / 6.0F;
            const float bottom = hue_bar.top + hue_bar.height() * static_cast<float>(section + 1) / 6.0F;
            const Color from = state.faded(from_hsv({.hue = static_cast<float>(section) / 6.0F, .saturation = 1, .value = 1}));
            const Color to = state.faded(from_hsv({.hue = static_cast<float>(section + 1) / 6.0F, .saturation = 1, .value = 1}));
            list.add_rectangle_filled_gradient({.left = hue_bar.left, .top = top, .right = hue_bar.right, .bottom = bottom}, from, from, to, to);
        }
        draw_bar_marker(list, hue_bar, hue_bar.top + hsv.hue * hue_bar.height());
    }

    // The alpha bar: the color from opaque to transparent over a checkerboard.
    if (alpha_bar)
    {
        if (drag_region(state, *window, alpha_area, ContextState::hash(id, "#alpha")))
        {
            color.alpha = 1.0F - std::clamp((mouse.y - alpha_area.top) / std::max(alpha_area.height() - 1, 1.0F), 0.0F, 1.0F);
            alpha_changed = true;
        }
        draw_checkerboard(list, alpha_area, std::max(2.0F, std::floor(bar_width * 0.5F)));
        const Color opaque = state.faded(color.with_alpha(1));
        list.add_rectangle_filled_gradient(alpha_area, opaque, opaque, opaque.with_alpha(0), opaque.with_alpha(0));
        draw_bar_marker(list, alpha_area, alpha_area.top + (1.0F - color.alpha) * alpha_area.height());
    }

    // A right click on the square (or the wheel) chooses the picker, unless NO_OPTIONS.
    const std::uint32_t picker_options = ContextState::hash(id, "#picker_options");
    const UiRectangle picker_area = wheel ? area : UiRectangle{.left = area.left, .top = area.top, .right = hue_bar.right, .bottom = area.bottom};
    const bool over_picker = mouse.x >= picker_area.left && mouse.x < picker_area.right && mouse.y >= picker_area.top && mouse.y < picker_area.bottom;
    if (!has_any(flags, ColorEditFlag::NO_OPTIONS) && state.mouse[std::to_underlying(MouseButton::RIGHT)].released && over_picker
        && state.hovered_window == static_cast<std::uint32_t>(window - state.windows.data()))
    {
        if (PopupState* opened = state.open_popup(picker_options, mouse, 0, 0))
        {
            opened->opener_item = id;
        }
    }
    if (state.begin_popup(*this, picker_options, UiWindowFlag::NONE))
    {
        constexpr ColorEditFlag PICKER = ColorEditFlag::PICKER_HUE_BAR | ColorEditFlag::PICKER_HUE_WHEEL;
        if (selectable("Hue bar", !wheel))
        {
            choose_option(state, PICKER, ColorEditFlag::PICKER_HUE_BAR);
        }
        if (selectable("Hue wheel", wheel))
        {
            choose_option(state, PICKER, ColorEditFlag::PICKER_HUE_WHEEL);
        }
        state.end_popup(*this);
    }

    // The color from its new HSV (the hue and saturation remembered for grays and black).
    bool changed = alpha_changed;
    if (hsv_changed)
    {
        const Color updated = from_hsv(hsv).with_alpha(color.alpha);
        changed = changed || updated != color;
        color = updated;
        save_hue(state, hsv, color);
    }

    // The side preview with the label under it (or the label alone), and the inputs below.
    const std::string_view text = ContextState::display_text(label);
    const bool labeled = !has_any(flags, ColorEditFlag::NO_LABEL) && !text.empty();
    if (side_preview || labeled)
    {
        same_line(0, style.item_inner_spacing.x);
        begin_group();
        if (side_preview)
        {
            color_button("##current", color, flags & (ColorEditFlag::NO_ALPHA | ColorEditFlag::ALPHA_PREVIEW_HALF), {.x = frame * 3, .y = frame * 2});
        }
        if (labeled)
        {
            text_unformatted(text);
        }
        end_group();
    }
    if (!has_any(flags, ColorEditFlag::NO_INPUTS))
    {
        set_next_item_width(content_right - origin.x + preview_width);
        const ColorEditFlag display = flags & (ColorEditFlag::NO_ALPHA | ColorEditFlag::HDR | ColorEditFlag::DISPLAY_RGB | ColorEditFlag::DISPLAY_HSV
                                               | ColorEditFlag::DISPLAY_HEXADECIMAL | ColorEditFlag::INTEGER_CHANNELS | ColorEditFlag::FLOAT_CHANNELS);
        changed = color_edit("##inputs", color, display | ColorEditFlag::NO_PICKER | ColorEditFlag::NO_SMALL_PREVIEW | ColorEditFlag::NO_LABEL) || changed;
    }
    pop_id();
    end_group();
    state.last_item.id = id;
    if (changed)
    {
        state.mark_edited(id);
    }
    return changed;
}

} // namespace gfx::ui
