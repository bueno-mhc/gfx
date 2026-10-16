/// @file window.cppm
/// @brief Window: a native window, its description and its validation. The values a render thread
///        needs to size a swapchain (size, framebuffer size, content scale, minimized state, native
///        handle) are readable from any thread; every other operation runs on the platform thread.
export module gfx.platform.interface:window;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import :platform_object;
import :image;
import :monitor;
import :event;

export namespace gfx::platform
{

/// @brief Width-to-height ratio a window keeps when resized (e.g. 16:9).
struct AspectRatio
{
    std::uint32_t numerator = 0;   ///< Width part.
    std::uint32_t denominator = 0; ///< Height part.

    constexpr bool operator==(const AspectRatio&) const = default;
};

/// @brief Where a fullscreen window goes.
struct FullscreenDescription
{
    MonitorHandle monitor{};               ///< Monitor (null = the monitor the window is on).
    std::optional<VideoMode> video_mode{}; ///< EXCLUSIVE_FULLSCREEN: video mode (std::nullopt = the monitor's current mode).
};

/// @brief How to create a Window. Validated at creation (validate_window_description()).
struct WindowDescription
{
    std::string_view title = "gfx";                                              ///< Title (UTF-8).
    Extent size{.width = 1280, .height = 720, .depth = 1};                       ///< Size in window coordinates.
    std::optional<Offset> position{};                                            ///< Position (std::nullopt = centered).
    std::optional<Extent> minimum_size{};                                        ///< Smallest size the user can resize to.
    std::optional<Extent> maximum_size{};                                        ///< Largest size the user can resize to.
    std::optional<AspectRatio> aspect_ratio{};                                   ///< Ratio kept when resizing.
    WindowMode mode = WindowMode::WINDOWED;                                      ///< How it occupies the screen.
    WindowFlag flags = WindowFlag::RESIZABLE | WindowFlag::HIGH_PIXEL_DENSITY | WindowFlag::FOCUS_ON_SHOW; ///< Options.
    FullscreenDescription fullscreen{};                                          ///< Fullscreen modes: monitor and video mode.
    SurfaceApi surface_api = SurfaceApi::NONE;                                   ///< Graphics API that will present to it.
    std::span<const ImageData> icon{};                                           ///< Icon, in one or more resolutions.
};

/// @brief How to start text input (Window::start_text_input()).
struct TextInputDescription
{
    Rectangle area{};                ///< Where the text is edited, in window coordinates (IME candidate placement).
    std::int32_t cursor = 0;         ///< Cursor offset inside the area, in window coordinates.
    bool composition_events = false; ///< The application draws IME compositions itself (TEXT_COMPOSITION events);
                                     ///< false lets the operating system show them.
};

/// @brief Checks @p description: sizes, limits, aspect ratio, title, fullscreen settings, and that
///        @p backend honors every flag (@p supported_flags). Backends call it at
///        ValidationLevel::CREATION and above.
/// @throws InvalidUsageError for an inconsistent description, BackendCapabilityError naming every
///         flag the backend cannot honor.
void
validate_window_description(const WindowDescription& description,
                            WindowFlag supported_flags,
                            BackendIdentity backend);

/// @brief A native window. Owned through std::unique_ptr; destroying it closes the window (its
///        native resources are released at the next event pump, never inside a backend callback).
class Window : public PlatformObject
{
public:
    // ----------------------------------------------------------------------------------------- any thread

    /// @brief Size in window coordinates (thread-safe).
    [[nodiscard]] auto size() const noexcept -> Extent
    {
        return unpack(m_size.load(std::memory_order_acquire));
    }

    /// @brief Size in pixels: what a swapchain uses (thread-safe).
    [[nodiscard]] auto framebuffer_size() const noexcept -> Extent
    {
        return unpack(m_framebuffer_size.load(std::memory_order_acquire));
    }

    /// @brief UI scale of the monitor the window is on, e.g. 1.5 (thread-safe).
    [[nodiscard]] auto content_scale() const noexcept -> float
    {
        return m_content_scale.load(std::memory_order_acquire);
    }

    /// @brief Pixels per window coordinate: framebuffer size / size (thread-safe; 1 when minimized).
    [[nodiscard]] auto pixel_density() const noexcept -> float
    {
        const Extent logical = size();
        const Extent pixels = framebuffer_size();
        return logical.width == 0 ? 1.0F : static_cast<float>(pixels.width) / static_cast<float>(logical.width);
    }

    /// @brief Whether the window is minimized: a swapchain should not present then (thread-safe).
    [[nodiscard]] auto is_minimized() const noexcept -> bool
    {
        return m_minimized.load(std::memory_order_acquire);
    }

    /// @brief Operating-system handles, for the graphics library to create a surface (thread-safe).
    [[nodiscard]] auto native_handle() const noexcept -> NativeWindowHandle
    {
        return m_native_handle;
    }

    /// @brief Handle naming this window in events.
    [[nodiscard]] virtual auto handle() const noexcept -> WindowHandle = 0;

    // ----------------------------------------------------------------------------------------- platform thread

    /// @brief Title (UTF-8).
    [[nodiscard]] virtual auto title() const -> std::string = 0;

    /// @brief Changes the title (UTF-8).
    virtual void set_title(std::string_view title) = 0;

    /// @brief Changes the icon; several resolutions let the system pick (PlatformFeature::WINDOW_ICON).
    virtual void set_icon(std::span<const ImageData> images) = 0;

    /// @brief Resizes the window (window coordinates).
    virtual void set_size(Extent size) = 0;

    /// @brief Position on the desktop (PlatformFeature::WINDOW_POSITION).
    [[nodiscard]] virtual auto position() const -> Offset = 0;

    /// @brief Moves the window (PlatformFeature::WINDOW_POSITION).
    virtual void set_position(Offset position) = 0;

    /// @brief Sets the size limits the user can resize to (std::nullopt = no limit).
    virtual void set_size_limits(std::optional<Extent> minimum_size,
                                 std::optional<Extent> maximum_size) = 0;

    /// @brief Keeps a width-to-height ratio when resizing (std::nullopt = free).
    virtual void set_aspect_ratio(std::optional<AspectRatio> aspect_ratio) = 0;

    /// @brief Allows or forbids resizing by the user.
    virtual void set_resizable(bool resizable) = 0;

    /// @brief How the window occupies the screen.
    [[nodiscard]] virtual auto mode() const -> WindowMode = 0;

    /// @brief Changes how the window occupies the screen.
    virtual void set_mode(WindowMode mode,
                          const FullscreenDescription& fullscreen) = 0;

    /// @brief Shows the window.
    virtual void show() = 0;

    /// @brief Hides the window.
    virtual void hide() = 0;

    /// @brief Minimizes the window.
    virtual void minimize() = 0;

    /// @brief Maximizes the window.
    virtual void maximize() = 0;

    /// @brief Leaves the minimized or maximized state.
    virtual void restore() = 0;

    /// @brief Gives the window the input focus and raises it.
    virtual void focus() = 0;

    /// @brief Whether the window is shown.
    [[nodiscard]] virtual auto is_visible() const -> bool = 0;

    /// @brief Whether the window has the keyboard focus.
    [[nodiscard]] virtual auto is_focused() const -> bool = 0;

    /// @brief Whether the mouse is over the window (PlatformFeature::WINDOW_HOVER).
    [[nodiscard]] virtual auto is_hovered() const -> bool = 0;

    /// @brief Whether the window is maximized.
    [[nodiscard]] virtual auto is_maximized() const -> bool = 0;

    /// @brief Whether the window is fully covered (PlatformFeature::WINDOW_OCCLUSION).
    [[nodiscard]] virtual auto is_occluded() const -> bool = 0;

    /// @brief Monitor showing most of the window.
    [[nodiscard]] virtual auto monitor() const -> MonitorHandle = 0;

    /// @brief Shows @p cursor over the window (null handle = the default arrow).
    virtual void set_cursor(CursorHandle cursor) = 0;

    /// @brief How the window treats the cursor (CursorMode::CAPTURED for camera controls).
    virtual void set_cursor_mode(CursorMode mode) = 0;

    /// @brief How the window treats the cursor.
    [[nodiscard]] virtual auto cursor_mode() const -> CursorMode = 0;

    /// @brief Cursor position in window coordinates.
    [[nodiscard]] virtual auto cursor_position() const -> Vector2 = 0;

    /// @brief Moves the cursor (window coordinates).
    virtual void set_cursor_position(Vector2 position) = 0;

    /// @brief Starts text input: TEXT_INPUT events (and TEXT_COMPOSITION events when requested) arrive
    ///        for this window, and the system's on-screen keyboard or IME may appear.
    virtual void start_text_input(const TextInputDescription& description) = 0;

    /// @brief Stops text input.
    virtual void stop_text_input() = 0;

    /// @brief Whether text input is active for this window.
    [[nodiscard]] virtual auto is_text_input_active() const -> bool = 0;

protected:
    using PlatformObject::PlatformObject;

    /// @brief Backends record the native handles once, at creation.
    void set_native_handle(NativeWindowHandle handle) noexcept
    {
        m_native_handle = handle;
    }

    /// @brief Backends keep the sizes current (at creation and when the pump sees a resize).
    void update_size(Extent size,
                     Extent framebuffer_size) noexcept
    {
        m_size.store(pack(size), std::memory_order_release);
        m_framebuffer_size.store(pack(framebuffer_size), std::memory_order_release);
    }

    /// @brief Backends keep the content scale current.
    void update_content_scale(float scale) noexcept
    {
        m_content_scale.store(scale, std::memory_order_release);
    }

    /// @brief Backends keep the minimized state current.
    void update_minimized(bool minimized) noexcept
    {
        m_minimized.store(minimized, std::memory_order_release);
    }

private:
    [[nodiscard]] static constexpr auto pack(Extent extent) noexcept -> std::uint64_t
    {
        return (std::uint64_t{extent.width} << 32U) | extent.height;
    }

    [[nodiscard]] static constexpr auto unpack(std::uint64_t value) noexcept -> Extent
    {
        return Extent{.width = static_cast<std::uint32_t>(value >> 32U), .height = static_cast<std::uint32_t>(value), .depth = 1};
    }

private:
    std::atomic<std::uint64_t> m_size{0};
    std::atomic<std::uint64_t> m_framebuffer_size{0};
    std::atomic<float> m_content_scale{1.0F};
    std::atomic<bool> m_minimized{false};
    NativeWindowHandle m_native_handle{};
};

// ------------------------------------------------------------------------------------------------- Default definitions

auto
Window::title() const -> std::string
{
    throw_not_implemented("Window::title");
}

void
Window::set_title(std::string_view /*title*/)
{
    throw_not_implemented("Window::set_title");
}

void
Window::set_icon(std::span<const ImageData> /*images*/)
{
    throw_not_implemented("Window::set_icon");
}

void
Window::set_size(Extent /*size*/)
{
    throw_not_implemented("Window::set_size");
}

auto
Window::position() const -> Offset
{
    throw_not_implemented("Window::position");
}

void
Window::set_position(Offset /*position*/)
{
    throw_not_implemented("Window::set_position");
}

void
Window::set_size_limits(std::optional<Extent> /*minimum_size*/,
                        std::optional<Extent> /*maximum_size*/)
{
    throw_not_implemented("Window::set_size_limits");
}

void
Window::set_aspect_ratio(std::optional<AspectRatio> /*aspect_ratio*/)
{
    throw_not_implemented("Window::set_aspect_ratio");
}

void
Window::set_resizable(bool /*resizable*/)
{
    throw_not_implemented("Window::set_resizable");
}

auto
Window::mode() const -> WindowMode
{
    throw_not_implemented("Window::mode");
}

void
Window::set_mode(WindowMode /*mode*/,
                 const FullscreenDescription& /*fullscreen*/)
{
    throw_not_implemented("Window::set_mode");
}

void
Window::show()
{
    throw_not_implemented("Window::show");
}

void
Window::hide()
{
    throw_not_implemented("Window::hide");
}

void
Window::minimize()
{
    throw_not_implemented("Window::minimize");
}

void
Window::maximize()
{
    throw_not_implemented("Window::maximize");
}

void
Window::restore()
{
    throw_not_implemented("Window::restore");
}

void
Window::focus()
{
    throw_not_implemented("Window::focus");
}

auto
Window::is_visible() const -> bool
{
    throw_not_implemented("Window::is_visible");
}

auto
Window::is_focused() const -> bool
{
    throw_not_implemented("Window::is_focused");
}

auto
Window::is_hovered() const -> bool
{
    throw_not_implemented("Window::is_hovered");
}

auto
Window::is_maximized() const -> bool
{
    throw_not_implemented("Window::is_maximized");
}

auto
Window::is_occluded() const -> bool
{
    throw_not_implemented("Window::is_occluded");
}

auto
Window::monitor() const -> MonitorHandle
{
    throw_not_implemented("Window::monitor");
}

void
Window::set_cursor(CursorHandle /*cursor*/)
{
    throw_not_implemented("Window::set_cursor");
}

void
Window::set_cursor_mode(CursorMode /*mode*/)
{
    throw_not_implemented("Window::set_cursor_mode");
}

auto
Window::cursor_mode() const -> CursorMode
{
    throw_not_implemented("Window::cursor_mode");
}

auto
Window::cursor_position() const -> Vector2
{
    throw_not_implemented("Window::cursor_position");
}

void
Window::set_cursor_position(Vector2 /*position*/)
{
    throw_not_implemented("Window::set_cursor_position");
}

void
Window::start_text_input(const TextInputDescription& /*description*/)
{
    throw_not_implemented("Window::start_text_input");
}

void
Window::stop_text_input()
{
    throw_not_implemented("Window::stop_text_input");
}

auto
Window::is_text_input_active() const -> bool
{
    throw_not_implemented("Window::is_text_input_active");
}

// ------------------------------------------------------------------------------------------------- Validation

void
validate_window_description(const WindowDescription& description,
                            WindowFlag supported_flags,
                            BackendIdentity backend)
{
    static constexpr std::string_view OPERATION = "Platform::create_window";
    const auto reject = [backend](std::string problem, std::string suggestion)
    {
        throw InvalidUsageError({
            .backend = backend,
            .operation = std::string{OPERATION},
            .problem = std::move(problem),
            .suggestion = std::move(suggestion),
        });
    };

    // The title is text: well-formed UTF-8.
    if (!is_valid_utf8(description.title))
    {
        reject("the title is not valid UTF-8", "pass UTF-8 text");
    }

    // Windowed modes need a size; fullscreen modes take the monitor's.
    const bool windowed = description.mode == WindowMode::WINDOWED || description.mode == WindowMode::BORDERLESS;
    if (windowed && (description.size.width == 0 || description.size.height == 0))
    {
        reject(std::format("the size {}x{} is empty", description.size.width, description.size.height),
               "give the window a width and a height");
    }

    // Limits: minimum below maximum, the size between them.
    if (description.minimum_size && description.maximum_size
        && (description.minimum_size->width > description.maximum_size->width
            || description.minimum_size->height > description.maximum_size->height))
    {
        reject(std::format("the minimum size {}x{} exceeds the maximum size {}x{}", description.minimum_size->width,
                           description.minimum_size->height, description.maximum_size->width, description.maximum_size->height),
               "swap the limits or remove one");
    }
    if (windowed && description.minimum_size
        && (description.size.width < description.minimum_size->width || description.size.height < description.minimum_size->height))
    {
        reject(std::format("the size {}x{} is below the minimum size {}x{}", description.size.width, description.size.height,
                           description.minimum_size->width, description.minimum_size->height),
               "create the window at least as large as its minimum size");
    }
    if (windowed && description.maximum_size
        && (description.size.width > description.maximum_size->width || description.size.height > description.maximum_size->height))
    {
        reject(std::format("the size {}x{} is above the maximum size {}x{}", description.size.width, description.size.height,
                           description.maximum_size->width, description.maximum_size->height),
               "create the window at most as large as its maximum size");
    }

    // An aspect ratio has two non-zero terms.
    if (description.aspect_ratio && (description.aspect_ratio->numerator == 0 || description.aspect_ratio->denominator == 0))
    {
        reject(std::format("the aspect ratio {}:{} has a zero term", description.aspect_ratio->numerator,
                           description.aspect_ratio->denominator),
               "use positive terms, e.g. 16:9, or no aspect ratio");
    }

    // A video mode only makes sense in exclusive fullscreen.
    if (description.fullscreen.video_mode && description.mode != WindowMode::EXCLUSIVE_FULLSCREEN)
    {
        reject(std::format("a video mode is given for a {} window", description.mode),
               "set mode to WindowMode::EXCLUSIVE_FULLSCREEN, or remove the video mode");
    }

    // Every flag must be one the backend honors.
    const WindowFlag unsupported = description.flags & ~supported_flags;
    if (!is_empty(unsupported))
    {
        throw BackendCapabilityError({
            .backend = backend,
            .operation = std::string{OPERATION},
            .capability = std::format("window flags {}", unsupported),
            .reason = std::format("the {} backend cannot honor {}", backend.name(), unsupported),
            .suggestion = std::format("remove {} from WindowDescription::flags (supported: {})", unsupported, supported_flags),
        });
    }
}

} // namespace gfx::platform
