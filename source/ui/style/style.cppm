/// @file style.cppm
/// @brief gfx.ui.style: Style (the sizes, paddings, roundings and alignments of the UI) and Theme (a style and
///        a color per StyleColor), with the built-in themes made at compile time by builtin_theme(). The dark,
///        light and classic colors are Dear ImGui's; the high-contrast theme is gfx's.
export module gfx.ui.style;

import std;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.draw;

export namespace gfx::ui
{

/// @brief Sizes, paddings, roundings and alignments, in UI units. The members a StyleVariable names can be
///        changed for a part of a frame with UiFrame::push_style_variable().
struct Style
{
    float alpha = 1.0F;                                        ///< Opacity of everything.
    float disabled_alpha = 0.6F;                               ///< Opacity of disabled items, times alpha.
    Vector2 window_padding{.x = 8.0F, .y = 8.0F};              ///< Between a window's edge and its content.
    float window_rounding = 0.0F;                              ///< Radius of window corners.
    float window_border_size = 1.0F;                           ///< Width of window borders.
    Vector2 window_minimum_size{.x = 32.0F, .y = 32.0F};       ///< Smallest window size.
    Vector2 window_title_align{.x = 0.0F, .y = 0.5F};          ///< Alignment of a title in its bar (0 to 1).
    float child_rounding = 0.0F;                               ///< Radius of child window corners.
    float child_border_size = 1.0F;                            ///< Width of child window borders.
    float popup_rounding = 0.0F;                               ///< Radius of popup corners.
    float popup_border_size = 1.0F;                            ///< Width of popup borders.
    Vector2 frame_padding{.x = 4.0F, .y = 3.0F};               ///< Between a frame's edge and its content.
    float frame_rounding = 0.0F;                               ///< Radius of frame corners.
    float frame_border_size = 0.0F;                            ///< Width of frame borders.
    Vector2 item_spacing{.x = 8.0F, .y = 4.0F};                ///< Between items.
    Vector2 item_inner_spacing{.x = 4.0F, .y = 4.0F};          ///< Between the parts of an item.
    float indent_spacing = 21.0F;                              ///< Indentation of indent() and tree nodes.
    Vector2 cell_padding{.x = 4.0F, .y = 2.0F};                ///< Inside table cells.
    float scrollbar_size = 14.0F;                              ///< Width of vertical scrollbars.
    float scrollbar_rounding = 9.0F;                           ///< Radius of scrollbar grabs.
    float grab_minimum_size = 12.0F;                           ///< Smallest size of slider and scrollbar grabs.
    float grab_rounding = 0.0F;                                ///< Radius of grabs.
    float tab_rounding = 5.0F;                                 ///< Radius of tab corners.
    float tab_border_size = 0.0F;                              ///< Width of tab borders.
    Vector2 button_text_align{.x = 0.5F, .y = 0.5F};           ///< Alignment of a button's label.
    Vector2 selectable_text_align{.x = 0.0F, .y = 0.0F};       ///< Alignment of a selectable's label.
    float separator_text_border_size = 3.0F;                   ///< Thickness of the lines of separator_text().
    Vector2 separator_text_align{.x = 0.0F, .y = 0.5F};        ///< Alignment of the text of separator_text().
    Vector2 separator_text_padding{.x = 20.0F, .y = 3.0F};     ///< Around the text of separator_text().
    float docking_separator_size = 2.0F;                       ///< Width of the separators between docked windows.
    float columns_minimum_spacing = 6.0F;                      ///< Smallest width of a columns() column.

    /// @brief This style with every size multiplied by @p factor (alignments and opacities unchanged): a UI for
    ///        another density, or one magnified for accessibility.
    [[nodiscard]] constexpr auto scaled(float factor) const noexcept -> Style
    {
        Style result = *this;
        const auto scale = [factor](Vector2 value) constexpr { return Vector2{.x = value.x * factor, .y = value.y * factor}; };
        result.window_padding = scale(window_padding);
        result.window_rounding *= factor;
        result.window_border_size *= factor;
        result.window_minimum_size = scale(window_minimum_size);
        result.child_rounding *= factor;
        result.child_border_size *= factor;
        result.popup_rounding *= factor;
        result.popup_border_size *= factor;
        result.frame_padding = scale(frame_padding);
        result.frame_rounding *= factor;
        result.frame_border_size *= factor;
        result.item_spacing = scale(item_spacing);
        result.item_inner_spacing = scale(item_inner_spacing);
        result.indent_spacing *= factor;
        result.cell_padding = scale(cell_padding);
        result.scrollbar_size *= factor;
        result.scrollbar_rounding *= factor;
        result.grab_minimum_size *= factor;
        result.grab_rounding *= factor;
        result.tab_rounding *= factor;
        result.tab_border_size *= factor;
        result.separator_text_border_size *= factor;
        result.separator_text_padding = scale(separator_text_padding);
        result.docking_separator_size *= factor;
        result.columns_minimum_spacing *= factor;
        return result;
    }

    /// @brief Styles compare member by member.
    [[nodiscard]] constexpr auto operator==(const Style&) const noexcept -> bool = default;
};

/// @brief Whether @p variable names a Vector2 member of Style (otherwise a float).
[[nodiscard]] constexpr auto
is_vector_variable(StyleVariable variable) noexcept -> bool
{
    switch (variable)
    {
        case StyleVariable::WINDOW_PADDING:
        case StyleVariable::WINDOW_MINIMUM_SIZE:
        case StyleVariable::WINDOW_TITLE_ALIGN:
        case StyleVariable::FRAME_PADDING:
        case StyleVariable::ITEM_SPACING:
        case StyleVariable::ITEM_INNER_SPACING:
        case StyleVariable::CELL_PADDING:
        case StyleVariable::BUTTON_TEXT_ALIGN:
        case StyleVariable::SELECTABLE_TEXT_ALIGN:
        case StyleVariable::SEPARATOR_TEXT_ALIGN:
        case StyleVariable::SEPARATOR_TEXT_PADDING:
            return true;
        default:
            return false;
    }
}

/// @brief The float member of @p style that @p variable names (nullptr for a Vector2 variable).
[[nodiscard]] constexpr auto
style_float(Style& style,
            StyleVariable variable) noexcept -> float*
{
    switch (variable)
    {
        case StyleVariable::ALPHA:                      return &style.alpha;
        case StyleVariable::DISABLED_ALPHA:             return &style.disabled_alpha;
        case StyleVariable::WINDOW_ROUNDING:            return &style.window_rounding;
        case StyleVariable::WINDOW_BORDER_SIZE:         return &style.window_border_size;
        case StyleVariable::CHILD_ROUNDING:             return &style.child_rounding;
        case StyleVariable::CHILD_BORDER_SIZE:          return &style.child_border_size;
        case StyleVariable::POPUP_ROUNDING:             return &style.popup_rounding;
        case StyleVariable::POPUP_BORDER_SIZE:          return &style.popup_border_size;
        case StyleVariable::FRAME_ROUNDING:             return &style.frame_rounding;
        case StyleVariable::FRAME_BORDER_SIZE:          return &style.frame_border_size;
        case StyleVariable::INDENT_SPACING:             return &style.indent_spacing;
        case StyleVariable::SCROLLBAR_SIZE:             return &style.scrollbar_size;
        case StyleVariable::SCROLLBAR_ROUNDING:         return &style.scrollbar_rounding;
        case StyleVariable::GRAB_MINIMUM_SIZE:          return &style.grab_minimum_size;
        case StyleVariable::GRAB_ROUNDING:              return &style.grab_rounding;
        case StyleVariable::TAB_ROUNDING:               return &style.tab_rounding;
        case StyleVariable::TAB_BORDER_SIZE:            return &style.tab_border_size;
        case StyleVariable::SEPARATOR_TEXT_BORDER_SIZE: return &style.separator_text_border_size;
        case StyleVariable::DOCKING_SEPARATOR_SIZE:     return &style.docking_separator_size;
        default:                                        return nullptr;
    }
}

/// @brief The Vector2 member of @p style that @p variable names (nullptr for a float variable).
[[nodiscard]] constexpr auto
style_vector(Style& style,
             StyleVariable variable) noexcept -> Vector2*
{
    switch (variable)
    {
        case StyleVariable::WINDOW_PADDING:         return &style.window_padding;
        case StyleVariable::WINDOW_MINIMUM_SIZE:    return &style.window_minimum_size;
        case StyleVariable::WINDOW_TITLE_ALIGN:     return &style.window_title_align;
        case StyleVariable::FRAME_PADDING:          return &style.frame_padding;
        case StyleVariable::ITEM_SPACING:           return &style.item_spacing;
        case StyleVariable::ITEM_INNER_SPACING:     return &style.item_inner_spacing;
        case StyleVariable::CELL_PADDING:           return &style.cell_padding;
        case StyleVariable::BUTTON_TEXT_ALIGN:      return &style.button_text_align;
        case StyleVariable::SELECTABLE_TEXT_ALIGN:  return &style.selectable_text_align;
        case StyleVariable::SEPARATOR_TEXT_ALIGN:   return &style.separator_text_align;
        case StyleVariable::SEPARATOR_TEXT_PADDING: return &style.separator_text_padding;
        default:                                    return nullptr;
    }
}

/// @brief A style and a color per StyleColor: the whole look of a UI. A plain value: copy it, change it,
///        give it to UiContext::set_theme().
struct Theme
{
    std::string_view name{};                                     ///< Shown to users (a literal: it is not copied).
    Style style{};                                               ///< Sizes, paddings, roundings.
    std::array<Color, ENUMERATOR_COUNT<StyleColor>> colors{};    ///< One per StyleColor, in its order.

    /// @brief The color of @p element.
    [[nodiscard]] constexpr auto color(StyleColor element) const noexcept -> Color
    {
        return colors[std::to_underlying(element)];
    }

    /// @brief Sets the color of @p element.
    constexpr void set_color(StyleColor element,
                             Color value) noexcept
    {
        colors[std::to_underlying(element)] = value;
    }

    /// @brief Themes compare member by member.
    [[nodiscard]] constexpr auto operator==(const Theme&) const noexcept -> bool = default;
};

/// @brief The color between @p from (0) and @p to (1), channel by channel.
[[nodiscard]] constexpr auto
mix(Color from,
    Color to,
    float fraction) noexcept -> Color
{
    return {.red = from.red + (to.red - from.red) * fraction,
            .green = from.green + (to.green - from.green) * fraction,
            .blue = from.blue + (to.blue - from.blue) * fraction,
            .alpha = from.alpha + (to.alpha - from.alpha) * fraction};
}


} // namespace gfx::ui

// The built-in themes' colors (not exported).
namespace gfx::ui::theme_colors
{

/// The tabs, docking and link colors every theme derives from its header and title colors.
constexpr void
derive(Theme& theme,
       float tab_mix,
       float dimmed_mix) noexcept
{
    const Color header = theme.color(StyleColor::HEADER);
    const Color header_active = theme.color(StyleColor::HEADER_ACTIVE);
    const Color title = theme.color(StyleColor::TITLE_BACKGROUND);
    const Color title_active = theme.color(StyleColor::TITLE_BACKGROUND_ACTIVE);
    const Color tab = mix(header, title_active, tab_mix);
    const Color tab_selected = mix(header_active, title_active, 0.60F);
    theme.set_color(StyleColor::TAB, tab);
    theme.set_color(StyleColor::TAB_HOVERED, theme.color(StyleColor::HEADER_HOVERED));
    theme.set_color(StyleColor::TAB_SELECTED, tab_selected);
    theme.set_color(StyleColor::TAB_SELECTED_OVERLINE, header_active);
    theme.set_color(StyleColor::TAB_DIMMED, mix(tab, title, dimmed_mix));
    theme.set_color(StyleColor::TAB_DIMMED_SELECTED, mix(tab_selected, title, 0.40F));
    theme.set_color(StyleColor::DOCKING_PREVIEW, header_active.with_alpha(header_active.alpha * 0.7F));
    theme.set_color(StyleColor::TEXT_LINK, header_active);
    theme.set_color(StyleColor::INPUT_TEXT_CURSOR, theme.color(StyleColor::TEXT_NORMAL));
}

/// Dear ImGui's dark colors.
constexpr auto
dark() noexcept -> Theme
{
    Theme theme{.name = "Dark"};
    const auto set = [&theme](StyleColor element, float red, float green, float blue, float alpha) constexpr
    {
        theme.set_color(element, {.red = red, .green = green, .blue = blue, .alpha = alpha});
    };
    set(StyleColor::TEXT_NORMAL, 1.00F, 1.00F, 1.00F, 1.00F);
    set(StyleColor::TEXT_DISABLED, 0.50F, 0.50F, 0.50F, 1.00F);
    set(StyleColor::WINDOW_BACKGROUND, 0.06F, 0.06F, 0.06F, 0.94F);
    set(StyleColor::CHILD_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::POPUP_BACKGROUND, 0.08F, 0.08F, 0.08F, 0.94F);
    set(StyleColor::BORDER, 0.43F, 0.43F, 0.50F, 0.50F);
    set(StyleColor::BORDER_SHADOW, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::FRAME_BACKGROUND, 0.16F, 0.29F, 0.48F, 0.54F);
    set(StyleColor::FRAME_BACKGROUND_HOVERED, 0.26F, 0.59F, 0.98F, 0.40F);
    set(StyleColor::FRAME_BACKGROUND_ACTIVE, 0.26F, 0.59F, 0.98F, 0.67F);
    set(StyleColor::TITLE_BACKGROUND, 0.04F, 0.04F, 0.04F, 1.00F);
    set(StyleColor::TITLE_BACKGROUND_ACTIVE, 0.16F, 0.29F, 0.48F, 1.00F);
    set(StyleColor::TITLE_BACKGROUND_COLLAPSED, 0.00F, 0.00F, 0.00F, 0.51F);
    set(StyleColor::MENU_BAR_BACKGROUND, 0.14F, 0.14F, 0.14F, 1.00F);
    set(StyleColor::SCROLLBAR_BACKGROUND, 0.02F, 0.02F, 0.02F, 0.53F);
    set(StyleColor::SCROLLBAR_GRAB, 0.31F, 0.31F, 0.31F, 1.00F);
    set(StyleColor::SCROLLBAR_GRAB_HOVERED, 0.41F, 0.41F, 0.41F, 1.00F);
    set(StyleColor::SCROLLBAR_GRAB_ACTIVE, 0.51F, 0.51F, 0.51F, 1.00F);
    set(StyleColor::CHECK_MARK, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::SLIDER_GRAB, 0.24F, 0.52F, 0.88F, 1.00F);
    set(StyleColor::SLIDER_GRAB_ACTIVE, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::BUTTON, 0.26F, 0.59F, 0.98F, 0.40F);
    set(StyleColor::BUTTON_HOVERED, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::BUTTON_ACTIVE, 0.06F, 0.53F, 0.98F, 1.00F);
    set(StyleColor::HEADER, 0.26F, 0.59F, 0.98F, 0.31F);
    set(StyleColor::HEADER_HOVERED, 0.26F, 0.59F, 0.98F, 0.80F);
    set(StyleColor::HEADER_ACTIVE, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::SEPARATOR, 0.43F, 0.43F, 0.50F, 0.50F);
    set(StyleColor::SEPARATOR_HOVERED, 0.10F, 0.40F, 0.75F, 0.78F);
    set(StyleColor::SEPARATOR_ACTIVE, 0.10F, 0.40F, 0.75F, 1.00F);
    set(StyleColor::RESIZE_GRIP, 0.26F, 0.59F, 0.98F, 0.20F);
    set(StyleColor::RESIZE_GRIP_HOVERED, 0.26F, 0.59F, 0.98F, 0.67F);
    set(StyleColor::RESIZE_GRIP_ACTIVE, 0.26F, 0.59F, 0.98F, 0.95F);
    set(StyleColor::DOCKING_EMPTY_BACKGROUND, 0.20F, 0.20F, 0.20F, 1.00F);
    set(StyleColor::PLOT_LINES, 0.61F, 0.61F, 0.61F, 1.00F);
    set(StyleColor::PLOT_LINES_HOVERED, 1.00F, 0.43F, 0.35F, 1.00F);
    set(StyleColor::PLOT_HISTOGRAM, 0.90F, 0.70F, 0.00F, 1.00F);
    set(StyleColor::PLOT_HISTOGRAM_HOVERED, 1.00F, 0.60F, 0.00F, 1.00F);
    set(StyleColor::TABLE_HEADER_BACKGROUND, 0.19F, 0.19F, 0.20F, 1.00F);
    set(StyleColor::TABLE_BORDER_STRONG, 0.31F, 0.31F, 0.35F, 1.00F);
    set(StyleColor::TABLE_BORDER_LIGHT, 0.23F, 0.23F, 0.25F, 1.00F);
    set(StyleColor::TABLE_ROW_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE, 1.00F, 1.00F, 1.00F, 0.06F);
    set(StyleColor::TEXT_SELECTED_BACKGROUND, 0.26F, 0.59F, 0.98F, 0.35F);
    set(StyleColor::DRAG_DROP_TARGET, 1.00F, 1.00F, 0.00F, 0.90F);
    set(StyleColor::NAVIGATION_HIGHLIGHT, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::NAVIGATION_WINDOWING_HIGHLIGHT, 1.00F, 1.00F, 1.00F, 0.70F);
    set(StyleColor::NAVIGATION_WINDOWING_DIM_BACKGROUND, 0.80F, 0.80F, 0.80F, 0.20F);
    set(StyleColor::MODAL_WINDOW_DIM_BACKGROUND, 0.80F, 0.80F, 0.80F, 0.35F);
    derive(theme, 0.80F, 0.80F);
    return theme;
}

/// Dear ImGui's light colors.
constexpr auto
light() noexcept -> Theme
{
    Theme theme{.name = "Light"};
    const auto set = [&theme](StyleColor element, float red, float green, float blue, float alpha) constexpr
    {
        theme.set_color(element, {.red = red, .green = green, .blue = blue, .alpha = alpha});
    };
    set(StyleColor::TEXT_NORMAL, 0.00F, 0.00F, 0.00F, 1.00F);
    set(StyleColor::TEXT_DISABLED, 0.60F, 0.60F, 0.60F, 1.00F);
    set(StyleColor::WINDOW_BACKGROUND, 0.94F, 0.94F, 0.94F, 1.00F);
    set(StyleColor::CHILD_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::POPUP_BACKGROUND, 1.00F, 1.00F, 1.00F, 0.98F);
    set(StyleColor::BORDER, 0.00F, 0.00F, 0.00F, 0.30F);
    set(StyleColor::BORDER_SHADOW, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::FRAME_BACKGROUND, 1.00F, 1.00F, 1.00F, 1.00F);
    set(StyleColor::FRAME_BACKGROUND_HOVERED, 0.26F, 0.59F, 0.98F, 0.40F);
    set(StyleColor::FRAME_BACKGROUND_ACTIVE, 0.26F, 0.59F, 0.98F, 0.67F);
    set(StyleColor::TITLE_BACKGROUND, 0.96F, 0.96F, 0.96F, 1.00F);
    set(StyleColor::TITLE_BACKGROUND_ACTIVE, 0.82F, 0.82F, 0.82F, 1.00F);
    set(StyleColor::TITLE_BACKGROUND_COLLAPSED, 1.00F, 1.00F, 1.00F, 0.51F);
    set(StyleColor::MENU_BAR_BACKGROUND, 0.86F, 0.86F, 0.86F, 1.00F);
    set(StyleColor::SCROLLBAR_BACKGROUND, 0.98F, 0.98F, 0.98F, 0.53F);
    set(StyleColor::SCROLLBAR_GRAB, 0.69F, 0.69F, 0.69F, 0.80F);
    set(StyleColor::SCROLLBAR_GRAB_HOVERED, 0.49F, 0.49F, 0.49F, 0.80F);
    set(StyleColor::SCROLLBAR_GRAB_ACTIVE, 0.49F, 0.49F, 0.49F, 1.00F);
    set(StyleColor::CHECK_MARK, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::SLIDER_GRAB, 0.26F, 0.59F, 0.98F, 0.78F);
    set(StyleColor::SLIDER_GRAB_ACTIVE, 0.46F, 0.54F, 0.80F, 0.60F);
    set(StyleColor::BUTTON, 0.26F, 0.59F, 0.98F, 0.40F);
    set(StyleColor::BUTTON_HOVERED, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::BUTTON_ACTIVE, 0.06F, 0.53F, 0.98F, 1.00F);
    set(StyleColor::HEADER, 0.26F, 0.59F, 0.98F, 0.31F);
    set(StyleColor::HEADER_HOVERED, 0.26F, 0.59F, 0.98F, 0.80F);
    set(StyleColor::HEADER_ACTIVE, 0.26F, 0.59F, 0.98F, 1.00F);
    set(StyleColor::SEPARATOR, 0.39F, 0.39F, 0.39F, 0.62F);
    set(StyleColor::SEPARATOR_HOVERED, 0.14F, 0.44F, 0.80F, 0.78F);
    set(StyleColor::SEPARATOR_ACTIVE, 0.14F, 0.44F, 0.80F, 1.00F);
    set(StyleColor::RESIZE_GRIP, 0.35F, 0.35F, 0.35F, 0.17F);
    set(StyleColor::RESIZE_GRIP_HOVERED, 0.26F, 0.59F, 0.98F, 0.67F);
    set(StyleColor::RESIZE_GRIP_ACTIVE, 0.26F, 0.59F, 0.98F, 0.95F);
    set(StyleColor::DOCKING_EMPTY_BACKGROUND, 0.20F, 0.20F, 0.20F, 1.00F);
    set(StyleColor::PLOT_LINES, 0.39F, 0.39F, 0.39F, 1.00F);
    set(StyleColor::PLOT_LINES_HOVERED, 1.00F, 0.43F, 0.35F, 1.00F);
    set(StyleColor::PLOT_HISTOGRAM, 0.90F, 0.70F, 0.00F, 1.00F);
    set(StyleColor::PLOT_HISTOGRAM_HOVERED, 1.00F, 0.45F, 0.00F, 1.00F);
    set(StyleColor::TABLE_HEADER_BACKGROUND, 0.78F, 0.87F, 0.98F, 1.00F);
    set(StyleColor::TABLE_BORDER_STRONG, 0.57F, 0.57F, 0.64F, 1.00F);
    set(StyleColor::TABLE_BORDER_LIGHT, 0.68F, 0.68F, 0.74F, 1.00F);
    set(StyleColor::TABLE_ROW_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE, 0.30F, 0.30F, 0.30F, 0.09F);
    set(StyleColor::TEXT_SELECTED_BACKGROUND, 0.26F, 0.59F, 0.98F, 0.35F);
    set(StyleColor::DRAG_DROP_TARGET, 0.26F, 0.59F, 0.98F, 0.95F);
    set(StyleColor::NAVIGATION_HIGHLIGHT, 0.26F, 0.59F, 0.98F, 0.80F);
    set(StyleColor::NAVIGATION_WINDOWING_HIGHLIGHT, 0.70F, 0.70F, 0.70F, 0.70F);
    set(StyleColor::NAVIGATION_WINDOWING_DIM_BACKGROUND, 0.20F, 0.20F, 0.20F, 0.20F);
    set(StyleColor::MODAL_WINDOW_DIM_BACKGROUND, 0.20F, 0.20F, 0.20F, 0.35F);
    derive(theme, 0.90F, 0.80F);
    return theme;
}

/// Dear ImGui's classic colors.
constexpr auto
classic() noexcept -> Theme
{
    Theme theme{.name = "Classic"};
    const auto set = [&theme](StyleColor element, float red, float green, float blue, float alpha) constexpr
    {
        theme.set_color(element, {.red = red, .green = green, .blue = blue, .alpha = alpha});
    };
    set(StyleColor::TEXT_NORMAL, 0.90F, 0.90F, 0.90F, 1.00F);
    set(StyleColor::TEXT_DISABLED, 0.60F, 0.60F, 0.60F, 1.00F);
    set(StyleColor::WINDOW_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.85F);
    set(StyleColor::CHILD_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::POPUP_BACKGROUND, 0.11F, 0.11F, 0.14F, 0.92F);
    set(StyleColor::BORDER, 0.50F, 0.50F, 0.50F, 0.50F);
    set(StyleColor::BORDER_SHADOW, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::FRAME_BACKGROUND, 0.43F, 0.43F, 0.43F, 0.39F);
    set(StyleColor::FRAME_BACKGROUND_HOVERED, 0.47F, 0.47F, 0.69F, 0.40F);
    set(StyleColor::FRAME_BACKGROUND_ACTIVE, 0.42F, 0.41F, 0.64F, 0.69F);
    set(StyleColor::TITLE_BACKGROUND, 0.27F, 0.27F, 0.54F, 0.83F);
    set(StyleColor::TITLE_BACKGROUND_ACTIVE, 0.32F, 0.32F, 0.63F, 0.87F);
    set(StyleColor::TITLE_BACKGROUND_COLLAPSED, 0.40F, 0.40F, 0.80F, 0.20F);
    set(StyleColor::MENU_BAR_BACKGROUND, 0.40F, 0.40F, 0.55F, 0.80F);
    set(StyleColor::SCROLLBAR_BACKGROUND, 0.20F, 0.25F, 0.30F, 0.60F);
    set(StyleColor::SCROLLBAR_GRAB, 0.40F, 0.40F, 0.80F, 0.30F);
    set(StyleColor::SCROLLBAR_GRAB_HOVERED, 0.40F, 0.40F, 0.80F, 0.40F);
    set(StyleColor::SCROLLBAR_GRAB_ACTIVE, 0.41F, 0.39F, 0.80F, 0.60F);
    set(StyleColor::CHECK_MARK, 0.90F, 0.90F, 0.90F, 0.50F);
    set(StyleColor::SLIDER_GRAB, 1.00F, 1.00F, 1.00F, 0.30F);
    set(StyleColor::SLIDER_GRAB_ACTIVE, 0.41F, 0.39F, 0.80F, 0.60F);
    set(StyleColor::BUTTON, 0.35F, 0.40F, 0.61F, 0.62F);
    set(StyleColor::BUTTON_HOVERED, 0.40F, 0.48F, 0.71F, 0.79F);
    set(StyleColor::BUTTON_ACTIVE, 0.46F, 0.54F, 0.80F, 1.00F);
    set(StyleColor::HEADER, 0.40F, 0.40F, 0.90F, 0.45F);
    set(StyleColor::HEADER_HOVERED, 0.45F, 0.45F, 0.90F, 0.80F);
    set(StyleColor::HEADER_ACTIVE, 0.53F, 0.53F, 0.87F, 0.80F);
    set(StyleColor::SEPARATOR, 0.50F, 0.50F, 0.50F, 0.60F);
    set(StyleColor::SEPARATOR_HOVERED, 0.60F, 0.60F, 0.70F, 1.00F);
    set(StyleColor::SEPARATOR_ACTIVE, 0.70F, 0.70F, 0.90F, 1.00F);
    set(StyleColor::RESIZE_GRIP, 1.00F, 1.00F, 1.00F, 0.10F);
    set(StyleColor::RESIZE_GRIP_HOVERED, 0.78F, 0.82F, 1.00F, 0.60F);
    set(StyleColor::RESIZE_GRIP_ACTIVE, 0.78F, 0.82F, 1.00F, 0.90F);
    set(StyleColor::DOCKING_EMPTY_BACKGROUND, 0.20F, 0.20F, 0.20F, 1.00F);
    set(StyleColor::PLOT_LINES, 1.00F, 1.00F, 1.00F, 1.00F);
    set(StyleColor::PLOT_LINES_HOVERED, 0.90F, 0.70F, 0.00F, 1.00F);
    set(StyleColor::PLOT_HISTOGRAM, 0.90F, 0.70F, 0.00F, 1.00F);
    set(StyleColor::PLOT_HISTOGRAM_HOVERED, 1.00F, 0.60F, 0.00F, 1.00F);
    set(StyleColor::TABLE_HEADER_BACKGROUND, 0.27F, 0.27F, 0.38F, 1.00F);
    set(StyleColor::TABLE_BORDER_STRONG, 0.31F, 0.31F, 0.45F, 1.00F);
    set(StyleColor::TABLE_BORDER_LIGHT, 0.26F, 0.26F, 0.28F, 1.00F);
    set(StyleColor::TABLE_ROW_BACKGROUND, 0.00F, 0.00F, 0.00F, 0.00F);
    set(StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE, 1.00F, 1.00F, 1.00F, 0.07F);
    set(StyleColor::TEXT_SELECTED_BACKGROUND, 0.00F, 0.00F, 1.00F, 0.35F);
    set(StyleColor::DRAG_DROP_TARGET, 1.00F, 1.00F, 0.00F, 0.90F);
    set(StyleColor::NAVIGATION_HIGHLIGHT, 0.45F, 0.45F, 0.90F, 0.80F);
    set(StyleColor::NAVIGATION_WINDOWING_HIGHLIGHT, 1.00F, 1.00F, 1.00F, 0.70F);
    set(StyleColor::NAVIGATION_WINDOWING_DIM_BACKGROUND, 0.80F, 0.80F, 0.80F, 0.20F);
    set(StyleColor::MODAL_WINDOW_DIM_BACKGROUND, 0.20F, 0.20F, 0.20F, 0.35F);
    derive(theme, 0.80F, 0.80F);
    return theme;
}

/// White on black, yellow where the focus and the pointer are, every edge bordered: contrast ratios above 7:1
/// (WCAG AAA) for text and every interactive state.
constexpr auto
high_contrast() noexcept -> Theme
{
    Theme theme{.name = "High contrast"};
    theme.style.window_border_size = 2.0F;
    theme.style.frame_border_size = 1.0F;
    theme.style.popup_border_size = 2.0F;
    theme.style.child_border_size = 1.0F;
    theme.style.tab_border_size = 1.0F;
    theme.style.disabled_alpha = 0.75F;
    const Color black{.red = 0, .green = 0, .blue = 0, .alpha = 1};
    const Color white{.red = 1, .green = 1, .blue = 1, .alpha = 1};
    const Color yellow{.red = 1, .green = 1, .blue = 0, .alpha = 1};
    const Color cyan{.red = 0, .green = 1, .blue = 1, .alpha = 1};
    const Color blue{.red = 0.0F, .green = 0.24F, .blue = 0.60F, .alpha = 1};
    const Color gray{.red = 0.75F, .green = 0.75F, .blue = 0.75F, .alpha = 1};
    for (Color& color : theme.colors)
    {
        color = black;
    }
    theme.set_color(StyleColor::TEXT_NORMAL, white);
    theme.set_color(StyleColor::TEXT_DISABLED, gray);
    theme.set_color(StyleColor::CHILD_BACKGROUND, black.with_alpha(0));
    theme.set_color(StyleColor::BORDER, white);
    theme.set_color(StyleColor::BORDER_SHADOW, black.with_alpha(0));
    theme.set_color(StyleColor::FRAME_BACKGROUND_HOVERED, blue);
    theme.set_color(StyleColor::FRAME_BACKGROUND_ACTIVE, blue);
    theme.set_color(StyleColor::TITLE_BACKGROUND_ACTIVE, blue);
    theme.set_color(StyleColor::SCROLLBAR_GRAB, gray);
    theme.set_color(StyleColor::SCROLLBAR_GRAB_HOVERED, white);
    theme.set_color(StyleColor::SCROLLBAR_GRAB_ACTIVE, yellow);
    theme.set_color(StyleColor::CHECK_MARK, yellow);
    theme.set_color(StyleColor::SLIDER_GRAB, white);
    theme.set_color(StyleColor::SLIDER_GRAB_ACTIVE, yellow);
    theme.set_color(StyleColor::BUTTON_HOVERED, blue);
    theme.set_color(StyleColor::BUTTON_ACTIVE, blue);
    theme.set_color(StyleColor::HEADER, blue);
    theme.set_color(StyleColor::HEADER_HOVERED, blue);
    theme.set_color(StyleColor::HEADER_ACTIVE, blue);
    theme.set_color(StyleColor::SEPARATOR, white);
    theme.set_color(StyleColor::SEPARATOR_HOVERED, yellow);
    theme.set_color(StyleColor::SEPARATOR_ACTIVE, yellow);
    theme.set_color(StyleColor::RESIZE_GRIP, gray);
    theme.set_color(StyleColor::RESIZE_GRIP_HOVERED, white);
    theme.set_color(StyleColor::RESIZE_GRIP_ACTIVE, yellow);
    theme.set_color(StyleColor::DOCKING_EMPTY_BACKGROUND, black);
    theme.set_color(StyleColor::PLOT_LINES, white);
    theme.set_color(StyleColor::PLOT_LINES_HOVERED, yellow);
    theme.set_color(StyleColor::PLOT_HISTOGRAM, cyan);
    theme.set_color(StyleColor::PLOT_HISTOGRAM_HOVERED, yellow);
    theme.set_color(StyleColor::TABLE_BORDER_STRONG, white);
    theme.set_color(StyleColor::TABLE_BORDER_LIGHT, gray);
    theme.set_color(StyleColor::TABLE_ROW_BACKGROUND, black.with_alpha(0));
    theme.set_color(StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE, white.with_alpha(0.12F));
    theme.set_color(StyleColor::TEXT_SELECTED_BACKGROUND, blue);
    theme.set_color(StyleColor::DRAG_DROP_TARGET, yellow);
    theme.set_color(StyleColor::NAVIGATION_HIGHLIGHT, yellow);
    theme.set_color(StyleColor::NAVIGATION_WINDOWING_HIGHLIGHT, yellow);
    theme.set_color(StyleColor::NAVIGATION_WINDOWING_DIM_BACKGROUND, black.with_alpha(0.6F));
    theme.set_color(StyleColor::MODAL_WINDOW_DIM_BACKGROUND, black.with_alpha(0.6F));
    derive(theme, 0.0F, 0.0F);
    theme.set_color(StyleColor::TAB, black);
    theme.set_color(StyleColor::TAB_SELECTED, blue);
    theme.set_color(StyleColor::TAB_SELECTED_OVERLINE, yellow);
    theme.set_color(StyleColor::TEXT_LINK, cyan);
    return theme;
}

} // namespace gfx::ui::theme_colors

export namespace gfx::ui
{

/// @brief The built-in theme @p kind, made at compile time when @p kind is a constant.
[[nodiscard]] constexpr auto
builtin_theme(ThemeKind kind) noexcept -> Theme
{
    switch (kind)
    {
        case ThemeKind::DARK:          return theme_colors::dark();
        case ThemeKind::LIGHT:         return theme_colors::light();
        case ThemeKind::CLASSIC:       return theme_colors::classic();
        case ThemeKind::HIGH_CONTRAST: return theme_colors::high_contrast();
    }
    return theme_colors::dark();
}

} // namespace gfx::ui
