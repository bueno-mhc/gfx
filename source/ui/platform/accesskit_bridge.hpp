/// @file accesskit_bridge.hpp
/// @brief A std-free interface to AccessKit's Windows adapter (accesskit_bridge.cpp, a plain translation unit:
///        AccessKit's header includes <windows.h>, which cannot share a unit with modules). ui_platform.cpp turns the
///        UI's accessibility tree into these plain values and checks that the constants match its enumerations.
#pragma once

namespace gfx::ui::accesskit_bridge
{

/// Bits of AccessibilityState.
inline constexpr unsigned int STATE_DISABLED = 1U << 0;
inline constexpr unsigned int STATE_FOCUSABLE = 1U << 1;
inline constexpr unsigned int STATE_CHECKABLE = 1U << 2;
inline constexpr unsigned int STATE_CHECKED = 1U << 3;
inline constexpr unsigned int STATE_MIXED = 1U << 4;
inline constexpr unsigned int STATE_SELECTABLE = 1U << 5;
inline constexpr unsigned int STATE_SELECTED = 1U << 6;
inline constexpr unsigned int STATE_EXPANDABLE = 1U << 7;
inline constexpr unsigned int STATE_EXPANDED = 1U << 8;
inline constexpr unsigned int STATE_MODAL = 1U << 9;
inline constexpr unsigned int STATE_READ_ONLY = 1U << 10;
inline constexpr unsigned int STATE_PROTECTED = 1U << 11;
inline constexpr unsigned int STATE_MULTILINE = 1U << 12;
inline constexpr unsigned int STATE_BUSY = 1U << 13;

/// Bits of AccessibilityAction.
inline constexpr unsigned int ACTION_FOCUS = 1U << 0;
inline constexpr unsigned int ACTION_CLICK = 1U << 1;
inline constexpr unsigned int ACTION_INCREMENT = 1U << 2;
inline constexpr unsigned int ACTION_DECREMENT = 1U << 3;
inline constexpr unsigned int ACTION_SET_VALUE = 1U << 4;
inline constexpr unsigned int ACTION_EXPAND = 1U << 5;
inline constexpr unsigned int ACTION_COLLAPSE = 1U << 6;
inline constexpr unsigned int ACTION_SHOW_CONTEXT_MENU = 1U << 7;
inline constexpr unsigned int ACTION_SCROLL_INTO_VIEW = 1U << 8;

/// Values of AccessibilityLiveRegion.
inline constexpr unsigned int LIVE_OFF = 0;
inline constexpr unsigned int LIVE_POLITE = 1;
inline constexpr unsigned int LIVE_ASSERTIVE = 2;

/// Values of AccessibilityRole the bridge treats apart (the others map by a table of ROLE_COUNT entries).
inline constexpr unsigned int ROLE_STATIC_TEXT = 4;
inline constexpr unsigned int ROLE_TEXT_FIELD = 12;
inline constexpr unsigned int ROLE_MENU_ITEM = 18;
inline constexpr unsigned int ROLE_COUNT = 33;

/// Bytes of UTF-8 text (not terminated).
struct Text
{
    const char* data = nullptr;
    unsigned long long size = 0;
};

/// A node of the accessibility tree.
struct Node
{
    unsigned long long id = 0;
    unsigned int role = 0;
    unsigned int states = 0;
    unsigned int actions = 0;
    unsigned int live = LIVE_OFF;
    double left = 0;   ///< Bounds, physical pixels of the window's client area.
    double top = 0;
    double right = 0;
    double bottom = 0;
    Text name{};
    Text description{};
    Text value{};
    Text shortcut{};
    bool has_number = false;
    double number = 0;
    double minimum = 0;
    double maximum = 0;
    double step = 0;
    const unsigned long long* children = nullptr;
    unsigned long long child_count = 0;
};

/// What an assistive technology asked (its text lives only during the callback).
struct Request
{
    unsigned int action = 0;
    unsigned long long target = 0;
    Text text{};
    bool has_number = false;
    double number = 0;
};

/// Receives the requests, on any thread.
using RequestCallback = void (*)(void* user,
                                 const Request& request);

/// An adapter (opaque).
struct Adapter;

/// An adapter for window @p window (a HWND as an integer), made on its thread: the window is subclassed to answer
/// WM_GETOBJECT (an assistive technology's first request activates the adapter: it is given a tree of the root
/// @p root named @p title until the next update) and to follow its keyboard focus. Null when it cannot be made.
auto create(unsigned long long window,
            Text title,
            unsigned long long root,
            RequestCallback callback,
            void* user) -> Adapter*;

/// Removes the subclass and frees the adapter (on the window's thread).
void destroy(Adapter* adapter);

/// Whether an assistive technology has asked for the tree.
auto active(const Adapter* adapter) -> bool;

/// Whether the next update must give every node (the adapter was activated since the last one).
auto needs_full_update(const Adapter* adapter) -> bool;

/// Gives AccessKit @p count nodes (every node when @p full, else those that changed since the last update) and the
/// focused node; nothing while not active.
void update(Adapter* adapter,
            const Node* nodes,
            unsigned long long count,
            unsigned long long focus,
            bool full);

} // namespace gfx::ui::accesskit_bridge
