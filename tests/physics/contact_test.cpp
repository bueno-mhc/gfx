// Contact manifolds of convex pairs: resting shapes get the points their supporting features give (a box on a box 4, a
// capsule lying 2, a cylinder standing 4 of its cap's 8), feature ids that stay through small motions, speculative
// contacts, the reversed order giving the reversed normal, the analytic paths agreeing with the general one, the
// reduction of many points to four, and a manifold computed at compile time.
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

// A box resting 0.01 into another: four points at the top face's corners, normal +Y, the box-box separating axes.
constexpr std::optional<ContactManifold> RESTING = collide_convex(BoxShape{.half_extents = {.x = 2, .y = 0.5F, .z = 2}}, Transform{}, BoxShape{},
                                                                  Transform{.position = {.y = 0.99F}});
static_assert(RESTING.has_value() && RESTING->point_count == 4 && RESTING->algorithm == ContactAlgorithm::BOX_BOX);
static_assert(approximately_equal(RESTING->normal, {.y = 1}, 1e-6F) && std::abs(RESTING->deepest() + 0.01F) < 1e-5F);
static_assert(!collide_convex(BoxShape{}, Transform{}, BoxShape{}, Transform{.position = {.y = 1.1F}}).has_value(), "beyond the speculative distance");

// ===================================================================================================== run time

/// The local features of @p manifold's points, sorted.
auto
features_of(const ContactManifold& manifold) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> features;
    for (const ContactPoint& point : manifold.contacts())
    {
        features.push_back(point.feature.local);
    }
    std::ranges::sort(features);
    return features;
}

TEST(ContactTest, RestingShapesGetTheirFeaturesPoints)
{
    // A ground box and shapes resting 0.01 deep on its top (y = 0).
    const BoxShape ground{.half_extents = {.x = 5, .y = 1, .z = 5}};
    const Transform ground_pose{.position = {.y = -1}};
    const auto check = [&](const auto& shape, const Transform& pose, std::uint32_t points, ContactAlgorithm algorithm, std::string_view label) {
        const std::optional<ContactManifold> manifold = collide_convex(ground, ground_pose, shape, pose);
        ASSERT_TRUE(manifold.has_value()) << label;
        EXPECT_EQ(manifold->point_count, points) << label;
        EXPECT_EQ(manifold->algorithm, algorithm) << label;
        EXPECT_TRUE(approximately_equal(manifold->normal, {.y = 1}, 1e-4F)) << label << ": " << describe(manifold->normal);
        for (const ContactPoint& point : manifold->contacts())
        {
            EXPECT_NEAR(point.separation, -0.01F, 1e-4F) << label;
            EXPECT_NEAR(point.position.y, -0.005F, 1e-3F) << label << ": midway between the surfaces";
        }
    };
    check(BoxShape{}, {.position = {.y = 0.49F}}, 4, ContactAlgorithm::BOX_BOX, "box");
    check(SphereShape{.radius = 0.5F}, {.position = {.y = 0.49F}}, 1, ContactAlgorithm::SPHERE_BOX, "sphere");
    check(CapsuleShape{.half_height = 1, .radius = 0.25F}, {.position = {.y = 0.24F}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)}, 2,
          ContactAlgorithm::GJK, "capsule lying");
    check(CapsuleShape{.half_height = 1, .radius = 0.25F}, {.position = {.y = 1.24F}}, 1, ContactAlgorithm::GJK, "capsule standing");
    check(CylinderShape{.half_height = 0.5F, .radius = 0.5F}, {.position = {.y = 0.49F}}, 4, ContactAlgorithm::EPA, "cylinder standing");
    check(CylinderShape{.half_height = 0.5F, .radius = 0.5F}, {.position = {.y = 0.49F}, .orientation = Quaternion::from_axis_angle({.x = 1}, PI / 2)}, 2,
          ContactAlgorithm::EPA, "cylinder lying");
    check(ConeShape{.half_height = 0.5F, .radius = 0.5F}, {.position = {.y = 0.49F}}, 4, ContactAlgorithm::EPA, "cone on its base");
    const HullShape cube = HullShape::from_points(std::array{Vector3{.x = -0.5F, .y = -0.5F, .z = -0.5F}, Vector3{.x = 0.5F, .y = -0.5F, .z = -0.5F},
                                                              Vector3{.x = -0.5F, .y = 0.5F, .z = -0.5F}, Vector3{.x = 0.5F, .y = 0.5F, .z = -0.5F},
                                                              Vector3{.x = -0.5F, .y = -0.5F, .z = 0.5F}, Vector3{.x = 0.5F, .y = -0.5F, .z = 0.5F},
                                                              Vector3{.x = -0.5F, .y = 0.5F, .z = 0.5F}, Vector3{.x = 0.5F, .y = 0.5F, .z = 0.5F}});
    check(cube, {.position = {.y = 0.49F}}, 4, ContactAlgorithm::EPA, "hull cube");
    check(BoxShape{.half_extents = {.x = 0.4F, .y = 0.4F, .z = 0.4F}, .radius = 0.1F}, {.position = {.y = 0.49F}}, 4, ContactAlgorithm::BOX_BOX, "rounded box");
}

TEST(ContactTest, FeatureIdsSurviveSmallMotions)
{
    // A box resting on another, nudged and turned a little each step: the same four features (warm starting holds).
    const BoxShape ground{.half_extents = {.x = 5, .y = 1, .z = 5}};
    const Transform ground_pose{.position = {.y = -1}};
    const BoxShape crate{};
    const std::optional<ContactManifold> start = collide_convex(ground, ground_pose, crate, {.position = {.y = 0.49F}});
    ASSERT_TRUE(start.has_value());
    const std::vector<std::uint32_t> features = features_of(*start);
    Generator generator{42};
    for (std::size_t step = 0; step < 100; ++step)
    {
        const Transform pose{.position = {.x = generator.between(-0.05F, 0.05F), .y = 0.49F + generator.between(-0.005F, 0.005F), .z = generator.between(-0.05F, 0.05F)},
                             .orientation = Quaternion::from_axis_angle(generator.direction(), generator.between(0, 0.01F))};
        const std::optional<ContactManifold> manifold = collide_convex(ground, ground_pose, crate, pose);
        ASSERT_TRUE(manifold.has_value());
        EXPECT_EQ(features_of(*manifold), features) << "step " << step;
    }

    // A sphere on a box keeps its one feature while it rolls over the face; a box on a hull keeps its four.
    const std::optional<ContactManifold> rolling = collide_convex(SphereShape{.radius = 0.5F}, {.position = {.x = 1, .y = 0.49F}}, ground, ground_pose);
    const std::optional<ContactManifold> rolled = collide_convex(SphereShape{.radius = 0.5F}, {.position = {.x = 1.3F, .y = 0.48F}}, ground, ground_pose);
    ASSERT_TRUE(rolling.has_value() && rolled.has_value());
    EXPECT_EQ(rolling->points[0].feature, rolled->points[0].feature);
}

TEST(ContactTest, SpeculativeContactsAndOrder)
{
    // Apart by less than the speculative distance: a contact with a positive separation; farther: none.
    const std::optional<ContactManifold> near = collide_convex(BoxShape{}, {}, BoxShape{}, {.position = {.y = 1.01F}});
    ASSERT_TRUE(near.has_value());
    EXPECT_NEAR(near->deepest(), 0.01F, 1e-5F);
    EXPECT_FALSE(collide_convex(BoxShape{}, {}, BoxShape{}, {.position = {.y = 1.03F}}).has_value());
    EXPECT_TRUE(collide_convex(BoxShape{}, {}, BoxShape{}, {.position = {.y = 1.03F}}, {.speculative_distance = 0.05F}).has_value());

    // The other order: the opposite normal, the same points, features with the roles swapped.
    Generator generator{9};
    for (std::size_t sample = 0; sample < 200; ++sample)
    {
        const Transform first{.position = generator.vector(-0.3F, 0.3F), .orientation = generator.rotation()};
        const Transform second{.position = generator.vector(-1, 1), .orientation = generator.rotation()};
        const CapsuleShape capsule{.half_height = 0.5F, .radius = 0.3F};
        const BoxShape box{.half_extents = {.x = 0.6F, .y = 0.4F, .z = 0.5F}};
        const std::optional<ContactManifold> forward = collide_convex(capsule, first, box, second);
        const std::optional<ContactManifold> backward = collide_convex(box, second, capsule, first);
        ASSERT_EQ(forward.has_value(), backward.has_value()) << "sample " << sample;
        if (forward)
        {
            EXPECT_TRUE(approximately_equal(forward->normal, -backward->normal, 1e-3F)) << "sample " << sample << ": " << describe(forward->normal) << " vs "
                                                                                        << describe(backward->normal);
            EXPECT_NEAR(forward->deepest(), backward->deepest(), 1e-3F) << "sample " << sample;
        }
    }
}

TEST(ContactTest, AnalyticPathsAgreeWithTheGeneralOne)
{
    Generator generator{31};
    std::size_t overlapping = 0;
    std::size_t compared = 0;
    for (std::size_t sample = 0; sample < 400; ++sample)
    {
        const Transform first{.position = generator.vector(-0.5F, 0.5F), .orientation = generator.rotation()};
        const Transform second{.position = generator.vector(-1.5F, 1.5F), .orientation = generator.rotation()};

        // Box against box: separating axes versus GJK and EPA: the same pairs touch; when they choose the same normal
        // (the separating axes prefer a face to a slightly shallower edge, EPA does not), the same depth.
        const BoxShape a{.half_extents = generator.vector(0.3F, 1)};
        const BoxShape b{.half_extents = generator.vector(0.3F, 1)};
        const std::optional<ContactManifold> analytic = collide_convex(a, first, b, second);
        const std::optional<ContactManifold> general = collide_convex_general(a, first, b, second, {});
        ASSERT_EQ(analytic.has_value(), general.has_value()) << "sample " << sample;
        if (analytic && analytic->deepest() < -1e-2F)
        {
            ++overlapping;
            if (dot(analytic->normal, general->normal) > 0.999F)
            {
                ++compared;
                EXPECT_NEAR(analytic->deepest(), general->deepest(), 1e-2F * std::abs(analytic->deepest()) + 2e-3F) << "sample " << sample;
            }
        }

        // Spheres and capsules: the analytic results against the general path.
        const SphereShape sphere{.radius = generator.between(0.2F, 0.8F)};
        const CapsuleShape capsule{.half_height = generator.between(0.1F, 0.8F), .radius = generator.between(0.2F, 0.5F)};
        const std::optional<ContactManifold> exact = collide_convex(sphere, first, capsule, second);
        const std::optional<ContactManifold> approximated = collide_convex_general(sphere, first, capsule, second, {});
        ASSERT_EQ(exact.has_value(), approximated.has_value()) << "sample " << sample;
        if (exact)
        {
            EXPECT_EQ(exact->algorithm, ContactAlgorithm::SPHERE_CAPSULE);
            EXPECT_NEAR(exact->deepest(), approximated->deepest(), 1e-3F) << "sample " << sample;
            EXPECT_GT(dot(exact->normal, approximated->normal), 0.999F) << "sample " << sample;
        }
    }
    EXPECT_GT(overlapping, 100U);
    EXPECT_GT(compared * 10, overlapping * 6) << "most pairs choose the same normal";

    // Parallel capsules side by side: two points, along their overlap.
    const std::optional<ContactManifold> logs = collide_convex(CapsuleShape{.half_height = 1, .radius = 0.25F}, {.orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)},
                                                               CapsuleShape{.half_height = 1, .radius = 0.25F},
                                                               {.position = {.x = 0.5F, .y = 0.49F}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)});
    ASSERT_TRUE(logs.has_value());
    EXPECT_EQ(logs->point_count, 2U);
    EXPECT_EQ(logs->algorithm, ContactAlgorithm::CAPSULE_CAPSULE);
    EXPECT_NEAR(logs->points[0].separation, -0.01F, 1e-5F);
    EXPECT_NEAR(std::abs(logs->points[0].position.x - logs->points[1].position.x), 1.5F, 1e-4F) << "the overlap from x = -0.5 to 1";
}

TEST(ContactTest, ManyPointsReduceToFourThatKeepTheShape)
{
    // An octagon of contacts and the center: the deepest, then the widest spread.
    std::array<ContactPoint, 9> points{};
    for (std::size_t index = 0; index < 8; ++index)
    {
        const float angle = 2 * PI * static_cast<float>(index) / 8;
        points[index] = {.position = {.x = std::cos(angle), .z = std::sin(angle)}, .separation = -0.01F, .feature = {.local = static_cast<std::uint32_t>(index)}};
    }
    points[8] = {.position = {}, .separation = -0.02F, .feature = {.local = 8}};
    points[3].separation = -0.03F;
    ContactManifold manifold{};
    reduce_contact_points(points, {.y = 1}, manifold);
    ASSERT_EQ(manifold.point_count, 4U);
    EXPECT_EQ(manifold.points[0].feature.local, 3U) << "the deepest first";
    EXPECT_EQ(manifold.points[1].feature.local, 7U) << "the farthest from it";
    // The last two on either side of the line through the first two: two triangles of area 1 (a square of the octagon).
    const Vector3 base = manifold.points[1].position - manifold.points[0].position;
    const float left = dot(cross(base, manifold.points[2].position - manifold.points[0].position), {.y = 1});
    const float right = dot(cross(base, manifold.points[3].position - manifold.points[0].position), {.y = 1});
    EXPECT_LT(left * right, 0) << "on opposite sides";
    EXPECT_NEAR((std::abs(left) + std::abs(right)) * 0.5F, 2, 1e-5F);
}

} // namespace
