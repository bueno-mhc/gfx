/// @file primitive_kernels.hpp
/// @brief A std-free interface to the SIMD batch kernels of gfx.physics.primitives (primitive_kernels.cpp, a plain
///        translation unit: SIMDe's headers include C++ standard headers, which cannot share a unit with `import std`).
///        The module passes its arrays as floats (layouts checked by static_assert there): six planes of four floats
///        (normal x, y, z, distance), boxes of six (minimum x, y, z, maximum x, y, z), spheres of four (center x, y, z,
///        radius); results are FrustumIntersection values (0 outside, 1 intersecting, 2 inside).
#pragma once

namespace gfx::physics::primitive_kernels
{

/// Element counts.
using Size = decltype(sizeof(0));

/// Where each of the @p count boxes at @p boxes is with respect to the frustum of the six @p planes, into @p output.
void classify_boxes(const float* planes,
                    const float* boxes,
                    unsigned char* output,
                    Size count) noexcept;

/// Where each of the @p count spheres at @p spheres is with respect to the frustum of the six @p planes, into
/// @p output.
void classify_spheres(const float* planes,
                      const float* spheres,
                      unsigned char* output,
                      Size count) noexcept;

} // namespace gfx::physics::primitive_kernels
