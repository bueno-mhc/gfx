/// @file numbers.cpp
/// @brief Numeric widgets on NumberField (every arithmetic type): formatting with std::format formats, parsing
///        typed values, input_number() with step buttons, sliders (linear or logarithmic) and drags, their typed
///        input (Control+click, double click, Return), and multi-component rows.
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

/// Arithmetic on field values: exact for every 64-bit integer where long double has a 64-bit mantissa.
using Number = long double;

/// A formatted value (cut at its capacity).
struct FormattedNumber
{
    std::array<char, 64> characters{};
    std::size_t size = 0;
    bool failed = false;

    [[nodiscard]] auto view() const noexcept -> std::string_view
    {
        return {characters.data(), size};
    }
};

/// An output iterator writing into a FormattedNumber, dropping what does not fit.
struct BoundedOutput
{
    using difference_type = std::ptrdiff_t;

    FormattedNumber* target = nullptr;

    auto operator*() noexcept -> BoundedOutput&
    {
        return *this;
    }

    auto operator=(char character) noexcept -> BoundedOutput&
    {
        if (target->size < target->characters.size())
        {
            target->characters[target->size++] = character;
        }
        return *this;
    }

    auto operator++() noexcept -> BoundedOutput&
    {
        return *this;
    }

    auto operator++(int) noexcept -> BoundedOutput
    {
        return *this;
    }
};

/// @p value as a Number.
auto
to_number(const NumberField::Value& value) noexcept -> Number
{
    return std::visit([](auto number) { return static_cast<Number>(number); }, value);
}

/// Whether the field holds a floating-point number.
auto
is_floating(const NumberField& field) noexcept -> bool
{
    return std::holds_alternative<double>(field.value);
}

/// @p number as a value of the field's type: within the type's limits, integers rounded.
auto
make_value(const NumberField& field,
           Number number) noexcept -> NumberField::Value
{
    if (std::isnan(number))
    {
        return field.value;
    }
    if (number <= to_number(field.lowest))
    {
        return field.lowest;
    }
    if (number >= to_number(field.highest))
    {
        return field.highest;
    }
    return std::visit(
        [number](auto current) -> NumberField::Value
        {
            using Type = decltype(current);
            if constexpr (std::is_floating_point_v<Type>)
            {
                return static_cast<double>(number);
            }
            else
            {
                return static_cast<Type>(std::round(number));
            }
        },
        field.value);
}

/// @p field's value formatted with @p format (an invalid format falls back to the type's default format).
auto
format_value(const NumberField& field,
             std::string_view format) noexcept -> FormattedNumber
{
    FormattedNumber result{};
    const auto attempt = [&](std::string_view used) noexcept
    {
        result.size = 0;
        try
        {
            std::visit(
                [&](auto number)
                {
                    const auto value = number;
                    std::vformat_to(BoundedOutput{&result}, used, std::make_format_args(value));
                },
                field.value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    if (!attempt(format.empty() ? field.default_format : format))
    {
        result.failed = true;
        if (!attempt(field.default_format))
        {
            result.size = 1;
            result.characters[0] = '?';
        }
    }
    return result;
}

/// The replacement field of @p format ("{:.2f}" of "{:.2f} m/s"), or the type's default format when it has none.
auto
replacement_field(const NumberField& field,
                  std::string_view format) noexcept -> std::string_view
{
    for (std::size_t index = 0; index < format.size(); ++index)
    {
        if (format[index] != '{')
        {
            continue;
        }
        if (index + 1 < format.size() && format[index + 1] == '{')
        {
            ++index;
            continue;
        }
        const std::size_t close = format.find('}', index);
        return close == std::string_view::npos ? field.default_format : format.substr(index, close - index + 1);
    }
    return field.default_format;
}

/// The decimals @p format shows (".N" in its replacement field), 0 for integers, -1 when it does not say.
auto
format_precision(const NumberField& field,
                 std::string_view format) noexcept -> std::int32_t
{
    if (!is_floating(field))
    {
        return 0;
    }
    const std::string_view replacement = replacement_field(field, format.empty() ? field.default_format : format);
    const std::size_t colon = replacement.find(':');
    const std::size_t dot = colon == std::string_view::npos ? std::string_view::npos : replacement.find('.', colon);
    if (dot == std::string_view::npos)
    {
        return -1;
    }
    std::int32_t precision = 0;
    const auto [end, error] = std::from_chars(replacement.data() + dot + 1, replacement.data() + replacement.size(), precision);
    return error == std::errc{} && end != replacement.data() + dot + 1 ? std::min(precision, 30) : -1;
}

/// @p number rounded to @p precision decimals (unchanged when the precision is unknown).
auto
round_to_precision(Number number,
                   std::int32_t precision) noexcept -> Number
{
    if (precision < 0 || !std::isfinite(number))
    {
        return number;
    }
    const Number scale = std::pow(Number{10}, static_cast<Number>(precision));
    const Number rounded = std::round(number * scale) / scale;
    return std::isfinite(rounded) ? rounded : number;
}

/// The number at the start of @p text (spaces around it allowed, other text after it ignored), or nothing.
auto
parse_number(std::string_view text,
             const NumberField& field) noexcept -> std::optional<Number>
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
    }
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();

    // Integers exactly (64-bit values do not survive a double); anything else as a floating-point number.
    if (!is_floating(field))
    {
        if (std::holds_alternative<std::uint64_t>(field.value))
        {
            std::uint64_t value = 0;
            const auto [stop, error] = std::from_chars(begin, end, value);
            if (error == std::errc{} && (stop == end || *stop == ' ' || *stop == '\t'))
            {
                return static_cast<Number>(value);
            }
        }
        else
        {
            std::int64_t value = 0;
            const auto [stop, error] = std::from_chars(begin, end, value);
            if (error == std::errc{} && (stop == end || *stop == ' ' || *stop == '\t'))
            {
                return static_cast<Number>(value);
            }
        }
    }
    double value = 0;
    const auto [stop, error] = std::from_chars(begin, end, value);
    if (error == std::errc::result_out_of_range)
    {
        return text.starts_with('-') ? -std::numeric_limits<Number>::infinity() : std::numeric_limits<Number>::infinity();
    }
    if (error != std::errc{} || stop == begin)
    {
        return std::nullopt;
    }
    return static_cast<Number>(value);
}

/// The logarithmic scale of sliders whose range touches or spans zero: sign(x) log(1 + |x| / epsilon).
auto
logarithmic(Number value,
            Number epsilon) noexcept -> Number
{
    const Number magnitude = std::log1p(std::abs(value) / epsilon);
    return value < 0 ? -magnitude : magnitude;
}

/// The inverse of logarithmic().
auto
exponential(Number scaled,
            Number epsilon) noexcept -> Number
{
    const Number magnitude = epsilon * std::expm1(std::abs(scaled));
    return scaled < 0 ? -magnitude : magnitude;
}

/// Whether a logarithmic scale from @p minimum to @p maximum is the ratio of their logarithms: both ends of one
/// sign and at least @p epsilon from zero (otherwise logarithmic() spans zero).
auto
ratio_of_logarithms(Number minimum,
                    Number maximum,
                    Number epsilon) noexcept -> bool
{
    return ((minimum > 0 && maximum > 0) || (minimum < 0 && maximum < 0)) && std::min(std::abs(minimum), std::abs(maximum)) >= epsilon;
}

/// Where @p value is between @p minimum and @p maximum, 0 to 1 (either may be the larger).
auto
slider_ratio(Number value,
             Number minimum,
             Number maximum,
             bool logarithmic_scale,
             Number epsilon) noexcept -> float
{
    if (minimum == maximum)
    {
        return 0.0F;
    }
    const Number low = std::min(minimum, maximum);
    const Number high = std::max(minimum, maximum);
    const Number clamped = std::clamp(value, low, high);
    Number ratio = (clamped - minimum) / (maximum - minimum);
    if (logarithmic_scale && ratio_of_logarithms(minimum, maximum, epsilon))
    {
        ratio = std::log(clamped / minimum) / std::log(maximum / minimum);
    }
    else if (logarithmic_scale)
    {
        ratio = (logarithmic(clamped, epsilon) - logarithmic(minimum, epsilon)) / (logarithmic(maximum, epsilon) - logarithmic(minimum, epsilon));
    }
    return std::clamp(static_cast<float>(ratio), 0.0F, 1.0F);
}

/// The value at @p ratio between @p minimum and @p maximum.
auto
slider_value(float ratio,
             Number minimum,
             Number maximum,
             bool logarithmic_scale,
             Number epsilon) noexcept -> Number
{
    if (logarithmic_scale && ratio_of_logarithms(minimum, maximum, epsilon))
    {
        return minimum * std::pow(maximum / minimum, static_cast<Number>(ratio));
    }
    if (logarithmic_scale)
    {
        const Number from = logarithmic(minimum, epsilon);
        return exponential(from + static_cast<Number>(ratio) * (logarithmic(maximum, epsilon) - from), epsilon);
    }
    return minimum + static_cast<Number>(ratio) * (maximum - minimum);
}

/// A numeric item: its identifier, frame and label.
struct NumberItem
{
    std::uint32_t id = 0;
    UiRectangle frame{};
    TextLayout label{};
    bool visible = false;
};

/// Lays out a framed numeric item of the item width (minus @p reserved on its right) and its label.
auto
number_item(ContextState& state,
            WindowState& window,
            std::string_view label,
            float reserved,
            std::string_view operation) noexcept -> NumberItem
{
    const Style& style = state.style;
    NumberItem item{.id = state.widget_id(label)};
    const float width = std::max(1.0F, state.take_item_width(window) - reserved);
    const float height = state.current_font().line_height + style.frame_padding.y * 2;
    item.label = state.layout_text(ContextState::display_text(label), 0, operation);
    const Vector2 position = window.cursor;
    item.frame = {.left = position.x, .top = position.y, .right = position.x + width, .bottom = position.y + height};
    const float total = width + reserved + (item.label.width > 0 ? style.item_inner_spacing.x + item.label.width : 0.0F);
    state.item_size(window, {.x = total, .y = height}, style.frame_padding.y);
    item.visible = state.item_add(window, item.frame, item.id, true, operation, label);
    return item;
}

/// The label of @p item, @p offset after its frame.
void
draw_number_label(ContextState& state,
                  WindowState& window,
                  const NumberItem& item,
                  float offset = 0) noexcept
{
    const Style& style = state.style;
    state.draw_text(state.list_of(window), item.label, {.x = item.frame.right + offset + style.item_inner_spacing.x, .y = item.frame.top + style.frame_padding.y},
                    state.color(StyleColor::TEXT_NORMAL));
}

/// The frame's text area (inside the frame padding).
auto
text_area(const ContextState& state,
          const UiRectangle& frame) noexcept -> UiRectangle
{
    return {.left = frame.left + state.style.frame_padding.x, .top = frame.top, .right = frame.right - state.style.frame_padding.x, .bottom = frame.bottom};
}

/// The formatted value centered in @p frame, clipped to it.
void
draw_centered_value(ContextState& state,
                    DrawList& list,
                    const UiRectangle& frame,
                    std::string_view text,
                    std::string_view operation) noexcept
{
    const TextLayout layout = state.layout_text(text, 0, operation);
    const Vector2 position{.x = frame.left + std::max(0.0F, frame.width() - layout.width) * 0.5F, .y = frame.top + state.style.frame_padding.y};
    list.push_clip_rectangle(frame);
    state.draw_text(list, layout, position, state.color(StyleColor::TEXT_NORMAL));
    list.pop_clip_rectangle();
}

/// Whether the keyboard drives item @p id (it has the visible focus in the focused window).
auto
keyboard_focused(const ContextState& state,
                 const WindowState& window,
                 std::uint32_t id) noexcept -> bool
{
    return state.focus_id == id && state.focus_visible && !state.disabled && state.focused_window != ContextState::NO_WINDOW && window.root == state.focused_window;
}

/// The steps asked of item @p id (the last item) this frame: MOVE_RIGHT minus MOVE_LEFT while the keyboard focuses it
/// (the moves are used), and an assistive technology's INCREMENT or DECREMENT.
auto
value_steps(ContextState& state,
            const WindowState& window,
            std::uint32_t id) noexcept -> std::int32_t
{
    std::int32_t steps = 0;
    if (keyboard_focused(state, window, id))
    {
        const auto count = [&state](UiNavigationCommand command)
        {
            return state.navigation_triggered(command) ? static_cast<std::int32_t>(state.navigation_count(command)) : 0;
        };
        const std::int32_t right = count(UiNavigationCommand::MOVE_RIGHT);
        const std::int32_t left = count(UiNavigationCommand::MOVE_LEFT);
        if (right + left > 0)
        {
            state.consume_navigation(UiNavigationCommand::MOVE_LEFT | UiNavigationCommand::MOVE_RIGHT);
        }
        steps = right - left;
    }
    steps += state.item_request(AccessibilityAction::INCREMENT) != nullptr ? 1 : 0;
    steps -= state.item_request(AccessibilityAction::DECREMENT) != nullptr ? 1 : 0;
    return steps;
}

/// The number an assistive technology set for the last item this frame, if any.
auto
requested_number(const ContextState& state) noexcept -> std::optional<Number>
{
    const UiAccessibilityRequest* set = state.item_request(AccessibilityAction::SET_VALUE);
    return set != nullptr && set->has_number ? std::optional<Number>{static_cast<Number>(set->number)} : std::nullopt;
}

/// Describes the numeric last item for assistive technologies: @p role, its value as @p text and as a number, its range
/// (when @p ranged) and its step. Its record.
auto
describe_number_item(ContextState& state,
                     AccessibilityRole role,
                     const NumberField& field,
                     std::string_view text,
                     bool ranged,
                     double step) noexcept -> std::uint32_t
{
    const std::uint32_t record = state.describe_item(role, AccessibilityState::NONE,
                                                     AccessibilityAction::INCREMENT | AccessibilityAction::DECREMENT | AccessibilityAction::SET_VALUE, text);
    state.describe_number(record, static_cast<double>(to_number(field.value)), ranged ? static_cast<double>(to_number(field.minimum)) : 0.0,
                          ranged ? static_cast<double>(to_number(field.maximum)) : 0.0, step);
    return record;
}

/// Typed input of a slider or drag: starts on Control+click, a double click or Return (unless SliderFlag::NO_INPUT);
/// while it is on, the item is a text field. Returns whether it is on, and in @p changed whether the value changed.
auto
typed_input(ContextState& state,
            WindowState& window,
            std::string_view label,
            const NumberItem& item,
            NumberField& field,
            std::string_view format,
            SliderFlag flags,
            bool clamp,
            bool& changed) noexcept -> bool
{
    changed = false;
    if (state.temp_input_id != item.id || (state.text_edit.id != item.id && state.text_edit.ended_id != item.id))
    {
        // Not (or no longer) typing: maybe it starts now.
        if (state.temp_input_id == item.id)
        {
            state.temp_input_id = 0;
        }
        if (has_any(flags, SliderFlag::NO_INPUT) || state.disabled)
        {
            return false;
        }
        const bool hovered = state.item_hoverable(window, item.frame, item.id);
        const bool control = has_any(state.modifiers, KeyModifier::CONTROL | KeyModifier::SUPER);
        const bool clicked = hovered && state.mouse[0].clicked && (control || state.mouse[0].double_clicked);
        const bool returned = keyboard_focused(state, window, item.id) && state.navigation_pressed(UiNavigationCommand::INPUT);
        if (!clicked && !returned)
        {
            return false;
        }
        const FormattedNumber text = format_value(field, replacement_field(field, format));
        state.start_text_edit(item.id, text.view(), is_floating(field) ? InputTextFlag::CHARACTERS_SCIENTIFIC : InputTextFlag::CHARACTERS_DECIMAL,
                              false, true, 0);
        if (state.text_edit.id != item.id)
        {
            return false;
        }
        state.text_edit.editor.select_all();
        state.set_active(item.id, window);
        if (clicked)
        {
            state.take_focus(item.id);
        }
        state.temp_input_id = item.id;
    }

    // The text field in the item's frame; its text is applied when editing ends by Return, Tab or a click.
    const FormattedNumber shown = format_value(field, format);
    const TextFieldResult result = text_field(state, window, {.id = item.id,
                                                              .frame = item.frame,
                                                              .origin = {.x = item.frame.left + state.style.frame_padding.x, .y = item.frame.top + state.style.frame_padding.y},
                                                              .clip = text_area(state, item.frame),
                                                              .text = shown.view(),
                                                              .flags = state.text_edit.flags,
                                                              .numeric = true,
                                                              .label = label});
    if (result.ended)
    {
        state.temp_input_id = 0;
        if (result.committed)
        {
            if (std::optional<Number> parsed = parse_number(result.text, field))
            {
                if (clamp)
                {
                    const Number minimum = to_number(field.minimum);
                    const Number maximum = to_number(field.maximum);
                    *parsed = std::clamp(*parsed, std::min(minimum, maximum), std::max(minimum, maximum));
                }
                const NumberField::Value value = make_value(field, *parsed);
                changed = value != field.value;
                field.value = value;
            }
        }
    }
    draw_number_label(state, window, item);
    return true;
}

/// A step button of input_number(): repeats while held.
auto
step_button(ContextState& state,
            WindowState& window,
            const UiRectangle& rectangle,
            std::uint32_t id,
            std::string_view text) noexcept -> bool
{
    static_cast<void>(state.item_add(window, rectangle, id, false, "input_number", text));
    const ButtonState button = state.button_behavior(window, rectangle, id, ButtonFlag::REPEAT);
    DrawList& list = state.list_of(window);
    const StyleColor fill = button.held && button.hovered ? StyleColor::BUTTON_ACTIVE : button.hovered ? StyleColor::BUTTON_HOVERED : StyleColor::BUTTON;
    state.draw_frame(list, rectangle, state.color(fill), state.style.frame_rounding);
    draw_centered_value(state, list, rectangle, text, "input_number");
    return button.pressed;
}

} // namespace

auto
UiFrame::number_input(std::string_view label,
                      NumberField& field,
                      NumberField::Value step,
                      NumberField::Value step_fast,
                      std::string_view format,
                      InputTextFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("input_number");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // The field, then - and + (when there is a step), then the label.
    const Style& style = state.style;
    const float button_size = state.current_font().line_height + style.frame_padding.y * 2;
    const bool buttons = to_number(step) != 0;
    const float reserved = buttons ? (button_size + style.item_inner_spacing.x) * 2 : 0.0F;
    const NumberItem item = number_item(state, *window, label, reserved, "input_number");
    if (!item.visible)
    {
        return false;
    }
    if (format_value(field, format).failed)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "input_number", "the format is not a std::format format for this type: the default one is used", label, item.id);
    }

    // Its node now (the step buttons become the last item), with what assistive technologies ask of it.
    const std::uint32_t record = describe_number_item(state, AccessibilityRole::SPIN_BUTTON, field, format_value(field, format).view(), false,
                                                      static_cast<double>(to_number(step)));
    const std::int32_t requested_steps = (state.item_request(AccessibilityAction::INCREMENT) != nullptr ? 1 : 0)
                                         - (state.item_request(AccessibilityAction::DECREMENT) != nullptr ? 1 : 0);
    const std::optional<Number> requested = requested_number(state);

    // Step buttons first (the field stays the last item): Control steps by step_fast.
    bool changed = false;
    if (requested.has_value())
    {
        const NumberField::Value value = make_value(field, *requested);
        changed = value != field.value;
        field.value = value;
    }
    if (requested_steps != 0)
    {
        const NumberField::Value value = make_value(field, to_number(field.value) + static_cast<Number>(requested_steps) * to_number(step));
        changed = changed || value != field.value;
        field.value = value;
    }
    if (buttons)
    {
        const bool control = has_any(state.modifiers, KeyModifier::CONTROL | KeyModifier::SUPER);
        const Number amount = control && to_number(step_fast) != 0 ? to_number(step_fast) : to_number(step);
        const float left = item.frame.right + style.item_inner_spacing.x;
        const UiRectangle minus{.left = left, .top = item.frame.top, .right = left + button_size, .bottom = item.frame.bottom};
        const UiRectangle plus{.left = minus.right + style.item_inner_spacing.x, .top = item.frame.top, .right = minus.right + style.item_inner_spacing.x + button_size,
                               .bottom = item.frame.bottom};
        const auto apply = [&](Number sign)
        {
            const NumberField::Value value = make_value(field, to_number(field.value) + sign * amount);
            changed = changed || value != field.value;
            field.value = value;
        };
        if (step_button(state, *window, minus, ContextState::hash(item.id, "-"), "-"))
        {
            apply(-1);
        }
        if (step_button(state, *window, plus, ContextState::hash(item.id, "+"), "+"))
        {
            apply(1);
        }
        state.last_item.id = item.id;
        state.last_item.rectangle = item.frame;
    }

    // The text field: the value with its format when not edited; edited without the format's other text, applied
    // on every change (with ENTER_RETURNS_TRUE: when Return confirms it); Escape restores the value it had.
    const FormattedNumber shown = format_value(field, format);
    const FormattedNumber editable = format_value(field, replacement_field(field, format));
    const InputTextFlag filters = is_floating(field) ? InputTextFlag::CHARACTERS_SCIENTIFIC : InputTextFlag::CHARACTERS_DECIMAL;
    const TextFieldResult result = text_field(state, *window, {.id = item.id,
                                                               .frame = item.frame,
                                                               .origin = {.x = item.frame.left + style.frame_padding.x, .y = item.frame.top + style.frame_padding.y},
                                                               .clip = text_area(state, item.frame),
                                                               .text = shown.view(),
                                                               .edit_text = editable.view(),
                                                               .flags = flags | filters,
                                                               .numeric = true,
                                                               .label = label});
    const bool on_enter = has_any(flags, InputTextFlag::ENTER_RETURNS_TRUE);
    const bool apply = on_enter ? result.ended && result.committed : (result.active && result.changed) || result.ended;
    if (apply)
    {
        if (const std::optional<Number> parsed = parse_number(result.text, field))
        {
            const NumberField::Value value = make_value(field, *parsed);
            changed = changed || value != field.value;
            field.value = value;
        }
    }
    draw_number_label(state, *window, item, reserved);
    if (changed)
    {
        state.mark_edited(item.id);
        state.accessible_text(record, 2, format_value(field, format).view());
        state.describe_number(record, static_cast<double>(to_number(field.value)), 0.0, 0.0, static_cast<double>(to_number(step)));
    }
    return changed;
}

auto
UiFrame::number_slider(std::string_view label,
                       NumberField& field,
                       std::string_view format,
                       SliderFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("slider");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const NumberItem item = number_item(state, *window, label, 0, "slider");
    if (!item.visible)
    {
        return false;
    }
    const FormattedNumber shown = format_value(field, format);
    if (shown.failed)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "slider", "the format is not a std::format format for this type: the default one is used", label, item.id);
    }

    // Typed input replaces the slider while it lasts.
    bool changed = false;
    const std::optional<Number> requested = requested_number(state);
    if (typed_input(state, *window, label, item, field, format, flags, has_any(flags, SliderFlag::CLAMP_ON_INPUT), changed))
    {
        if (changed)
        {
            state.mark_edited(item.id);
        }
        static_cast<void>(describe_number_item(state, AccessibilityRole::SLIDER, field, format_value(field, format).view(), true, 0.0));
        return changed;
    }

    // The grab: as wide as one step of an integer range (at least grab_minimum_size), within 2 units of padding.
    const Style& style = state.style;
    const bool integer = !is_floating(field);
    const Number minimum = to_number(field.minimum);
    const Number maximum = to_number(field.maximum);
    const std::int32_t precision = format_precision(field, format);
    const bool logarithmic_scale = has_any(flags, SliderFlag::LOGARITHMIC);
    const Number epsilon = integer ? Number{1} : precision >= 0 ? std::pow(Number{10}, static_cast<Number>(-precision)) : Number{1.0e-3L};
    constexpr float padding = 2.0F;
    const float usable = std::max(1.0F, item.frame.width() - padding * 2);
    float grab = style.grab_minimum_size;
    if (integer && !logarithmic_scale && std::abs(maximum - minimum) < 1.0e6L)
    {
        grab = std::max(usable / static_cast<float>(std::abs(maximum - minimum) + 1), style.grab_minimum_size);
    }
    grab = std::min(grab, usable);
    const float track = usable - grab;
    const float track_left = item.frame.left + padding + grab * 0.5F;

    // The mouse: pressed on it, the value follows it until released.
    const bool hovered = state.item_hoverable(*window, item.frame, item.id);
    if (hovered)
    {
        state.hovered_id = item.id;
        state.last_item.hovered = true;
    }
    if (hovered && state.mouse[0].clicked && state.active_id != item.id)
    {
        state.set_active(item.id, *window);
        state.active_button = MouseButton::LEFT;
        state.take_focus(item.id);
    }
    const auto set = [&](Number number)
    {
        if (integer)
        {
            number = std::round(number);
        }
        else if (!has_any(flags, SliderFlag::NO_ROUND_TO_FORMAT))
        {
            number = round_to_precision(number, precision);
        }
        const NumberField::Value value = make_value(field, std::clamp(number, std::min(minimum, maximum), std::max(minimum, maximum)));
        changed = changed || value != field.value;
        field.value = value;
    };
    bool held = false;
    if (state.active_id == item.id && !state.active_by_keyboard)
    {
        state.active_id_alive = true;
        if (state.mouse[0].down)
        {
            held = true;
            const float ratio = track > 0 ? std::clamp((state.mouse_position.x - track_left) / track, 0.0F, 1.0F) : 0.0F;
            set(slider_value(ratio, minimum, maximum, logarithmic_scale, epsilon));
        }
        else
        {
            state.clear_active();
        }
    }

    // The keyboard: MOVE_LEFT and MOVE_RIGHT step (integers by 1, others by a hundredth of the range; Shift: ten times
    // more), as assistive technologies' INCREMENT and DECREMENT do; they can also set the value.
    const std::int32_t steps = value_steps(state, *window, item.id);
    const bool fast = has_any(state.modifiers, KeyModifier::SHIFT);
    const Number unit = integer ? Number{1} : (maximum - minimum) / 100;
    if (steps != 0)
    {
        const Number step = unit * (fast ? DRAG_FAST_FACTOR : 1.0L) * static_cast<Number>(steps);
        if (logarithmic_scale && !integer)
        {
            const float ratio = slider_ratio(to_number(field.value), minimum, maximum, true, epsilon);
            set(slider_value(std::clamp(ratio + static_cast<float>(steps) * (fast ? 0.1F : 0.01F), 0.0F, 1.0F), minimum, maximum, true, epsilon));
        }
        else
        {
            set(to_number(field.value) + step);
        }
    }
    if (requested.has_value())
    {
        set(*requested);
    }

    // The frame, the grab, the value, the label.
    DrawList& list = state.list_of(*window);
    const StyleColor fill = held ? StyleColor::FRAME_BACKGROUND_ACTIVE : hovered ? StyleColor::FRAME_BACKGROUND_HOVERED : StyleColor::FRAME_BACKGROUND;
    state.draw_frame(list, item.frame, state.color(fill), style.frame_rounding);
    state.draw_focus(list, item.frame, item.id, style.frame_rounding);
    if (minimum != maximum)
    {
        const float ratio = slider_ratio(to_number(field.value), minimum, maximum, logarithmic_scale, epsilon);
        const float center = track_left + ratio * track;
        list.add_rectangle_filled({.left = center - grab * 0.5F, .top = item.frame.top + padding, .right = center + grab * 0.5F, .bottom = item.frame.bottom - padding},
                                  state.color(held ? StyleColor::SLIDER_GRAB_ACTIVE : StyleColor::SLIDER_GRAB), style.grab_rounding);
    }
    draw_centered_value(state, list, item.frame, changed ? format_value(field, format).view() : shown.view(), "slider");
    draw_number_label(state, *window, item);
    static_cast<void>(describe_number_item(state, AccessibilityRole::SLIDER, field, changed ? format_value(field, format).view() : shown.view(), true,
                                           static_cast<double>(unit)));
    if (changed)
    {
        state.mark_edited(item.id);
    }
    return changed;
}

auto
UiFrame::number_drag(std::string_view label,
                     NumberField& field,
                     float speed,
                     std::string_view format,
                     SliderFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("drag");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const NumberItem item = number_item(state, *window, label, 0, "drag");
    if (!item.visible)
    {
        return false;
    }
    const FormattedNumber shown = format_value(field, format);
    if (shown.failed)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "drag", "the format is not a std::format format for this type: the default one is used", label, item.id);
    }

    // Clamped when the minimum is below the maximum, or equal to it (both 0: only with CLAMP_ZERO_RANGE).
    const Number minimum = to_number(field.minimum);
    const Number maximum = to_number(field.maximum);
    const bool clamped = minimum < maximum || (minimum == maximum && (minimum != 0 || has_any(flags, SliderFlag::CLAMP_ZERO_RANGE)));
    bool changed = false;
    const std::optional<Number> requested = requested_number(state);
    if (typed_input(state, *window, label, item, field, format, flags, clamped && has_any(flags, SliderFlag::CLAMP_ON_INPUT), changed))
    {
        if (changed)
        {
            state.mark_edited(item.id);
        }
        static_cast<void>(describe_number_item(state, AccessibilityRole::SPIN_BUTTON, field, format_value(field, format).view(), clamped, 0.0));
        return changed;
    }

    // The mouse: pressed on it, horizontal motion past the drag threshold changes the value (Shift faster, Alt slower).
    const bool integer = !is_floating(field);
    const std::int32_t precision = format_precision(field, format);
    const bool hovered = state.item_hoverable(*window, item.frame, item.id);
    if (hovered)
    {
        state.hovered_id = item.id;
        state.last_item.hovered = true;
    }
    if (hovered && state.mouse[0].clicked && state.active_id != item.id)
    {
        state.set_active(item.id, *window);
        state.active_button = MouseButton::LEFT;
        state.take_focus(item.id);
        state.drag_remainder = 0;
        state.drag_started = false;
    }
    const double tweak = has_any(flags, SliderFlag::NO_SPEED_TWEAKS)          ? 1.0
                         : has_any(state.modifiers, KeyModifier::SHIFT)       ? DRAG_FAST_FACTOR
                         : has_any(state.modifiers, KeyModifier::ALT)         ? DRAG_SLOW_FACTOR
                                                                              : 1.0;
    double amount = 0;
    bool held = false;
    if (state.active_id == item.id && !state.active_by_keyboard)
    {
        state.active_id_alive = true;
        if (state.mouse[0].down)
        {
            held = true;
            float motion = 0;
            if (state.drag_started)
            {
                motion = state.mouse_delta.x;
            }
            else if (state.mouse[0].drag_maximum >= state.description.input.drag_threshold)
            {
                state.drag_started = true;
                motion = state.mouse_position.x - state.mouse[0].clicked_position.x;
            }
            amount = static_cast<double>(motion) * speed * tweak;
        }
        else
        {
            state.clear_active();
        }
    }

    // The keyboard: MOVE_LEFT and MOVE_RIGHT step by the speed (integers by at least 1), as assistive technologies'
    // INCREMENT and DECREMENT do.
    const std::int32_t steps = value_steps(state, *window, item.id);
    const double unit = integer ? std::max(1.0, std::round(static_cast<double>(speed))) : static_cast<double>(speed);
    if (steps != 0)
    {
        amount += unit * tweak * steps;
        state.drag_remainder = 0;
    }
    if (requested.has_value())
    {
        amount += static_cast<double>(*requested - to_number(field.value));
        state.drag_remainder = 0;
    }

    // The value moves by what accumulated, rounded to the format; the rounding's remainder waits for more motion.
    if (amount != 0)
    {
        const Number current = to_number(field.value);
        const Number target = current + static_cast<Number>(state.drag_remainder + amount);
        Number rounded = integer ? std::trunc(target) : has_any(flags, SliderFlag::NO_ROUND_TO_FORMAT) ? target : round_to_precision(target, precision);
        state.drag_remainder = static_cast<double>(target - rounded);
        if (clamped)
        {
            if (has_any(flags, SliderFlag::WRAP_AROUND) && minimum < maximum)
            {
                const Number span = integer ? maximum - minimum + 1 : maximum - minimum;
                rounded = minimum + std::fmod(std::fmod(rounded - minimum, span) + span, span);
            }
            else
            {
                rounded = std::clamp(rounded, minimum, maximum);
            }
        }
        const NumberField::Value value = make_value(field, rounded);
        changed = value != field.value;
        field.value = value;
    }

    // The frame, the value, the label.
    const Style& style = state.style;
    DrawList& list = state.list_of(*window);
    const StyleColor fill = held ? StyleColor::FRAME_BACKGROUND_ACTIVE : hovered ? StyleColor::FRAME_BACKGROUND_HOVERED : StyleColor::FRAME_BACKGROUND;
    state.draw_frame(list, item.frame, state.color(fill), style.frame_rounding);
    state.draw_focus(list, item.frame, item.id, style.frame_rounding);
    draw_centered_value(state, list, item.frame, changed ? format_value(field, format).view() : shown.view(), "drag");
    draw_number_label(state, *window, item);
    static_cast<void>(describe_number_item(state, AccessibilityRole::SPIN_BUTTON, field, changed ? format_value(field, format).view() : shown.view(), clamped, unit));
    if (changed)
    {
        state.mark_edited(item.id);
    }
    return changed;
}

auto
UiFrame::begin_components(std::string_view label,
                          std::size_t count) noexcept -> float
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("components");
    if (window == nullptr || window->skip_items)
    {
        return 0.0F;
    }

    // One group for the row; the components share the item width.
    const float total = state.take_item_width(*window);
    begin_group();
    push_id(label);
    const auto components = static_cast<float>(std::max<std::size_t>(count, 1));
    return std::max(1.0F, (total - state.style.item_inner_spacing.x * (components - 1)) / components);
}

void
UiFrame::next_component(std::size_t component,
                        float width) noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    if (component > 0)
    {
        pop_id();
        same_line(0, state.style.item_inner_spacing.x);
    }
    push_id(static_cast<std::int32_t>(component));
    set_next_item_width(width);
}

void
UiFrame::end_components(std::string_view label,
                        std::size_t count) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("components");
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    if (count > 0)
    {
        pop_id();
    }
    pop_id();

    // The label after the components, on their line.
    const std::string_view text = ContextState::display_text(label);
    if (!text.empty())
    {
        same_line(0, state.style.item_inner_spacing.x);
        const Style& style = state.style;
        const TextLayout layout = state.layout_text(text, 0, "components");
        const float height = state.current_font().line_height + style.frame_padding.y * 2;
        const UiRectangle rectangle{.left = window->cursor.x, .top = window->cursor.y, .right = window->cursor.x + layout.width, .bottom = window->cursor.y + height};
        const Vector2 position{.x = rectangle.left, .y = rectangle.top + style.frame_padding.y};
        state.item_size(*window, {.x = layout.width, .y = height}, style.frame_padding.y);
        if (state.item_add(*window, rectangle, 0, false, "components", {}))
        {
            state.draw_text(state.list_of(*window), layout, position, state.color(StyleColor::TEXT_NORMAL));
        }
    }
    end_group();
}

} // namespace gfx::ui
