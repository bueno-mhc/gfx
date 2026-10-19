/// @file accessibility_tree.cppm
/// @brief gfx.ui.context:accessibility_tree: what a frame gives assistive technologies (screen readers) — its
///        accessibility tree (UiAccessibilityNode, UiAccessibilityTree), with what changed since the tree before, so a
///        provider (gfx.ui.platform's UiAccessibilityAdapter, or the application's own) sends only the changes.
export module gfx.ui.context:accessibility_tree;

import std;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;

export namespace gfx::ui
{

/// @brief A node of the accessibility tree: the whole UI (the root), a window, a container (a menu bar, a tab bar, a
///        table, a tree node's content) or an item. Its views and span stay valid until the next
///        UiContext::end_frame().
struct UiAccessibilityNode
{
    AccessibilityNodeId id = 0;                                         ///< The same from frame to frame (see AccessibilityNodeId).
    AccessibilityNodeId parent = 0;                                     ///< Its parent (0: the root has none).
    AccessibilityRole role = AccessibilityRole::GENERIC;                ///< What it is.
    AccessibilityState states = AccessibilityState::NONE;               ///< What it is in.
    AccessibilityAction actions = AccessibilityAction::NONE;            ///< What can be asked of it (UiAccessibilityRequest).
    AccessibilityLiveRegion live_region = AccessibilityLiveRegion::OFF; ///< Changes of its name or value are announced.
    UiRectangle bounds{};                                               ///< Where it is on the display, UI units.
    std::string_view name{};                                            ///< What it is called: its label, or the accessible label given.
    std::string_view description{};                                     ///< A longer description (set_next_item_accessible_description()).
    std::string_view value{};                                           ///< Its value as text: a field's text, a slider's formatted value, a combo's choice.
    std::string_view shortcut{};                                        ///< The key chord that activates it ("Ctrl+S").
    bool has_number = false;                                            ///< A numeric value is given (sliders, drags, progress bars).
    double number = 0.0;                                                ///< The value.
    double minimum = 0.0;                                               ///< Its smallest value (when minimum < maximum).
    double maximum = 0.0;                                               ///< Its largest value.
    double step = 0.0;                                                  ///< What AccessibilityAction::INCREMENT adds (0: unknown).
    std::span<const AccessibilityNodeId> children{};                    ///< Its children, in order (the navigation order).
};

/// @brief The accessibility tree of the last frame (UiContext::accessibility_tree()), and what changed since the tree
///        before it. Valid until the next UiContext::end_frame(). Built only while accessibility is enabled
///        (UiContext::set_accessibility_enabled()); empty otherwise.
struct UiAccessibilityTree
{
    std::span<const UiAccessibilityNode> nodes{};     ///< Every node; the root (ACCESSIBILITY_ROOT_NODE) first.
    std::span<const std::uint32_t> changed{};         ///< Indices into nodes of the nodes new or different since the tree before.
    std::span<const AccessibilityNodeId> removed{};   ///< Nodes of the tree before that this one no longer has.
    AccessibilityNodeId focus = ACCESSIBILITY_ROOT_NODE; ///< The node with the keyboard focus (its window's, or the root, when no item has it).
    bool focus_changed = false;                       ///< focus differs from the tree before's.
    std::uint64_t frame = 0;                          ///< The frame it describes (0: no tree).
    std::uint64_t previous_frame = 0;                 ///< The frame of the tree it is compared with (0: none, every node is new).
    float pixels_per_unit = 1.0F;                     ///< The frame's pixels per UI unit (providers give bounds in pixels).

    /// @brief The node @p id, or nullptr.
    [[nodiscard]] auto find(AccessibilityNodeId id) const noexcept -> const UiAccessibilityNode*
    {
        const auto found = std::ranges::find(nodes, id, &UiAccessibilityNode::id);
        return found != nodes.end() ? &*found : nullptr;
    }
};

} // namespace gfx::ui
