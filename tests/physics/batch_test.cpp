// The batch tests of the physics library: frustum culling of boxes and spheres on the SIMD kernels gives exactly what
// the scalar classify() gives, element by element (volumes on the planes included), for every length (the kernels'
// tails); at compile time the same function runs the scalar path; short outputs are refused.
#include <gtest/gtest.h>

import std;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.primitives;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::box_frustum;
using gfx::tests::Generator;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

constexpr Frustum CAGE = box_frustum(4);
constexpr std::array CAGE_BOXES{Aabb::from_center({}, {.x = 1, .y = 1, .z = 1}), Aabb{.minimum = {.x = 4}, .maximum = {.x = 5, .y = 1, .z = 1}},
                                Aabb{.minimum = {.x = 4.5F}, .maximum = {.x = 5, .y = 1, .z = 1}}};
constexpr std::array CAGE_CLASSES = [] {
    std::array<FrustumIntersection, CAGE_BOXES.size()> classes{};
    classify(CAGE, CAGE_BOXES, classes);
    return classes;
}();
static_assert(CAGE_CLASSES == std::array{FrustumIntersection::INSIDE, FrustumIntersection::INTERSECTING, FrustumIntersection::OUTSIDE});

// ===================================================================================================== run time

/// Frustums to cull with: a box-shaped one (volumes exactly on its planes), a perspective seen from a turned camera,
/// and a reversed-depth perspective with its far plane at infinity (a plane with no normal).
auto
frustums() -> std::vector<std::pair<std::string, Frustum>>
{
    const Transform camera{.position = {.x = -1, .y = 3, .z = 6}, .orientation = Quaternion::from_yaw_pitch_roll({.yaw = -0.6F, .pitch = 0.3F, .roll = 0})};
    const Matrix4 view = Matrix4::from_transform(inverse(camera));
    return {{"cage", box_frustum(4)},
            {"perspective", Frustum::from_matrix(gfx::tests::perspective(60 * PI / 180, 16.0F / 9.0F, 0.1F, 50) * view)},
            {"infinite", Frustum::from_matrix(gfx::tests::reversed_infinite_perspective(60 * PI / 180, 1, 0.1F) * view, DepthRange::ZERO_TO_ONE, true)}};
}

/// Boxes and spheres: random ones, then ones on a grid of half units (exactly on the cage's planes, or touching them).
struct Volumes
{
    std::vector<Aabb> boxes{};
    std::vector<Sphere> spheres{};
};

auto
volumes() -> Volumes
{
    Volumes result;
    Generator generator{8086};
    for (std::size_t index = 0; index < 4000; ++index)
    {
        result.boxes.push_back(Aabb::from_center(generator.vector(-30, 30), generator.vector(0, 5)));
        result.spheres.push_back({.center = generator.vector(-30, 30), .radius = generator.between(0, 5)});
    }
    for (int x = -12; x <= 12; ++x)
    {
        for (int size = 0; size <= 4; ++size)
        {
            const float low = static_cast<float>(x) * 0.5F;
            const float high = low + static_cast<float>(size) * 0.5F;
            result.boxes.push_back({.minimum = {.x = low, .y = -1, .z = low}, .maximum = {.x = high, .y = 1, .z = high}});
            result.spheres.push_back({.center = {.x = low, .y = 0, .z = -low}, .radius = static_cast<float>(size) * 0.5F});
        }
    }
    return result;
}

TEST(BatchTest, FrustumCullingMatchesTheScalarClassification)
{
    const Volumes inputs = volumes();
    for (const auto& [name, frustum] : frustums())
    {
        std::vector<FrustumIntersection> box_classes(inputs.boxes.size());
        classify(frustum, inputs.boxes, box_classes);
        std::array<std::size_t, 3> counts{};
        for (std::size_t index = 0; index < inputs.boxes.size(); ++index)
        {
            ASSERT_EQ(box_classes[index], classify(frustum, inputs.boxes[index])) << name << ", box " << index;
            ++counts[std::to_underlying(box_classes[index])];
        }
        std::vector<FrustumIntersection> sphere_classes(inputs.spheres.size());
        classify(frustum, inputs.spheres, sphere_classes);
        for (std::size_t index = 0; index < inputs.spheres.size(); ++index)
        {
            ASSERT_EQ(sphere_classes[index], classify(frustum, inputs.spheres[index])) << name << ", sphere " << index;
            ++counts[std::to_underlying(sphere_classes[index])];
        }

        // Every class occurs, so each decision of the kernels was taken.
        EXPECT_GT(counts[0], 20U) << name;
        EXPECT_GT(counts[1], 20U) << name;
        EXPECT_GT(counts[2], 20U) << name;
    }
}

TEST(BatchTest, EveryLengthIsClassified)
{
    // Lengths 0 to 9 at every offset, into an output longer than needed: elements past the input stay untouched.
    const Volumes inputs = volumes();
    const Frustum frustum = frustums()[1].second;
    for (std::size_t offset = 0; offset < 4; ++offset)
    {
        for (std::size_t length = 0; length < 10; ++length)
        {
            std::array<FrustumIntersection, 12> classes{};
            classes.fill(static_cast<FrustumIntersection>(0xEE));
            const std::span<const Aabb> boxes = std::span{inputs.boxes}.subspan(offset * 7, length);
            classify(frustum, boxes, classes);
            for (std::size_t index = 0; index < classes.size(); ++index)
            {
                const FrustumIntersection expected = index < length ? classify(frustum, boxes[index]) : static_cast<FrustumIntersection>(0xEE);
                EXPECT_EQ(classes[index], expected) << "offset " << offset << ", length " << length << ", element " << index;
            }
        }
    }
}

TEST(BatchTest, ShortOutputsAreRefused)
{
    const std::vector<Aabb> boxes(5);
    std::vector<FrustumIntersection> classes(4);
    try
    {
        classify(box_frustum(1), boxes, classes);
        FAIL() << "a short output must be refused";
    }
    catch (const PhysicsError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::physics::GEOMETRY] classify: the output holds 4 results for 5 elements")) << message;
    }
    const std::vector<Sphere> spheres(2);
    EXPECT_THROW(classify(box_frustum(1), spheres, std::span{classes}.first(1)), PhysicsError);

    // Nothing to classify, nothing written.
    EXPECT_NO_THROW(classify(box_frustum(1), std::span<const Aabb>{}, std::span<FrustumIntersection>{}));
}

} // namespace
