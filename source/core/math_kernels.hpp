/// @file math_kernels.hpp
/// @brief A std-free interface to the SIMD batch kernels of gfx.core.math (math_kernels.cpp, a plain translation
///        unit: SIMDe's headers include C++ standard headers, which cannot share a unit with `import std`). The math
///        module passes its arrays as floats: Vector3 is three packed floats, Matrix4 sixteen floats, column after
///        column (both checked by static_assert there).
#pragma once

namespace gfx::math_kernels
{

/// Element counts.
using Size = decltype(sizeof(0));

/// Writes `matrix * (x, y, z, 1)` (@p translate) or `matrix * (x, y, z, 0)` of each of the @p count points of
/// @p points (x, y, z after x, y, z) into @p output, which may be @p points itself. No perspective division.
void transform_points(const float* matrix,
                      const float* points,
                      float* output,
                      Size count,
                      bool translate) noexcept;

/// `destination[i] += factor * source[i]` for the @p count floats; @p destination may be @p source.
void multiply_add(float* destination,
                  const float* source,
                  float factor,
                  Size count) noexcept;

} // namespace gfx::math_kernels
