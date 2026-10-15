/// @file direct3d12_backend.cppm
/// @brief Module gfx.backends.direct3d12: the Direct3D 12 backend, not implemented yet. It is built so
///        that requesting it reports exactly that, instead of "unknown backend".
export module gfx.backends.direct3d12;

import std;
import gfx.interface;

export namespace gfx::direct3d12
{

/// @brief Why the Direct3D 12 backend cannot run: it is not implemented yet.
[[nodiscard]] auto unavailable_reason() -> std::string
{
    return "the Direct3D 12 backend is not implemented yet";
}

} // namespace gfx::direct3d12
