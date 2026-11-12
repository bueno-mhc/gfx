// Tetrahedral meshes for XPBD volumes: TetGen's files read (read_tetgen_mesh()), a mesh's boundary
// (tetrahedral_mesh_surface()), and a torus tetrahedralized by tools/tetrahedralize.py (written by the CTest fixture
// gfx_tetrahedral_meshes into GFX_TEST_TETRAHEDRAL_DIRECTORY) simulated as a soft body.
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

/// Six times the signed volume of a tetrahedron.
[[nodiscard]] auto
six_volume(Vector3 a,
           Vector3 b,
           Vector3 c,
           Vector3 d) -> float
{
    return dot(b - a, cross(c - a, d - a));
}

/// The volume a closed surface encloses (the divergence theorem: positive when wound outward).
[[nodiscard]] auto
enclosed_volume(std::span<const Vector3> positions,
                std::span<const std::array<std::uint32_t, 3>> surface) -> float
{
    float total = 0.0F;
    for (const auto& [a, b, c] : surface)
    {
        total += dot(positions[a], cross(positions[b], positions[c])) / 6.0F;
    }
    return total;
}

/// Whether every edge of @p surface is used once in each direction (a closed, consistently wound surface).
[[nodiscard]] auto
closed(std::span<const std::array<std::uint32_t, 3>> surface) -> bool
{
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (const auto& [a, b, c] : surface)
    {
        for (const auto& [from, to] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}})
        {
            ++edges[{from, to}];
        }
    }
    return std::ranges::all_of(edges, [&edges](const auto& entry) { return entry.second == 1 && edges.contains({entry.first.second, entry.first.first}); });
}

/// A file's text (empty when it cannot be read).
[[nodiscard]] auto
read_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

TEST(TetrahedralMeshTest, TetGenFilesAreReadWhateverTheirIndexBase)
{
    // Indices from 1, comments, attributes and markers; a tetrahedron given negatively is turned positive.
    const std::string_view node = "# five vertices\n5 3 1 1\n1 0 0 0 7.5 1\n2 1 0 0 7.5 0\n3 0 1 0 7.5 0\n4 0 0 1 7.5 0\n5 1 1 1 7.5 0\n";
    const std::string_view element = "2 4 0\n1 1 2 3 4\n2 2 3 5 4 # given negatively\n";
    const TetrahedralMesh mesh = read_tetgen_mesh(node, element);
    ASSERT_EQ(mesh.positions.size(), 5U);
    ASSERT_EQ(mesh.tetrahedra.size(), 2U);
    EXPECT_EQ(mesh.positions[4].x, 1.0F);
    EXPECT_EQ(mesh.tetrahedra[0], (std::array<std::uint32_t, 4>{0, 1, 2, 3}));
    for (const auto& [a, b, c, d] : mesh.tetrahedra)
    {
        EXPECT_GT(six_volume(mesh.positions[a], mesh.positions[b], mesh.positions[c], mesh.positions[d]), 0.0F);
    }

    // Indices from 0, as tools/tetrahedralize.py writes them (Windows line ends too).
    const TetrahedralMesh zero = read_tetgen_mesh("4 3 0 0\r\n0 0 0 0\r\n1 1 0 0\r\n2 0 1 0\r\n3 0 0 1\r\n", "1 4 0\r\n0 0 1 2 3\r\n");
    ASSERT_EQ(zero.tetrahedra.size(), 1U);
    EXPECT_EQ(zero.tetrahedra[0], (std::array<std::uint32_t, 4>{0, 1, 2, 3}));
}

TEST(TetrahedralMeshTest, MalformedFilesAreRefusedNamingTheProblem)
{
    const std::string_view node = "4 3 0 0\n0 0 0 0\n1 1 0 0\n2 0 1 0\n3 0 0 1\n";
    const auto refusal = [](std::string_view node_text, std::string_view element_text) -> std::string {
        try
        {
            (void)read_tetgen_mesh(node_text, element_text);
        }
        catch (const PhysicsError& error)
        {
            return error.what();
        }
        return {};
    };
    EXPECT_NE(refusal(node, "1 10 0\n0 0 1 2 3 0 0 0 0 0 0\n").find("10 corners"), std::string::npos) << "quadratic tetrahedra";
    EXPECT_NE(refusal(node, "1 4 0\n0 0 1 2 9\n").find("names vertex 9"), std::string::npos) << "an index out of range";
    EXPECT_NE(refusal("4 3 0 0\n0 0 0 0\n1 one 0 0\n2 0 1 0\n3 0 0 1\n", "1 4 0\n0 0 1 2 3\n").find("'one'"), std::string::npos) << "not a number";
    EXPECT_NE(refusal(node, "2 4 0\n0 0 1 2 3\n").find("hold"), std::string::npos) << "a row missing";
}

TEST(TetrahedralMeshTest, TheBoundaryOfAMeshIsClosedAndWoundOutward)
{
    // A box of 2 × 3 × 4 cells, six tetrahedra each: its boundary is two triangles per cell face on the box's sides.
    const TetrahedralMesh box = tetrahedral_box({.half_extents = {.x = 1.0F, .y = 1.5F, .z = 2.0F}, .cells = {2, 3, 4}});
    const std::vector<std::array<std::uint32_t, 3>> surface = tetrahedral_mesh_surface(box);
    EXPECT_EQ(surface.size(), 2U * 2U * (2U * 3U + 3U * 4U + 2U * 4U));
    EXPECT_TRUE(closed(surface));
    EXPECT_NEAR(enclosed_volume(box.positions, surface), 2.0F * 3.0F * 4.0F, 1.0e-4F) << "wound outward, enclosing the box";
}

TEST(TetrahedralMeshTest, AToolTetrahedralizedTorusIsAVolumeThatKeepsItsShape)
{
    // tools/tetrahedralize.py's torus (major radius 1, minor 0.35): its boundary closed and wound outward, its volume the
    // torus's (2π² R r² = 2.42) within 5 %; dropped as a soft body from 1 m onto the floor of a reference-backend world,
    // after 2 s every particle is finite and above the floor and the volume is kept within 5 %.
    const std::filesystem::path directory{GFX_TEST_TETRAHEDRAL_DIRECTORY};
    const std::string node = read_text(directory / "torus.node");
    const std::string element = read_text(directory / "torus.ele");
    if (node.empty() || element.empty())
    {
        GTEST_SKIP() << "no tetrahedralized torus in " << directory.generic_string() << " (CTest's fixture gfx_tetrahedral_meshes writes it: Python 3)";
    }
    TetrahedralMesh mesh = read_tetgen_mesh(node, element);
    const std::vector<std::array<std::uint32_t, 3>> surface = tetrahedral_mesh_surface(mesh);
    EXPECT_TRUE(closed(surface));
    const float rest = enclosed_volume(mesh.positions, surface);
    EXPECT_NEAR(rest, 2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * 1.0F * 0.35F * 0.35F, 0.05F * 2.42F);
    for (Vector3& position : mesh.positions)
    {
        position.y += 1.0F;
    }

    const std::unique_ptr<PhysicsWorld> world = create_physics_world({}, PhysicsBackendKind::REFERENCE);
    const std::unique_ptr<RigidBody> floor = world->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}});
    const std::unique_ptr<Collider> ground = floor->add_collider({.shape = make_shape(BoxShape{.half_extents = {.x = 10.0F, .y = 0.5F, .z = 10.0F}})});
    const std::unique_ptr<SoftBody> torus = world->create_soft_body(volume_preset(mesh, {.density = 300.0F}));
    for (int step = 0; step < 120; ++step)
    {
        world->step(1.0F / 60.0F);
    }
    std::vector<Vector3> positions(torus->particle_count());
    (void)torus->positions(positions);
    for (const Vector3 position : positions)
    {
        ASSERT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
        EXPECT_GT(position.y, -0.03F) << "a particle went through the floor";
    }
    EXPECT_LT(std::ranges::max(positions, {}, &Vector3::y).y, 0.8F) << "the torus fell";
    EXPECT_NEAR(enclosed_volume(positions, surface), rest, 0.05F * rest) << "the volume is kept";
}

} // namespace
