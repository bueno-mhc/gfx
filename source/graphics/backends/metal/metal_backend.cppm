/// @file metal_backend.cppm
/// @brief Module gfx.backends.metal: the Metal backend, not implemented yet. It is built so that
///        requesting it reports exactly that, instead of "unknown backend".
export module gfx.backends.metal;

import std;
import gfx.interface;

export namespace gfx::metal
{

/// @brief Why the Metal backend cannot run: it is not implemented yet.
[[nodiscard]] auto unavailable_reason() -> std::string
{
    return "the Metal backend is not implemented yet";
}

} // namespace gfx::metal
