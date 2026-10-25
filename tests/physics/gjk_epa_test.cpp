// GJK and EPA: distances against exact references over random poses (a sphere and a box: the box's closest point; a
// capsule and a box: the closest point sampled along the segment; two spheres), penetration depths and normals against
// the separating axis test's minimum (exact for boxes), touching shapes, and both at compile time.
#include <gtest/gtest.h>

import std;
import gfx.physics.narrow_phase;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::describe;
using gfx::tests::Generator;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

constexpr Transform AT_ORIGIN{};
constexpr Transform THREE_RIGHT{.position = {.x = 3}};
static_assert(std::abs(gjk(SphereShape{.radius = 1}, AT_ORIGIN, SphereShape{.radius = 0.5F}, THREE_RIGHT).distance - 3) < 1e-6F, "between the cores: the centers");
static_assert(std::abs(convex_distance(SphereShape{.radius = 1}, AT_ORIGIN, SphereShape{.radius = 0.5F}, THREE_RIGHT).distance - 1.5F) < 1e-6F);
static_assert(std::abs(convex_distance(BoxShape{}, AT_ORIGIN, BoxShape{}, {.position = {.y = 1.25F}}).distance - 0.25F) < 1e-6F);
static_assert(gjk(BoxShape{}, AT_ORIGIN, BoxShape{}, {.position = {.y = 0.75F}}).status == GjkStatus::OVERLAPPING);
static_assert(std::abs(convex_distance(BoxShape{}, AT_ORIGIN, BoxShape{}, {.position = {.y = 0.75F}}).distance + 0.25F) < 1e-5F, "EPA: overlapping by a quarter");
static_assert(approximately_equal(convex_distance(BoxShape{}, AT_ORIGIN, BoxShape{}, {.position = {.y = 0.75F}}).normal, {.y = 1}, 1e-5F));

// ===================================================================================================== run time

/// The separating axis test's least overlap of two boxes (their penetration depth: for polytopes it is reached along
/// a face normal or an edges' cross product) and its axis, from the first to the second; negative: the gap.
auto
box_penetration(const BoxShape& first,
                const Transform& first_pose,
                const BoxShape& second,
                const Transform& second_pose) -> std::pair<float, Vector3>
{
    const Obb a{.center = first_pose.position, .half_extents = first.half_extents, .orientation = first_pose.orientation};
    const Obb b{.center = second_pose.position, .half_extents = second.half_extents, .orientation = second_pose.orientation};
    std::vector<Vector3> axes;
    for (const Vector3 axis : a.axes())
    {
        axes.push_back(axis);
    }
    for (const Vector3 axis : b.axes())
    {
        axes.push_back(axis);
    }
    for (const Vector3 first_axis : a.axes())
    {
        for (const Vector3 second_axis : b.axes())
        {
            const Vector3 across = cross(first_axis, second_axis);
            if (length(across) > 1e-3F)
            {
                axes.push_back(normalized(across));
            }
        }
    }
    float least = std::numeric_limits<float>::max();
    Vector3 best{};
    for (Vector3 axis : axes)
    {
        // The overlap along the axis, oriented from the first box to the second.
        axis = dot(axis, b.center - a.center) >= 0 ? axis : -axis;
        float a_high = std::numeric_limits<float>::lowest();
        float b_low = std::numeric_limits<float>::max();
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            a_high = std::max(a_high, dot(a.corner(corner), axis));
            b_low = std::min(b_low, dot(b.corner(corner), axis));
        }
        if (a_high - b_low < least)
        {
            least = a_high - b_low;
            best = axis;
        }
    }
    return {least, best};
}

TEST(GjkEpaTest, DistancesMatchExactReferences)
{
    Generator generator{1234};
    for (std::size_t sample = 0; sample < 500; ++sample)
    {
        // A sphere and a turned box: the distance from the center to the box's closest point, minus the radius.
        const BoxShape box{.half_extents = generator.vector(0.2F, 2)};
        const Transform box_pose{.position = generator.vector(-1, 1), .orientation = generator.rotation()};
        const SphereShape sphere{.radius = generator.between(0.1F, 1)};
        const Transform sphere_pose{.position = generator.vector(-5, 5)};
        const Obb oriented{.center = box_pose.position, .half_extents = box.half_extents, .orientation = box_pose.orientation};
        const Vector3 closest = oriented.closest_point(sphere_pose.position);
        const float expected = distance(closest, sphere_pose.position) - sphere.radius;
        if (distance(closest, sphere_pose.position) > 1e-3F)
        {
            const ShapeDistance found = convex_distance(sphere, sphere_pose, box, box_pose);
            EXPECT_NEAR(found.distance, expected, 1e-4F) << "sample " << sample;
            EXPECT_TRUE(approximately_equal(found.point_second, closest, 1e-3F)) << "sample " << sample << ": " << describe(found.point_second) << " vs " << describe(closest);
        }

        // A capsule and the box: the closest point of the segment, sampled (the distance is convex along it).
        const CapsuleShape capsule{.half_height = generator.between(0.1F, 2), .radius = generator.between(0.1F, 0.5F)};
        const Transform capsule_pose{.position = generator.vector(-5, 5), .orientation = generator.rotation()};
        float nearest = std::numeric_limits<float>::max();
        const Vector3 top = transform_point(capsule_pose, {.y = capsule.half_height});
        const Vector3 bottom = transform_point(capsule_pose, {.y = -capsule.half_height});
        for (int step = 0; step <= 2000; ++step)
        {
            const Vector3 point = interpolate(bottom, top, static_cast<float>(step) / 2000);
            nearest = std::min(nearest, distance(oriented.closest_point(point), point));
        }
        if (nearest > 1e-2F)
        {
            const float found = convex_distance(capsule, capsule_pose, box, box_pose).distance + capsule.radius;
            EXPECT_LE(found, nearest + 1e-4F) << "sample " << sample;
            EXPECT_GE(found, nearest - capsule.half_height / 1000 - 1e-4F) << "sample " << sample;
        }
    }
}

TEST(GjkEpaTest, PenetrationsMatchTheSeparatingAxisMinimum)
{
    Generator generator{777};
    std::size_t overlapping = 0;
    for (std::size_t sample = 0; sample < 1000; ++sample)
    {
        const BoxShape first{.half_extents = generator.vector(0.3F, 1.5F)};
        const BoxShape second{.half_extents = generator.vector(0.3F, 1.5F)};
        const Transform first_pose{.position = generator.vector(-0.5F, 0.5F), .orientation = generator.rotation()};
        const Transform second_pose{.position = generator.vector(-1.5F, 1.5F), .orientation = generator.rotation()};
        const auto [penetration, axis] = box_penetration(first, first_pose, second, second_pose);
        if (penetration < 1e-3F)
        {
            continue;
        }
        ++overlapping;
        const ShapeDistance found = convex_distance(first, first_pose, second, second_pose);
        EXPECT_NEAR(-found.distance, penetration, 2e-3F * penetration + 1e-4F) << "sample " << sample;

        // The normal separates them by the depth: moving the second box along it leaves them touching.
        const auto [after, after_axis] = box_penetration(first, first_pose, second, {.position = second_pose.position + found.normal * (-found.distance), .orientation = second_pose.orientation});
        EXPECT_NEAR(after, 0, 3e-3F * penetration + 1e-4F) << "sample " << sample << ", normal " << describe(found.normal) << " vs " << describe(axis);
    }
    EXPECT_GT(overlapping, 300U);
}

TEST(GjkEpaTest, CurvedShapesAndTouchingShapes)
{
    // Spheres overlapping: depth r1 + r2 - d along the centers' line; a sphere's core inside a box: out by its nearest face.
    const ShapeDistance spheres = convex_distance(SphereShape{.radius = 1}, {}, SphereShape{.radius = 1}, {.position = {.x = 1.5F, .y = 0.5F}});
    EXPECT_NEAR(spheres.distance, std::sqrt(2.5F) - 2, 1e-5F);
    EXPECT_TRUE(approximately_equal(spheres.normal, normalized(Vector3{.x = 1.5F, .y = 0.5F}), 1e-5F));
    const ShapeDistance inside = convex_distance(BoxShape{.half_extents = {.x = 2, .y = 1, .z = 2}}, {}, SphereShape{.radius = 0.25F}, {.position = {.x = 0.5F, .y = 0.75F}});
    EXPECT_NEAR(inside.distance, -0.5F, 1e-4F) << "0.25 to the top face plus the radius";
    EXPECT_TRUE(approximately_equal(inside.normal, {.y = 1}, 1e-4F)) << describe(inside.normal);

    // A cylinder over a box and a cone over it (smooth supports converge slowly but within the iterations).
    const ShapeDistance cylinder = convex_distance(BoxShape{}, {}, CylinderShape{.half_height = 1, .radius = 0.5F},
                                                   {.position = {.x = 0.2F, .y = 2}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)});
    EXPECT_NEAR(cylinder.distance, 1.0F, 1e-4F) << "lying on its side, 1 above the top face";
    const ShapeDistance cone = convex_distance(BoxShape{}, {}, ConeShape{.half_height = 0.5F, .radius = 0.5F},
                                               {.position = {.y = 1.25F}, .orientation = Quaternion::from_axis_angle({.x = 1}, PI)});
    EXPECT_NEAR(cone.distance, 0.25F, 1e-4F) << "upside down, its apex 0.25 above the box";

    // Touching exactly: depth 0, either way, without failing.
    const ShapeDistance touching = convex_distance(BoxShape{}, {}, BoxShape{}, {.position = {.x = 1}});
    EXPECT_NEAR(touching.distance, 0, 1e-5F);
    EXPECT_TRUE(approximately_equal(touching.normal, {.x = 1}, 1e-4F)) << describe(touching.normal);
}

TEST(GjkEpaTest, SliversOfNearlyRepeatedPointsDoNotMakeSeparatedShapesOverlap)
{
    // A hull 0.436 away from a cylinder (found by the shape-cast test): the cylinder's rim gave GJK three support points
    // almost together, and the tetrahedron they made with a fourth "held" the origin. The poses are exact (hex floats).
    const std::array<Vector3, 7> points{Vector3{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0.2F, .z = 0.1F}, {.x = 0, .y = 1.3F, .z = 0},
                                        {.x = 0.1F, .y = -0.9F, .z = 0.2F}, {.x = 0, .y = 0, .z = 1.1F}, {.x = 0.2F, .y = 0.1F, .z = -1},
                                        {.x = 0.6F, .y = 0.6F, .z = 0.6F}};
    const HullShape hull = HullShape::from_points(points);
    const CylinderShape cylinder{.half_height = 0.7F, .radius = 0.9F};
    const Transform hull_pose{.position = {.x = 0x1.2ae9cep+0F, .y = 0x1.337b1p-2F, .z = 0x1.093fp-4F},
                              .orientation = {.x = -0x1.5629ecp-1F, .y = 0x1.8172f8p-5F, .z = 0x1.735a92p-3F, .w = 0x1.709a86p-1F}};
    const Transform cylinder_pose{.position = {.x = -0x1.0333fcp-1F, .y = -0x1.bc0f02p-1F, .z = 0x1.c7a28cp-1F},
                                  .orientation = {.x = -0x1.12ff6ep-3F, .y = -0x1.d078e2p-3F, .z = 0x1.262904p-11F, .w = 0x1.ede592p-1F}};
    const GjkResult result = gjk(hull, hull_pose, cylinder, cylinder_pose);
    EXPECT_EQ(result.status, GjkStatus::SEPARATED);
    EXPECT_NEAR(convex_distance(hull, hull_pose, cylinder, cylinder_pose).distance, 0.436F, 2e-3F);
}

} // namespace
