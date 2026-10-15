#pragma once
/// @file renderdoc_capture.hpp
/// @brief RenderDoc's in-application API, behind plain functions. The implementation needs <windows.h>,
///        which cannot share a translation unit with modules (see
///        CompilationFailure.WindowsHeadersConflictWithModules), so it lives in a translation unit of its own.

namespace gfx::vulkan::renderdoc
{

/// @brief Whether RenderDoc is injected into the process (its API can be reached).
[[nodiscard]] bool
is_attached() noexcept;

/// @brief Starts capturing the work of every device of the Vulkan instance @p instance (a VkInstance).
/// @param title Title of the capture (may be null).
/// @return Whether RenderDoc started the capture.
[[nodiscard]] bool
start_capture(void* instance, const char* title) noexcept;

/// @brief Ends the capture started by start_capture() and writes it.
/// @return Whether RenderDoc wrote a capture.
[[nodiscard]] bool
end_capture(void* instance) noexcept;

} // namespace gfx::vulkan::renderdoc
