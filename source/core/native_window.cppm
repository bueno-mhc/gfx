/// @file native_window.cppm
/// @brief NativeWindowHandle: the operating-system handles of a window, which the platform library
///        exports and the graphics library consumes to create surfaces. Handles are integers (no
///        pointer types and no platform header reach the public API), and only operating-system
///        handles are carried: graphics backends never need GLFW or SDL objects.
export module gfx.core.native_window;

import std;
import gfx.core.enums;

export namespace gfx
{

/// @brief A Win32 window: what vkCreateWin32SurfaceKHR and DXGI need.
struct Win32WindowHandle
{
    std::uintptr_t instance = 0; ///< HINSTANCE of the module that registered the window class.
    std::uintptr_t window = 0;   ///< HWND.

    constexpr bool operator==(const Win32WindowHandle&) const = default;
};

/// @brief An X11 window reached through Xlib: what vkCreateXlibSurfaceKHR needs.
struct XlibWindowHandle
{
    std::uintptr_t display = 0; ///< Display*.
    std::uintptr_t window = 0;  ///< Window (an XID).

    constexpr bool operator==(const XlibWindowHandle&) const = default;
};

/// @brief An X11 window reached through XCB: what vkCreateXcbSurfaceKHR needs.
struct XcbWindowHandle
{
    std::uintptr_t connection = 0; ///< xcb_connection_t*.
    std::uint32_t window = 0;      ///< xcb_window_t.

    constexpr bool operator==(const XcbWindowHandle&) const = default;
};

/// @brief A Wayland surface: what vkCreateWaylandSurfaceKHR needs.
struct WaylandWindowHandle
{
    std::uintptr_t display = 0; ///< wl_display*.
    std::uintptr_t surface = 0; ///< wl_surface*.

    constexpr bool operator==(const WaylandWindowHandle&) const = default;
};

/// @brief A macOS window, with the Metal layer the platform backend attached to its view: what
///        vkCreateMetalSurfaceEXT and Metal need.
struct CocoaWindowHandle
{
    std::uintptr_t window = 0;      ///< NSWindow*.
    std::uintptr_t metal_layer = 0; ///< CAMetalLayer*.

    constexpr bool operator==(const CocoaWindowHandle&) const = default;
};

/// @brief An HTML canvas, named by a CSS selector (e.g. "#canvas"): what WebGPU needs.
struct WebCanvasHandle
{
    std::array<char, 64> selector{}; ///< NUL-terminated CSS selector.

    constexpr bool operator==(const WebCanvasHandle&) const = default;
};

/// @brief A Win32, Xlib, XCB, Wayland, Cocoa or web window handle.
template<class Type>
concept NativeWindowAlternative = std::same_as<Type, Win32WindowHandle> || std::same_as<Type, XlibWindowHandle>
                                  || std::same_as<Type, XcbWindowHandle> || std::same_as<Type, WaylandWindowHandle>
                                  || std::same_as<Type, CocoaWindowHandle> || std::same_as<Type, WebCanvasHandle>;

/// @brief The operating-system handles of a window, for graphics backends to create surfaces: one of
///        the window handle alternatives, or none.
///
/// @code
/// if (const auto* win32 = window.get_if<gfx::Win32WindowHandle>())
/// {
///     create_win32_surface(win32->instance, win32->window);
/// }
/// @endcode
class NativeWindowHandle
{
public:
    /// @brief The alternatives, in NativeWindowKind order (std::monostate = NONE).
    using Alternative = std::variant<std::monostate, Win32WindowHandle, XlibWindowHandle, XcbWindowHandle,
                                     WaylandWindowHandle, CocoaWindowHandle, WebCanvasHandle>;

    /// @brief No window (NativeWindowKind::NONE).
    constexpr NativeWindowHandle() noexcept = default;

    /// @brief The window @p handle.
    template<NativeWindowAlternative Handle>
    constexpr explicit NativeWindowHandle(const Handle& handle) noexcept
        : m_handle{handle}
    {
    }

    /// @brief Windowing system of the handle.
    [[nodiscard]] constexpr auto kind() const noexcept -> NativeWindowKind
    {
        // The alternatives are declared in NativeWindowKind order.
        return static_cast<NativeWindowKind>(m_handle.index());
    }

    /// @brief Whether the handle names a window.
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return kind() != NativeWindowKind::NONE;
    }

    /// @brief The alternative @p Handle, or null when the handle holds another one.
    template<NativeWindowAlternative Handle>
    [[nodiscard]] constexpr auto get_if() const noexcept -> const Handle*
    {
        return std::get_if<Handle>(&m_handle);
    }

    /// @brief Calls @p visitor with the alternative held (std::monostate when none).
    template<class Visitor>
    constexpr auto visit(Visitor&& visitor) const -> decltype(auto)
    {
        return std::visit(std::forward<Visitor>(visitor), m_handle);
    }

    /// @brief Same window.
    constexpr bool operator==(const NativeWindowHandle&) const = default;

private:
    Alternative m_handle{};
};

static_assert(std::variant_size_v<NativeWindowHandle::Alternative> == ENUMERATOR_COUNT<NativeWindowKind>,
              "one NativeWindowHandle alternative per NativeWindowKind, in the same order");
static_assert(std::is_trivially_copyable_v<NativeWindowHandle>);

} // namespace gfx
