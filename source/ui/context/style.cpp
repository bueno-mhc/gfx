/// @file style.cpp
/// @brief UiFrame's style stacks: fonts, colors, style variables and disabled blocks.
module gfx.ui.context;

import std;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import :state;

namespace gfx::ui
{

void
UiFrame::push_font(FontHandle font) noexcept
{
    ContextState& state = *m_state;
    if (state.fonts.font(font) == nullptr)
    {
        state.report(UiFrameProblem::NO_FONT, "push_font", "the handle names no font of the context's library: the current font stays");
        state.font_stack.push_back(state.current_font());
        return;
    }
    state.font_stack.push_back(state.metrics_of(font));
}

void
UiFrame::pop_font() noexcept
{
    if (m_state->font_stack.empty())
    {
        m_state->report(UiFrameProblem::UNBALANCED_STACK, "pop_font", "pop_font() without push_font()");
        return;
    }
    m_state->font_stack.pop_back();
}

auto
UiFrame::font() const noexcept -> FontHandle
{
    return m_state->current_font().font;
}

auto
UiFrame::font_size() const noexcept -> float
{
    return m_state->current_font().size;
}

void
UiFrame::push_style_color(StyleColor element,
                          Color color) noexcept
{
    ContextState& state = *m_state;
    Color& current = state.colors[std::to_underlying(element)];
    state.color_stack.push_back({.element = element, .previous = current});
    current = color;
}

void
UiFrame::pop_style_color(std::uint32_t count) noexcept
{
    ContextState& state = *m_state;
    for (std::uint32_t popped = 0; popped < count; ++popped)
    {
        if (state.color_stack.empty())
        {
            state.report(UiFrameProblem::UNBALANCED_STACK, "pop_style_color", "more colors popped than pushed");
            return;
        }
        const ColorBackup backup = state.color_stack.back();
        state.color_stack.pop_back();
        state.colors[std::to_underlying(backup.element)] = backup.previous;
    }
}

void
UiFrame::push_style_variable(StyleVariable variable,
                             float value) noexcept
{
    ContextState& state = *m_state;
    float* member = style_float(state.style, variable);
    if (member == nullptr)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "push_style_variable", "this variable is a Vector2: push a Vector2");
        return;
    }
    state.variable_stack.push_back({.variable = variable, .previous = {.x = *member, .y = 0}});
    *member = value;
}

void
UiFrame::push_style_variable(StyleVariable variable,
                             Vector2 value) noexcept
{
    ContextState& state = *m_state;
    Vector2* member = style_vector(state.style, variable);
    if (member == nullptr)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "push_style_variable", "this variable is a float: push a float");
        return;
    }
    state.variable_stack.push_back({.variable = variable, .previous = *member});
    *member = value;
}

void
UiFrame::pop_style_variable(std::uint32_t count) noexcept
{
    ContextState& state = *m_state;
    for (std::uint32_t popped = 0; popped < count; ++popped)
    {
        if (state.variable_stack.empty())
        {
            state.report(UiFrameProblem::UNBALANCED_STACK, "pop_style_variable", "more style variables popped than pushed");
            return;
        }
        const VariableBackup backup = state.variable_stack.back();
        state.variable_stack.pop_back();
        if (float* member = style_float(state.style, backup.variable); member != nullptr)
        {
            *member = backup.previous.x;
        }
        else if (Vector2* vector = style_vector(state.style, backup.variable); vector != nullptr)
        {
            *vector = backup.previous;
        }
    }
}

auto
UiFrame::style() const noexcept -> const Style&
{
    return m_state->style;
}

auto
UiFrame::style_color(StyleColor element) const noexcept -> Color
{
    return m_state->colors[std::to_underlying(element)];
}

void
UiFrame::begin_disabled(bool disabled) noexcept
{
    ContextState& state = *m_state;
    state.disabled_stack.push_back(state.disabled);
    state.disabled = state.disabled || disabled;
}

void
UiFrame::end_disabled() noexcept
{
    ContextState& state = *m_state;
    if (state.disabled_stack.empty())
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_disabled", "end_disabled() without begin_disabled()");
        return;
    }
    state.disabled = state.disabled_stack.back();
    state.disabled_stack.pop_back();
}

} // namespace gfx::ui
