/// @file semaphore.cppm
/// @brief Semaphore: binary GPU-to-GPU signal, used with swapchains (acquire and present).
export module gfx.interface:semaphore;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a Semaphore.
struct SemaphoreDescription
{
    std::string_view debug_name{}; ///< Name shown by debugging tools.
};

/// @brief Binary semaphore: signaled once, waited on once. Prefer TimelineSemaphore for every other
///        cross-queue synchronization.
class Semaphore : public BackendObject
{
public:
    /// @brief Handle naming this semaphore in submissions and presentation.
    [[nodiscard]] virtual auto handle() const noexcept -> SemaphoreHandle = 0;
};

} // namespace gfx
