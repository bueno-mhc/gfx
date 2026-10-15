/// @file cursor.cppm
/// @brief Cursor: a standard or custom mouse cursor a window can show (Window::set_cursor()).
export module gfx.platform.interface:cursor;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import :platform_object;
import :image;

export namespace gfx::platform
{

/// @brief How to create a custom cursor.
struct CursorImageDescription
{
    ImageData image{};  ///< Cursor image (RGBA8 or PNG).
    Offset hot_spot{};  ///< Pixel of the image that points (from the top-left corner).
};

/// @brief A mouse cursor. Destroying a cursor a window shows makes the window show the default cursor.
class Cursor : public PlatformObject
{
public:
    /// @brief Handle naming this cursor in Window::set_cursor().
    [[nodiscard]] virtual auto handle() const noexcept -> CursorHandle = 0;

    /// @brief The standard shape, or std::nullopt for a custom cursor.
    [[nodiscard]] virtual auto shape() const noexcept -> std::optional<CursorShape> = 0;

protected:
    using PlatformObject::PlatformObject;
};

} // namespace gfx::platform
