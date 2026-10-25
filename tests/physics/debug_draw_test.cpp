// Debug drawing: every shape kind as a mesh whose vertices lie on the shape's surface, whose normals have unit length and
// point out, whose triangles face out and close it (each edge run once each way), enclosing the shape's volume (Steiner's
// formula for rounded boxes and hulls) more closely with more segments; triangle meshes, height fields and compounds as
// their parts; line lists.
#include <gtest/gtest.h>

import std;
import gfx.physics.debug_draw;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::describe;

constexpr float PI = std::numbers::pi_v<float>;

static_assert(DebugColor{.red = 1, .green = 2, .blue = 3, .alpha = 4}.packed() == 0x04030201U, "red in the lowest byte");

/// A wedge (a triangular prism) around the origin, and the same rounded by 0.1.
constexpr std::array WEDGE_POINTS{Vector3{.x = -0.5F, .y = -0.4F, .z = -0.5F}, Vector3{.x = 0.5F, .y = -0.4F, .z = -0.5F}, Vector3{.x = -0.5F, .y = 0.6F, .z = -0.5F},
                                  Vector3{.x = -0.5F, .y = -0.4F, .z = 0.5F},  Vector3{.x = 0.5F, .y = -0.4F, .z = 0.5F},  Vector3{.x = -0.5F, .y = 0.6F, .z = 0.5F}};
constexpr auto WEDGE = convex_hull<6, 5, 18>(WEDGE_POINTS);
constexpr auto ROUNDED_WEDGE = convex_hull<6, 5, 18>(WEDGE_POINTS, 0.1F);

/// The volume of @p hull rounded by a sphere of @p radius (Steiner's formula): the hull's volume, its area times the
/// radius, each edge's length times the angle between its faces' normals times r² / 2, and a sphere.
[[nodiscard]] auto
rounded_volume(const HullView& hull,
               float radius) -> double
{
    double area = 0.0;
    for (std::size_t face_index = 0; face_index < hull.faces.size(); ++face_index)
    {
        const HullFace face = hull.faces[face_index];
        const Vector3 first = hull.vertices[hull.face_vertices[face.first]];
        for (std::uint32_t corner = 1; corner + 1 < face.count; ++corner)
        {
            const Vector3 b = hull.vertices[hull.face_vertices[face.first + corner]];
            const Vector3 c = hull.vertices[hull.face_vertices[face.first + corner + 1]];
            area += 0.5 * static_cast<double>(length(cross(b - first, c - first)));
        }
    }
    double edges = 0.0;
    for (std::size_t first_face = 0; first_face < hull.faces.size(); ++first_face)
    {
        for (std::size_t second_face = first_face + 1; second_face < hull.faces.size(); ++second_face)
        {
            // The vertices the two faces share make their common edge.
            std::vector<std::uint16_t> shared;
            const auto first_corners = hull.face_vertices.subspan(hull.faces[first_face].first, hull.faces[first_face].count);
            const auto second_corners = hull.face_vertices.subspan(hull.faces[second_face].first, hull.faces[second_face].count);
            for (const std::uint16_t corner : first_corners)
            {
                if (std::ranges::find(second_corners, corner) != second_corners.end())
                {
                    shared.push_back(corner);
                }
            }
            if (shared.size() == 2)
            {
                const double angle = std::acos(std::clamp(static_cast<double>(dot(hull.planes[first_face].normal, hull.planes[second_face].normal)), -1.0, 1.0));
                edges += static_cast<double>(distance(hull.vertices[shared[0]], hull.vertices[shared[1]])) * angle;
            }
        }
    }
    const double r = radius;
    return static_cast<double>(mass_properties(hull, 1.0F).mass) + area * r + edges * r * r * 0.5 + 4.0 / 3.0 * std::numbers::pi * r * r * r;
}

/// The volume of a box rounded by its radius: the box, a slab on each face, a quarter cylinder along each edge, an eighth
/// of a sphere at each corner.
[[nodiscard]] auto
rounded_box_volume(const BoxShape& box) -> double
{
    const double x = box.half_extents.x;
    const double y = box.half_extents.y;
    const double z = box.half_extents.z;
    const double r = box.radius;
    return 8.0 * x * y * z + 8.0 * r * (x * y + y * z + z * x) + 2.0 * std::numbers::pi * r * r * (x + y + z) + 4.0 / 3.0 * std::numbers::pi * r * r * r;
}

/// Whether @p mesh is closed: every edge, by its ends' positions, runs as often one way as the other.
[[nodiscard]] auto
closed(const DebugMesh& mesh) -> bool
{
    using Point = std::tuple<float, float, float>;
    std::map<std::pair<Point, Point>, int> edges;
    const auto point = [&mesh](std::uint32_t index) { return Point{mesh.vertices[index].position.x, mesh.vertices[index].position.y, mesh.vertices[index].position.z}; };
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const Point from = point(mesh.indices[index + corner]);
            const Point to = point(mesh.indices[index + (corner + 1) % 3]);
            ++edges[{from, to}];
            --edges[{to, from}];
        }
    }
    return std::ranges::all_of(edges, [](const auto& edge) { return edge.second == 0; });
}

/// The box around @p mesh's vertices.
[[nodiscard]] auto
mesh_bounds(const DebugMesh& mesh) -> Aabb
{
    Aabb bounds = Aabb::empty();
    for (const DebugVertex& vertex : mesh.vertices)
    {
        bounds = bounds.merged(vertex.position);
    }
    return bounds;
}

/// A convex shape around the origin and the volume it encloses.
struct ConvexCase
{
    std::string name;
    std::shared_ptr<const Shape> shape;
    double volume = 0.0;
};

[[nodiscard]] auto
convex_cases() -> std::vector<ConvexCase>
{
    const BoxShape rounded_box{.half_extents = {.x = 0.5F, .y = 0.3F, .z = 0.2F}, .radius = 0.1F};
    return {
        {.name = "sphere", .shape = make_shape(SphereShape{.radius = 0.5F}), .volume = 4.0 / 3.0 * std::numbers::pi * 0.125},
        {.name = "capsule", .shape = make_shape(CapsuleShape{.half_height = 0.4F, .radius = 0.3F}), .volume = std::numbers::pi * 0.09 * 0.8 + 4.0 / 3.0 * std::numbers::pi * 0.027},
        {.name = "cylinder", .shape = make_shape(CylinderShape{.half_height = 0.4F, .radius = 0.3F}), .volume = std::numbers::pi * 0.09 * 0.8},
        {.name = "cone", .shape = make_shape(ConeShape{.half_height = 0.5F, .radius = 0.4F}), .volume = std::numbers::pi * 0.16 * 1.0 / 3.0},
        {.name = "box", .shape = make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.3F, .z = 0.2F}}), .volume = 8.0 * 0.5 * 0.3 * 0.2},
        {.name = "rounded box", .shape = make_shape(rounded_box), .volume = rounded_box_volume(rounded_box)},
        {.name = "hull", .shape = make_shape(HullShape(WEDGE)), .volume = static_cast<double>(mass_properties(WEDGE.view(), 1.0F).mass)},
        {.name = "rounded hull", .shape = make_shape(HullShape(ROUNDED_WEDGE)), .volume = rounded_volume(WEDGE.view(), 0.1F)},
    };
}

TEST(DebugDrawTest, ConvexShapesBecomeClosedMeshesOnTheirSurfaces)
{
    for (const ConvexCase& item : convex_cases())
    {
        SCOPED_TRACE(item.name);
        const DebugMesh mesh = tessellate(*item.shape);
        ASSERT_GT(mesh.triangle_count(), 0U);
        EXPECT_FALSE(mesh.two_sided);
        EXPECT_TRUE(closed(mesh)) << "every edge runs once each way";

        // Every vertex on the surface, its normal of unit length and pointing out (the origin is inside each shape).
        for (const DebugVertex& vertex : mesh.vertices)
        {
            const std::optional<ShapeClosestPoint> closest = closest_point_on_shape(*item.shape, {}, vertex.position, 1.0F);
            ASSERT_TRUE(closest.has_value()) << describe(vertex.position);
            EXPECT_NEAR(closest->distance, 0.0F, 2.0e-4F) << describe(vertex.position);
            EXPECT_NEAR(length(vertex.normal), 1.0F, 1.0e-4F);
            EXPECT_GT(dot(vertex.normal, vertex.position), 0.0F) << describe(vertex.position) << " " << describe(vertex.normal);
        }

        // Every triangle faces away from the origin, and agrees with its vertices' normals.
        for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
        {
            const DebugVertex& a = mesh.vertices[mesh.indices[index]];
            const DebugVertex& b = mesh.vertices[mesh.indices[index + 1]];
            const DebugVertex& c = mesh.vertices[mesh.indices[index + 2]];
            const Vector3 face = cross(b.position - a.position, c.position - a.position);
            EXPECT_GT(dot(face, a.position + b.position + c.position), 0.0F);
            EXPECT_GT(dot(face, a.normal + b.normal + c.normal), 0.0F);
        }

        // Inside the shape's box and reaching its sides.
        const Aabb shape_box = item.shape->bounds();
        const Aabb box = mesh_bounds(mesh);
        EXPECT_TRUE(shape_box.expanded(1.0e-5F).contains(box)) << describe(box.minimum) << " " << describe(box.maximum);
        EXPECT_GT(box.extents().x, 0.98F * shape_box.extents().x);
        EXPECT_GT(box.extents().y, 0.98F * shape_box.extents().y);
        EXPECT_GT(box.extents().z, 0.98F * shape_box.extents().z);
    }
}

TEST(DebugDrawTest, MeshesEncloseTheShapesVolumesCloserWithMoreSegments)
{
    for (const ConvexCase& item : convex_cases())
    {
        SCOPED_TRACE(item.name);
        const double coarse = enclosed_volume(tessellate(*item.shape, {.segments = 32}));
        const double fine = enclosed_volume(tessellate(*item.shape, {.segments = 128}));
        const double coarse_error = std::abs(coarse / item.volume - 1.0);
        const double fine_error = std::abs(fine / item.volume - 1.0);
        EXPECT_LT(coarse_error, 0.03) << coarse << " for " << item.volume;
        EXPECT_LT(fine_error, 0.002) << fine << " for " << item.volume;
        EXPECT_LE(coarse, item.volume * (1.0 + 1.0e-5)) << "inscribed: never more than the shape";
        if (coarse_error > 1.0e-5)
        {
            EXPECT_LT(fine_error, coarse_error / 3.0) << "converges";
        }
    }
}

TEST(DebugDrawTest, BoxesAndHullsWithoutRoundingAreTheirFaces)
{
    // A box: six faces of two triangles, four vertices each with the face's normal.
    const DebugMesh box = tessellate(*make_shape(BoxShape{.half_extents = {.x = 1, .y = 2, .z = 3}}));
    EXPECT_EQ(box.triangle_count(), 12U);
    EXPECT_EQ(box.vertices.size(), 24U);
    EXPECT_NEAR(enclosed_volume(box), 48.0F, 1.0e-4F);

    // The wedge: two triangles and three rectangles, eight triangles in all.
    const DebugMesh wedge = tessellate(*make_shape(HullShape(WEDGE)));
    EXPECT_EQ(wedge.triangle_count(), 8U);
    EXPECT_EQ(wedge.vertices.size(), 18U);
}

TEST(DebugDrawTest, TriangleMeshesAndHeightFieldsAreTheirTriangles)
{
    // A one-sided square of two triangles, and the same two-sided.
    const std::array vertices{Vector3{}, Vector3{.x = 1}, Vector3{.x = 1, .z = -1}, Vector3{.z = -1}};
    const std::array<std::array<std::uint32_t, 3>, 2> triangles{std::array<std::uint32_t, 3>{0, 1, 2}, std::array<std::uint32_t, 3>{0, 2, 3}};
    const DebugMesh square = tessellate(*make_shape(TriangleMeshShape({.vertices = vertices, .triangles = triangles})));
    ASSERT_EQ(square.triangle_count(), 2U);
    EXPECT_FALSE(square.two_sided);
    for (std::size_t triangle = 0; triangle < 2; ++triangle)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const DebugVertex& vertex = square.vertices[square.indices[triangle * 3 + corner]];
            EXPECT_EQ(vertex.position, vertices[triangles[triangle][corner]]) << "in the mesh's order";
            EXPECT_TRUE(approximately_equal(vertex.normal, {.y = 1}, 1.0e-6F));
        }
    }
    EXPECT_TRUE(tessellate(*make_shape(TriangleMeshShape({.vertices = vertices, .triangles = triangles, .two_sided = true}))).two_sided);

    // A 3 × 4 height field: 2 × 3 cells of two triangles, facing up, at the samples' positions.
    const std::array heights{0.0F, 0.5F, 1.0F, 0.0F, 0.25F, 0.5F, 0.75F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F};
    const HeightFieldShape field({.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 3, 4}, .scale = {.x = 2, .y = 3, .z = 0.5F}});
    const DebugMesh ground = tessellate(*make_shape(field));
    EXPECT_EQ(ground.triangle_count(), 12U);
    for (const DebugVertex& vertex : ground.vertices)
    {
        EXPECT_GT(vertex.normal.y, 0.0F);
        const auto column = static_cast<std::size_t>(std::lround(vertex.position.x / 2.0F));
        const auto row = static_cast<std::size_t>(std::lround(vertex.position.z / 0.5F));
        ASSERT_LT(row, 3U);
        ASSERT_LT(column, 4U);
        EXPECT_FLOAT_EQ(vertex.position.y, heights[row * 4 + column] * 3.0F) << describe(vertex.position);
    }
}

TEST(DebugDrawTest, CompoundsAreTheirChildrenAtTheirPoses)
{
    // A sphere to the right, a box turned a quarter about y to the left.
    const auto sphere = make_shape(SphereShape{.radius = 0.5F});
    const auto box = make_shape(BoxShape{.half_extents = {.x = 1, .y = 0.5F, .z = 0.25F}});
    const Transform right{.position = {.x = 2}};
    const Transform left{.position = {.x = -2}, .orientation = Quaternion::from_axis_angle({.y = 1}, PI / 2)};
    const auto compound = make_shape(CompoundShape({CompoundChild{.shape = sphere, .pose = right}, CompoundChild{.shape = box, .pose = left}}));

    const DebugMesh mesh = tessellate(*compound);
    EXPECT_EQ(mesh.vertices.size(), tessellate(*sphere).vertices.size() + tessellate(*box).vertices.size());
    EXPECT_TRUE(closed(mesh));
    EXPECT_NEAR(enclosed_volume(mesh), enclosed_volume(tessellate(*sphere)) + enclosed_volume(tessellate(*box)), 1.0e-4F);
    for (const DebugVertex& vertex : mesh.vertices)
    {
        const std::optional<ShapeClosestPoint> closest = closest_point_on_shape(*compound, {}, vertex.position, 1.0F);
        ASSERT_TRUE(closest.has_value());
        EXPECT_NEAR(closest->distance, 0.0F, 2.0e-4F) << describe(vertex.position);
    }
    const Aabb bounds = mesh_bounds(mesh);
    EXPECT_NEAR(bounds.minimum.x, -2.25F, 1.0e-5F) << "the box turned: its z extent along x";
    EXPECT_NEAR(bounds.maximum.x, 2.5F, 1.0e-5F);

    // Appending keeps what the mesh held.
    DebugMesh both = tessellate(*sphere);
    tessellate(*box, both);
    EXPECT_EQ(both.vertices.size(), tessellate(*sphere).vertices.size() + tessellate(*box).vertices.size());
}

TEST(DebugDrawTest, TooFewSegmentsAreRefused)
{
    try
    {
        static_cast<void>(tessellate(*make_shape(SphereShape{}), {.segments = 2}));
        FAIL() << "2 segments were accepted";
    }
    catch (const PhysicsError& error)
    {
        EXPECT_TRUE(std::string_view{error.what()}.starts_with("[gfx::physics::GEOMETRY] tessellate: 2 segments")) << error.what();
    }
    DebugLines lines;
    EXPECT_THROW(lines.add_circle({}, {.y = 1}, 1.0F, {}, 2), PhysicsError);
    EXPECT_THROW(lines.add_circle({}, {}, 1.0F, {}), PhysicsError);
}

TEST(DebugDrawTest, LinesOutlineBoxesAxesArrowsCirclesAndContacts)
{
    DebugLines lines;
    constexpr DebugColor YELLOW{.red = 255, .green = 255, .blue = 0};

    // A box's 12 edges, each between two of its corners, one axis apart.
    lines.add_box(Aabb::from_center({.x = 1}, {.x = 1, .y = 2, .z = 3}), YELLOW);
    ASSERT_EQ(lines.line_count(), 12U);
    for (std::size_t line = 0; line < 12; ++line)
    {
        const Vector3 from = lines.vertices()[2 * line].position;
        const Vector3 to = lines.vertices()[2 * line + 1].position;
        for (const Vector3 end : {from, to})
        {
            EXPECT_TRUE((end.x == 0 || end.x == 2) && std::abs(end.y) == 2 && std::abs(end.z) == 3) << describe(end);
        }
        const Vector3 difference = component_absolute(to - from);
        EXPECT_EQ(static_cast<int>(difference.x > 0) + static_cast<int>(difference.y > 0) + static_cast<int>(difference.z > 0), 1);
        EXPECT_EQ(lines.vertices()[2 * line].color, YELLOW);
    }

    // A turned box: its corners turned.
    lines.clear();
    const Quaternion turn = Quaternion::from_axis_angle({.z = 1}, PI / 2);
    lines.add_box(Obb{.center = {}, .half_extents = {.x = 2, .y = 1, .z = 1}, .orientation = turn}, YELLOW);
    for (const DebugLineVertex& vertex : lines.vertices())
    {
        EXPECT_NEAR(std::abs(vertex.position.x), 1.0F, 1.0e-5F) << "the x half extent of 2 now along y";
        EXPECT_NEAR(std::abs(vertex.position.y), 2.0F, 1.0e-5F);
    }

    // Axes: three colored segments of the length asked.
    lines.clear();
    lines.add_axes({.position = {.y = 1}}, 0.5F);
    ASSERT_EQ(lines.line_count(), 3U);
    EXPECT_EQ(lines.vertices()[1].position, (Vector3{.x = 0.5F, .y = 1}));
    EXPECT_EQ(lines.vertices()[0].color, (DebugColor{.red = 255, .green = 0, .blue = 0}));
    EXPECT_EQ(lines.vertices()[5].position, (Vector3{.y = 1, .z = 0.5F}));

    // An arrow: the shaft and four strokes back from the tip; without length, the shaft only.
    lines.clear();
    lines.add_arrow({}, {.y = 2}, YELLOW, 0.4F);
    ASSERT_EQ(lines.line_count(), 5U);
    for (std::size_t line = 1; line < 5; ++line)
    {
        EXPECT_EQ(lines.vertices()[2 * line].position, (Vector3{.y = 2}));
        EXPECT_NEAR(lines.vertices()[2 * line + 1].position.y, 1.6F, 1.0e-6F);
    }
    lines.clear();
    lines.add_arrow({.x = 1}, {.x = 1}, YELLOW, 0.4F);
    EXPECT_EQ(lines.line_count(), 1U);

    // A circle: closed, every point at its radius in its plane.
    lines.clear();
    lines.add_circle({.y = 1}, {.y = 3}, 2.0F, YELLOW, 24);
    ASSERT_EQ(lines.line_count(), 24U);
    EXPECT_EQ(lines.vertices().front().position, lines.vertices().back().position);
    for (const DebugLineVertex& vertex : lines.vertices())
    {
        EXPECT_NEAR(distance(vertex.position, {.y = 1}), 2.0F, 1.0e-5F);
        EXPECT_NEAR(vertex.position.y, 1.0F, 1.0e-6F);
    }

    // Contacts: a cross and a normal arrow per point.
    lines.clear();
    ContactManifold manifold{.normal = {.y = 1}, .point_count = 2};
    manifold.points[0].position = {.x = 1};
    manifold.points[1].position = {.x = -1};
    lines.add_contacts(manifold, YELLOW, 0.1F, 0.5F);
    EXPECT_EQ(lines.line_count(), 2U * (3U + 5U));
    EXPECT_EQ(lines.vertices()[7].position, (Vector3{.x = 1, .y = 0.5F})) << "the first point's arrow ends half a unit up";

    // Clearing keeps the storage.
    const std::size_t held = lines.vertices().size();
    lines.clear();
    EXPECT_TRUE(lines.vertices().empty());
    lines.add_contacts(manifold, YELLOW, 0.1F, 0.5F);
    EXPECT_EQ(lines.vertices().size(), held);
}

} // namespace
