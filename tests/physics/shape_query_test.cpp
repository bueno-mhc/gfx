// Queries against one shape: ray casts of convex shapes against their exact signed distance functions sampled along each
// ray (first hit, misses, normals, rays from inside), meshes, height fields and compounds against brute force over their
// triangles and children, shape casts against bisection on the overlap test, overlaps and closest points against GJK and
// analytic answers.
#include <gtest/gtest.h>

import std;
import gfx.physics.narrow_phase;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::Generator;

/// Distance tolerance of the conservative advancement (its touching distance is 1e-4).
constexpr float TOLERANCE = 2e-3F;

/// A convex shape and its exact signed distance in its own frame.
struct ConvexCase
{
    std::string name;
    std::shared_ptr<const Shape> shape;
    std::function<float(Vector3)> distance;
};

[[nodiscard]] auto
segment_distance(Vector3 point,
                 float half_height) -> float
{
    const Vector3 nearest{.y = std::clamp(point.y, -half_height, half_height)};
    return length(point - nearest);
}

[[nodiscard]] auto
box_distance(Vector3 point,
             Vector3 half_extents) -> float
{
    const Vector3 q{.x = std::abs(point.x) - half_extents.x, .y = std::abs(point.y) - half_extents.y, .z = std::abs(point.z) - half_extents.z};
    const Vector3 outside = component_maximum(q, {});
    return length(outside) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0F);
}

[[nodiscard]] auto
convex_cases() -> std::vector<ConvexCase>
{
    std::vector<ConvexCase> cases;
    cases.push_back({"sphere", make_shape(SphereShape{.radius = 1.2F}), [](Vector3 p) { return length(p) - 1.2F; }});
    cases.push_back({"capsule", make_shape(CapsuleShape{.half_height = 1.0F, .radius = 0.5F}), [](Vector3 p) { return segment_distance(p, 1.0F) - 0.5F; }});
    cases.push_back({"box", make_shape(BoxShape{.half_extents = {.x = 1.0F, .y = 0.5F, .z = 1.5F}}),
                     [](Vector3 p) { return box_distance(p, {.x = 1.0F, .y = 0.5F, .z = 1.5F}); }});
    cases.push_back({"rounded box", make_shape(BoxShape{.half_extents = {.x = 0.8F, .y = 0.6F, .z = 0.4F}, .radius = 0.2F}),
                     [](Vector3 p) { return box_distance(p, {.x = 0.8F, .y = 0.6F, .z = 0.4F}) - 0.2F; }});
    cases.push_back({"cylinder", make_shape(CylinderShape{.half_height = 0.7F, .radius = 0.9F}), [](Vector3 p) {
                         const float radial = std::sqrt(p.x * p.x + p.z * p.z) - 0.9F;
                         const float axial = std::abs(p.y) - 0.7F;
                         return std::min(std::max(radial, axial), 0.0F) + std::sqrt(std::max(radial, 0.0F) * std::max(radial, 0.0F) + std::max(axial, 0.0F) * std::max(axial, 0.0F));
                     }});

    // A hull: its planes give the distance inside exactly, and outside a lower bound (enough to tell hits from misses).
    const std::array<Vector3, 7> points{Vector3{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0.2F, .z = 0.1F}, {.x = 0, .y = 1.3F, .z = 0},
                                        {.x = 0.1F, .y = -0.9F, .z = 0.2F}, {.x = 0, .y = 0, .z = 1.1F}, {.x = 0.2F, .y = 0.1F, .z = -1},
                                        {.x = 0.6F, .y = 0.6F, .z = 0.6F}};
    const auto hull = std::make_shared<const Shape>(HullShape::from_points(points));
    cases.push_back({"hull", hull, [hull](Vector3 p) {
                         float farthest = -std::numeric_limits<float>::infinity();
                         for (const Plane& plane : hull->get_if<HullShape>()->view().planes)
                         {
                             farthest = std::max(farthest, plane.signed_distance(p));
                         }
                         return farthest;
                     }});
    return cases;
}

TEST(ShapeQueryTest, ConvexRayCastsFindTheFirstSurfacePoint)
{
    Generator generator{2024};
    for (const ConvexCase& convex : convex_cases())
    {
        std::size_t hits = 0;
        std::size_t misses = 0;
        for (int sample = 0; sample < 120; ++sample)
        {
            // A ray from around the shape toward a point near it, in a random pose.
            const Transform pose{.position = generator.vector(-5, 5), .orientation = generator.rotation()};
            const Vector3 origin_local = generator.direction() * generator.between(4, 6);
            const Vector3 target_local = generator.vector(-1.8F, 1.8F);
            const Ray ray{.origin = transform_point(pose, origin_local), .direction = rotate(pose.orientation, normalized(target_local - origin_local))};
            const float maximum = 12.0F;
            const std::optional<ShapeRayHit> hit = ray_cast_shape(*convex.shape, pose, ray, maximum);

            // The distance along the ray, sampled densely in the shape's frame.
            const Transform to_local = inverse(pose);
            const auto distance_at = [&](float t) { return convex.distance(transform_point(to_local, ray.at(t))); };
            constexpr float STEP = 2e-3F;
            float deepest = std::numeric_limits<float>::infinity();
            for (float t = 0; t <= maximum; t += STEP)
            {
                deepest = std::min(deepest, distance_at(t));
            }
            if (deepest < -TOLERANCE)
            {
                ASSERT_TRUE(hit.has_value()) << convex.name << " sample " << sample << ": the ray goes " << -deepest << " deep but no hit";
                ++hits;
                EXPECT_NEAR(distance_at(hit->distance), 0.0F, TOLERANCE) << convex.name << " sample " << sample << ": the hit is not on the surface";
                for (float t = 0; t < hit->distance - 1e-2F; t += STEP)
                {
                    ASSERT_GT(distance_at(t), -TOLERANCE) << convex.name << " sample " << sample << ": an earlier point at " << t << " is inside";
                }

                // The normal faces the ray and follows the distance's gradient.
                const Vector3 at = transform_point(to_local, ray.at(hit->distance));
                const float h = 1e-3F;
                const Vector3 gradient{.x = convex.distance(at + Vector3{.x = h}) - convex.distance(at - Vector3{.x = h}),
                                       .y = convex.distance(at + Vector3{.y = h}) - convex.distance(at - Vector3{.y = h}),
                                       .z = convex.distance(at + Vector3{.z = h}) - convex.distance(at - Vector3{.z = h})};
                EXPECT_LT(dot(hit->normal, ray.direction), 0.0F) << convex.name << " sample " << sample;
                EXPECT_GT(dot(hit->normal, rotate(pose.orientation, normalized(gradient))), 0.7F) << convex.name << " sample " << sample;
            }
            else if (deepest > TOLERANCE)
            {
                EXPECT_FALSE(hit.has_value()) << convex.name << " sample " << sample << ": a hit at " << hit->distance << " though the ray stays "
                                              << deepest << " away";
                ++misses;
            }
        }
        EXPECT_GT(hits, 15U) << convex.name;
        EXPECT_GT(misses, 5U) << convex.name;

        // A ray from inside hits at once, its normal against it; a short ray stops before the surface.
        const Transform pose{.position = {.x = 1, .y = 2, .z = 3}};
        const Ray inside{.origin = pose.position, .direction = {.x = 1}};
        const std::optional<ShapeRayHit> start = ray_cast_shape(*convex.shape, pose, inside, 10);
        ASSERT_TRUE(start.has_value()) << convex.name;
        EXPECT_EQ(start->distance, 0.0F) << convex.name;
        EXPECT_EQ(start->normal, (Vector3{.x = -1})) << convex.name;
        EXPECT_FALSE(ray_cast_shape(*convex.shape, pose, Ray{.origin = {.x = 10, .y = 2, .z = 3}, .direction = {.x = -1}}, 5).has_value()) << convex.name;
    }
}

/// The nearest hit of @p ray among @p triangles (two-sided or front only), by testing each.
[[nodiscard]] auto
brute_force_triangles(std::span<const TriangleShape> triangles,
                      const Ray& ray,
                      float maximum,
                      bool two_sided) -> std::optional<float>
{
    std::optional<float> best;
    for (const TriangleShape& triangle : triangles)
    {
        // The plane, then the inside test by edge sides.
        const Vector3 normal = cross(triangle.second - triangle.first, triangle.third - triangle.first);
        const float approach = dot(normal, ray.direction);
        if (approach == 0.0F || (!two_sided && approach > 0.0F))
        {
            continue;
        }
        const float t = dot(normal, triangle.first - ray.origin) / approach;
        if (t < 0.0F || t > maximum)
        {
            continue;
        }
        const Vector3 p = ray.at(t);
        const bool inside = dot(cross(triangle.second - triangle.first, p - triangle.first), normal) >= 0.0F
                            && dot(cross(triangle.third - triangle.second, p - triangle.second), normal) >= 0.0F
                            && dot(cross(triangle.first - triangle.third, p - triangle.third), normal) >= 0.0F;
        if (inside && (!best || t < *best))
        {
            best = t;
        }
    }
    return best;
}

TEST(ShapeQueryTest, MeshAndHeightFieldRayCastsMatchBruteForce)
{
    // A bumpy height field of 40 × 30 cells and the same surface as a one-sided triangle mesh.
    constexpr std::size_t ROWS = 31;
    constexpr std::size_t COLUMNS = 41;
    std::vector<float> heights(ROWS * COLUMNS);
    for (std::size_t row = 0; row < ROWS; ++row)
    {
        for (std::size_t column = 0; column < COLUMNS; ++column)
        {
            heights[row * COLUMNS + column] = 0.6F * std::sin(0.4F * static_cast<float>(column)) * std::cos(0.3F * static_cast<float>(row));
        }
    }
    const Vector3 scale{.x = 0.5F, .y = 2.0F, .z = 0.7F};
    const HeightFieldShape field{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), ROWS, COLUMNS}, .scale = scale}};
    std::vector<TriangleShape> triangles;
    for (std::uint32_t index = 0; index < field.triangle_count(); ++index)
    {
        triangles.push_back(field.triangle(index));
    }
    std::vector<Vector3> vertices;
    std::vector<std::array<std::uint32_t, 3>> indices;
    for (const TriangleShape& triangle : triangles)
    {
        const auto first = static_cast<std::uint32_t>(vertices.size());
        vertices.insert(vertices.end(), {triangle.first, triangle.second, triangle.third});
        indices.push_back({first, first + 1, first + 2});
    }
    const Shape field_shape{field};
    const Shape mesh_shape{TriangleMeshShape{{.vertices = vertices, .triangles = indices}}};
    const Shape two_sided_shape{TriangleMeshShape{{.vertices = vertices, .triangles = indices, .two_sided = true}}};

    // Rays from above at every angle down to grazing, and from below; the field in a pose.
    Generator generator{77};
    const Transform pose{.position = {.x = 3, .y = -1, .z = 2}, .orientation = Quaternion::from_axis_angle({.y = 1}, 0.4F)};
    std::size_t hits = 0;
    for (int sample = 0; sample < 300; ++sample)
    {
        const bool from_below = sample % 5 == 0;
        const Vector3 origin_local{.x = generator.between(-5, 25), .y = from_below ? -4.0F : generator.between(1.5F, 6), .z = generator.between(-5, 25)};
        const Vector3 target_local{.x = generator.between(0, 20), .y = generator.between(-1.2F, 1.2F), .z = generator.between(0, 21)};
        const Ray local{.origin = origin_local, .direction = normalized(target_local - origin_local)};
        const Ray ray{.origin = transform_point(pose, local.origin), .direction = rotate(pose.orientation, local.direction)};
        const float maximum = sample % 3 == 0 ? std::numeric_limits<float>::infinity() : 25.0F;

        const std::optional<float> one_sided = brute_force_triangles(triangles, local, maximum, false);
        const std::optional<float> both_sides = brute_force_triangles(triangles, local, maximum, true);
        for (const auto& [shape, expected, name] : {std::tuple{&field_shape, one_sided, "height field"}, std::tuple{&mesh_shape, one_sided, "mesh"},
                                                    std::tuple{&two_sided_shape, both_sides, "two-sided mesh"}})
        {
            const std::optional<ShapeRayHit> hit = ray_cast_shape(*shape, pose, ray, maximum);
            ASSERT_EQ(hit.has_value(), expected.has_value()) << name << " sample " << sample;
            if (hit)
            {
                EXPECT_NEAR(hit->distance, *expected, 1e-3F) << name << " sample " << sample;
                EXPECT_LT(dot(hit->normal, ray.direction), 0.0F) << name << " sample " << sample;
                EXPECT_LT(hit->sub_shape, field.triangle_count()) << name;
                hits += shape == &field_shape ? 1U : 0U;
            }
        }
    }
    EXPECT_GT(hits, 150U);
}

TEST(ShapeQueryTest, CompoundRayCastsHitTheNearestChild)
{
    const Shape compound{CompoundShape{{CompoundChild{.shape = make_shape(SphereShape{.radius = 1}), .pose = {.position = {.x = -3}}},
                                        CompoundChild{.shape = make_shape(BoxShape{.half_extents = {.x = 1, .y = 1, .z = 1}}), .pose = {.position = {.x = 3}}}}}};
    const Transform pose{.position = {.y = 10}};

    // Along +X the sphere (child 0) comes first, along -X the box (child 1).
    const std::optional<ShapeRayHit> forward = ray_cast_shape(compound, pose, Ray{.origin = {.x = -10, .y = 10}, .direction = {.x = 1}}, 100);
    ASSERT_TRUE(forward.has_value());
    EXPECT_NEAR(forward->distance, 6.0F, 1e-4F);
    EXPECT_EQ(forward->sub_shape, 0U);
    const std::optional<ShapeRayHit> backward = ray_cast_shape(compound, pose, Ray{.origin = {.x = 10, .y = 10}, .direction = {.x = -1}}, 100);
    ASSERT_TRUE(backward.has_value());
    EXPECT_NEAR(backward->distance, 6.0F, 1e-3F);
    EXPECT_EQ(backward->sub_shape, 1U);
    EXPECT_NEAR(backward->normal.x, 1.0F, 1e-3F);
    EXPECT_FALSE(ray_cast_shape(compound, pose, Ray{.origin = {.x = 10, .y = 13}, .direction = {.x = -1}}, 100).has_value());
}

TEST(ShapeQueryTest, ShapeCastsStopWhereTheShapesFirstTouch)
{
    Generator generator{99};
    const std::vector<ConvexCase> cases = convex_cases();
    std::size_t hits = 0;
    for (int sample = 0; sample < 200; ++sample)
    {
        // A moving shape aimed past a target, both in random poses.
        const ConvexCase& moving = cases[static_cast<std::size_t>(sample) % cases.size()];
        const ConvexCase& target = cases[static_cast<std::size_t>(sample / 7) % cases.size()];
        const Transform target_pose{.position = generator.vector(-1, 1), .orientation = generator.rotation()};
        const Transform start{.position = generator.direction() * 6.0F, .orientation = generator.rotation()};
        const Vector3 translation = (generator.vector(-1.5F, 1.5F) - start.position) * 1.6F;
        const std::optional<ShapeCastResult> hit = cast_shape(*moving.shape, start, translation, *target.shape, target_pose);

        // The reference: the shapes' distance along the motion, in fine steps (GJK and EPA: GJK over-estimates distances, so
        // a small one is a touch).
        const auto distance_at = [&](float fraction) {
            const Transform at{.position = start.position + translation * fraction, .orientation = start.orientation};
            return shape_distance({.shape = moving.shape.get(), .pose = at}, {.shape = target.shape.get(), .pose = target_pose}).distance;
        };
        constexpr int STEPS = 512;
        float deepest = std::numeric_limits<float>::infinity();
        for (int index = 0; index <= STEPS; ++index)
        {
            deepest = std::min(deepest, distance_at(static_cast<float>(index) / STEPS));
        }
        const std::string label = std::format("{} cast at {}, sample {}", moving.name, target.name, sample);
        if (deepest < -TOLERANCE)
        {
            // A clear overlap is hit: where the shapes touch, with no clear overlap before.
            ASSERT_TRUE(hit.has_value()) << label;
            EXPECT_NEAR(distance_at(hit->fraction), 0.0F, TOLERANCE) << label << ": not touching at the hit (" << hit->fraction << ")";
            for (int index = 0; static_cast<float>(index) / STEPS < hit->fraction; ++index)
            {
                ASSERT_GT(distance_at(static_cast<float>(index) / STEPS), -TOLERANCE) << label << ": overlapping before the hit at " << hit->fraction;
            }
            EXPECT_GT(dot(hit->normal, translation), 0.0F) << label;
            ++hits;
        }
        else if (deepest > TOLERANCE)
        {
            EXPECT_FALSE(hit.has_value()) << label << " hits at " << hit->fraction << ", the shapes staying " << deepest << " apart";
        }
    }
    EXPECT_GT(hits, 40U);

    // Overlapping at the start: fraction 0; moving away: no hit; a one-sided mesh stops what comes at its front only.
    const Shape ball{SphereShape{.radius = 0.5F}};
    EXPECT_EQ(cast_shape(ball, {}, {.x = 1}, ball, {.position = {.x = 0.5F}})->fraction, 0.0F);
    EXPECT_FALSE(cast_shape(ball, {}, {.x = -1}, ball, {.position = {.x = 2}}).has_value());
    const std::array<Vector3, 4> corners{Vector3{.x = -5, .z = -5}, {.x = 5, .z = -5}, {.x = 5, .z = 5}, {.x = -5, .z = 5}};
    const std::array<std::array<std::uint32_t, 3>, 2> quad{{{0, 2, 1}, {0, 3, 2}}};
    const Shape floor{TriangleMeshShape{{.vertices = corners, .triangles = quad}}};
    const std::optional<ShapeCastResult> landing = cast_shape(ball, {.position = {.y = 3}}, {.y = -5}, floor, {});
    ASSERT_TRUE(landing.has_value());
    EXPECT_NEAR(landing->fraction, 2.5F / 5.0F, 1e-3F);
    EXPECT_NEAR(landing->normal.y, -1.0F, 1e-3F);
    EXPECT_FALSE(cast_shape(ball, {.position = {.y = -3}}, {.y = 5}, floor, {}).has_value()) << "from behind a one-sided floor";
}

TEST(ShapeQueryTest, OverlapsAndClosestPointsMatchGjkAndAnalyticAnswers)
{
    Generator generator{5};
    const std::vector<ConvexCase> cases = convex_cases();
    std::size_t overlapping = 0;
    for (int sample = 0; sample < 300; ++sample)
    {
        // Overlap tests agree with the distance of GJK and EPA.
        const ConvexCase& first = cases[static_cast<std::size_t>(sample) % cases.size()];
        const ConvexCase& second = cases[static_cast<std::size_t>(sample / 5) % cases.size()];
        const Transform first_pose{.position = generator.vector(-1.5F, 1.5F), .orientation = generator.rotation()};
        const Transform second_pose{.position = generator.vector(-1.5F, 1.5F), .orientation = generator.rotation()};
        const ShapeDistance distance = shape_distance({.shape = first.shape.get(), .pose = first_pose}, {.shape = second.shape.get(), .pose = second_pose});
        if (std::abs(distance.distance) > 1e-3F)
        {
            EXPECT_EQ(shapes_overlap(*first.shape, first_pose, *second.shape, second_pose), distance.distance < 0.0F) << first.name << " and " << second.name;
            overlapping += distance.distance < 0.0F ? 1U : 0U;
        }

        // The closest point of a convex shape: on its surface, at the signed distance.
        const Vector3 point = generator.vector(-3, 3);
        const std::optional<ShapeClosestPoint> closest = closest_point_on_shape(*first.shape, first_pose, point, std::numeric_limits<float>::infinity());
        ASSERT_TRUE(closest.has_value());
        const float exact = first.name == "hull" ? closest->distance : first.distance(transform_point(inverse(first_pose), point));
        EXPECT_NEAR(closest->distance, exact, TOLERANCE) << first.name << " sample " << sample;
        EXPECT_NEAR(first.distance(transform_point(inverse(first_pose), closest->point)), 0.0F, TOLERANCE) << first.name << " sample " << sample;
        if (closest->distance > 0.0F)
        {
            EXPECT_NEAR(length(closest->point - point), closest->distance, TOLERANCE) << first.name;
        }
        EXPECT_FALSE(closest_point_on_shape(*first.shape, first_pose, first_pose.position + Vector3{.x = 50}, 10).has_value());
    }
    EXPECT_GT(overlapping, 30U);

    // Triangles: the nearest of a mesh's, from either side.
    const std::array<Vector3, 4> corners{Vector3{.x = -1, .z = -1}, {.x = 1, .z = -1}, {.x = 1, .z = 1}, {.x = -1, .z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 2> quad{{{0, 2, 1}, {0, 3, 2}}};
    const Shape floor{TriangleMeshShape{{.vertices = corners, .triangles = quad}}};
    const Transform raised{.position = {.y = 2}};
    const std::optional<ShapeClosestPoint> above = closest_point_on_shape(floor, raised, {.x = 0.3F, .y = 5, .z = -0.2F}, 10);
    ASSERT_TRUE(above.has_value());
    EXPECT_NEAR(above->distance, 3.0F, 1e-4F);
    EXPECT_NEAR(above->point.y, 2.0F, 1e-5F);
    const std::optional<ShapeClosestPoint> corner = closest_point_on_shape(floor, raised, {.x = 4, .y = 2, .z = 5}, 10);
    ASSERT_TRUE(corner.has_value());
    EXPECT_NEAR(corner->distance, 5.0F, 1e-4F) << "to the corner (1, 2, 1)";
    EXPECT_TRUE(shapes_overlap(Shape{SphereShape{.radius = 0.5F}}, {.position = {.y = 1.6F}}, floor, raised)) << "from below too";
    EXPECT_FALSE(shapes_overlap(Shape{SphereShape{.radius = 0.5F}}, {.position = {.y = 1.4F}}, floor, raised));
    EXPECT_FALSE(shapes_overlap(floor, raised, Shape{SphereShape{.radius = 0.5F}}, {})) << "the first shape must be convex";
}

} // namespace
