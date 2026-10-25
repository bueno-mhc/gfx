// Collision of any two shapes: a box sliding over a triangulated floor keeps the floor's normal at the seams (active
// edges) and one merged manifold; one-sided meshes ignore what is below them; a box on a ridge of a height field gets
// two normals; compounds name their children; two meshes collide triangle by triangle; the settings and the inputs are
// checked; the collector keeps its storage; distances need convex shapes. Also the manifold cache.
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
using gfx::tests::PhysicsHandles;

constexpr float PI = std::numbers::pi_v<float>;

/// A floor of @p cells × @p cells unit cells (two triangles each) at y = 0, facing up.
auto
floor_mesh(std::uint32_t cells,
           bool two_sided = false) -> std::shared_ptr<const Shape>
{
    std::vector<Vector3> vertices;
    for (std::uint32_t row = 0; row <= cells; ++row)
    {
        for (std::uint32_t column = 0; column <= cells; ++column)
        {
            vertices.push_back({.x = static_cast<float>(column) - static_cast<float>(cells) * 0.5F, .z = static_cast<float>(row) - static_cast<float>(cells) * 0.5F});
        }
    }
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::uint32_t row = 0; row < cells; ++row)
    {
        for (std::uint32_t column = 0; column < cells; ++column)
        {
            const std::uint32_t corner = row * (cells + 1) + column;
            triangles.push_back({corner, corner + cells + 1, corner + cells + 2});
            triangles.push_back({corner, corner + cells + 2, corner + 1});
        }
    }
    return make_shape(TriangleMeshShape{{.vertices = vertices, .triangles = triangles, .two_sided = two_sided}});
}

TEST(CollideTest, BoxesSlideOverMeshSeamsWithTheFloorsNormal)
{
    // A box 0.01 into an 8 × 8 floor, slid across it (over vertices, edges and diagonals): always one manifold of four
    // points facing up, wherever the seams are.
    const std::shared_ptr<const Shape> floor = floor_mesh(8);
    const std::shared_ptr<const Shape> crate = make_shape(BoxShape{.half_extents = {.x = 0.7F, .y = 0.5F, .z = 0.7F}});
    ContactCollector contacts;
    for (int step = 0; step <= 60; ++step)
    {
        const float x = -2.0F + static_cast<float>(step) * 0.07F;
        collide({.shape = crate.get(), .pose = {.position = {.x = x, .y = 0.49F, .z = 0.3F * x}}}, {.shape = floor.get()}, contacts);
        ASSERT_EQ(contacts.manifolds().size(), 1U) << "step " << step;
        const ContactManifold& manifold = contacts.manifolds()[0];
        EXPECT_TRUE(approximately_equal(manifold.normal, {.y = -1}, 1e-4F)) << "step " << step << ": " << describe(manifold.normal) << " (from the box to the floor)";
        EXPECT_EQ(manifold.point_count, 4U) << "step " << step;
        EXPECT_NEAR(manifold.deepest(), -0.01F, 1e-4F) << "step " << step;
        EXPECT_NE(manifold.sub_shape_second, NO_SUB_SHAPE) << "a triangle of the floor";
        EXPECT_EQ(manifold.sub_shape_first, NO_SUB_SHAPE);
    }

    // The same in the other order: the normal up, the triangles on the first side.
    collide({.shape = floor.get()}, {.shape = crate.get(), .pose = {.position = {.y = 0.49F}}}, contacts);
    ASSERT_EQ(contacts.manifolds().size(), 1U);
    EXPECT_TRUE(approximately_equal(contacts.manifolds()[0].normal, {.y = 1}, 1e-4F));
    EXPECT_NE(contacts.manifolds()[0].sub_shape_first, NO_SUB_SHAPE);
    for (const ContactPoint& point : contacts.manifolds()[0].contacts())
    {
        EXPECT_NE(point.feature.sub_shape_first, NO_SUB_SHAPE) << "each point names its triangle";
    }
}

TEST(CollideTest, OneSidedMeshesIgnoreWhatIsBelow)
{
    // A sphere just below a one-sided floor: no contact; two-sided: a contact pushing it down.
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.5F});
    ContactCollector contacts;
    collide({.shape = ball.get(), .pose = {.position = {.y = -0.49F}}}, {.shape = floor_mesh(4).get()}, contacts);
    EXPECT_TRUE(contacts.manifolds().empty());
    const std::shared_ptr<const Shape> both_sides = floor_mesh(4, true);
    collide({.shape = ball.get(), .pose = {.position = {.y = -0.49F}}}, {.shape = both_sides.get()}, contacts);
    ASSERT_EQ(contacts.manifolds().size(), 1U);
    EXPECT_TRUE(approximately_equal(contacts.manifolds()[0].normal, {.y = 1}, 1e-4F)) << "from the ball up to the floor above it";
}

TEST(CollideTest, HeightFieldRidgesGiveTwoNormals)
{
    // A ridge along x = 2 (heights 0, 1, 0 across columns 0, 1, 2 at spacing 2): a box straddling it touches both slopes.
    const std::array<float, 9> samples{0, 1, 0,
                                       0, 1, 0,
                                       0, 1, 0};
    const std::shared_ptr<const Shape> field = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{samples.data(), 3, 3},
                                                                            .scale = {.x = 2, .y = 1, .z = 2}}});
    const std::shared_ptr<const Shape> crate = make_shape(BoxShape{.half_extents = {.x = 1, .y = 0.2F, .z = 0.5F}});
    ContactCollector contacts;
    collide({.shape = crate.get(), .pose = {.position = {.x = 2, .y = 1.15F, .z = 2}}}, {.shape = field.get()}, contacts);
    ASSERT_EQ(contacts.manifolds().size(), 2U);
    const Vector3 left = contacts.manifolds()[0].normal;
    const Vector3 right = contacts.manifolds()[1].normal;
    EXPECT_LT(left.y, -0.8F) << describe(left);
    EXPECT_LT(right.y, -0.8F) << describe(right);
    EXPECT_LT(left.x * right.x, 0) << "the two slopes lean opposite ways";

    // At most one manifold: the deepest stays.
    const float deeper = std::min(contacts.manifolds()[0].deepest(), contacts.manifolds()[1].deepest());
    collide({.shape = crate.get(), .pose = {.position = {.x = 2, .y = 1.15F, .z = 2}}}, {.shape = field.get()}, contacts, {.maximum_manifolds = 1});
    ASSERT_EQ(contacts.manifolds().size(), 1U);
    EXPECT_EQ(contacts.manifolds()[0].deepest(), deeper);

    // On the flat part, one manifold facing straight up.
    const std::array<float, 9> flat{};
    const std::shared_ptr<const Shape> plain = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{flat.data(), 3, 3}}});
    collide({.shape = crate.get(), .pose = {.position = {.x = 1, .y = 0.19F, .z = 1}}}, {.shape = plain.get()}, contacts);
    ASSERT_EQ(contacts.manifolds().size(), 1U);
    EXPECT_TRUE(approximately_equal(contacts.manifolds()[0].normal, {.y = -1}, 1e-4F)) << describe(contacts.manifolds()[0].normal);
    EXPECT_EQ(contacts.manifolds()[0].point_count, 4U);
}

TEST(CollideTest, CompoundsNameTheirChildren)
{
    // A table: a top and four legs; standing on a floor box, the legs touch it (four manifolds merged into one: the
    // same normal) and the merged manifold names a leg.
    const std::shared_ptr<const Shape> leg = make_shape(BoxShape{.half_extents = {.x = 0.1F, .y = 0.5F, .z = 0.1F}});
    const std::shared_ptr<const Shape> top = make_shape(BoxShape{.half_extents = {.x = 1, .y = 0.1F, .z = 1}});
    const Shape table{CompoundShape{{{.shape = top, .pose = {.position = {.y = 1.1F}}},
                                     {.shape = leg, .pose = {.position = {.x = -0.9F, .y = 0.5F, .z = -0.9F}}},
                                     {.shape = leg, .pose = {.position = {.x = 0.9F, .y = 0.5F, .z = -0.9F}}},
                                     {.shape = leg, .pose = {.position = {.x = -0.9F, .y = 0.5F, .z = 0.9F}}},
                                     {.shape = leg, .pose = {.position = {.x = 0.9F, .y = 0.5F, .z = 0.9F}}}}}};
    const std::shared_ptr<const Shape> ground = make_shape(BoxShape{.half_extents = {.x = 5, .y = 0.5F, .z = 5}});
    ContactCollector contacts;
    collide({.shape = &table, .pose = {.position = {.y = 0.01F - 0.02F}}}, {.shape = ground.get(), .pose = {.position = {.y = -0.5F}}}, contacts);
    ASSERT_EQ(contacts.manifolds().size(), 1U);
    const ContactManifold& manifold = contacts.manifolds()[0];
    EXPECT_EQ(manifold.point_count, 4U) << "the sixteen leg contacts reduced to four";
    EXPECT_GE(manifold.sub_shape_first, 1U);
    EXPECT_LE(manifold.sub_shape_first, 4U);
    EXPECT_EQ(manifold.sub_shape_second, NO_SUB_SHAPE);
    float spread = 0;
    for (const ContactPoint& point : manifold.contacts())
    {
        spread = std::max(spread, std::abs(point.position.x));
    }
    EXPECT_GT(spread, 0.7F) << "the points reach out to the legs";

}

TEST(CollideTest, MeshesCollideTriangleByTriangle)
{
    // A small floor lowered onto a big one, turned: triangle pairs by separating axes.
    const std::shared_ptr<const Shape> big = floor_mesh(4, true);
    const std::shared_ptr<const Shape> small = floor_mesh(2, true);
    ContactCollector contacts;
    collide({.shape = big.get()}, {.shape = small.get(), .pose = {.position = {.y = 0.01F}, .orientation = Quaternion::from_axis_angle({.x = 1}, 0.004F)}}, contacts);
    ASSERT_FALSE(contacts.manifolds().empty());
    for (const ContactManifold& manifold : contacts.manifolds())
    {
        EXPECT_EQ(manifold.algorithm, ContactAlgorithm::TRIANGLE_TRIANGLE);
        EXPECT_GT(std::abs(manifold.normal.y), 0.99F) << describe(manifold.normal);
    }
    collide({.shape = big.get()}, {.shape = small.get(), .pose = {.position = {.y = 0.5F}}}, contacts);
    EXPECT_TRUE(contacts.manifolds().empty()) << "half a unit apart";
}

TEST(CollideTest, InputsAreChecked)
{
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{});
    ContactCollector contacts;
    EXPECT_THROW(collide({.shape = nullptr}, {.shape = ball.get()}, contacts), PhysicsError);
    try
    {
        collide({.shape = ball.get()}, {.shape = ball.get()}, contacts, {.speculative_distance = -1});
        FAIL();
    }
    catch (const PhysicsError& error)
    {
        EXPECT_TRUE(std::string_view{error.what()}.starts_with("[gfx::physics::NARROW_PHASE] collide: invalid settings")) << error.what();
    }
    EXPECT_THROW((void)shape_distance({.shape = floor_mesh(1).get()}, {.shape = ball.get()}), PhysicsError);
    EXPECT_NEAR(shape_distance({.shape = ball.get()}, {.shape = ball.get(), .pose = {.position = {.z = 3}}}).distance, 2, 1e-6F);

    // The collector keeps its storage: after the first collision, the same pair again changes nothing of it.
    collide({.shape = ball.get()}, {.shape = ball.get(), .pose = {.position = {.x = 0.9F}}}, contacts);
    ASSERT_EQ(contacts.manifolds().size(), 1U);
    EXPECT_EQ(contacts.point_count(), 1U);
    const ContactManifold* storage = contacts.manifolds().data();
    collide({.shape = ball.get()}, {.shape = ball.get(), .pose = {.position = {.x = 0.8F}}}, contacts);
    EXPECT_EQ(contacts.manifolds().data(), storage);
}

TEST(ManifoldCacheTest, ImpulsesFollowTheirFeatures)
{
    const ManifoldKey key{.first = PhysicsHandles::make<ColliderTag>(1), .second = PhysicsHandles::make<ColliderTag>(2)};
    ManifoldCache cache{16};
    const BoxShape ground{.half_extents = {.x = 5, .y = 1, .z = 5}};
    std::optional<ContactManifold> manifold = collide_convex(ground, {.position = {.y = -1}}, BoxShape{}, {.position = {.y = 0.49F}});
    ASSERT_TRUE(manifold.has_value());

    // First step: nothing to carry over. The solver writes impulses; the next step's points get them by feature.
    EXPECT_EQ(cache.update(key, *manifold), 0U);
    for (std::uint32_t index = 0; index < manifold->point_count; ++index)
    {
        manifold->points[index].normal_impulse = static_cast<float>(index + 1);
        manifold->points[index].tangent_impulse = {static_cast<float>(index) * 0.1F, -1};
    }
    cache.store(key, *manifold);
    cache.end_step();
    std::optional<ContactManifold> next = collide_convex(ground, {.position = {.y = -1}}, BoxShape{}, {.position = {.x = 0.01F, .y = 0.495F}});
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(cache.update(key, *next), 4U);
    for (const ContactPoint& point : next->contacts())
    {
        const auto previous = std::ranges::find_if(manifold->contacts(), [&point](const ContactPoint& old) { return old.feature == point.feature; });
        ASSERT_NE(previous, manifold->contacts().end());
        EXPECT_EQ(point.normal_impulse, previous->normal_impulse);
        EXPECT_EQ(point.tangent_impulse, previous->tangent_impulse);
    }

    // A step without the pair forgets it; growing past the capacity keeps every manifold.
    cache.end_step();
    cache.end_step();
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.find(key), nullptr);
    for (std::uint32_t index = 0; index < 100; ++index)
    {
        ContactManifold copy = *manifold;
        cache.update({.first = PhysicsHandles::make<ColliderTag>(index), .second = PhysicsHandles::make<ColliderTag>(index + 1000), .sub_shape_second = index}, copy);
    }
    EXPECT_EQ(cache.size(), 100U);
    for (std::uint32_t index = 0; index < 100; ++index)
    {
        EXPECT_NE(cache.find({.first = PhysicsHandles::make<ColliderTag>(index), .second = PhysicsHandles::make<ColliderTag>(index + 1000), .sub_shape_second = index}), nullptr);
    }
    cache.end_step();
    EXPECT_EQ(cache.size(), 100U) << "updated this step: kept";
    cache.clear();
    EXPECT_EQ(cache.size(), 0U);
}

} // namespace
