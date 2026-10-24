/// @file batch.cppm
/// @brief Tests of many primitives at once: frustum culling of boxes and spheres. In constant expressions they run the
///        scalar classify() for each element; at run time, SIMD kernels (SIMDe: primitive_kernels.cpp) computing the
///        same arithmetic in the same order, so both give the same results.
module;
#include "primitive_kernels.hpp"
export module gfx.physics.primitives:batch;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import :plane;
import :aabb;
import :sphere;
import :frustum;
import :intersection;

namespace gfx::physics::primitives_detail
{

static_assert(sizeof(Plane) == 4 * sizeof(float) && sizeof(Frustum) == 24 * sizeof(float) && sizeof(Aabb) == 6 * sizeof(float)
                  && sizeof(Sphere) == 4 * sizeof(float) && sizeof(FrustumIntersection) == 1,
              "the batch kernels read primitives as packed floats and write FrustumIntersection values as bytes");
static_assert(std::to_underlying(FrustumIntersection::OUTSIDE) == 0 && std::to_underlying(FrustumIntersection::INTERSECTING) == 1
                  && std::to_underlying(FrustumIntersection::INSIDE) == 2,
              "the batch kernels write FrustumIntersection's values");

/// Throws when @p output is shorter than @p input.
constexpr void
require_results(std::size_t input,
                std::size_t output,
                std::string_view operation)
{
    if (output < input)
    {
        throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY,
                            .operation = std::string{operation},
                            .problem = std::format("the output holds {} results for {} elements", output, input),
                            .suggestion = "give an output span at least as long as the elements",
                            .context = {}});
    }
}

} // namespace gfx::physics::primitives_detail

export namespace gfx::physics
{

/// @brief Where each of @p boxes is with respect to @p frustum, into @p output (SIMD at run time).
/// @throws PhysicsError (GEOMETRY) when @p output is shorter than @p boxes.
constexpr void
classify(const Frustum& frustum,
         std::span<const Aabb> boxes,
         std::span<FrustumIntersection> output)
{
    primitives_detail::require_results(boxes.size(), output.size(), "classify");
    if consteval
    {
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            output[index] = classify(frustum, boxes[index]);
        }
    }
    else
    {
        if (!boxes.empty())
        {
            primitive_kernels::classify_boxes(&frustum.planes[0].normal.x, &boxes.data()->minimum.x, reinterpret_cast<unsigned char*>(output.data()),
                                              boxes.size());
        }
    }
}

/// @brief Where each of @p spheres is with respect to @p frustum, into @p output (SIMD at run time).
/// @throws PhysicsError (GEOMETRY) when @p output is shorter than @p spheres.
constexpr void
classify(const Frustum& frustum,
         std::span<const Sphere> spheres,
         std::span<FrustumIntersection> output)
{
    primitives_detail::require_results(spheres.size(), output.size(), "classify");
    if consteval
    {
        for (std::size_t index = 0; index < spheres.size(); ++index)
        {
            output[index] = classify(frustum, spheres[index]);
        }
    }
    else
    {
        if (!spheres.empty())
        {
            primitive_kernels::classify_spheres(&frustum.planes[0].normal.x, &spheres.data()->center.x, reinterpret_cast<unsigned char*>(output.data()),
                                                spheres.size());
        }
    }
}

} // namespace gfx::physics
