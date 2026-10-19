/// @file handles.cppm
/// @brief The handles and identifiers of the UI library. Handles name objects the UI context issues and
///        keeps (fonts, atlases, themes, styles, dock nodes, draw lists, layers, input contexts);
///        identifiers name what the application declares by name every frame (windows, panels, widgets,
///        menus, popups, tooltips, tabs, splitters, tables, dock spaces), as Dear ImGui's IDs, hashed at
///        compile time for literals. Handle<Tag> and Identifier<Tag> come from gfx.core.handles, which
///        this module re-exports; each kind has its own tag, so kinds cannot be mixed.
export module gfx.ui.handles;

import std;
export import gfx.core.handles;
import gfx.ui.enums;

export namespace gfx::ui
{

// ================================================================================================= Tags of handles

/// @brief Tag of FontHandle.
struct FontTag
{
    static constexpr std::string_view NAME = "Font";
};

/// @brief Tag of FontAtlasHandle.
struct FontAtlasTag
{
    static constexpr std::string_view NAME = "FontAtlas";
};

/// @brief Tag of ThemeHandle.
struct ThemeTag
{
    static constexpr std::string_view NAME = "Theme";
};

/// @brief Tag of StyleHandle.
struct StyleTag
{
    static constexpr std::string_view NAME = "Style";
};

/// @brief Tag of DockNodeHandle.
struct DockNodeTag
{
    static constexpr std::string_view NAME = "DockNode";
};

/// @brief Tag of DrawListHandle.
struct DrawListTag
{
    static constexpr std::string_view NAME = "DrawList";
};

/// @brief Tag of LayerHandle.
struct LayerTag
{
    static constexpr std::string_view NAME = "Layer";
};

/// @brief Tag of InputContextHandle.
struct InputContextTag
{
    static constexpr std::string_view NAME = "InputContext";
};

// ================================================================================================= Tags of identifiers

/// @brief Tag of WidgetIdentifier.
struct WidgetTag
{
    static constexpr std::string_view NAME = "Widget";
};

/// @brief Tag of WindowIdentifier (a UI window, not a platform window).
struct WindowTag
{
    static constexpr std::string_view NAME = "Window";
};

/// @brief Tag of PanelIdentifier.
struct PanelTag
{
    static constexpr std::string_view NAME = "Panel";
};

/// @brief Tag of DockSpaceIdentifier.
struct DockSpaceTag
{
    static constexpr std::string_view NAME = "DockSpace";
};

/// @brief Tag of TabIdentifier.
struct TabTag
{
    static constexpr std::string_view NAME = "Tab";
};

/// @brief Tag of MenuIdentifier.
struct MenuTag
{
    static constexpr std::string_view NAME = "Menu";
};

/// @brief Tag of MenuItemIdentifier.
struct MenuItemTag
{
    static constexpr std::string_view NAME = "MenuItem";
};

/// @brief Tag of PopupIdentifier.
struct PopupTag
{
    static constexpr std::string_view NAME = "Popup";
};

/// @brief Tag of TooltipIdentifier.
struct TooltipTag
{
    static constexpr std::string_view NAME = "Tooltip";
};

/// @brief Tag of SplitterIdentifier.
struct SplitterTag
{
    static constexpr std::string_view NAME = "Splitter";
};

/// @brief Tag of TableIdentifier.
struct TableTag
{
    static constexpr std::string_view NAME = "Table";
};

// ================================================================================================= Handles and identifiers

using FontHandle = Handle<FontTag>;                 ///< Names a font: an atlas at a size, with features.
using FontAtlasHandle = Handle<FontAtlasTag>;       ///< Names a loaded font atlas.
using ThemeHandle = Handle<ThemeTag>;               ///< Names a theme (style variables and colors).
using StyleHandle = Handle<StyleTag>;               ///< Names a style.
using DockNodeHandle = Handle<DockNodeTag>;         ///< Names a node of a dock tree.
using DrawListHandle = Handle<DrawListTag>;         ///< Names a draw list of a frame.
using LayerHandle = Handle<LayerTag>;               ///< Names a drawing layer (background, windows, popups, ...).
using InputContextHandle = Handle<InputContextTag>; ///< Names an input context (keymap, focus, navigation).

using WidgetIdentifier = Identifier<WidgetTag>;       ///< Names a widget: its label, combined with the ID stack.
using WindowIdentifier = Identifier<WindowTag>;       ///< Names a UI window ("Inspector"_window).
using PanelIdentifier = Identifier<PanelTag>;         ///< Names a panel (child region) of a window.
using DockSpaceIdentifier = Identifier<DockSpaceTag>; ///< Names a dock space ("editor"_dock_space).
using TabIdentifier = Identifier<TabTag>;             ///< Names a tab of a tab bar.
using MenuIdentifier = Identifier<MenuTag>;           ///< Names a menu.
using MenuItemIdentifier = Identifier<MenuItemTag>;   ///< Names a menu item.
using PopupIdentifier = Identifier<PopupTag>;         ///< Names a popup ("confirm_delete"_popup).
using TooltipIdentifier = Identifier<TooltipTag>;     ///< Names a tooltip.
using SplitterIdentifier = Identifier<SplitterTag>;   ///< Names a splitter.
using TableIdentifier = Identifier<TableTag>;         ///< Names a table ("objects"_table).

/// @brief Names a node of the accessibility tree, the same from frame to frame: ACCESSIBILITY_ROOT_NODE for the
///        whole UI, a window's identifier in the high 32 bits for its window, and with an item's identifier in the
///        low 32 bits for the item (assistive technologies send it back in UiAccessibilityRequest::target).
using AccessibilityNodeId = std::uint64_t;

/// @brief A glyph of a font: its index in the font's glyph table (what shaping produces and atlases
///        are keyed by), not a code point.
class GlyphIndex
{
public:
    /// @brief Glyph 0, the font's `.notdef` (shown for characters the font lacks).
    constexpr GlyphIndex() noexcept = default;

    /// @brief Glyph @p value of a font.
    constexpr explicit GlyphIndex(std::uint32_t value) noexcept
        : m_value{value}
    {
    }

    /// @brief Index of the glyph in its font.
    [[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t
    {
        return m_value;
    }

    /// @brief Whether this is the `.notdef` glyph.
    [[nodiscard]] constexpr auto is_notdef() const noexcept -> bool
    {
        return m_value == 0;
    }

    /// @brief Glyphs compare by index.
    [[nodiscard]] constexpr auto operator<=>(const GlyphIndex&) const noexcept = default;

private:
    std::uint32_t m_value = 0;
};

// ================================================================================================= Concepts

/// @brief A handle the UI context issues.
template<class Type>
concept UiHandle = TaggedHandle<Type>
                   && (std::same_as<Type, FontHandle> || std::same_as<Type, FontAtlasHandle> || std::same_as<Type, ThemeHandle>
                       || std::same_as<Type, StyleHandle> || std::same_as<Type, DockNodeHandle>
                       || std::same_as<Type, DrawListHandle> || std::same_as<Type, LayerHandle>
                       || std::same_as<Type, InputContextHandle>);

/// @brief A handle naming font data (a font, or the atlas behind it).
template<class Type>
concept FontResourceHandle = std::same_as<Type, FontHandle> || std::same_as<Type, FontAtlasHandle>;

/// @brief An identifier the application declares for the UI.
template<class Type>
concept UiIdentifier = TaggedIdentifier<Type>
                       && (std::same_as<Type, WidgetIdentifier> || std::same_as<Type, WindowIdentifier>
                           || std::same_as<Type, PanelIdentifier> || std::same_as<Type, DockSpaceIdentifier>
                           || std::same_as<Type, TabIdentifier> || std::same_as<Type, MenuIdentifier>
                           || std::same_as<Type, MenuItemIdentifier> || std::same_as<Type, PopupIdentifier>
                           || std::same_as<Type, TooltipIdentifier> || std::same_as<Type, SplitterIdentifier>
                           || std::same_as<Type, TableIdentifier>);

// ================================================================================================= Literals

/// @brief Compile-time identifiers: `using namespace gfx::ui::literals;` then `"Inspector"_window`.
namespace literals
{

/// @brief The UI window named @p name, e.g. `"Inspector"_window`.
consteval auto
operator""_window(const char* name,
                  std::size_t length) -> WindowIdentifier
{
    return WindowIdentifier{std::string_view{name, length}};
}

/// @brief The panel named @p name.
consteval auto
operator""_panel(const char* name,
                 std::size_t length) -> PanelIdentifier
{
    return PanelIdentifier{std::string_view{name, length}};
}

/// @brief The dock space named @p name, e.g. `"editor"_dock_space`.
consteval auto
operator""_dock_space(const char* name,
                      std::size_t length) -> DockSpaceIdentifier
{
    return DockSpaceIdentifier{std::string_view{name, length}};
}

/// @brief The tab named @p name.
consteval auto
operator""_tab(const char* name,
               std::size_t length) -> TabIdentifier
{
    return TabIdentifier{std::string_view{name, length}};
}

/// @brief The menu named @p name.
consteval auto
operator""_menu(const char* name,
                std::size_t length) -> MenuIdentifier
{
    return MenuIdentifier{std::string_view{name, length}};
}

/// @brief The popup named @p name, e.g. `"confirm_delete"_popup`.
consteval auto
operator""_popup(const char* name,
                 std::size_t length) -> PopupIdentifier
{
    return PopupIdentifier{std::string_view{name, length}};
}

/// @brief The table named @p name.
consteval auto
operator""_table(const char* name,
                 std::size_t length) -> TableIdentifier
{
    return TableIdentifier{std::string_view{name, length}};
}

} // namespace literals

} // namespace gfx::ui

/// @brief Formats a glyph as "Glyph(42)".
template<>
struct std::formatter<gfx::ui::GlyphIndex, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(const gfx::ui::GlyphIndex& glyph, FormatContext& context) const -> FormatContext::iterator
    {
        const std::string text = std::format("Glyph({})", glyph.value());
        return std::formatter<std::string_view, char>::format(text, context);
    }
};
