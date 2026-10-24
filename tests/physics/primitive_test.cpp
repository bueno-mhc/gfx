// The primitives of the physics library: planes, boxes, spheres, oriented boxes, rays, segments, convex hulls and
// frustums. Most results are checked at compile time (the primitives are constexpr); the oriented boxes' separating
// axis test and the segments' closest points are compared at run time with straightforward references.
#include <gtest/gtest.h>

import std;
import gfx.core.errors;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.primitives;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::describe;
using gfx::tests::Generator;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

// Planes: the right-hand rule, distances, projections.
constexpr Plane FLOOR = Plane::from_points({.x = 0, .y = 2, .z = 0}, {.x = 0, .y = 2, .z = 1}, {.x = 1, .y = 2, .z = 0});
static_assert(FLOOR.normal == Vector3{.y = 1} && FLOOR.distance == 2, "counter-clockwise points seen from the front");
static_assert(FLOOR.signed_distance({.x = 5, .y = 3, .z = -1}) == 1 && FLOOR.flipped().signed_distance({.y = 3}) == -1);
static_assert(FLOOR.project({.x = 5, .y = 7, .z = -1}) == Vector3{.x = 5, .y = 2, .z = -1});
static_assert(Plane::from_coefficients({.x = 0, .y = 2, .z = 0, .w = -4}) == Plane{.normal = {.y = 1}, .distance = 2}, "normalized; a x + b y + c z + d >= 0 is in front");
static_assert(Plane::from_coefficients({}).signed_distance({.x = 1e30F}) > 0, "no direction: everything is in front");

// Boxes.
constexpr std::array BOX_POINTS{Vector3{.x = 1, .y = -2, .z = 3}, Vector3{.x = -1, .y = 4, .z = 0}, Vector3{.x = 0, .y = 0, .z = 5}};
constexpr Aabb POINTS_BOX = Aabb::from_points(BOX_POINTS);
static_assert(POINTS_BOX == Aabb{.minimum = {.x = -1, .y = -2, .z = 0}, .maximum = {.x = 1, .y = 4, .z = 5}});
static_assert(POINTS_BOX.center() == Vector3{.y = 1, .z = 2.5F} && POINTS_BOX.half_extents() == Vector3{.x = 1, .y = 3, .z = 2.5F});
static_assert(POINTS_BOX.volume() == 60 && POINTS_BOX.surface_area() == 2 * (12 + 30 + 10));
static_assert(Aabb::empty().is_empty() && !POINTS_BOX.is_empty() && Aabb::empty().merged(POINTS_BOX) == POINTS_BOX);
static_assert(POINTS_BOX.contains(Vector3{.x = 1, .y = 4, .z = 5}) && !POINTS_BOX.contains(Vector3{.x = 1.5F}));
static_assert(POINTS_BOX.contains(Aabb::from_center({.y = 1, .z = 2}, {.x = 1, .y = 1, .z = 1})));
static_assert(POINTS_BOX.closest_point({.x = 3, .y = 1, .z = -2}) == Vector3{.x = 1, .y = 1, .z = 0} && POINTS_BOX.squared_distance({.x = 3, .y = 1, .z = -2}) == 8);
static_assert(POINTS_BOX.corner(0) == POINTS_BOX.minimum && POINTS_BOX.corner(7) == POINTS_BOX.maximum && POINTS_BOX.corner(5) == Vector3{.x = 1, .y = -2, .z = 5});
static_assert(POINTS_BOX.expanded(1).contains(Vector3{.x = 2, .y = -3, .z = -1}));
static_assert(POINTS_BOX.support({.x = -1, .y = 1, .z = -1}) == Vector3{.x = -1, .y = 4, .z = 0});
static_assert(intersects(POINTS_BOX, Aabb::from_center({.x = 2}, {.x = 1, .y = 1, .z = 1})), "boxes that touch intersect");
static_assert(!intersects(POINTS_BOX, Aabb::from_center({.x = 2.01F}, {.x = 1, .y = 1, .z = 1})));
static_assert(POINTS_BOX.transformed(Matrix4::translation({.x = 1})) == Aabb{.minimum = {.x = 0, .y = -2}, .maximum = {.x = 2, .y = 4, .z = 5}});
static_assert(approximately_equal(POINTS_BOX.transformed(Transform{.orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)}).minimum, {.x = -4, .y = -1, .z = 0}, 1e-5F),
              "a quarter turn about Z takes the box's Y range to -X");

// Spheres.
constexpr Sphere UNIT = {.center = {}, .radius = 1};
static_assert(UNIT.contains(Vector3{.x = 1}) && !UNIT.contains(Vector3{.x = 0.8F, .y = 0.8F}));
static_assert(UNIT.bounds() == Aabb::from_center({}, {.x = 1, .y = 1, .z = 1}));
static_assert(UNIT.merged({.center = {.x = 4}, .radius = 1}) == Sphere{.center = {.x = 2}, .radius = 3});
static_assert(UNIT.merged({.center = {.x = 0.5F}, .radius = 0.25F}) == UNIT, "a sphere that holds the other is the merge");
static_assert(Sphere::from_points(BOX_POINTS).center == POINTS_BOX.center());
static_assert(intersects(UNIT, Sphere{.center = {.x = 2}, .radius = 1}) && !intersects(UNIT, Sphere{.center = {.x = 2.01F}, .radius = 1}));
static_assert(intersects(UNIT, Aabb::from_center({.x = 1.5F, .y = 1.5F}, {.x = 1, .y = 1, .z = 1})));
static_assert(!intersects(Aabb::from_center({.x = 1.8F, .y = 1.8F}, {.x = 1, .y = 1, .z = 1}), UNIT), "the box's corner is farther than the radius");

// Oriented boxes.
constexpr Obb TURNED = Obb::from_aabb(Aabb::from_center({}, {.x = 2, .y = 1, .z = 1}), {.position = {.x = 10}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)});
static_assert(approximately_equal(TURNED.axes()[0], {.y = 1}, 1e-6F) && approximately_equal(TURNED.bounds().half_extents(), {.x = 1, .y = 2, .z = 1}, 1e-5F));
static_assert(TURNED.contains(Vector3{.x = 10, .y = 1.9F}) && !TURNED.contains(Vector3{.x = 11.5F}));
static_assert(approximately_equal(TURNED.closest_point({.x = 10, .y = 5}), {.x = 10, .y = 2}, 1e-5F));
static_assert(intersects(TURNED, Sphere{.center = {.x = 10, .y = 2.5F}, .radius = 0.6F}) && !intersects(TURNED, Sphere{.center = {.x = 11.7F}, .radius = 0.6F}));
static_assert(intersects(TURNED, Aabb::from_center({.x = 10, .y = 2.5F}, {.x = 0.1F, .y = 0.6F, .z = 0.1F})));

// Rays and segments.
static_assert(Ray::from_points({.x = 1}, {.x = 1, .y = 5}) == Ray{.origin = {.x = 1}, .direction = {.y = 1}} && Ray{}.at(2) == Vector3{.z = -2});
constexpr Segment ROD{.start = {.x = -1}, .end = {.x = 3}};
static_assert(ROD.length() == 4 && ROD.at(0.25F) == Vector3{} && ROD.closest_fraction({.x = 5, .y = 1}) == 1 && ROD.closest_point({.x = 1, .y = 2}) == Vector3{.x = 1});
static_assert(ROD.squared_distance({.x = -3}) == 4 && ROD.support({.x = -1}) == ROD.start);
static_assert(Segment{.start = {.x = 1}, .end = {.x = 1}}.closest_fraction({.x = 7}) == 0, "a degenerate segment is its start");

// Convex hulls.
constexpr FixedConvexHull<8, 6> CUBE = box_hull({.x = 1, .y = 2, .z = 3});
static_assert(CUBE.bounds() == Aabb::from_center({}, {.x = 1, .y = 2, .z = 3}) && CUBE.contains({.x = 1, .y = -2, .z = 0}) && !CUBE.contains({.z = 3.1F}));
static_assert(CUBE.support({.x = 1, .y = -1, .z = 1}) == Vector3{.x = 1, .y = -2, .z = 3});

// What a convex shape is: support, margin and bounds.
static_assert(ConvexShape<Aabb> && ConvexShape<Sphere> && ConvexShape<Obb> && ConvexShape<Segment> && ConvexShape<ConvexHullView>
              && ConvexShape<FixedConvexHull<8, 6>> && ConvexShape<ConvexHull>);
static_assert(!ConvexShape<Plane> && !ConvexShape<Ray> && !ConvexShape<Frustum> && !ConvexShape<Vector3>);
static_assert(support_point(Sphere{.center = {.x = 1}, .radius = 2}, {.y = 10}) == Vector3{.x = 1, .y = 2}, "the margin rounds the core shape");
static_assert(support_point(POINTS_BOX, {.x = 1, .y = 1, .z = 1}) == POINTS_BOX.maximum);

// Frustums: a 90° perspective from 1 to 100 units along -Z.
constexpr Frustum VIEW = Frustum::from_matrix(gfx::tests::perspective(PI / 2, 1, 1, 100));
static_assert(VIEW.contains({.z = -10}) && VIEW.contains({.x = 9.9F, .y = -9.9F, .z = -10}) && VIEW.contains({.z = -99.9F}));
static_assert(!VIEW.contains({.z = -0.9F}) && !VIEW.contains({.z = -100.5F}) && !VIEW.contains({.x = 10.5F, .z = -10}) && !VIEW.contains({.z = 10}));

// ===================================================================================================== run time

/// How far apart two oriented boxes are along @p axis (negative: their projections overlap), from their corners.
auto
gap_along(Vector3 axis,
          const Obb& first,
          const Obb& second) -> float
{
    const auto projection = [axis](const Obb& box) {
        float low = std::numeric_limits<float>::max();
        float high = std::numeric_limits<float>::lowest();
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            const float along = dot(box.corner(corner), axis);
            low = std::min(low, along);
            high = std::max(high, along);
        }
        return std::pair{low, high};
    };
    const auto [first_low, first_high] = projection(first);
    const auto [second_low, second_high] = projection(second);
    return std::max(second_low - first_high, first_low - second_high);
}

/// The largest gap between two oriented boxes over the 15 axes of the separating axis theorem, from their corners
/// (positive: separated).
auto
separation(const Obb& first,
           const Obb& second) -> float
{
    std::vector<Vector3> axes;
    for (const Vector3 axis : first.axes())
    {
        axes.push_back(axis);
    }
    for (const Vector3 axis : second.axes())
    {
        axes.push_back(axis);
    }
    for (const Vector3 first_axis : first.axes())
    {
        for (const Vector3 second_axis : second.axes())
        {
            const Vector3 across = cross(first_axis, second_axis);
            if (length(across) > 1e-3F)
            {
                axes.push_back(normalized(across));
            }
        }
    }
    float largest = std::numeric_limits<float>::lowest();
    for (const Vector3 axis : axes)
    {
        largest = std::max(largest, gap_along(axis, first, second));
    }
    return largest;
}

TEST(PrimitivesTest, OrientedBoxesIntersectAsTheirCornersProject)
{
    // Random pairs; pairs within 0.001 of touching are skipped (rounding decides those).
    Generator generator{0x5EED};
    std::size_t intersecting = 0;
    std::size_t separated = 0;
    for (std::size_t sample = 0; sample < 4000; ++sample)
    {
        const Obb first{.center = generator.vector(-2, 2), .half_extents = generator.vector(0.1F, 1.5F), .orientation = generator.rotation()};
        // Every tenth pair is axis-aligned with the first, or parallel along one axis (cross products vanish).
        Quaternion orientation = generator.rotation();
        if (sample % 10 == 0)
        {
            orientation = first.orientation;
        }
        else if (sample % 10 == 1)
        {
            orientation = first.orientation * Quaternion::from_axis_angle({.z = 1}, generator.between(0, PI));
        }
        const Obb second{.center = generator.vector(-2, 2), .half_extents = generator.vector(0.1F, 1.5F), .orientation = orientation};
        const float gap = separation(first, second);
        if (std::abs(gap) < 1e-3F)
        {
            continue;
        }
        const bool expected = gap < 0;
        EXPECT_EQ(intersects(first, second), expected) << "sample " << sample << ", gap " << gap;
        EXPECT_EQ(intersects(second, first), expected) << "sample " << sample << ", gap " << gap;
        (expected ? intersecting : separated) += 1;
    }
    EXPECT_GT(intersecting, 500U);
    EXPECT_GT(separated, 500U);
}

TEST(PrimitivesTest, OrientedBoxBoundsHoldEveryCorner)
{
    Generator generator{77};
    for (std::size_t sample = 0; sample < 200; ++sample)
    {
        const Obb box{.center = generator.vector(-5, 5), .half_extents = generator.vector(0.1F, 3), .orientation = generator.rotation()};
        std::array<Vector3, 8> corners{};
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            corners[corner] = box.corner(corner);
            EXPECT_TRUE(box.contains(interpolate(box.center, corners[corner], 0.999F))) << describe(corners[corner]);
            EXPECT_FALSE(box.contains(interpolate(box.center, corners[corner], 1.001F))) << describe(corners[corner]);
        }

        // The bounds are the corners' bounds (tight), and the support point is the corner farthest along.
        const Aabb expected = Aabb::from_points(corners);
        EXPECT_TRUE(approximately_equal(box.bounds().minimum, expected.minimum, 1e-4F)) << describe(box.bounds().minimum) << " vs " << describe(expected.minimum);
        EXPECT_TRUE(approximately_equal(box.bounds().maximum, expected.maximum, 1e-4F)) << describe(box.bounds().maximum) << " vs " << describe(expected.maximum);
        const Vector3 direction = generator.direction();
        const float farthest = std::ranges::max(corners | std::views::transform([direction](Vector3 corner) { return dot(corner, direction); }));
        EXPECT_NEAR(dot(box.support(direction), direction), farthest, 1e-4F);

        // Transformed boxes: by the box's own matrix, the Arvo bounds equal the corners' bounds too.
        const Transform move{.position = generator.vector(-3, 3), .orientation = generator.rotation()};
        const Aabb moved = expected.transformed(move);
        Aabb moved_corners = Aabb::empty();
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            moved_corners = moved_corners.merged(transform_point(move, expected.corner(corner)));
        }
        EXPECT_TRUE(approximately_equal(moved.minimum, moved_corners.minimum, 1e-4F)) << describe(moved.minimum) << " vs " << describe(moved_corners.minimum);
        EXPECT_TRUE(approximately_equal(moved.maximum, moved_corners.maximum, 1e-4F)) << describe(moved.maximum) << " vs " << describe(moved_corners.maximum);
    }
}

TEST(PrimitivesTest, SegmentClosestPointsAreTheNearestPair)
{
    // The reference: for 2001 points along the first segment, the nearest point of the second (exact), the smallest
    // distance kept. The distance between the segments is convex along the first, so the sampled minimum is within
    // half a step of the true one.
    const auto reference = [](const Segment& first, const Segment& second) {
        float nearest = std::numeric_limits<float>::max();
        for (int step = 0; step <= 2000; ++step)
        {
            const Vector3 point = first.at(static_cast<float>(step) / 2000.0F);
            nearest = std::min(nearest, distance(point, second.closest_point(point)));
        }
        return nearest;
    };
    const auto check = [&reference](const Segment& first, const Segment& second, std::string_view label) {
        const SegmentClosestPoints closest = closest_points(first, second);
        ASSERT_GE(closest.first_fraction, 0.0F) << label;
        ASSERT_LE(closest.first_fraction, 1.0F) << label;
        ASSERT_GE(closest.second_fraction, 0.0F) << label;
        ASSERT_LE(closest.second_fraction, 1.0F) << label;
        EXPECT_TRUE(approximately_equal(closest.first_point, first.at(closest.first_fraction), 1e-6F)) << label;
        EXPECT_TRUE(approximately_equal(closest.second_point, second.at(closest.second_fraction), 1e-6F)) << label;
        const float found = distance(closest.first_point, closest.second_point);
        const float sampled = reference(first, second);
        EXPECT_LE(found, sampled + 1e-4F) << label;
        EXPECT_GE(found, sampled - 0.5F * first.length() / 2000.0F - 1e-4F) << label;
    };

    // Crossing, parallel (overlapping or not), collinear, degenerate (points), and random segments.
    check({.start = {.x = -1}, .end = {.x = 1}}, {.start = {.y = -1, .z = 1}, .end = {.y = 1, .z = 1}}, "crossing");
    check({.start = {}, .end = {.x = 2}}, {.start = {.x = 1, .y = 1}, .end = {.x = 3, .y = 1}}, "parallel overlapping");
    check({.start = {}, .end = {.x = 2}}, {.start = {.x = 3, .y = 1}, .end = {.x = 5, .y = 1}}, "parallel apart");
    check({.start = {}, .end = {.x = 2}}, {.start = {.x = 4}, .end = {.x = 6}}, "collinear");
    check({.start = {.y = 1}, .end = {.y = 1}}, {.start = {.x = -1}, .end = {.x = 1}}, "first is a point");
    check({.start = {.x = -1}, .end = {.x = 1}}, {.start = {.x = 3, .y = 2}, .end = {.x = 3, .y = 2}}, "second is a point");
    check({.start = {.z = 1}, .end = {.z = 1}}, {.start = {.y = 2}, .end = {.y = 2}}, "two points");
    Generator generator{4242};
    for (std::size_t sample = 0; sample < 300; ++sample)
    {
        const Segment first{.start = generator.vector(-2, 2), .end = generator.vector(-2, 2)};
        const Segment second{.start = generator.vector(-2, 2), .end = generator.vector(-2, 2)};
        check(first, second, std::format("random {}", sample));
    }
}

TEST(PrimitivesTest, ConvexHullsCheckTheirFaces)
{
    // A valid hull: a box's, through the fixed hull or spans.
    const ConvexHull cube{box_hull({.x = 1, .y = 2, .z = 3})};
    EXPECT_EQ(cube.bounds(), Aabb::from_center({}, {.x = 1, .y = 2, .z = 3}));
    EXPECT_EQ(cube.support({.x = -1, .y = 1, .z = -1}), (Vector3{.x = -1, .y = 2, .z = -3}));
    EXPECT_TRUE(cube.contains({.x = 0.5F}));
    EXPECT_FALSE(cube.contains({.y = 2.5F}));
    EXPECT_EQ(cube.view().vertices.size(), 8U);

    // A tetrahedron given by hand.
    const std::array<Vector3, 4> corners{Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}};
    const std::array<Plane, 4> faces{Plane{.normal = {.x = -1}, .distance = 0}, Plane{.normal = {.y = -1}, .distance = 0},
                                     Plane{.normal = {.z = -1}, .distance = 0},
                                     Plane::from_point_normal({.x = 1}, normalized(Vector3{.x = 1, .y = 1, .z = 1}))};
    const ConvexHull tetrahedron{corners, faces};
    EXPECT_TRUE(tetrahedron.contains({.x = 0.2F, .y = 0.2F, .z = 0.2F}));
    EXPECT_FALSE(tetrahedron.contains({.x = 0.5F, .y = 0.5F, .z = 0.5F}));

    // Invalid hulls: nothing to hold, a normal that is not unit length, a vertex in front of a face.
    const auto rejects = [](std::span<const Vector3> vertices, std::span<const Plane> planes, std::string_view expected) {
        try
        {
            [[maybe_unused]] const ConvexHull hull{vertices, planes};
            ADD_FAILURE() << "the hull must be rejected: " << expected;
        }
        catch (const PhysicsError& error)
        {
            const std::string_view message = error.what();
            EXPECT_TRUE(message.starts_with("[gfx::physics::GEOMETRY] ConvexHull::ConvexHull: ")) << message;
            EXPECT_NE(message.find(expected), std::string_view::npos) << message;
        }
    };
    rejects({}, faces, "it has 0 vertices and 4 faces");
    std::array<Plane, 4> long_normal = faces;
    long_normal[1].normal = {.y = -2};
    rejects(corners, long_normal, "the normal of face 1 has a length of 2, not 1");
    const std::array<Vector3, 5> outside{Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}, Vector3{.x = 1, .y = 1, .z = 1}};
    rejects(outside, faces, "vertex 4 is");
}

TEST(PrimitivesTest, FrustumsComeFromProjectionMatrices)
{
    // The planes of a 90° perspective: sides at 45°, near and far along Z; both depth ranges give the same planes.
    const Matrix4 projection = gfx::tests::perspective(PI / 2, 1, 1, 100);
    const Frustum frustum = Frustum::from_matrix(projection);
    const float diagonal = 1 / std::numbers::sqrt2_v<float>;
    EXPECT_TRUE(approximately_equal(frustum.plane(FrustumPlane::LEFT).normal, {.x = diagonal, .z = -diagonal}, 1e-5F));
    EXPECT_TRUE(approximately_equal(frustum.plane(FrustumPlane::TOP).normal, {.y = -diagonal, .z = -diagonal}, 1e-5F));
    EXPECT_TRUE(approximately_equal(frustum.plane(FrustumPlane::NEAR_PLANE).normal, {.z = -1}, 1e-6F));
    EXPECT_NEAR(frustum.plane(FrustumPlane::NEAR_PLANE).distance, 1, 1e-5F);
    EXPECT_TRUE(approximately_equal(frustum.plane(FrustumPlane::FAR_PLANE).normal, {.z = 1}, 1e-6F));
    EXPECT_NEAR(frustum.plane(FrustumPlane::FAR_PLANE).distance, -100, 5e-3F);
    const Frustum open_gl = Frustum::from_matrix(gfx::tests::perspective(PI / 2, 1, 1, 100, DepthRange::NEGATIVE_ONE_TO_ONE), DepthRange::NEGATIVE_ONE_TO_ONE);
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        EXPECT_TRUE(approximately_equal(open_gl.planes[plane].normal, frustum.planes[plane].normal, 1e-5F)) << "plane " << plane;
        EXPECT_NEAR(open_gl.planes[plane].distance, frustum.planes[plane].distance, 5e-3F) << "plane " << plane;
    }

    // Reversed depth (z' = w - z): the same planes once the flag says so.
    const Matrix4 flip{.columns = {Vector4{.x = 1}, Vector4{.y = 1}, Vector4{.z = -1}, Vector4{.z = 1, .w = 1}}};
    const Frustum reversed = Frustum::from_matrix(flip * projection, DepthRange::ZERO_TO_ONE, true);
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        EXPECT_TRUE(approximately_equal(reversed.planes[plane].normal, frustum.planes[plane].normal, 1e-5F)) << "plane " << plane;
        EXPECT_NEAR(reversed.planes[plane].distance, frustum.planes[plane].distance, 5e-3F) << "plane " << plane;
    }

    // An infinite far plane (reversed depth): no far plane bounds the frustum.
    const Frustum infinite = Frustum::from_matrix(gfx::tests::reversed_infinite_perspective(PI / 2, 1, 1), DepthRange::ZERO_TO_ONE, true);
    EXPECT_EQ(infinite.plane(FrustumPlane::FAR_PLANE).normal, Vector3{});
    EXPECT_TRUE(infinite.contains({.z = -1e20F}));
    EXPECT_FALSE(infinite.contains({.z = -0.5F}));
    EXPECT_TRUE(approximately_equal(infinite.plane(FrustumPlane::NEAR_PLANE).normal, {.z = -1}, 1e-6F));

    // A view-projection: the camera at (0, 0, 10) looking along -Z sees the origin, not what is behind it.
    const Frustum seen = Frustum::from_matrix(projection * Matrix4::translation({.z = -10}));
    EXPECT_TRUE(seen.contains({}));
    EXPECT_FALSE(seen.contains({.z = 10.5F}));
    EXPECT_FALSE(seen.contains({.x = 11, .z = 0}));
}

} // namespace
