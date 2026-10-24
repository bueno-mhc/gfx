// The SIMD batch kernels of gfx.physics.primitives (primitive_kernels.hpp), on SIMDe's portable SSE intrinsics:
// native SSE2 on x86-64, NEON on 64-bit ARM, WASM SIMD on WebAssembly, scalar code elsewhere. Each element is tested
// against four planes per register (the six planes and two that everything is in front of), computing exactly what
// the scalar classify() computes, in the same order.
#include <x86/sse2.h>

#include <float.h>

#include "primitive_kernels.hpp"

namespace gfx::physics::primitive_kernels
{

namespace
{

/// FrustumIntersection's values.
constexpr unsigned char OUTSIDE = 0;
constexpr unsigned char INTERSECTING = 1;
constexpr unsigned char INSIDE = 2;

/// Four planes as lanes: their normals' components, the components' absolute values, their distances.
struct PlaneLanes
{
    simde__m128 x;
    simde__m128 y;
    simde__m128 z;
    simde__m128 absolute_x;
    simde__m128 absolute_y;
    simde__m128 absolute_z;
    simde__m128 distance;
};

/// The six planes in two groups of four lanes; the last two lanes of the second group are planes everything is in
/// front of (no normal, the lowest distance), as Plane::from_coefficients() makes for coefficients without direction.
struct FrustumLanes
{
    PlaneLanes first;
    PlaneLanes second;
};

auto
plane_lanes(const float* planes,
            int first,
            int count) noexcept -> PlaneLanes
{
    // Gather each component of up to four planes; missing planes have no normal and the lowest distance.
    float values[7][4]{};
    for (int lane = 0; lane < 4; ++lane)
    {
        const bool present = lane < count;
        const float* plane = planes + (first + lane) * 4;
        values[0][lane] = present ? plane[0] : 0.0F;
        values[1][lane] = present ? plane[1] : 0.0F;
        values[2][lane] = present ? plane[2] : 0.0F;
        values[3][lane] = present ? (plane[0] < 0 ? -plane[0] : plane[0]) : 0.0F;
        values[4][lane] = present ? (plane[1] < 0 ? -plane[1] : plane[1]) : 0.0F;
        values[5][lane] = present ? (plane[2] < 0 ? -plane[2] : plane[2]) : 0.0F;
        values[6][lane] = present ? plane[3] : -FLT_MAX;
    }
    return {.x = simde_mm_loadu_ps(values[0]),
            .y = simde_mm_loadu_ps(values[1]),
            .z = simde_mm_loadu_ps(values[2]),
            .absolute_x = simde_mm_loadu_ps(values[3]),
            .absolute_y = simde_mm_loadu_ps(values[4]),
            .absolute_z = simde_mm_loadu_ps(values[5]),
            .distance = simde_mm_loadu_ps(values[6])};
}

auto
frustum_lanes(const float* planes) noexcept -> FrustumLanes
{
    return {.first = plane_lanes(planes, 0, 4), .second = plane_lanes(planes, 4, 2)};
}

/// `dot(normal, point) - distance` for the four planes: x, then + y, then + z, then - distance.
auto
signed_distances(const PlaneLanes& planes,
                 float x,
                 float y,
                 float z) noexcept -> simde__m128
{
    simde__m128 sum = simde_mm_mul_ps(planes.x, simde_mm_set1_ps(x));
    sum = simde_mm_add_ps(sum, simde_mm_mul_ps(planes.y, simde_mm_set1_ps(y)));
    sum = simde_mm_add_ps(sum, simde_mm_mul_ps(planes.z, simde_mm_set1_ps(z)));
    return simde_mm_sub_ps(sum, planes.distance);
}

/// `dot(|normal|, extents)` for the four planes: x, then + y, then + z.
auto
reaches(const PlaneLanes& planes,
        float x,
        float y,
        float z) noexcept -> simde__m128
{
    simde__m128 sum = simde_mm_mul_ps(planes.absolute_x, simde_mm_set1_ps(x));
    sum = simde_mm_add_ps(sum, simde_mm_mul_ps(planes.absolute_y, simde_mm_set1_ps(y)));
    return simde_mm_add_ps(sum, simde_mm_mul_ps(planes.absolute_z, simde_mm_set1_ps(z)));
}

/// The classification from the distances and reaches of both groups: outside when behind a plane by more than its
/// reach, inside when in front of every plane by at least it.
auto
classification(simde__m128 first_distance,
               simde__m128 first_reach,
               simde__m128 second_distance,
               simde__m128 second_reach) noexcept -> unsigned char
{
    const simde__m128 zero = simde_mm_setzero_ps();
    const int outside = simde_mm_movemask_ps(simde_mm_cmplt_ps(first_distance, simde_mm_sub_ps(zero, first_reach)))
                        | simde_mm_movemask_ps(simde_mm_cmplt_ps(second_distance, simde_mm_sub_ps(zero, second_reach)));
    if (outside != 0)
    {
        return OUTSIDE;
    }
    const int inside = simde_mm_movemask_ps(simde_mm_cmpge_ps(first_distance, first_reach)) & simde_mm_movemask_ps(simde_mm_cmpge_ps(second_distance, second_reach));
    return inside == 0xF ? INSIDE : INTERSECTING;
}

} // namespace

void
classify_boxes(const float* planes,
               const float* boxes,
               unsigned char* output,
               Size count) noexcept
{
    const FrustumLanes frustum = frustum_lanes(planes);
    for (Size index = 0; index < count; ++index)
    {
        // The box's center and half extents, as Aabb::center() and Aabb::half_extents() compute them.
        const float* box = boxes + index * 6;
        const float center_x = (box[0] + box[3]) * 0.5F;
        const float center_y = (box[1] + box[4]) * 0.5F;
        const float center_z = (box[2] + box[5]) * 0.5F;
        const float extent_x = (box[3] - box[0]) * 0.5F;
        const float extent_y = (box[4] - box[1]) * 0.5F;
        const float extent_z = (box[5] - box[2]) * 0.5F;
        output[index] = classification(signed_distances(frustum.first, center_x, center_y, center_z), reaches(frustum.first, extent_x, extent_y, extent_z),
                                       signed_distances(frustum.second, center_x, center_y, center_z), reaches(frustum.second, extent_x, extent_y, extent_z));
    }
}

void
classify_spheres(const float* planes,
                 const float* spheres,
                 unsigned char* output,
                 Size count) noexcept
{
    const FrustumLanes frustum = frustum_lanes(planes);
    for (Size index = 0; index < count; ++index)
    {
        // A sphere's reach is its radius on every plane.
        const float* sphere = spheres + index * 4;
        const simde__m128 radius = simde_mm_set1_ps(sphere[3]);
        output[index] = classification(signed_distances(frustum.first, sphere[0], sphere[1], sphere[2]), radius,
                                       signed_distances(frustum.second, sphere[0], sphere[1], sphere[2]), radius);
    }
}

} // namespace gfx::physics::primitive_kernels
