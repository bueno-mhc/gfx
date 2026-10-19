/// @file ui_platform.cpp
/// @brief UiPlatformInput (window events → UiFrameInput), UiWindowServices (clipboard, cursors, text input), the
///        navigation commands of the input system's UI context, and UiAccessibilityAdapter (AccessKit, through the
///        plain translation unit accesskit_bridge.cpp).
module;

#if defined(GFX_UI_ACCESSKIT)
#include "accesskit_bridge.hpp"
#endif

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
    m_composition.reserve(64);
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
                m_composition.clear();
                break;
            case EventType::TEXT_COMPOSITION:
            {
                // The composition in progress (empty when it ends) stays until it changes.
                const auto& composition = event.as<platform::TextCompositionEvent>();
                m_composition.assign(events.text(event));
                m_composition_cursor = composition.cursor;
                break;
            }
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
            .text = m_text,
            .composition = m_composition,
            .composition_cursor = m_composition_cursor};
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
    // Compositions drawn by the field where the backend reports them; the system shows them otherwise.
    m_window->start_text_input({.area = rectangle, .composition_events = m_platform->supports(platform::PlatformFeature::TEXT_COMPOSITION)});
}

auto
UiWindowServices::open_url(std::string_view /*url*/) -> bool
{
    return false;
}


// ===================================================================================================== navigation

auto
navigation_input(const platform::InputFrame& frame,
                 platform::PlayerIdentifier player) -> UiNavigationInput
{
    // Each command's action, held and just pressed; the scrolling stick with y downwards.
    UiNavigationInput input{};
    for (std::size_t index = 0; index < UI_NAVIGATION_ACTIONS.size(); ++index)
    {
        const platform::ActionIdentifier action = platform::ActionIdentifier::from_name(UI_NAVIGATION_ACTIONS[index]);
        const auto command = static_cast<UiNavigationCommand>(1U << index);
        input.down = frame.is_action_pressed(action, player) ? input.down | command : input.down;
        input.pressed = frame.was_action_just_pressed(action, player) ? input.pressed | command : input.pressed;
    }
    const Vector2 scroll = frame.get_action_value_2d(platform::ActionIdentifier::from_name(UI_SCROLL_ACTION), player);
    input.scroll = {.x = scroll.x, .y = -scroll.y};

    // The moving stick (up positive): its larger axis, pushed far enough, holds a move.
    const Vector2 move = frame.get_action_value_2d(platform::ActionIdentifier::from_name(UI_MOVE_ACTION), player);
    if (std::max(std::abs(move.x), std::abs(move.y)) >= UI_STICK_MOVE_THRESHOLD)
    {
        const UiNavigationCommand direction = std::abs(move.x) > std::abs(move.y) ? (move.x > 0 ? UiNavigationCommand::MOVE_RIGHT : UiNavigationCommand::MOVE_LEFT)
                                              : move.y > 0                        ? UiNavigationCommand::MOVE_UP
                                                                                  : UiNavigationCommand::MOVE_DOWN;
        input.down = input.down | direction;
    }
    return input;
}

// ===================================================================================================== accessibility

namespace
{

/// A request received, its text kept apart until take_requests().
struct ReceivedRequest
{
    UiAccessibilityRequest request{};
    std::size_t text_offset = 0;
    std::size_t text_size = 0;
};

#if defined(GFX_UI_ACCESSKIT)
namespace bridge = accesskit_bridge;

// The bridge's constants are the enumerations' values.
static_assert(std::to_underlying(AccessibilityState::DISABLED) == bridge::STATE_DISABLED);
static_assert(std::to_underlying(AccessibilityState::FOCUSABLE) == bridge::STATE_FOCUSABLE);
static_assert(std::to_underlying(AccessibilityState::CHECKABLE) == bridge::STATE_CHECKABLE);
static_assert(std::to_underlying(AccessibilityState::CHECKED) == bridge::STATE_CHECKED);
static_assert(std::to_underlying(AccessibilityState::MIXED) == bridge::STATE_MIXED);
static_assert(std::to_underlying(AccessibilityState::SELECTABLE) == bridge::STATE_SELECTABLE);
static_assert(std::to_underlying(AccessibilityState::SELECTED) == bridge::STATE_SELECTED);
static_assert(std::to_underlying(AccessibilityState::EXPANDABLE) == bridge::STATE_EXPANDABLE);
static_assert(std::to_underlying(AccessibilityState::EXPANDED) == bridge::STATE_EXPANDED);
static_assert(std::to_underlying(AccessibilityState::MODAL) == bridge::STATE_MODAL);
static_assert(std::to_underlying(AccessibilityState::READ_ONLY) == bridge::STATE_READ_ONLY);
static_assert(std::to_underlying(AccessibilityState::PROTECTED) == bridge::STATE_PROTECTED);
static_assert(std::to_underlying(AccessibilityState::MULTILINE) == bridge::STATE_MULTILINE);
static_assert(std::to_underlying(AccessibilityState::BUSY) == bridge::STATE_BUSY);
static_assert(std::to_underlying(AccessibilityAction::FOCUS) == bridge::ACTION_FOCUS);
static_assert(std::to_underlying(AccessibilityAction::CLICK) == bridge::ACTION_CLICK);
static_assert(std::to_underlying(AccessibilityAction::INCREMENT) == bridge::ACTION_INCREMENT);
static_assert(std::to_underlying(AccessibilityAction::DECREMENT) == bridge::ACTION_DECREMENT);
static_assert(std::to_underlying(AccessibilityAction::SET_VALUE) == bridge::ACTION_SET_VALUE);
static_assert(std::to_underlying(AccessibilityAction::EXPAND) == bridge::ACTION_EXPAND);
static_assert(std::to_underlying(AccessibilityAction::COLLAPSE) == bridge::ACTION_COLLAPSE);
static_assert(std::to_underlying(AccessibilityAction::SHOW_CONTEXT_MENU) == bridge::ACTION_SHOW_CONTEXT_MENU);
static_assert(std::to_underlying(AccessibilityAction::SCROLL_INTO_VIEW) == bridge::ACTION_SCROLL_INTO_VIEW);
static_assert(EnumerationTraits<AccessibilityAction>::NAMES.size() == 9, "the bridge maps nine actions");
static_assert(std::to_underlying(AccessibilityLiveRegion::OFF) == bridge::LIVE_OFF);
static_assert(std::to_underlying(AccessibilityLiveRegion::POLITE) == bridge::LIVE_POLITE);
static_assert(std::to_underlying(AccessibilityLiveRegion::ASSERTIVE) == bridge::LIVE_ASSERTIVE);
static_assert(std::to_underlying(AccessibilityRole::STATIC_TEXT) == bridge::ROLE_STATIC_TEXT);
static_assert(std::to_underlying(AccessibilityRole::TEXT_FIELD) == bridge::ROLE_TEXT_FIELD);
static_assert(std::to_underlying(AccessibilityRole::MENU_ITEM) == bridge::ROLE_MENU_ITEM);
static_assert(ENUMERATOR_COUNT<AccessibilityRole> == bridge::ROLE_COUNT, "the bridge maps every role");
static_assert(std::same_as<AccessibilityNodeId, unsigned long long>, "children are given to AccessKit as they are");

/// The bridge's text of @p text.
auto
text_of(std::string_view text) noexcept -> bridge::Text
{
    return {.data = text.data(), .size = text.size()};
}
#endif

} // namespace

/// What an adapter holds: the bridge's adapter, the nodes given to it, the requests received.
struct UiAccessibilityAdapter::State
{
    platform::Window* window = nullptr;
    std::thread::id thread{};
    std::uint64_t given_frame = 0;
    std::string title{};
    std::mutex mutex{};
    std::vector<ReceivedRequest> received{};
    std::string received_text{};
    std::vector<ReceivedRequest> taken_received{};
    std::string taken_text{};
    std::vector<UiAccessibilityRequest> taken{};
#if defined(GFX_UI_ACCESSKIT)
    bridge::Adapter* adapter = nullptr;
    std::vector<bridge::Node> nodes{};

    /// Keeps a request (any thread).
    static void receive(void* user,
                        const bridge::Request& request)
    {
        State& state = *static_cast<State*>(user);
        try
        {
            const std::scoped_lock lock{state.mutex};
            const std::size_t offset = state.received_text.size();
            state.received_text.append(request.text.data != nullptr ? std::string_view{request.text.data, static_cast<std::size_t>(request.text.size)}
                                                                    : std::string_view{});
            state.received.push_back({.request = {.action = static_cast<AccessibilityAction>(request.action),
                                                  .target = request.target,
                                                  .number = request.number,
                                                  .has_number = request.has_number},
                                      .text_offset = offset,
                                      .text_size = state.received_text.size() - offset});
        }
        catch (...)
        {
            // Out of memory: the request is dropped (assistive technologies repeat what does not happen).
        }
    }
#endif
};

UiAccessibilityAdapter::UiAccessibilityAdapter(platform::Window& window)
    : m_state{std::make_unique<State>()}
{
    State& state = *m_state;
    state.window = &window;
    state.thread = std::this_thread::get_id();
    state.received.reserve(16);
    state.taken_received.reserve(16);
    state.taken.reserve(16);
#if defined(GFX_UI_ACCESSKIT)
    // AccessKit's adapter of the window's Win32 handle, named by its title until the tree arrives.
    const NativeWindowHandle native = window.native_handle();
    const Win32WindowHandle* handle = native.get_if<Win32WindowHandle>();
    if (handle == nullptr || handle->window == 0)
    {
        throw_unsupported(UiSubsystem::ACCESSIBILITY, "UiAccessibilityAdapter", "accessibility", "the window has no Win32 handle",
                          "give it a window of the platform library's Windows backends (SDL3, GLFW)");
    }
    state.title = window.title();
    state.adapter = bridge::create(handle->window, text_of(state.title), ACCESSIBILITY_ROOT_NODE, &State::receive, &state);
    if (state.adapter == nullptr)
    {
        throw BackendApiError({.backend = UiSubsystem::ACCESSIBILITY,
                               .operation = "UiAccessibilityAdapter",
                               .api_function = "SetWindowSubclass",
                               .result_code = 0,
                               .result_name = "FALSE"});
    }
#else
    throw_unsupported(UiSubsystem::ACCESSIBILITY, "UiAccessibilityAdapter", "accessibility", "AccessKit's adapter is built for Windows only",
                      "build the accessibility tree with UiContext::set_accessibility_enabled() and give it to the platform's own provider");
#endif
}

UiAccessibilityAdapter::~UiAccessibilityAdapter()
{
#if defined(GFX_UI_ACCESSKIT)
    bridge::destroy(m_state->adapter);
#endif
}

auto
UiAccessibilityAdapter::active() const noexcept -> bool
{
#if defined(GFX_UI_ACCESSKIT)
    return bridge::active(m_state->adapter);
#else
    return false;
#endif
}

void
UiAccessibilityAdapter::update(UiContext& context)
{
    State& state = *m_state;
    if (std::this_thread::get_id() != state.thread)
    {
        throw InvalidUsageError({.backend = UiSubsystem::ACCESSIBILITY, .operation = "UiAccessibilityAdapter::update", .problem = "called on another thread",
                                 .suggestion = "call it on the thread that made the adapter (the window's)"});
    }
#if defined(GFX_UI_ACCESSKIT)
    // Nothing until an assistive technology asks; then the tree is built from the next frame.
    if (!bridge::active(state.adapter))
    {
        return;
    }
    if (!context.accessibility_enabled())
    {
        context.set_accessibility_enabled(true);
        return;
    }
    const UiAccessibilityTree tree = context.accessibility_tree();
    if (tree.frame == 0 || tree.frame == state.given_frame)
    {
        return;
    }

    // Every node after an activation or a frame not given, else what changed; bounds in pixels; the root named as
    // the window.
    const bool full = bridge::needs_full_update(state.adapter) || tree.previous_frame == 0 || tree.previous_frame != state.given_frame;
    state.nodes.clear();
    const auto convert = [&](const UiAccessibilityNode& node)
    {
        const float scale = tree.pixels_per_unit;
        const bool root = node.id == ACCESSIBILITY_ROOT_NODE;
        if (root)
        {
            state.title = state.window->title();
        }
        state.nodes.push_back({.id = node.id,
                               .role = std::to_underlying(node.role),
                               .states = std::to_underlying(node.states),
                               .actions = std::to_underlying(node.actions),
                               .live = std::to_underlying(node.live_region),
                               .left = static_cast<double>(node.bounds.left * scale),
                               .top = static_cast<double>(node.bounds.top * scale),
                               .right = static_cast<double>(node.bounds.right * scale),
                               .bottom = static_cast<double>(node.bounds.bottom * scale),
                               .name = text_of(root && node.name.empty() ? std::string_view{state.title} : node.name),
                               .description = text_of(node.description),
                               .value = text_of(node.value),
                               .shortcut = text_of(node.shortcut),
                               .has_number = node.has_number,
                               .number = node.number,
                               .minimum = node.minimum,
                               .maximum = node.maximum,
                               .step = node.step,
                               .children = node.children.data(),
                               .child_count = node.children.size()});
    };
    if (full)
    {
        std::ranges::for_each(tree.nodes, convert);
    }
    else
    {
        for (const std::uint32_t index : tree.changed)
        {
            convert(tree.nodes[index]);
        }
    }
    bridge::update(state.adapter, state.nodes.data(), state.nodes.size(), tree.focus, full);
    state.given_frame = tree.frame;
#else
    static_cast<void>(context);
#endif
}

auto
UiAccessibilityAdapter::take_requests() -> std::span<const UiAccessibilityRequest>
{
    // The requests received, swapped out under the lock; their texts point into the taken copy.
    State& state = *m_state;
    {
        const std::scoped_lock lock{state.mutex};
        std::swap(state.received, state.taken_received);
        std::swap(state.received_text, state.taken_text);
        state.received.clear();
        state.received_text.clear();
    }
    state.taken.clear();
    for (const ReceivedRequest& received : state.taken_received)
    {
        UiAccessibilityRequest request = received.request;
        request.text = std::string_view{state.taken_text}.substr(received.text_offset, received.text_size);
        state.taken.push_back(request);
    }
    return state.taken;
}

} // namespace gfx::ui
