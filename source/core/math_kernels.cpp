// The SIMD batch kernels of gfx.core.math (math_kernels.hpp), on SIMDe's portable SSE intrinsics: native SSE2 on
// x86-64, NEON on 64-bit ARM, WASM SIMD on WebAssembly, scalar code elsewhere. Four elements per iteration; the
// remaining ones go through scalar code computing in the same order, so that every element gets the same result
// wherever it falls.
#include <x86/sse2.h>

#include "math_kernels.hpp"

namespace gfx::math_kernels
{

namespace
{

/// Four points (x, y, z after x, y, z: twelve floats) as three lanes of four: every x, every y, every z.
struct PointLanes
{
    simde__m128 x;
    simde__m128 y;
    simde__m128 z;
};

/// The four points of the twelve floats at @p points, in lanes.
auto
load_points(const float* points) noexcept -> PointLanes
{
    // The floats in three registers: x0 y0 z0 x1 | y1 z1 x2 y2 | z2 x3 y3 z3.
    const simde__m128 first = simde_mm_loadu_ps(points);
    const simde__m128 second = simde_mm_loadu_ps(points + 4);
    const simde__m128 third = simde_mm_loadu_ps(points + 8);

    // x0 x1 x2 x3: first's 0 and 3, then second's 2 and third's 1 (gathered first).
    const simde__m128 x_tail = simde_mm_shuffle_ps(second, third, SIMDE_MM_SHUFFLE(1, 1, 2, 2));
    const simde__m128 x = simde_mm_shuffle_ps(first, x_tail, SIMDE_MM_SHUFFLE(2, 0, 3, 0));

    // y0 y1 y2 y3: first's 1, second's 0 and 3, third's 2.
    const simde__m128 y_head = simde_mm_shuffle_ps(first, second, SIMDE_MM_SHUFFLE(0, 0, 1, 1));
    const simde__m128 y_tail = simde_mm_shuffle_ps(second, third, SIMDE_MM_SHUFFLE(2, 2, 3, 3));
    const simde__m128 y = simde_mm_shuffle_ps(y_head, y_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0));

    // z0 z1 z2 z3: first's 2, second's 1, third's 0 and 3.
    const simde__m128 z_head = simde_mm_shuffle_ps(first, second, SIMDE_MM_SHUFFLE(1, 1, 2, 2));
    const simde__m128 z_tail = simde_mm_shuffle_ps(third, third, SIMDE_MM_SHUFFLE(3, 3, 0, 0));
    const simde__m128 z = simde_mm_shuffle_ps(z_head, z_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0));
    return {.x = x, .y = y, .z = z};
}

/// Writes the four points of @p lanes as twelve floats at @p output (x, y, z after x, y, z).
void
store_points(const PointLanes& lanes,
             float* output) noexcept
{
    // x0 y0 z0 x1.
    const simde__m128 first_head = simde_mm_shuffle_ps(lanes.x, lanes.y, SIMDE_MM_SHUFFLE(0, 0, 0, 0));
    const simde__m128 first_tail = simde_mm_shuffle_ps(lanes.z, lanes.x, SIMDE_MM_SHUFFLE(1, 1, 0, 0));
    simde_mm_storeu_ps(output, simde_mm_shuffle_ps(first_head, first_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0)));

    // y1 z1 x2 y2.
    const simde__m128 second_head = simde_mm_shuffle_ps(lanes.y, lanes.z, SIMDE_MM_SHUFFLE(1, 1, 1, 1));
    const simde__m128 second_tail = simde_mm_shuffle_ps(lanes.x, lanes.y, SIMDE_MM_SHUFFLE(2, 2, 2, 2));
    simde_mm_storeu_ps(output + 4, simde_mm_shuffle_ps(second_head, second_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0)));

    // z2 x3 y3 z3.
    const simde__m128 third_head = simde_mm_shuffle_ps(lanes.z, lanes.x, SIMDE_MM_SHUFFLE(3, 3, 2, 2));
    const simde__m128 third_tail = simde_mm_shuffle_ps(lanes.y, lanes.z, SIMDE_MM_SHUFFLE(3, 3, 3, 3));
    simde_mm_storeu_ps(output + 8, simde_mm_shuffle_ps(third_head, third_tail, SIMDE_MM_SHUFFLE(2, 0, 2, 0)));
}

/// One row of a column-major matrix applied to lanes: `m(row, 0) * x + m(row, 1) * y + m(row, 2) * z (+ m(row, 3))`.
auto
row_times(const float* matrix,
          int row,
          const PointLanes& lanes,
          bool translate) noexcept -> simde__m128
{
    simde__m128 sum = simde_mm_mul_ps(simde_mm_set1_ps(matrix[row]), lanes.x);
    sum = simde_mm_add_ps(sum, simde_mm_mul_ps(simde_mm_set1_ps(matrix[4 + row]), lanes.y));
    sum = simde_mm_add_ps(sum, simde_mm_mul_ps(simde_mm_set1_ps(matrix[8 + row]), lanes.z));
    return translate ? simde_mm_add_ps(sum, simde_mm_set1_ps(matrix[12 + row])) : sum;
}

/// The same for one point, in the same order.
auto
row_times(const float* matrix,
          int row,
          const float* point,
          bool translate) noexcept -> float
{
    float sum = matrix[row] * point[0];
    sum = sum + matrix[4 + row] * point[1];
    sum = sum + matrix[8 + row] * point[2];
    return translate ? sum + matrix[12 + row] : sum;
}

} // namespace

void
transform_points(const float* matrix,
                 const float* points,
                 float* output,
                 Size count,
                 bool translate) noexcept
{
    // Four points at a time: loaded entirely before their results are stored, so output may be points.
    Size index = 0;
    for (; index + 4 <= count; index += 4)
    {
        const PointLanes lanes = load_points(points + index * 3);
        store_points({.x = row_times(matrix, 0, lanes, translate),
                      .y = row_times(matrix, 1, lanes, translate),
                      .z = row_times(matrix, 2, lanes, translate)},
                     output + index * 3);
    }

    // The remaining points, one at a time.
    for (; index < count; ++index)
    {
        const float point[3]{points[index * 3], points[index * 3 + 1], points[index * 3 + 2]};
        output[index * 3] = row_times(matrix, 0, point, translate);
        output[index * 3 + 1] = row_times(matrix, 1, point, translate);
        output[index * 3 + 2] = row_times(matrix, 2, point, translate);
    }
}

void
multiply_add(float* destination,
             const float* source,
             float factor,
             Size count) noexcept
{
    // Four floats at a time, then the remaining ones.
    const simde__m128 lanes = simde_mm_set1_ps(factor);
    Size index = 0;
    for (; index + 4 <= count; index += 4)
    {
        const simde__m128 product = simde_mm_mul_ps(lanes, simde_mm_loadu_ps(source + index));
        simde_mm_storeu_ps(destination + index, simde_mm_add_ps(simde_mm_loadu_ps(destination + index), product));
    }
    for (; index < count; ++index)
    {
        destination[index] = destination[index] + factor * source[index];
    }
}

} // namespace gfx::math_kernels
