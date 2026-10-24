// The intersection tests of the physics library: volumes against planes and frustums (exact cases at compile time,
// random ones against the corners at run time), rays against every primitive (the slab and plane methods checked
// against each other and against the surfaces they hit), and segments against boxes and spheres.
#include <gtest/gtest.h>

import std;
import gfx.physics.enums;
import gfx.physics.primitives;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::box_frustum;
using gfx::tests::describe;
using gfx::tests::Generator;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

// Volumes against a plane (the floor y = 0, facing up).
constexpr Plane GROUND{};
static_assert(classify(GROUND, Aabb::from_center({.y = 2}, {.x = 1, .y = 1, .z = 1})) == PlaneSide::FRONT);
static_assert(classify(GROUND, Aabb::from_center({.y = 1}, {.x = 1, .y = 1, .z = 1})) == PlaneSide::INTERSECTING, "touching");
static_assert(classify(GROUND, Aabb::from_center({.y = -1.5F}, {.x = 1, .y = 1, .z = 1})) == PlaneSide::BACK);
static_assert(classify(GROUND, Sphere{.center = {.y = 0.5F}, .radius = 1}) == PlaneSide::INTERSECTING);
static_assert(classify(GROUND, Sphere{.center = {.y = -1.5F}, .radius = 1}) == PlaneSide::BACK);
static_assert(classify(GROUND, Obb{.center = {.y = 1.2F}, .half_extents = {.x = 1, .y = 1, .z = 1}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 4)})
                  == PlaneSide::INTERSECTING,
              "a cube on its edge reaches √2 down");
static_assert(classify(GROUND, Obb{.center = {.y = 1.5F}, .half_extents = {.x = 1, .y = 1, .z = 1}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 4)})
              == PlaneSide::FRONT);

// Volumes against a frustum shaped like the box [-4, 4]³: inside, touching a plane from inside, straddling, outside.
constexpr Frustum CAGE = box_frustum(4);
static_assert(classify(CAGE, Aabb::from_center({}, {.x = 1, .y = 1, .z = 1})) == FrustumIntersection::INSIDE);
static_assert(classify(CAGE, Aabb{.minimum = {.x = 3}, .maximum = {.x = 4, .y = 1, .z = 1}}) == FrustumIntersection::INSIDE, "on the plane counts as inside");
static_assert(classify(CAGE, Aabb{.minimum = {.x = 4}, .maximum = {.x = 5, .y = 1, .z = 1}}) == FrustumIntersection::INTERSECTING, "touching from outside");
static_assert(classify(CAGE, Aabb{.minimum = {.x = 4.5F}, .maximum = {.x = 5, .y = 1, .z = 1}}) == FrustumIntersection::OUTSIDE);
static_assert(classify(CAGE, Sphere{.center = {.z = -3}, .radius = 1}) == FrustumIntersection::INSIDE);
static_assert(classify(CAGE, Sphere{.center = {.z = -4.5F}, .radius = 1}) == FrustumIntersection::INTERSECTING);
static_assert(classify(CAGE, Sphere{.center = {.z = -5.5F}, .radius = 1}) == FrustumIntersection::OUTSIDE);
static_assert(classify(CAGE, Obb{.center = {.x = 3}, .half_extents = {.x = 1, .y = 1, .z = 1}, .orientation = Quaternion::from_axis_angle({.y = 1}, PI / 4)})
              == FrustumIntersection::INTERSECTING);
static_assert(classify(CAGE, Obb{.center = {.x = 5.5F}, .half_extents = {.x = 1, .y = 1, .z = 1}, .orientation = Quaternion::from_axis_angle({.y = 1}, PI / 4)})
              == FrustumIntersection::OUTSIDE);

// Rays: from outside, from inside (distance 0), pointing away, parallel to a slab, beyond the maximum.
constexpr Aabb CUBE = Aabb::from_center({}, {.x = 1, .y = 1, .z = 1});
constexpr Ray DOWN_Z{.origin = {.z = 10}, .direction = {.z = -1}};
static_assert(ray_distance(DOWN_Z, CUBE) == 9.0F && ray_distance(Ray{.origin = {.x = 0.5F}}, CUBE) == 0.0F);
static_assert(!ray_distance(Ray{.origin = {.z = 10}, .direction = {.z = 1}}, CUBE).has_value());
static_assert(!ray_distance(Ray{.origin = {.x = 2, .z = 10}}, CUBE).has_value() && ray_distance(Ray{.origin = {.x = 1, .z = 10}}, CUBE) == 9.0F,
              "parallel to the x slab: outside misses, on its boundary hits");
static_assert(!ray_distance(DOWN_Z, CUBE, 8.5F).has_value() && ray_distance(DOWN_Z, CUBE, 9.5F) == 9.0F);
static_assert(ray_distance(DOWN_Z, Sphere{.radius = 1}) == 9.0F && ray_distance(Ray{.origin = {.y = 0.5F}}, Sphere{.radius = 1}) == 0.0F);
static_assert(!ray_distance(Ray{.origin = {.z = 10}, .direction = {.z = 1}}, Sphere{.radius = 1}).has_value(), "the sphere is behind");
static_assert(ray_distance(Ray{.origin = {.x = 1, .z = 10}}, Sphere{.radius = 1}) == 10.0F && !ray_distance(Ray{.origin = {.x = 1.01F, .z = 10}}, Sphere{.radius = 1}).has_value(),
              "tangent");
static_assert(ray_distance(Ray{.origin = {.y = 5}, .direction = {.y = -1}}, GROUND) == 5.0F && ray_distance(Ray{.origin = {.y = -5}, .direction = {.y = 1}}, GROUND) == 5.0F);
static_assert(!ray_distance(Ray{.origin = {.y = 5}, .direction = {.x = 1}}, GROUND).has_value() && !ray_distance(Ray{.origin = {.y = 5}, .direction = {.y = 1}}, GROUND).has_value());
constexpr FixedConvexHull<8, 6> CUBE_HULL = box_hull({.x = 1, .y = 1, .z = 1});
static_assert(ray_distance(DOWN_Z, CUBE_HULL.view()) == 9.0F && ray_distance(Ray{}, CUBE_HULL.view()) == 0.0F);
static_assert(!ray_distance(Ray{.origin = {.z = 10}, .direction = {.z = 1}}, CUBE_HULL.view()).has_value() && !ray_distance(DOWN_Z, ConvexHullView{}).has_value());

// A cube turned 45° about Z: its edge is √2 from the center.
constexpr Obb DIAMOND{.center = {}, .half_extents = {.x = 1, .y = 1, .z = 1}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 4)};
static_assert(std::abs(*ray_distance(Ray{.origin = {.x = 10}, .direction = {.x = -1}}, DIAMOND) - (10 - std::numbers::sqrt2_v<float>)) < 1e-4F);

// Segments.
static_assert(intersects(Segment{.start = {.x = -5}, .end = {.x = 5}}, CUBE) && !intersects(Segment{.start = {.x = 2}, .end = {.x = 5}}, CUBE));
static_assert(intersects(Segment{.start = {.z = 3}, .end = {.z = 1}}, CUBE) && !intersects(Segment{.start = {.z = 3}, .end = {.z = 1.5F}}, CUBE), "the end touches");
static_assert(intersects(Segment{.start = {.x = -5, .y = 0.9F}, .end = {.x = 5, .y = 0.9F}}, Sphere{.radius = 1})
              && !intersects(Segment{.start = {.x = 2, .y = 0.5F}, .end = {.x = 5, .y = 0.5F}}, Sphere{.radius = 1}));

// ===================================================================================================== run time

/// The convex hull of an oriented box: its corners and the planes of its faces.
auto
hull_of(const Obb& box) -> std::pair<std::array<Vector3, 8>, std::array<Plane, 6>>
{
    std::array<Vector3, 8> corners{};
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
        corners[corner] = box.corner(corner);
    }
    const std::array<Vector3, 3> axes = box.axes();
    const std::array<float, 3> half{box.half_extents.x, box.half_extents.y, box.half_extents.z};
    std::array<Plane, 6> faces{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        faces[axis * 2] = Plane::from_point_normal(box.center + axes[axis] * half[axis], axes[axis]);
        faces[axis * 2 + 1] = Plane::from_point_normal(box.center - axes[axis] * half[axis], -axes[axis]);
    }
    return {corners, faces};
}

/// Where a volume is with respect to a frustum, from the signed distances of its corners to each plane; nullopt when a
/// corner is within 0.001 of a plane's boundary decision (rounding decides those).
auto
classify_corners(const Frustum& frustum,
                 std::span<const Vector3> corners) -> std::optional<FrustumIntersection>
{
    bool inside = true;
    bool outside = false;
    for (const Plane& plane : frustum.planes)
    {
        float lowest = std::numeric_limits<float>::max();
        float highest = std::numeric_limits<float>::lowest();
        for (const Vector3 corner : corners)
        {
            lowest = std::min(lowest, plane.signed_distance(corner));
            highest = std::max(highest, plane.signed_distance(corner));
        }
        if (std::abs(lowest) < 1e-3F || std::abs(highest) < 1e-3F)
        {
            return std::nullopt;
        }
        outside = outside || highest < 0;
        inside = inside && lowest >= 0;
    }
    return outside ? FrustumIntersection::OUTSIDE : inside ? FrustumIntersection::INSIDE : FrustumIntersection::INTERSECTING;
}

TEST(IntersectionTest, FrustumsClassifyVolumesAsTheirCorners)
{
    // A camera at (1, 2, 8), turned, with a 70° perspective from 0.5 to 30 units.
    const Transform camera{.position = {.x = 1, .y = 2, .z = 8}, .orientation = Quaternion::from_yaw_pitch_roll({.yaw = 0.4F, .pitch = -0.2F, .roll = 0.1F})};
    const Frustum frustum = Frustum::from_matrix(gfx::tests::perspective(70 * PI / 180, 1.5F, 0.5F, 30) * Matrix4::from_transform(inverse(camera)));

    Generator generator{2024};
    std::array<std::size_t, 3> boxes{};
    std::array<std::size_t, 3> oriented{};
    for (std::size_t sample = 0; sample < 3000; ++sample)
    {
        // Axis-aligned boxes.
        const Aabb box = Aabb::from_center(generator.vector(-20, 20), generator.vector(0.1F, 6));
        std::array<Vector3, 8> corners{};
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            corners[corner] = box.corner(corner);
        }
        if (const std::optional<FrustumIntersection> expected = classify_corners(frustum, corners))
        {
            EXPECT_EQ(classify(frustum, box), *expected) << "box " << sample;
            ++boxes[std::to_underlying(*expected)];
        }

        // Oriented boxes.
        const Obb turned{.center = generator.vector(-20, 20), .half_extents = generator.vector(0.1F, 6), .orientation = generator.rotation()};
        const std::array<Vector3, 8> turned_corners = hull_of(turned).first;
        if (const std::optional<FrustumIntersection> expected = classify_corners(frustum, turned_corners))
        {
            EXPECT_EQ(classify(frustum, turned), *expected) << "oriented box " << sample;
            ++oriented[std::to_underlying(*expected)];
        }

        // Spheres: outside when their bounds are, inside when their bounds are.
        const Sphere sphere{.center = generator.vector(-20, 20), .radius = generator.between(0.1F, 6)};
        const FrustumIntersection bounds = classify(frustum, sphere.bounds());
        if (bounds == FrustumIntersection::OUTSIDE || bounds == FrustumIntersection::INSIDE)
        {
            EXPECT_EQ(classify(frustum, sphere), bounds) << "sphere " << sample;
        }
    }
    for (std::size_t kind = 0; kind < 3; ++kind)
    {
        EXPECT_GT(boxes[kind], 50U) << "boxes of class " << kind;
        EXPECT_GT(oriented[kind], 50U) << "oriented boxes of class " << kind;
    }
}

TEST(IntersectionTest, RaysAgreeAcrossPrimitives)
{
    Generator generator{99};
    std::size_t hits = 0;
    std::size_t misses = 0;
    for (std::size_t sample = 0; sample < 3000; ++sample)
    {
        // Half of the rays aim at a point near the volumes, the others go anywhere.
        const Vector3 origin = generator.vector(-6, 6);
        const Vector3 target = generator.vector(-2, 2);
        const Vector3 direction = sample % 2 == 0 ? normalized(target - origin) : generator.direction();
        const Ray ray{.origin = origin, .direction = direction};

        // A box, by the slab method and as a hull (the ray moved into the hull's frame).
        const Aabb box = Aabb::from_center(generator.vector(-1, 1), generator.vector(0.2F, 2));
        const FixedConvexHull<8, 6> hull = box_hull(box.half_extents());
        const std::optional<float> slab = ray_distance(ray, box);
        const std::optional<float> faces = ray_distance(Ray{.origin = origin - box.center(), .direction = direction}, hull.view());
        ASSERT_EQ(slab.has_value(), faces.has_value()) << "box, sample " << sample;
        if (slab)
        {
            EXPECT_NEAR(*slab, *faces, 1e-4F) << "box, sample " << sample;
            EXPECT_LT(box.squared_distance(ray.at(*slab)), 1e-6F) << "box, sample " << sample;
            if (*slab > 0)
            {
                EXPECT_FALSE(ray_distance(ray, box, *slab * 0.5F).has_value()) << "box, sample " << sample;
            }
            EXPECT_EQ(ray_distance(ray, box, *slab * 1.5F + 1e-3F), slab) << "box, sample " << sample;
        }
        (slab ? hits : misses) += 1;

        // An oriented box, in its own frame and as a hull.
        const Obb turned{.center = generator.vector(-1, 1), .half_extents = generator.vector(0.2F, 2), .orientation = generator.rotation()};
        const auto [corners, planes] = hull_of(turned);
        const std::optional<float> local = ray_distance(ray, turned);
        const std::optional<float> through_faces = ray_distance(ray, ConvexHullView{.vertices = corners, .faces = planes});
        ASSERT_EQ(local.has_value(), through_faces.has_value()) << "oriented box, sample " << sample;
        if (local)
        {
            EXPECT_NEAR(*local, *through_faces, 1e-3F) << "oriented box, sample " << sample;
            EXPECT_LT(distance(turned.closest_point(ray.at(*local)), ray.at(*local)), 1e-3F) << "oriented box, sample " << sample;
        }

        // A sphere: the hit is on its surface (or the origin inside); a miss passes farther than the radius.
        const Sphere sphere{.center = generator.vector(-1, 1), .radius = generator.between(0.2F, 2)};
        const std::optional<float> round = ray_distance(ray, sphere);
        if (round)
        {
            if (*round == 0)
            {
                EXPECT_TRUE(sphere.contains(origin)) << "sphere, sample " << sample;
            }
            else
            {
                EXPECT_NEAR(distance(ray.at(*round), sphere.center), sphere.radius, 1e-4F) << "sphere, sample " << sample;
            }
        }
        else
        {
            const float nearest = std::max(0.0F, dot(sphere.center - origin, direction));
            EXPECT_GT(distance(ray.at(nearest), sphere.center), sphere.radius - 1e-4F) << "sphere, sample " << sample << " " << describe(origin);
        }
    }
    EXPECT_GT(hits, 500U);
    EXPECT_GT(misses, 500U);
}

TEST(IntersectionTest, SegmentsHitWhatTheirRaysHitWithinTheirLength)
{
    Generator generator{31337};
    for (std::size_t sample = 0; sample < 2000; ++sample)
    {
        const Segment segment{.start = generator.vector(-5, 5), .end = generator.vector(-5, 5)};
        const Aabb box = Aabb::from_center(generator.vector(-1, 1), generator.vector(0.2F, 2));
        const Ray ray = Ray::from_points(segment.start, segment.end);
        const std::optional<float> hit = ray_distance(ray, box);

        // Skip segments that end within 0.001 of the box (rounding decides those).
        if (hit && std::abs(*hit - segment.length()) < 1e-3F)
        {
            continue;
        }
        EXPECT_EQ(intersects(segment, box), hit.has_value() && *hit <= segment.length()) << "sample " << sample;

        // Spheres: the segment's distance to the center, against the radius.
        const Sphere sphere{.center = generator.vector(-1, 1), .radius = generator.between(0.2F, 2)};
        const std::optional<float> round = ray_distance(ray, sphere);
        if (round && std::abs(*round - segment.length()) < 1e-3F)
        {
            continue;
        }
        EXPECT_EQ(intersects(segment, sphere), round.has_value() && *round <= segment.length()) << "sample " << sample;
    }
}

} // namespace
