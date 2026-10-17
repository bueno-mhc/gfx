/// @file errors.cppm
/// @brief The errors of the UI library: the core error hierarchy (gfx.core.errors, re-exported here),
///        with the UI subsystem as the backend that raised them. A UiCapabilityError says which
///        subsystem cannot do what, why, and what to do instead: "[gfx::ui::FONT] FontSource::open:
///        WOFF2 font 'x.woff2' ...". Per-frame functions never throw (they report problems in the
///        frame report); construction and tooling functions do.
export module gfx.ui.errors;

import std;
export import gfx.core.errors;
import gfx.ui.enums;

export namespace gfx::ui
{

/// @brief Thrown when a UI subsystem cannot honor an operation: the core BackendCapabilityError, with a
///        UiSubsystem as its backend (`error.backend() == UiSubsystem::FONT`), so one `catch` serves
///        every library of gfx.
using UiCapabilityError = BackendCapabilityError;

/// @brief Throws the UiCapabilityError of @p subsystem.
/// @param subsystem  Subsystem that cannot honor the operation.
/// @param operation  Operation that was requested, e.g. "FontSource::open".
/// @param capability What is missing, e.g. "WOFF2 decoding".
/// @param reason     Why the subsystem cannot provide it.
/// @param suggestion What the caller can do instead.
/// @param location   Where the error is raised (defaults to the caller).
[[noreturn]] inline void
throw_unsupported(UiSubsystem subsystem,
                  std::string_view operation,
                  std::string_view capability,
                  std::string reason,
                  std::string suggestion,
                  std::source_location location = std::source_location::current())
{
    throw UiCapabilityError(
        {
            .backend = subsystem,
            .operation = std::string{operation},
            .capability = std::string{capability},
            .reason = std::move(reason),
            .suggestion = std::move(suggestion),
        },
        location);
}

} // namespace gfx::ui
