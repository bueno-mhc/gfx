/// @file platform.cppm
/// @brief Platform: the connection to a windowing backend (SDL3, GLFW, ...): it creates windows and
///        cursors, enumerates monitors, owns the clipboard and the input devices, and pumps events.
///
/// Threading (the model of commercial engines, see docs/platform-design.md A9): the thread that
/// creates the Platform is its platform thread (in practice the main thread); it pumps events and
/// performs every window, cursor, monitor, clipboard and device operation. push_event() and wake() may
/// be called from any thread, and Window's size, framebuffer size, content scale, minimized state and
/// native handle may be read from any thread.
///
/// @code
/// std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
/// std::unique_ptr<Window> window = platform->create_window({.title = "demo"});
/// while (running)
/// {
///     for (const Event& event : platform->poll_events())
///     {
///         running = event.type() != EventType::WINDOW_CLOSE_REQUESTED;
///     }
/// }
/// @endcode
export module gfx.platform.interface:platform;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import :platform_object;
import :event;
import :image;
import :monitor;
import :cursor;
import :window;
import :input_devices;

export namespace gfx::platform
{

/// @brief How to create a Platform.
struct PlatformDescription
{
    std::string_view application_name = "gfx application";            ///< Shown by the system (task bar, audio mixer).
    ValidationLevel validation_level = DEFAULT_VALIDATION_LEVEL;      ///< Checks the library performs.
    std::size_t event_queue_capacity = DEFAULT_EVENT_QUEUE_CAPACITY;  ///< Events a pump holds before the queue grows.
    std::size_t event_text_capacity = DEFAULT_EVENT_TEXT_CAPACITY;    ///< Text bytes a pump holds before the queue grows.
};

/// @brief Called with each event while it is pumped, before poll_events() returns, with the event's
///        text (empty when it has none). Needed where the system runs its own loop: Windows blocks the
///        application's loop while a window is resized or moved, and the watcher can render meanwhile.
using EventWatcherCallback = std::move_only_function<void(const Event& event, std::string_view text)>;

class Platform;

/// @brief Registration of an event watcher: the watcher is called until this object is destroyed or
///        reset. It may outlive the Platform (it detaches itself when the Platform goes first).
///        Platform thread only.
class EventWatcher
{
public:
    /// @brief No registration.
    EventWatcher() noexcept = default;

    /// @brief Unregisters the watcher.
    ~EventWatcher();

    EventWatcher(EventWatcher&& other) noexcept;
    auto operator=(EventWatcher&& other) noexcept -> EventWatcher&;
    EventWatcher(const EventWatcher&) = delete;
    auto operator=(const EventWatcher&) -> EventWatcher& = delete;

    /// @brief Whether the watcher is registered with a living platform.
    [[nodiscard]] auto is_active() const noexcept -> bool;

    /// @brief Unregisters the watcher now.
    void reset() noexcept;

private:
    friend class Platform;

    EventWatcher(std::weak_ptr<Platform* const> platform,
                 std::uint64_t identifier) noexcept;

private:
    std::weak_ptr<Platform* const> m_platform;
    std::uint64_t m_identifier = 0;
};

/// @brief A windowing backend: windows, cursors, monitors, clipboard, input devices and the event pump.
class Platform : public PlatformObject
{
public:
    ~Platform() override;

    // ----------------------------------------------------------------------------------------- event pump (platform thread)

    /// @brief Gathers the pending events without waiting.
    /// @return The events, valid until the next pump.
    /// @throws InvalidUsageError (ValidationLevel::CREATION and above) when called from another thread
    ///         than the platform thread, or from an event watcher.
    [[nodiscard]] auto poll_events() -> EventSpan;

    /// @brief Waits until at least one event arrives (or wake() is called), then gathers the events.
    [[nodiscard]] auto wait_events() -> EventSpan;

    /// @brief Waits until an event arrives, wake() is called, or @p timeout expires; then gathers the events.
    [[nodiscard]] auto wait_events_timeout(std::chrono::nanoseconds timeout) -> EventSpan;

    /// @brief The events of the last pump.
    [[nodiscard]] auto events() const noexcept -> EventSpan;

    /// @brief Calls @p callback with each event while it is pumped, until the returned registration is
    ///        destroyed.
    [[nodiscard]] auto add_event_watcher(EventWatcherCallback callback) -> EventWatcher;

    // ----------------------------------------------------------------------------------------- any thread

    /// @brief Queues @p event (with @p text when its payload references text) for the next pump of the
    ///        platform thread, and wakes a waiting pump. Thread-safe (replays, tests, cross-thread
    ///        notifications), like SDL_PushEvent.
    void push_event(const Event& event,
                    std::string_view text = {});

    /// @brief Ends a wait_events() or wait_events_timeout() in progress. Thread-safe.
    void wake();

    /// @brief The platform thread: the thread that created the platform.
    [[nodiscard]] auto thread() const noexcept -> std::thread::id;

    /// @brief Whether the caller runs on the platform thread.
    [[nodiscard]] auto is_platform_thread() const noexcept -> bool;

    /// @brief Name the application was created with.
    [[nodiscard]] auto application_name() const noexcept -> std::string_view;

    // ----------------------------------------------------------------------------------------- backend (platform thread)

    /// @brief Whether the backend provides @p feature.
    [[nodiscard]] virtual auto supports(PlatformFeature feature) const noexcept -> bool = 0;

    /// @brief Whether the backend provides the standard cursor @p shape.
    [[nodiscard]] virtual auto supports(CursorShape shape) const noexcept -> bool = 0;

    /// @brief The window flags the backend honors.
    [[nodiscard]] virtual auto supported_window_flags() const noexcept -> WindowFlag = 0;

    /// @brief Creates a window.
    /// @throws InvalidUsageError for an inconsistent description, BackendCapabilityError for an option
    ///         the backend cannot honor (validate_window_description()).
    [[nodiscard]] virtual auto create_window(const WindowDescription& description) -> std::unique_ptr<Window> = 0;

    /// @brief Creates a standard cursor.
    /// @throws BackendCapabilityError when the backend lacks the shape (supports(CursorShape)).
    [[nodiscard]] virtual auto create_cursor(CursorShape shape) -> std::unique_ptr<Cursor> = 0;

    /// @brief Creates a cursor from an image (PlatformFeature::CUSTOM_CURSORS).
    [[nodiscard]] virtual auto create_cursor(const CursorImageDescription& description) -> std::unique_ptr<Cursor> = 0;

    /// @brief The monitors connected now.
    [[nodiscard]] virtual auto monitors() const -> std::vector<MonitorHandle> = 0;

    /// @brief The primary monitor.
    [[nodiscard]] virtual auto primary_monitor() const -> MonitorHandle = 0;

    /// @brief What @p monitor is and how it is set up.
    /// @throws InvalidHandleError when the monitor was disconnected.
    [[nodiscard]] virtual auto monitor_properties(MonitorHandle monitor) const -> MonitorProperties = 0;

    /// @brief The video modes @p monitor can display (for EXCLUSIVE_FULLSCREEN).
    [[nodiscard]] virtual auto video_modes(MonitorHandle monitor) const -> std::vector<VideoMode> = 0;

    /// @brief Text on the clipboard (UTF-8; empty when there is none).
    [[nodiscard]] virtual auto clipboard_text() const -> std::string = 0;

    /// @brief Puts @p text (UTF-8) on the clipboard.
    virtual void set_clipboard_text(std::string_view text) = 0;

    /// @brief Image on the clipboard (PlatformFeature::CLIPBOARD_IMAGE; empty when there is none).
    [[nodiscard]] virtual auto clipboard_image() const -> DecodedImage = 0;

    /// @brief Puts @p image on the clipboard (PlatformFeature::CLIPBOARD_IMAGE).
    virtual void set_clipboard_image(const ImageData& image) = 0;

    /// @brief The input devices.
    [[nodiscard]] virtual auto input_devices() noexcept -> InputDevices& = 0;

    // ----------------------------------------------------------------------------------------- backend services

    /// @brief Runs @p release at the start of the next pump, on the platform thread: windows and cursors
    ///        destroyed while the backend may still reference them (e.g. from an event watcher) release
    ///        their native objects there. Reserved for backends (it needs a HandleKey).
    void defer_release(HandleKey key,
                       std::move_only_function<void()> release);

protected:
    /// @brief Records the platform thread (the caller) and prepares the event queues.
    explicit Platform(const PlatformDescription& description);

    /// @brief Gathers native events and passes each, normalized, to emit(). @p mode says whether to wait.
    virtual void pump(EventWaitMode mode,
                      std::chrono::nanoseconds timeout) = 0;

    /// @brief Ends a native wait in progress. Called from any thread.
    virtual void wake_backend() noexcept = 0;

    /// @brief Whether @p window still exists (events of destroyed windows are dropped).
    [[nodiscard]] virtual auto is_window_alive(WindowHandle window) const noexcept -> bool = 0;

    /// @brief Adds @p event to the pump's events and calls the event watchers.
    void emit(const Event& event);

    /// @brief Adds @p event, whose payload references @p text, to the pump's events and calls the watchers.
    void emit(const Event& event,
              std::string_view text);

    /// @brief Throws InvalidUsageError (ValidationLevel::CREATION and above) unless the caller runs on
    ///        the platform thread.
    void require_platform_thread(std::string_view operation,
                                 std::source_location location = std::source_location::current()) const;

    /// @brief Runs the pending deferred releases now. Backends call it in their destructor, while what
    ///        the releases use still exists.
    void release_deferred() noexcept;

private:
    friend class EventWatcher;

    /// A registered watcher.
    struct Watcher
    {
        std::uint64_t identifier = 0;
        EventWatcherCallback callback;
    };

    [[nodiscard]] auto run_pump(EventWaitMode mode,
                                std::chrono::nanoseconds timeout,
                                std::string_view operation) -> EventSpan;
    void deliver_pushed_events();
    void dispatch_to_watchers(const Event& event,
                              std::string_view text);
    void remove_event_watcher(std::uint64_t identifier) noexcept;

private:
    std::string m_application_name;
    std::thread::id m_thread;
    EventQueue m_queue;
    std::mutex m_pushed_mutex;
    EventQueue m_pushed;
    EventQueue m_transfer;
    std::vector<std::move_only_function<void()>> m_deferred;
    std::vector<Watcher> m_watchers;
    std::vector<std::uint64_t> m_removed_watchers;
    std::uint64_t m_next_watcher = 1;
    bool m_pumping = false;
    bool m_dispatching = false;
    std::shared_ptr<Platform* const> m_lifetime;
};

// ------------------------------------------------------------------------------------------------- Platform

Platform::Platform(const PlatformDescription& description)
    : PlatformObject{description.validation_level}
    , m_application_name(description.application_name)
    , m_thread{std::this_thread::get_id()}
    , m_queue{description.event_queue_capacity, description.event_text_capacity}
    , m_pushed{64, 1024}
    , m_transfer{64, 1024}
    , m_lifetime{std::make_shared<Platform* const>(this)}
{
}

Platform::~Platform()
{
    // Watchers still registered detach themselves; remaining releases run now.
    m_lifetime.reset();
    release_deferred();
}

auto
Platform::poll_events() -> EventSpan
{
    return run_pump(EventWaitMode::POLL, std::chrono::nanoseconds::zero(), "Platform::poll_events");
}

auto
Platform::wait_events() -> EventSpan
{
    return run_pump(EventWaitMode::WAIT, WAIT_INDEFINITELY, "Platform::wait_events");
}

auto
Platform::wait_events_timeout(std::chrono::nanoseconds timeout) -> EventSpan
{
    return run_pump(EventWaitMode::WAIT_WITH_TIMEOUT, std::max(timeout, std::chrono::nanoseconds::zero()), "Platform::wait_events_timeout");
}

auto
Platform::events() const noexcept -> EventSpan
{
    return m_queue.view();
}

auto
Platform::add_event_watcher(EventWatcherCallback callback) -> EventWatcher
{
    require_platform_thread("Platform::add_event_watcher");
    const std::uint64_t identifier = m_next_watcher++;
    m_watchers.push_back({.identifier = identifier, .callback = std::move(callback)});
    return EventWatcher{m_lifetime, identifier};
}

void
Platform::push_event(const Event& event,
                     std::string_view text)
{
    // Queue the event (and its text) for the platform thread's next pump.
    {
        const std::scoped_lock lock{m_pushed_mutex};
        if (event.text())
        {
            m_pushed.push(event, text);
        }
        else
        {
            m_pushed.push(event);
        }
    }

    // A pump waiting for events returns to deliver it.
    wake_backend();
}

void
Platform::wake()
{
    wake_backend();
}

auto
Platform::thread() const noexcept -> std::thread::id
{
    return m_thread;
}

auto
Platform::is_platform_thread() const noexcept -> bool
{
    return std::this_thread::get_id() == m_thread;
}

auto
Platform::application_name() const noexcept -> std::string_view
{
    return m_application_name;
}

void
Platform::defer_release(HandleKey /*key*/,
                        std::move_only_function<void()> release)
{
    m_deferred.push_back(std::move(release));
}

void
Platform::emit(const Event& event)
{
    // Events of destroyed windows are dropped.
    if (event.window().is_valid() && !is_window_alive(event.window()))
    {
        return;
    }
    m_queue.push(event);
    dispatch_to_watchers(event, {});
}

void
Platform::emit(const Event& event,
               std::string_view text)
{
    // Events of destroyed windows are dropped.
    if (event.window().is_valid() && !is_window_alive(event.window()))
    {
        return;
    }
    m_queue.push(event, text);
    dispatch_to_watchers(event, text);
}

void
Platform::require_platform_thread(std::string_view operation,
                                  std::source_location location) const
{
    if (validates(ValidationLevel::CREATION) && !is_platform_thread())
    {
        throw InvalidUsageError(
            {
                .backend = backend(),
                .operation = std::string{operation},
                .problem = "called from another thread than the platform thread (the thread that created the platform)",
                .suggestion = "pump events and call window, cursor, monitor, clipboard and device functions on the platform "
                              "thread; other threads may call push_event() and wake(), and read a window's sizes, scale "
                              "and native handle",
            },
            location);
    }
}

void
Platform::release_deferred() noexcept
{
    // Releases may defer more releases: run until none is left.
    while (!m_deferred.empty())
    {
        std::vector<std::move_only_function<void()>> releases = std::exchange(m_deferred, {});
        for (std::move_only_function<void()>& release : releases)
        {
            try
            {
                release();
            }
            catch (...)
            {
                // Destruction never throws; a failed native release only leaks.
            }
        }
    }
}

auto
Platform::run_pump(EventWaitMode mode,
                   std::chrono::nanoseconds timeout,
                   std::string_view operation) -> EventSpan
{
    // Pumps run on the platform thread, never from inside another pump (an event watcher).
    require_platform_thread(operation);
    if (m_pumping)
    {
        throw InvalidUsageError({
            .backend = backend(),
            .operation = std::string{operation},
            .problem = "events are being pumped already (called from an event watcher)",
            .suggestion = "handle the event in the watcher, and pump events from the application's loop only",
        });
    }
    m_pumping = true;
    struct PumpingGuard
    {
        bool& pumping;
        ~PumpingGuard()
        {
            pumping = false;
        }
    } pumping_guard{m_pumping};

    // Windows and cursors destroyed since the last pump release their native objects now.
    release_deferred();

    // A new set of events, starting with those other threads pushed.
    m_queue.clear();
    deliver_pushed_events();

    // Do not block when events are already waiting.
    if (!m_queue.empty())
    {
        mode = EventWaitMode::POLL;
    }
    pump(mode, timeout);

    // Events pushed while the backend waited (their push woke it).
    deliver_pushed_events();

    // Events of windows a watcher destroyed during this pump are dropped too.
    m_queue.remove_if([this](const Event& event)
    {
        return event.window().is_valid() && !is_window_alive(event.window());
    });
    return m_queue.view();
}

void
Platform::deliver_pushed_events()
{
    // Take the pushed events under the lock, emit them outside it (a watcher may push more).
    {
        const std::scoped_lock lock{m_pushed_mutex};
        std::swap(m_pushed, m_transfer);
    }
    const EventSpan pushed = m_transfer.view();
    for (const Event& event : pushed)
    {
        if (event.text())
        {
            emit(event, pushed.text(event));
        }
        else
        {
            emit(event);
        }
    }
    m_transfer.clear();
}

void
Platform::dispatch_to_watchers(const Event& event,
                               std::string_view text)
{
    if (m_watchers.empty())
    {
        return;
    }

    // Watchers added meanwhile see the next events; watchers removed meanwhile are skipped.
    m_dispatching = true;
    const std::size_t count = m_watchers.size();
    try
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!std::ranges::contains(m_removed_watchers, m_watchers[index].identifier))
            {
                m_watchers[index].callback(event, text);
            }
        }
    }
    catch (...)
    {
        m_dispatching = false;
        throw;
    }
    m_dispatching = false;

    // Remove the watchers unregistered during the dispatch.
    for (const std::uint64_t identifier : m_removed_watchers)
    {
        std::erase_if(m_watchers, [identifier](const Watcher& watcher)
        {
            return watcher.identifier == identifier;
        });
    }
    m_removed_watchers.clear();
}

void
Platform::remove_event_watcher(std::uint64_t identifier) noexcept
{
    // During a dispatch the watcher is only marked; it is removed when the dispatch ends.
    if (m_dispatching)
    {
        m_removed_watchers.push_back(identifier);
        return;
    }
    std::erase_if(m_watchers, [identifier](const Watcher& watcher)
    {
        return watcher.identifier == identifier;
    });
}

// ------------------------------------------------------------------------------------------------- Default definitions

auto
Platform::create_window(const WindowDescription& /*description*/) -> std::unique_ptr<Window>
{
    throw_not_implemented("Platform::create_window");
}

auto
Platform::create_cursor(CursorShape /*shape*/) -> std::unique_ptr<Cursor>
{
    throw_not_implemented("Platform::create_cursor");
}

auto
Platform::create_cursor(const CursorImageDescription& /*description*/) -> std::unique_ptr<Cursor>
{
    throw_not_implemented("Platform::create_cursor");
}

auto
Platform::monitors() const -> std::vector<MonitorHandle>
{
    throw_not_implemented("Platform::monitors");
}

auto
Platform::primary_monitor() const -> MonitorHandle
{
    throw_not_implemented("Platform::primary_monitor");
}

auto
Platform::monitor_properties(MonitorHandle /*monitor*/) const -> MonitorProperties
{
    throw_not_implemented("Platform::monitor_properties");
}

auto
Platform::video_modes(MonitorHandle /*monitor*/) const -> std::vector<VideoMode>
{
    throw_not_implemented("Platform::video_modes");
}

auto
Platform::clipboard_text() const -> std::string
{
    throw_not_implemented("Platform::clipboard_text");
}

void
Platform::set_clipboard_text(std::string_view /*text*/)
{
    throw_not_implemented("Platform::set_clipboard_text");
}

auto
Platform::clipboard_image() const -> DecodedImage
{
    throw_not_implemented("Platform::clipboard_image");
}

void
Platform::set_clipboard_image(const ImageData& /*image*/)
{
    throw_not_implemented("Platform::set_clipboard_image");
}

void
Platform::pump(EventWaitMode /*mode*/,
               std::chrono::nanoseconds /*timeout*/)
{
    throw_not_implemented("Platform::pump");
}

// ------------------------------------------------------------------------------------------------- EventWatcher

EventWatcher::EventWatcher(std::weak_ptr<Platform* const> platform,
                           std::uint64_t identifier) noexcept
    : m_platform{std::move(platform)}
    , m_identifier{identifier}
{
}

EventWatcher::~EventWatcher()
{
    reset();
}

EventWatcher::EventWatcher(EventWatcher&& other) noexcept
    : m_platform{std::move(other.m_platform)}
    , m_identifier{std::exchange(other.m_identifier, 0)}
{
}

auto
EventWatcher::operator=(EventWatcher&& other) noexcept -> EventWatcher&
{
    if (this != &other)
    {
        reset();
        m_platform = std::move(other.m_platform);
        m_identifier = std::exchange(other.m_identifier, 0);
    }
    return *this;
}

auto
EventWatcher::is_active() const noexcept -> bool
{
    return m_identifier != 0 && !m_platform.expired();
}

void
EventWatcher::reset() noexcept
{
    // Unregister from the platform, if it still exists.
    if (m_identifier != 0)
    {
        if (const std::shared_ptr<Platform* const> platform = m_platform.lock())
        {
            (*platform)->remove_event_watcher(m_identifier);
        }
        m_identifier = 0;
        m_platform.reset();
    }
}

} // namespace gfx::platform
