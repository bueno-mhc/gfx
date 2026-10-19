/// @file accesskit_bridge.cpp
/// @brief AccessKit's Windows adapter behind accesskit_bridge.hpp (a plain translation unit: AccessKit's header
///        includes <windows.h>). The window is subclassed (SetWindowSubclass) rather than given to AccessKit's
///        subclassing adapter, which must be made before the window is first shown: windows of the platform library
///        are shown when made. WM_GETOBJECT goes to the adapter; the first one activates it with a tree of the root
///        alone, and the next update gives every node.
#include "accesskit_bridge.hpp"

#include <windows.h>
#include <commctrl.h>

#include <accesskit.h>

#include <atomic>
#include <cstring>
#include <new>
#include <string>

namespace gfx::ui::accesskit_bridge
{

namespace
{

/// The identifier of the bridge's subclass of a window.
constexpr UINT_PTR SUBCLASS_IDENTIFIER = 0x67667875U;

/// AccessKit's role of each AccessibilityRole, by value.
constexpr accesskit_role ROLES[ROLE_COUNT] = {
    ACCESSKIT_ROLE_GENERIC_CONTAINER,  // GENERIC
    ACCESSKIT_ROLE_WINDOW,             // WINDOW
    ACCESSKIT_ROLE_DIALOG,             // DIALOG
    ACCESSKIT_ROLE_GROUP,              // GROUP
    ACCESSKIT_ROLE_LABEL,              // STATIC_TEXT
    ACCESSKIT_ROLE_LINK,               // LINK
    ACCESSKIT_ROLE_BUTTON,             // BUTTON
    ACCESSKIT_ROLE_CHECK_BOX,          // CHECK_BOX
    ACCESSKIT_ROLE_RADIO_BUTTON,       // RADIO_BUTTON
    ACCESSKIT_ROLE_SWITCH,             // SWITCH
    ACCESSKIT_ROLE_SLIDER,             // SLIDER
    ACCESSKIT_ROLE_SPIN_BUTTON,        // SPIN_BUTTON
    ACCESSKIT_ROLE_TEXT_INPUT,         // TEXT_FIELD
    ACCESSKIT_ROLE_COMBO_BOX,          // COMBO_BOX
    ACCESSKIT_ROLE_LIST_BOX,           // LIST
    ACCESSKIT_ROLE_LIST_BOX_OPTION,    // LIST_ITEM
    ACCESSKIT_ROLE_MENU_BAR,           // MENU_BAR
    ACCESSKIT_ROLE_MENU,               // MENU
    ACCESSKIT_ROLE_MENU_ITEM,          // MENU_ITEM
    ACCESSKIT_ROLE_TAB_LIST,           // TAB_LIST
    ACCESSKIT_ROLE_TAB,                // TAB
    ACCESSKIT_ROLE_TAB_PANEL,          // TAB_PANEL
    ACCESSKIT_ROLE_TREE,               // TREE
    ACCESSKIT_ROLE_TREE_ITEM,          // TREE_ITEM
    ACCESSKIT_ROLE_TABLE,              // TABLE
    ACCESSKIT_ROLE_ROW,                // ROW
    ACCESSKIT_ROLE_CELL,               // CELL
    ACCESSKIT_ROLE_COLUMN_HEADER,      // COLUMN_HEADER
    ACCESSKIT_ROLE_TOOLTIP,            // TOOLTIP
    ACCESSKIT_ROLE_PROGRESS_INDICATOR, // PROGRESS_BAR
    ACCESSKIT_ROLE_SCROLL_BAR,         // SCROLL_BAR
    ACCESSKIT_ROLE_SPLITTER,           // SEPARATOR
    ACCESSKIT_ROLE_IMAGE,              // IMAGE
};

/// AccessKit's action of each bit of AccessibilityAction, in bit order.
constexpr accesskit_action ACTIONS[] = {
    ACCESSKIT_ACTION_FOCUS,
    ACCESSKIT_ACTION_CLICK,
    ACCESSKIT_ACTION_INCREMENT,
    ACCESSKIT_ACTION_DECREMENT,
    ACCESSKIT_ACTION_SET_VALUE,
    ACCESSKIT_ACTION_EXPAND,
    ACCESSKIT_ACTION_COLLAPSE,
    ACCESSKIT_ACTION_SHOW_CONTEXT_MENU,
    ACCESSKIT_ACTION_SCROLL_INTO_VIEW,
};

} // namespace

/// An adapter: AccessKit's, the subclassed window, the update being given.
struct Adapter
{
    HWND window = nullptr;
    accesskit_windows_adapter* adapter = nullptr;
    std::string title{};
    unsigned long long root = 0;
    RequestCallback callback = nullptr;
    void* user = nullptr;
    std::atomic<bool> active{false};
    std::atomic<bool> needs_full{false};
    const Node* nodes = nullptr;
    unsigned long long count = 0;
    unsigned long long focus = 0;
};

namespace
{

/// AccessKit's node for @p node.
auto
make_node(const Node& node) -> accesskit_node*
{
    // The role (text fields and menu items by their states).
    accesskit_role role = node.role < ROLE_COUNT ? ROLES[node.role] : ACCESSKIT_ROLE_UNKNOWN;
    if (node.role == ROLE_TEXT_FIELD)
    {
        role = (node.states & STATE_PROTECTED) != 0 ? ACCESSKIT_ROLE_PASSWORD_INPUT
               : (node.states & STATE_MULTILINE) != 0 ? ACCESSKIT_ROLE_MULTILINE_TEXT_INPUT
                                                      : role;
    }
    if (node.role == ROLE_MENU_ITEM && (node.states & STATE_CHECKABLE) != 0)
    {
        role = ACCESSKIT_ROLE_MENU_ITEM_CHECK_BOX;
    }
    accesskit_node* result = accesskit_node_new(role);

    // Where it is, what it says (a static text's words are its value too).
    accesskit_node_set_bounds(result, accesskit_rect{node.left, node.top, node.right, node.bottom});
    if (node.name.size > 0)
    {
        accesskit_node_set_label_with_length(result, node.name.data, node.name.size);
    }
    if (node.description.size > 0)
    {
        accesskit_node_set_description_with_length(result, node.description.data, node.description.size);
    }
    const Text value = node.value.size > 0 || node.role != ROLE_STATIC_TEXT ? node.value : node.name;
    if (value.size > 0)
    {
        accesskit_node_set_value_with_length(result, value.data, value.size);
    }
    if (node.shortcut.size > 0)
    {
        accesskit_node_set_keyboard_shortcut_with_length(result, node.shortcut.data, node.shortcut.size);
    }

    // Its states.
    if ((node.states & STATE_DISABLED) != 0)
    {
        accesskit_node_set_disabled(result);
    }
    if ((node.states & STATE_READ_ONLY) != 0)
    {
        accesskit_node_set_read_only(result);
    }
    if ((node.states & STATE_MODAL) != 0)
    {
        accesskit_node_set_modal(result);
    }
    if ((node.states & STATE_BUSY) != 0)
    {
        accesskit_node_set_busy(result);
    }
    if ((node.states & STATE_CHECKABLE) != 0)
    {
        accesskit_node_set_toggled(result, (node.states & STATE_MIXED) != 0     ? ACCESSKIT_TOGGLED_MIXED
                                           : (node.states & STATE_CHECKED) != 0 ? ACCESSKIT_TOGGLED_TRUE
                                                                                : ACCESSKIT_TOGGLED_FALSE);
    }
    if ((node.states & STATE_SELECTABLE) != 0)
    {
        accesskit_node_set_selected(result, (node.states & STATE_SELECTED) != 0);
    }
    if ((node.states & STATE_EXPANDABLE) != 0)
    {
        accesskit_node_set_expanded(result, (node.states & STATE_EXPANDED) != 0);
    }
    if (node.live != LIVE_OFF)
    {
        accesskit_node_set_live(result, node.live == LIVE_ASSERTIVE ? ACCESSKIT_LIVE_ASSERTIVE : ACCESSKIT_LIVE_POLITE);
    }

    // Its number, its actions, its children.
    if (node.has_number)
    {
        accesskit_node_set_numeric_value(result, node.number);
        if (node.minimum < node.maximum)
        {
            accesskit_node_set_min_numeric_value(result, node.minimum);
            accesskit_node_set_max_numeric_value(result, node.maximum);
        }
        if (node.step > 0)
        {
            accesskit_node_set_numeric_value_step(result, node.step);
        }
    }
    for (unsigned int bit = 0; bit < sizeof(ACTIONS) / sizeof(ACTIONS[0]); ++bit)
    {
        if ((node.actions & (1U << bit)) != 0)
        {
            accesskit_node_add_action(result, ACTIONS[bit]);
        }
    }
    if (node.child_count > 0)
    {
        accesskit_node_set_children(result, static_cast<size_t>(node.child_count), node.children);
    }
    return result;
}

/// The tree of an adapter just activated: its root alone (the next update gives every node).
auto
placeholder_tree(void* user) -> accesskit_tree_update*
{
    Adapter& adapter = *static_cast<Adapter*>(user);
    adapter.needs_full = true;
    adapter.active = true;
    accesskit_node* root = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
    accesskit_node_set_label_with_length(root, adapter.title.data(), adapter.title.size());
    accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(1, adapter.root);
    accesskit_tree_info* tree = accesskit_tree_info_new(adapter.root);
    accesskit_tree_info_set_toolkit_name(tree, "gfx::ui");
    accesskit_tree_update_set_tree_info(update, tree);
    accesskit_tree_update_push_node(update, adapter.root, root);
    return update;
}

/// The update being given.
auto
tree_update(void* user) -> accesskit_tree_update*
{
    const Adapter& adapter = *static_cast<const Adapter*>(user);
    accesskit_tree_update* update = accesskit_tree_update_with_capacity_and_focus(static_cast<size_t>(adapter.count), adapter.focus);
    for (unsigned long long index = 0; index < adapter.count; ++index)
    {
        accesskit_tree_update_push_node(update, adapter.nodes[index].id, make_node(adapter.nodes[index]));
    }
    return update;
}

/// An assistive technology's request (any thread): given to the callback as plain values.
void
handle_action(accesskit_action_request* request,
              void* user)
{
    const Adapter& adapter = *static_cast<const Adapter*>(user);
    Request plain{.target = request->target_node};
    for (unsigned int bit = 0; bit < sizeof(ACTIONS) / sizeof(ACTIONS[0]); ++bit)
    {
        plain.action = ACTIONS[bit] == request->action ? 1U << bit : plain.action;
    }
    if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_VALUE && request->data.value.value != nullptr)
    {
        plain.text = {.data = request->data.value.value, .size = std::strlen(request->data.value.value)};
    }
    if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE)
    {
        plain.has_number = true;
        plain.number = request->data.value.numeric_value;
    }
    if (plain.action != 0)
    {
        adapter.callback(adapter.user, plain);
    }
    accesskit_action_request_free(request);
}

/// The window's subclass: WM_GETOBJECT to the adapter, the keyboard focus followed.
auto CALLBACK
window_procedure(HWND window,
                 UINT message,
                 WPARAM wparam,
                 LPARAM lparam,
                 UINT_PTR /*identifier*/,
                 DWORD_PTR data) -> LRESULT
{
    Adapter& adapter = *reinterpret_cast<Adapter*>(data);
    switch (message)
    {
        case WM_GETOBJECT:
        {
            const accesskit_opt_lresult result = accesskit_windows_adapter_handle_wm_getobject(adapter.adapter, wparam, lparam, placeholder_tree, &adapter);
            if (result.has_value)
            {
                return result.value;
            }
            break;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (accesskit_windows_queued_events* events = accesskit_windows_adapter_update_window_focus_state(adapter.adapter, message == WM_SETFOCUS))
            {
                accesskit_windows_queued_events_raise(events);
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(window, window_procedure, SUBCLASS_IDENTIFIER);
            break;
        default:
            break;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

} // namespace

auto
create(unsigned long long window,
       Text title,
       unsigned long long root,
       RequestCallback callback,
       void* user) -> Adapter*
{
    // AccessKit's adapter of the window, then the subclass that feeds it.
    const HWND handle = reinterpret_cast<HWND>(static_cast<UINT_PTR>(window));
    if (IsWindow(handle) == FALSE || callback == nullptr)
    {
        return nullptr;
    }
    Adapter* adapter = new (std::nothrow) Adapter{};
    if (adapter == nullptr)
    {
        return nullptr;
    }
    adapter->window = handle;
    adapter->title.assign(title.data != nullptr ? title.data : "", static_cast<size_t>(title.size));
    adapter->root = root;
    adapter->callback = callback;
    adapter->user = user;
    adapter->adapter = accesskit_windows_adapter_new(handle, GetFocus() == handle, handle_action, adapter);
    if (adapter->adapter == nullptr || SetWindowSubclass(handle, window_procedure, SUBCLASS_IDENTIFIER, reinterpret_cast<DWORD_PTR>(adapter)) == FALSE)
    {
        if (adapter->adapter != nullptr)
        {
            accesskit_windows_adapter_free(adapter->adapter);
        }
        delete adapter;
        return nullptr;
    }
    return adapter;
}

void
destroy(Adapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    RemoveWindowSubclass(adapter->window, window_procedure, SUBCLASS_IDENTIFIER);
    accesskit_windows_adapter_free(adapter->adapter);
    delete adapter;
}

auto
active(const Adapter* adapter) -> bool
{
    return adapter != nullptr && adapter->active.load();
}

auto
needs_full_update(const Adapter* adapter) -> bool
{
    return adapter != nullptr && adapter->needs_full.load();
}

void
update(Adapter* adapter,
       const Node* nodes,
       unsigned long long count,
       unsigned long long focus,
       bool full)
{
    if (adapter == nullptr || !adapter->active.load())
    {
        return;
    }

    // The factory reads the nodes while AccessKit updates its tree; the events are raised after (no lock held).
    adapter->nodes = nodes;
    adapter->count = count;
    adapter->focus = focus;
    if (full)
    {
        adapter->needs_full = false;
    }
    accesskit_windows_queued_events* events = accesskit_windows_adapter_update_if_active(adapter->adapter, tree_update, adapter);
    adapter->nodes = nullptr;
    adapter->count = 0;
    if (events != nullptr)
    {
        accesskit_windows_queued_events_raise(events);
    }
}

} // namespace gfx::ui::accesskit_bridge
