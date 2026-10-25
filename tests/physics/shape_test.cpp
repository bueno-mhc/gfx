// Collision shapes: supporting faces, hulls built by quickhull at compile time and at run time (every point inside,
// every vertex on its faces, Euler's formula), the SIMD support function against the scalar one, mass properties
// against their formulas and each other, meshes' and height fields' active edges, compounds, and the static BVH
// against brute force.
#include <gtest/gtest.h>

import std;
import gfx.physics.shapes;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::describe;
using gfx::tests::Generator;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

// Supporting faces: a box's face counter-clockwise around its normal, a capsule's segment, a cylinder's cap.
constexpr BoxShape CRATE{.half_extents = {.x = 1, .y = 2, .z = 3}};
constexpr SupportingFace CRATE_TOP = CRATE.supporting_face({.x = 0.1F, .y = 1, .z = -0.2F});
static_assert(CRATE_TOP.count == 4 && CRATE_TOP.face == 3 && CRATE_TOP.normal == Vector3{.y = 1});
static_assert(dot(cross(CRATE_TOP.vertices[1] - CRATE_TOP.vertices[0], CRATE_TOP.vertices[2] - CRATE_TOP.vertices[1]), CRATE_TOP.normal) > 0,
              "counter-clockwise around the outward normal");
static_assert(CRATE.supporting_face({.z = -1}).face == 4 && CRATE.supporting_face({.z = -1}).vertices[0].z == -3);
static_assert(CapsuleShape{}.supporting_face({.x = 1}).count == 2 && CapsuleShape{}.supporting_face({.y = 1}).count == 1);
static_assert(CylinderShape{}.supporting_face({.y = -1}).count == 8 && CylinderShape{}.supporting_face({.x = 1}).count == 2
              && CylinderShape{}.supporting_face({.x = 1, .y = 1}).count == 1);
static_assert(ConeShape{}.support({.y = 1}) == Vector3{.y = 0.5F} && ConeShape{}.support({.x = 1}) == Vector3{.x = 0.5F, .y = -0.5F});
static_assert(ConeShape{}.supporting_face({.y = -1}).count == 8);
static_assert(SphereShape{.radius = 2}.bounds() == Aabb::from_center({}, {.x = 2, .y = 2, .z = 2}) && CapsuleShape{}.bounds().maximum.y == 0.75F);
static_assert(FacedConvexShape<SphereShape> && FacedConvexShape<BoxShape> && FacedConvexShape<HullView> && FacedConvexShape<TriangleShape>
              && FacedConvexShape<HullShape>);

// A hull built by the compiler: a cube's corners with points inside it and on its faces give the cube.
constexpr std::array CUBE_POINTS{Vector3{.x = -1, .y = -1, .z = -1}, Vector3{.x = 1, .y = -1, .z = -1}, Vector3{.x = -1, .y = 1, .z = -1},
                                 Vector3{.x = 1, .y = 1, .z = -1},   Vector3{.x = -1, .y = -1, .z = 1}, Vector3{.x = 1, .y = -1, .z = 1},
                                 Vector3{.x = -1, .y = 1, .z = 1},   Vector3{.x = 1, .y = 1, .z = 1},   Vector3{},
                                 Vector3{.x = 0.5F, .y = 0.2F},      Vector3{.y = 1},                   Vector3{.x = 1, .z = 0.3F}};
constexpr auto CUBE = convex_hull<8, 6, 24>(CUBE_POINTS);
static_assert(CUBE.vertex_count == 8 && CUBE.face_count == 6 && CUBE.index_count == 24, "coplanar triangles merge into the cube's six squares");
static_assert(CUBE.support({.x = 1, .y = 1, .z = -1}) == Vector3{.x = 1, .y = 1, .z = -1});
static_assert(CUBE.supporting_face({.y = 1}).count == 4 && CUBE.supporting_face({.y = 1}).normal.y > 0.9999F);
static_assert(CUBE.view().primitive().contains({.x = 0.9F, .y = -0.9F, .z = 0.5F}) && !CUBE.view().primitive().contains({.x = 1.1F}));

// A wedge (a triangular prism): five faces, two triangles and three rectangles.
constexpr auto WEDGE = convex_hull<6, 5, 18>(std::array{Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}, Vector3{.x = 1, .z = 1}, Vector3{.y = 1, .z = 1}});
static_assert(WEDGE.face_count == 5 && WEDGE.index_count == 18);

// Mass properties: a unit-density cube of side 2 weighs 8 and has moments 8/3 · (1 + 1) = 16/3, by formula and by hull.
constexpr MassProperties CUBE_MASS = mass_properties(CUBE.view(), 1);
static_assert(std::abs(CUBE_MASS.mass - 8) < 1e-4F && approximately_equal(CUBE_MASS.center_of_mass, {}, 1e-5F));
static_assert(approximately_equal(CUBE_MASS.inertia, mass_properties(BoxShape{.half_extents = {.x = 1, .y = 1, .z = 1}}, 1).inertia, 1e-4F));
static_assert(std::abs(mass_properties(SphereShape{.radius = 1}, 3).mass - 4 * PI) < 1e-4F);
static_assert(std::abs(mass_properties(ConeShape{.half_height = 1, .radius = 1}, 1).center_of_mass.y + 0.5F) < 1e-6F, "a quarter of the height above the base");

// ===================================================================================================== run time

/// Every point of @p points is inside @p hull (within @p tolerance).
auto
holds_every_point(const HullView& hull,
                  std::span<const Vector3> points,
                  float tolerance) -> bool
{
    return std::ranges::all_of(points, [&](Vector3 point) {
        return std::ranges::all_of(hull.planes, [&](const Plane& plane) { return plane.signed_distance(point) <= tolerance; });
    });
}

TEST(ShapeTest, HullsHoldTheirPointsAndCloseUp)
{
    Generator generator{2718};
    for (std::size_t sample = 0; sample < 40; ++sample)
    {
        // Random clouds: on a sphere (every point a vertex), in a box (most inside), and a few, flattened.
        std::vector<Vector3> points;
        const std::size_t count = 8 + sample * 5;
        for (std::size_t index = 0; index < count; ++index)
        {
            const Vector3 point = sample % 3 == 0 ? generator.direction() * 2 : sample % 3 == 1 ? generator.vector(-1, 1) : component_multiply(generator.vector(-1, 1), {.x = 1, .y = 0.05F, .z = 1});
            points.push_back(point);
        }
        const HullShape hull = HullShape::from_points(points);
        const HullView view = hull.view();
        EXPECT_TRUE(holds_every_point(view, points, 1e-4F)) << "sample " << sample;

        // Every vertex is on each of its faces' planes, and behind every other.
        for (std::size_t face = 0; face < view.faces.size(); ++face)
        {
            ASSERT_GE(view.faces[face].count, 3U);
            for (std::size_t corner = 0; corner < view.faces[face].count; ++corner)
            {
                const Vector3 vertex = view.vertices[view.face_vertices[view.faces[face].first + corner]];
                EXPECT_NEAR(view.planes[face].signed_distance(vertex), 0, 1e-4F) << "sample " << sample << ", face " << face;
            }
            EXPECT_NEAR(length(view.planes[face].normal), 1, 1e-5F);
        }

        // Euler's formula for the polygons: V - E + F = 2 (each edge in two polygons).
        std::size_t edges = 0;
        for (const HullFace& face : view.faces)
        {
            edges += face.count;
        }
        EXPECT_EQ(static_cast<long>(view.vertices.size()) - static_cast<long>(edges / 2) + static_cast<long>(view.faces.size()), 2) << "sample " << sample;
        if (sample % 3 == 0)
        {
            EXPECT_EQ(view.vertices.size(), points.size()) << "points on a sphere are all vertices";
        }

        // The SIMD support function finds the scalar one's vertex.
        for (std::size_t direction = 0; direction < 20; ++direction)
        {
            const Vector3 toward = generator.direction();
            EXPECT_EQ(hull.support(toward), view.support(toward)) << "sample " << sample;
        }
    }
}

TEST(ShapeTest, HullsRejectPointsWithoutVolume)
{
    const auto rejects = [](std::vector<Vector3> points, std::string_view expected) {
        try
        {
            [[maybe_unused]] const HullShape hull = HullShape::from_points(points);
            ADD_FAILURE() << "must be rejected: " << expected;
        }
        catch (const PhysicsError& error)
        {
            const std::string_view message = error.what();
            EXPECT_TRUE(message.starts_with("[gfx::physics::GEOMETRY] HullShape::from_points: ")) << message;
            EXPECT_NE(message.find(expected), std::string_view::npos) << message;
        }
    };
    rejects({Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}}, "at least 4 points");
    rejects({Vector3{}, Vector3{}, Vector3{}, Vector3{}}, "the points coincide");
    rejects({Vector3{}, Vector3{.x = 1}, Vector3{.x = 2}, Vector3{.x = 3}}, "collinear");
    rejects({Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.x = 1, .y = 1}, Vector3{.x = 0.5F, .y = 0.3F}}, "coplanar");
    rejects({Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = std::numeric_limits<float>::infinity()}}, "finite");
}

TEST(ShapeTest, MassPropertiesFollowTheirFormulas)
{
    // A hull of many points on a cylinder approaches the cylinder; a box's tensor turned has the box's moments.
    std::vector<Vector3> rim;
    for (int index = 0; index < 256; ++index)
    {
        const float angle = 2 * PI * static_cast<float>(index) / 256;
        rim.push_back({.x = std::cos(angle), .y = -1, .z = std::sin(angle)});
        rim.push_back({.x = std::cos(angle), .y = 1, .z = std::sin(angle)});
    }
    const MassProperties approximated = mass_properties(HullShape::from_points(rim).view(), 2);
    const MassProperties exact = mass_properties(CylinderShape{.half_height = 1, .radius = 1}, 2);
    EXPECT_NEAR(approximated.mass, exact.mass, exact.mass * 1e-3F);
    EXPECT_TRUE(approximately_equal(approximated.inertia, exact.inertia, exact.inertia.element(1, 1) * 2e-3F));

    // A capsule is a cylinder and a sphere; a capsule of height 0 is a sphere.
    const MassProperties capsule = mass_properties(CapsuleShape{.half_height = 1, .radius = 0.5F}, 1);
    EXPECT_NEAR(capsule.mass, mass_properties(CylinderShape{.half_height = 1, .radius = 0.5F}, 1).mass + mass_properties(SphereShape{.radius = 0.5F}, 1).mass, 1e-5F);
    EXPECT_TRUE(approximately_equal(mass_properties(CapsuleShape{.half_height = 0, .radius = 0.5F}, 1).inertia, mass_properties(SphereShape{.radius = 0.5F}, 1).inertia, 1e-6F));

    // Principal axes of a turned box: its moments, and axes along its edges.
    const Quaternion turn = Quaternion::from_yaw_pitch_roll({.yaw = 0.4F, .pitch = 0.7F, .roll = -0.2F});
    const MassProperties box = mass_properties(BoxShape{.half_extents = {.x = 1, .y = 2, .z = 3}}, 1);
    const MassProperties turned = transformed(box, {.position = {.x = 5}, .orientation = turn});
    EXPECT_TRUE(approximately_equal(turned.center_of_mass, {.x = 5}, 1e-6F));
    const PrincipalAxes axes = principal_axes(turned.inertia);
    std::array<float, 3> moments{axes.moments.x, axes.moments.y, axes.moments.z};
    std::array<float, 3> expected{box.inertia.element(0, 0), box.inertia.element(1, 1), box.inertia.element(2, 2)};
    std::ranges::sort(moments);
    std::ranges::sort(expected);
    for (std::size_t index = 0; index < 3; ++index)
    {
        EXPECT_NEAR(moments[index], expected[index], expected[index] * 1e-4F);
    }
    const Matrix3 frame = Matrix3::from_rotation(axes.orientation);
    EXPECT_TRUE(approximately_equal(frame * Matrix3::from_diagonal(axes.moments) * transpose(frame), turned.inertia, 1e-2F)) << "the axes rebuild the tensor";

    // Two boxes side by side are one box twice as long (the parallel axis theorem).
    const BoxShape half{.half_extents = {.x = 1, .y = 1, .z = 1}};
    const MassProperties pair = combined(transformed(mass_properties(half, 1), {.position = {.x = -1}}), transformed(mass_properties(half, 1), {.position = {.x = 1}}));
    const MassProperties whole = mass_properties(BoxShape{.half_extents = {.x = 2, .y = 1, .z = 1}}, 1);
    EXPECT_NEAR(pair.mass, whole.mass, 1e-5F);
    EXPECT_TRUE(approximately_equal(pair.inertia, whole.inertia, 1e-4F));
}

/// A flat grid of 2 × 2 cells (8 triangles) of side @p size at height @p height, counter-clockwise from above.
auto
flat_grid(float size,
          float height) -> std::pair<std::vector<Vector3>, std::vector<std::array<std::uint32_t, 3>>>
{
    std::vector<Vector3> vertices;
    for (std::uint32_t row = 0; row < 3; ++row)
    {
        for (std::uint32_t column = 0; column < 3; ++column)
        {
            vertices.push_back({.x = (static_cast<float>(column) - 1) * size, .y = height, .z = (static_cast<float>(row) - 1) * size});
        }
    }
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::uint32_t row = 0; row < 2; ++row)
    {
        for (std::uint32_t column = 0; column < 2; ++column)
        {
            const std::uint32_t corner = row * 3 + column;
            triangles.push_back({corner, corner + 3, corner + 4});
            triangles.push_back({corner, corner + 4, corner + 1});
        }
    }
    return {vertices, triangles};
}

TEST(ShapeTest, MeshesKnowTheirActiveEdges)
{
    // A flat grid: the edges between triangles are inactive, the border's active; every triangle faces up.
    const auto [vertices, triangles] = flat_grid(1, 0);
    const TriangleMeshShape grid{{.vertices = vertices, .triangles = triangles}};
    EXPECT_FALSE(grid.closed());
    std::size_t active = 0;
    for (std::uint32_t index = 0; index < grid.triangle_count(); ++index)
    {
        EXPECT_TRUE(approximately_equal(grid.triangle(index).normal(), {.y = 1}, 1e-6F)) << "triangle " << index;
        active += static_cast<std::size_t>(std::popcount(static_cast<unsigned>(grid.active_edges(index))));
    }
    EXPECT_EQ(active, 8U) << "the grid's border: 8 edges";

    // A closed cube mesh: convex folds are active; its mass is the box's.
    const std::array<Vector3, 8> corners{Vector3{.x = -1, .y = -1, .z = -1}, Vector3{.x = 1, .y = -1, .z = -1}, Vector3{.x = -1, .y = 1, .z = -1}, Vector3{.x = 1, .y = 1, .z = -1},
                                         Vector3{.x = -1, .y = -1, .z = 1},  Vector3{.x = 1, .y = -1, .z = 1},  Vector3{.x = -1, .y = 1, .z = 1},  Vector3{.x = 1, .y = 1, .z = 1}};
    std::vector<std::array<std::uint32_t, 3>> faces;
    for (std::uint32_t face = 0; face < CUBE.face_count; ++face)
    {
        // The compile-time cube's polygons, fanned (its vertices are the corners, in input order).
        const HullFace polygon = CUBE.faces[face];
        for (std::uint32_t corner = 1; corner + 1 < polygon.count; ++corner)
        {
            faces.push_back({CUBE.face_vertices[polygon.first], CUBE.face_vertices[polygon.first + corner], CUBE.face_vertices[polygon.first + corner + 1]});
        }
    }
    const TriangleMeshShape cube{{.vertices = corners, .triangles = faces}};
    EXPECT_TRUE(cube.closed());
    const Shape shape{cube};
    const MassProperties mass = shape.mass_properties(1);
    EXPECT_NEAR(mass.mass, 8, 1e-4F);
    EXPECT_TRUE(approximately_equal(mass.inertia, CUBE_MASS.inertia, 1e-4F));
    std::size_t cube_active = 0;
    for (std::uint32_t index = 0; index < cube.triangle_count(); ++index)
    {
        cube_active += static_cast<std::size_t>(std::popcount(static_cast<unsigned>(cube.active_edges(index))));
    }
    EXPECT_EQ(cube_active, 24U) << "12 cube edges, active on both sides; the 6 diagonals inactive";

    // Invalid meshes.
    EXPECT_THROW(TriangleMeshShape({.vertices = vertices, .triangles = {}}), PhysicsError);
    const std::array<std::array<std::uint32_t, 3>, 1> out_of_range{{{0, 1, 99}}};
    EXPECT_THROW(TriangleMeshShape({.vertices = vertices, .triangles = out_of_range}), PhysicsError);
}

TEST(ShapeTest, HeightFieldsAreGridsOfTriangles)
{
    // 3 × 4 samples, a ridge along the middle column: the ridge's edges are active, the flat ones not.
    const std::array<float, 12> samples{0, 1, 0, 0,
                                        0, 1, 0, 0,
                                        0, 1, 0, 0};
    const HeightFieldShape field{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{samples.data(), 3, 4}, .scale = {.x = 2, .y = 0.5F, .z = 1}}};
    EXPECT_EQ(field.rows(), 3U);
    EXPECT_EQ(field.columns(), 4U);
    EXPECT_EQ(field.triangle_count(), 12U);
    EXPECT_EQ(field.bounds(), (Aabb{.minimum = {}, .maximum = {.x = 6, .y = 0.5F, .z = 2}}));
    EXPECT_EQ((field.heights()[1, 1]), 1.0F);
    for (std::uint32_t index = 0; index < field.triangle_count(); ++index)
    {
        EXPECT_GT(field.triangle(index).normal().y, 0) << "triangle " << index << " faces up";
    }
    // Cell (0, 2) is flat (both columns 2 and 3 at 0): its diagonal is inactive; its edge on column 2 borders the
    // slope down from the ridge (a concave fold seen from the flat side): inactive too. Cell (0, 0)'s edge on column 1
    // is the ridge (a convex fold seen from the slope): active.
    EXPECT_EQ(field.active_edges(2 * 2 + 0) & 0b100U, 0U) << "flat cell's diagonal";
    EXPECT_NE(field.active_edges(2 * 0 + 1) & 0b010U, 0U) << "the ridge, from the slope below it";

    // The triangles under a box.
    std::vector<std::uint32_t> found;
    field.query(Aabb{.minimum = {.x = 0.5F, .y = -1, .z = 0.2F}, .maximum = {.x = 1.5F, .y = 2, .z = 0.8F}}, [&found](std::uint32_t index, const TriangleShape&) { found.push_back(index); });
    EXPECT_EQ(found, (std::vector<std::uint32_t>{0, 1})) << "only cell (0, 0)";
    using Heights = std::mdspan<const float, std::dextents<std::size_t, 2>>;
    EXPECT_THROW(HeightFieldShape({.heights = Heights(samples.data(), 1, 12)}), PhysicsError);
}

TEST(ShapeTest, ShapesCheckTheirSizesAndCombineIntoCompounds)
{
    EXPECT_THROW(Shape{SphereShape{.radius = 0}}, PhysicsError);
    EXPECT_THROW((Shape{BoxShape{.half_extents = {.x = 1, .y = -1, .z = 1}}}), PhysicsError);
    EXPECT_THROW(Shape{CapsuleShape{.half_height = -0.1F}}, PhysicsError);
    try
    {
        [[maybe_unused]] const Shape refused{CylinderShape{.half_height = std::numeric_limits<float>::quiet_NaN()}};
        FAIL();
    }
    catch (const PhysicsError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("the half height of a cylinder must be finite and greater than 0 (it is nan)"), std::string_view::npos) << error.what();
    }

    // A compound of two spheres and a box: kinds, bounds, mass (the parallel axis theorem), and the children under a box.
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 1});
    const std::shared_ptr<const Shape> plank = make_shape(BoxShape{.half_extents = {.x = 3, .y = 0.25F, .z = 0.5F}});
    const Shape dumbbell{CompoundShape{{{.shape = ball, .pose = {.position = {.x = -3}}}, {.shape = ball, .pose = {.position = {.x = 3}}}, {.shape = plank}}}};
    EXPECT_EQ(dumbbell.kind(), ShapeKind::COMPOUND);
    EXPECT_FALSE(dumbbell.is_convex());
    EXPECT_EQ(dumbbell.bounds(), (Aabb{.minimum = {.x = -4, .y = -1, .z = -1}, .maximum = {.x = 4, .y = 1, .z = 1}}));
    const MassProperties mass = dumbbell.mass_properties(1);
    const float ball_mass = mass_properties(SphereShape{.radius = 1}, 1).mass;
    EXPECT_NEAR(mass.mass, 2 * ball_mass + 3, 1e-4F);
    EXPECT_TRUE(approximately_equal(mass.center_of_mass, {}, 1e-5F));
    EXPECT_GT(mass.inertia.element(1, 1), 2 * ball_mass * 9) << "the balls 3 units from the axis";
    std::vector<std::uint32_t> children;
    dumbbell.get_if<CompoundShape>()->query(Aabb::from_center({.x = 3.5F}, {.x = 0.1F, .y = 0.1F, .z = 0.1F}), [&children](std::uint32_t index, const CompoundChild&) {
        children.push_back(index);
    });
    EXPECT_EQ(children, std::vector<std::uint32_t>{1});
    EXPECT_THROW(CompoundShape{{}}, PhysicsError);

    // A shape's posed bounds: tight for convex shapes (a turned box's corners).
    const Transform pose{.position = {.y = 2}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 4)};
    const Aabb posed = make_shape(BoxShape{.half_extents = {.x = 1, .y = 1, .z = 1}})->bounds(pose);
    const float reach = std::numbers::sqrt2_v<float>;
    EXPECT_TRUE(approximately_equal(posed.minimum, {.x = -reach, .y = 2 - reach, .z = -1}, 1e-5F)) << describe(posed.minimum);
}

TEST(ShapeTest, StaticHierarchiesFindWhatBruteForceFinds)
{
    Generator generator{606};
    std::vector<Aabb> boxes;
    for (std::size_t index = 0; index < 2000; ++index)
    {
        boxes.push_back(Aabb::from_center(generator.vector(-50, 50), generator.vector(0.1F, 2)));
    }
    const StaticBvh bvh{boxes};
    EXPECT_EQ(bvh.items().size(), boxes.size());
    for (std::size_t sample = 0; sample < 200; ++sample)
    {
        // Boxes overlapping a query box, and those a segment crosses.
        const Aabb query = Aabb::from_center(generator.vector(-50, 50), generator.vector(0.5F, 10));
        std::vector<std::uint32_t> found;
        bvh.query(query, [&found](std::uint32_t item) { found.push_back(item); });
        std::ranges::sort(found);
        std::vector<std::uint32_t> expected;
        for (std::uint32_t index = 0; index < boxes.size(); ++index)
        {
            if (intersects(boxes[index], query))
            {
                expected.push_back(index);
            }
        }
        EXPECT_EQ(found, expected) << "box query " << sample;

        const Vector3 origin = generator.vector(-60, 60);
        const Vector3 direction = generator.direction();
        std::vector<std::uint32_t> crossed;
        bvh.query_segment(origin, direction, 40, [&crossed](std::uint32_t item) { crossed.push_back(item); });
        std::ranges::sort(crossed);
        std::vector<std::uint32_t> crossed_expected;
        for (std::uint32_t index = 0; index < boxes.size(); ++index)
        {
            if (intersects(Segment{.start = origin, .end = origin + direction * 40}, boxes[index]))
            {
                crossed_expected.push_back(index);
            }
        }
        EXPECT_EQ(crossed, crossed_expected) << "segment query " << sample;
    }

    // Pairs between two hierarchies, the second moved.
    std::vector<Aabb> others;
    for (std::size_t index = 0; index < 300; ++index)
    {
        others.push_back(Aabb::from_center(generator.vector(-20, 20), generator.vector(0.1F, 3)));
    }
    const StaticBvh other{others};
    const Transform moved{.position = {.x = 3, .y = -2}, .orientation = Quaternion::from_axis_angle({.y = 1}, 0.4F)};
    std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
    bvh.query_pairs(other, moved, [&pairs](std::uint32_t mine, std::uint32_t theirs) { pairs.insert({mine, theirs}); });
    std::set<std::pair<std::uint32_t, std::uint32_t>> expected_pairs;
    for (std::uint32_t mine = 0; mine < boxes.size(); ++mine)
    {
        for (std::uint32_t theirs = 0; theirs < others.size(); ++theirs)
        {
            if (intersects(boxes[mine], others[theirs].transformed(moved)))
            {
                expected_pairs.insert({mine, theirs});
            }
        }
    }
    EXPECT_EQ(pairs, expected_pairs);
}

} // namespace
