/// @file monitor.cppm
/// @brief Monitors and their video modes. Monitors belong to the Platform (Platform::monitors()); the
///        application holds MonitorHandles, which become stale when a monitor is disconnected
///        (MONITOR_DISCONNECTED).
export module gfx.platform.interface:monitor;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;

export namespace gfx::platform
{

/// @brief A resolution and refresh rate a monitor can display.
struct VideoMode
{
    Extent size{};              ///< Resolution in pixels.
    float refresh_rate = 0.0F;  ///< Refresh rate in hertz (0 when unknown).
    float pixel_density = 1.0F; ///< Pixels per window coordinate (above 1 on high-density modes).

    constexpr bool operator==(const VideoMode&) const = default;
};

/// @brief What a monitor is and how it is set up.
struct MonitorProperties
{
    MonitorHandle handle{};      ///< The monitor.
    std::string name{};          ///< Human-readable name (UTF-8).
    Rectangle bounds{};          ///< Area on the desktop, in window coordinates.
    Rectangle work_area{};       ///< Bounds minus task bars and docks.
    float content_scale = 1.0F;  ///< UI scale the user chose for the monitor (e.g. 1.5).
    VideoMode current_mode{};    ///< Video mode in use.
    bool primary = false;        ///< The primary monitor.
};

} // namespace gfx::platform
