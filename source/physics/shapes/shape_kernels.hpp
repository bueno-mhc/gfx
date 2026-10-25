/// @file shape_kernels.hpp
/// @brief The SIMD kernels of gfx.physics.shapes, behind a header without the standard library (SIMDe's headers cannot
///        share a translation unit with `import std`): plain floats in, plain results out.
#pragma once

namespace gfx::physics::shape_kernels
{

using Size = decltype(sizeof(0));

/// @brief The index of the first of the @p count points at @p points (x, y, z after x, y, z) with the largest dot
///        product with (@p x, @p y, @p z), each product computed as `(px x + py y) + pz z`; @p count is at least 1.
[[nodiscard]] auto support_index(const float* points,
                                 Size count,
                                 float x,
                                 float y,
                                 float z) noexcept -> Size;

} // namespace gfx::physics::shape_kernels
