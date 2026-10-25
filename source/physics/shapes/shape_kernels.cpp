// The SIMD kernels of gfx.physics.shapes (shape_kernels.hpp), on SIMDe's portable SSE intrinsics. support_index()
// takes four points per iteration and keeps, per lane, the first point with the largest product; the lanes are then
// reduced (largest product, smallest index among equals) and the remaining points go through scalar code: the result
// is the scalar loop's, first index of the maximum included.
#include <x86/sse2.h>

#include "shape_kernels.hpp"

namespace gfx::physics::shape_kernels
{

namespace
{

/// Four points (twelve floats) as three lanes: every x, every y, every z (as gfx.core.math's kernels load them).
struct PointLanes
{
    simde__m128 x;
    simde__m128 y;
    simde__m128 z;
};

auto
load_points(const float* points) noexcept -> PointLanes
{
    // x0 y0 z0 x1 | y1 z1 x2 y2 | z2 x3 y3 z3, regrouped by component.
    const simde__m128 first = simde_mm_loadu_ps(points);
    const simde__m128 second = simde_mm_loadu_ps(points + 4);
    const simde__m128 third = simde_mm_loadu_ps(points + 8);
    const simde__m128 x_tail = simde_mm_shuffle_ps(second, third, SIMDE_MM_SHUFFLE(1, 1, 2, 2));
    const simde__m128 x = simde_mm_shuffle_ps(first, x_tail, SIMDE_MM_SHUFFLE(2, 0, 3, 0));
    const simde__m128 y_head = simde_mm_shuffle_ps(first, second, SIMDE_MM_SHUFFLE(0, 0, 1, 1));
    const simde__m128 y_tail = simde_mm_shuffle_ps(second, third, SIMDE_MM_SHUFFLE(2, 2, 3, 3));
    const simde__m128 y = simde_mm_shuffle_ps(y_head, y_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0));
    const simde__m128 z_head = simde_mm_shuffle_ps(first, second, SIMDE_MM_SHUFFLE(1, 1, 2, 2));
    const simde__m128 z_tail = simde_mm_shuffle_ps(third, third, SIMDE_MM_SHUFFLE(3, 3, 0, 0));
    const simde__m128 z = simde_mm_shuffle_ps(z_head, z_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0));
    return {.x = x, .y = y, .z = z};
}

} // namespace

auto
support_index(const float* points,
              Size count,
              float x,
              float y,
              float z) noexcept -> Size
{
    // Per lane: the largest product and the index of the first point reaching it (strictly greater replaces).
    const simde__m128 direction_x = simde_mm_set1_ps(x);
    const simde__m128 direction_y = simde_mm_set1_ps(y);
    const simde__m128 direction_z = simde_mm_set1_ps(z);
    simde__m128 best = simde_mm_set1_ps(-3.402823466e+38F);
    simde__m128i best_index = simde_mm_set_epi32(3, 2, 1, 0);
    simde__m128i index = simde_mm_set_epi32(3, 2, 1, 0);
    const simde__m128i step = simde_mm_set1_epi32(4);
    const Size groups = count / 4;
    for (Size group = 0; group < groups; ++group)
    {
        const PointLanes lanes = load_points(points + group * 12);
        simde__m128 products = simde_mm_mul_ps(lanes.x, direction_x);
        products = simde_mm_add_ps(products, simde_mm_mul_ps(lanes.y, direction_y));
        products = simde_mm_add_ps(products, simde_mm_mul_ps(lanes.z, direction_z));
        const simde__m128 greater = simde_mm_cmpgt_ps(products, best);
        best = simde_mm_or_ps(simde_mm_and_ps(greater, products), simde_mm_andnot_ps(greater, best));
        const simde__m128i take = simde_mm_castps_si128(greater);
        best_index = simde_mm_or_si128(simde_mm_and_si128(take, index), simde_mm_andnot_si128(take, best_index));
        index = simde_mm_add_epi32(index, step);
    }

    // The lanes reduced: the largest product, the smallest index among equals; then the remaining points.
    float values[4];
    int indices[4];
    simde_mm_storeu_ps(values, best);
    simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(indices), best_index);
    Size result = 0;
    float largest = -3.402823466e+38F;
    bool found = false;
    if (groups > 0)
    {
        for (int lane = 0; lane < 4; ++lane)
        {
            const Size candidate = static_cast<Size>(indices[lane]);
            if (!found || values[lane] > largest || (values[lane] == largest && candidate < result))
            {
                largest = values[lane];
                result = candidate;
                found = true;
            }
        }
    }
    for (Size remaining = groups * 4; remaining < count; ++remaining)
    {
        const float* point = points + remaining * 3;
        const float product = point[0] * x + point[1] * y + point[2] * z;
        if (!found || product > largest)
        {
            largest = product;
            result = remaining;
            found = true;
        }
    }
    return result;
}

} // namespace gfx::physics::shape_kernels
