/// @file presets.cppm
/// @brief Soft bodies made for common uses: cloth (a grid of particles), rope (a line of them) and tetrahedral volumes,
///        as SoftBodyDescriptions an application may still edit (more pins, other compliances, self-collision); pins;
///        long-range attachments. Compliances are XPBD's: the inverse of a constraint's stiffness (0: rigid), in meters
///        per newton for distances and radians per newton-meter for angles (a volume's constraint is six times the
///        tetrahedron's volume: its compliance is in meters to the fifth per newton).
export module gfx.physics.dynamics:presets;

import std;
import gfx.core.math;
import gfx.physics.handles;
import gfx.physics.interface;

export namespace gfx::physics
{

// ===================================================================================================== cloth

/// @brief A cloth: columns × rows particles a spacing apart, the first row along `column_direction` from `origin`, the
///        next rows along `row_direction` (by default a curtain hanging down from its first row).
struct ClothPresetDescription
{
    std::uint32_t columns = 16;           ///< Particles along a row, at least 2.
    std::uint32_t rows = 16;              ///< Rows, at least 2.
    float spacing = 0.05F;                ///< Between neighbors (meters), greater than 0.
    Vector3 origin{};                     ///< The first particle (column 0 of row 0).
    Vector3 column_direction{.x = 1.0F};  ///< Along a row (normalized).
    Vector3 row_direction{.y = -1.0F};    ///< From a row to the next (normalized; not parallel to the columns).
    float area_density = 0.2F;            ///< Kilograms per square meter, greater than 0.
    float stretch_compliance = 0.0F;      ///< Of the grid's edges.
    float shear_compliance = 1.0e-6F;     ///< Of the cells' diagonals.
    float bending_compliance = 1.0e-3F;   ///< Of the bending constraints.
    bool dihedral_bending = false;        ///< DIHEDRAL constraints between the cells' triangles (else BENDING distances two particles apart).
    float particle_radius = 0.0F;         ///< For collisions; 0: half the spacing.
};

/// @brief The particle at @p column of @p row of a cloth of @p columns (rows follow each other).
[[nodiscard]] constexpr auto
cloth_particle(std::uint32_t columns,
               std::uint32_t column,
               std::uint32_t row) noexcept -> std::uint32_t
{
    return row * columns + column;
}

/// @brief A cloth: STRETCH along the grid's edges, SHEAR along the cells' diagonals, and BENDING distances two
///        particles apart along rows and columns (or DIHEDRAL angles between the triangles of neighboring cells).
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto cloth_preset(const ClothPresetDescription& description) -> SoftBodyDescription;

// ===================================================================================================== rope

/// @brief A rope: segments + 1 particles along a line.
struct RopePresetDescription
{
    std::uint32_t segments = 16;        ///< At least 1.
    float length = 1.0F;                ///< Meters, greater than 0.
    Vector3 origin{};                   ///< The first particle.
    Vector3 direction{.y = -1.0F};      ///< Toward the last (normalized).
    float mass = 0.1F;                  ///< Kilograms, greater than 0 (shared equally by the particles).
    float stretch_compliance = 0.0F;    ///< Of the segments.
    float bending_compliance = 1.0e-2F; ///< Of the distances two particles apart.
    float particle_radius = 0.0F;       ///< For collisions; 0: half a segment.
};

/// @brief A rope: STRETCH between neighbors, BENDING between every other particle.
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto rope_preset(const RopePresetDescription& description) -> SoftBodyDescription;

// ===================================================================================================== volumes

/// @brief Positions and the tetrahedra between them (four indices each).
struct TetrahedralMesh
{
    std::vector<Vector3> positions{};                       ///< The vertices.
    std::vector<std::array<std::uint32_t, 4>> tetrahedra{}; ///< Indices into the positions.
};

/// @brief A box split into cells, each cell into six tetrahedra (along its main diagonal: neighbors share faces).
struct TetrahedralBoxDescription
{
    Vector3 half_extents{.x = 0.5F, .y = 0.5F, .z = 0.5F}; ///< Greater than 0.
    std::array<std::uint32_t, 3> cells{2, 2, 2};           ///< Along x, y, z, at least 1 each.
    Transform pose{};                                      ///< Where its center goes.
};

/// @brief A tetrahedral box.
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto tetrahedral_box(const TetrahedralBoxDescription& description) -> TetrahedralMesh;

/// @brief How a volume is made of a tetrahedral mesh.
struct VolumePresetDescription
{
    float density = 100.0F;          ///< Kilograms per cubic meter, greater than 0 (each tetrahedron's mass goes to its four corners).
    float edge_compliance = 0.0F;    ///< Of the tetrahedra's edges (STRETCH).
    float volume_compliance = 0.0F;  ///< Of their volumes.
    float particle_radius = 0.0F;    ///< For collisions; 0: half the shortest edge.
};

/// @brief A tetrahedral mesh from the text of TetGen's files — what tools/tetrahedralize.py writes for any closed triangle
///        mesh (OBJ, STL, PLY, OFF), and TetGen itself: @p node ("<vertices> 3 <attributes> <markers>", then a row
///        "<index> <x> <y> <z> ..." per vertex) and @p element ("<tetrahedra> 4 <attributes>", then a row
///        "<index> <a> <b> <c> <d> ..." per tetrahedron). '#' starts a comment; indices count from 0 or 1 (the first vertex's
///        index says which); attributes and markers are ignored; each tetrahedron is oriented positively (two corners
///        swapped when given the other way).
/// @throws PhysicsError naming the file and the row when a file is malformed or an index is out of range.
[[nodiscard]] auto read_tetgen_mesh(std::string_view node,
                                    std::string_view element) -> TetrahedralMesh;

/// @brief The boundary of @p mesh: the faces that belong to one tetrahedron only, wound outward (counterclockwise seen from
///        outside) — the surface to draw of a volume made by volume_preset(), its vertices the particles'.
[[nodiscard]] auto tetrahedral_mesh_surface(const TetrahedralMesh& mesh) -> std::vector<std::array<std::uint32_t, 3>>;

/// @brief A volume: a particle per vertex, STRETCH along every edge (once), VOLUME for every tetrahedron.
/// @throws PhysicsError if the mesh's indices are out of range, a tetrahedron is flat, or @p description is invalid.
[[nodiscard]] auto volume_preset(const TetrahedralMesh& mesh,
                                 const VolumePresetDescription& description = {}) -> SoftBodyDescription;

// ===================================================================================================== pins and attachments

/// @brief Holds @p particle where @p description starts it.
/// @throws PhysicsError if @p particle is not one of its particles.
[[nodiscard]] auto pin_to_world(const SoftBodyDescription& description,
                                std::uint32_t particle,
                                float compliance = 0.0F) -> ParticlePin;

/// @brief Holds @p particle at @p point of the world.
[[nodiscard]] constexpr auto
pin_to_world(std::uint32_t particle,
             Vector3 point,
             float compliance = 0.0F) noexcept -> ParticlePin
{
    return {.particle = particle, .body = {}, .point = point, .compliance = compliance};
}

/// @brief Holds @p particle at @p point of @p body (in the body's frame).
[[nodiscard]] constexpr auto
pin_to_body(std::uint32_t particle,
            RigidBodyHandle body,
            Vector3 point,
            float compliance = 0.0F) noexcept -> ParticlePin
{
    return {.particle = particle, .body = body, .point = point, .compliance = compliance};
}

/// @brief Adds a TETHER from every movable particle that is not pinned to every particle pinned to the world, at their
///        starting distance: the cloth or rope cannot stretch away from its pins however few iterations run (long-range
///        attachments). Add the pins first.
/// @return How many tethers were added.
auto add_long_range_attachments(SoftBodyDescription& description,
                                float compliance = 0.0F) -> std::uint32_t;

} // namespace gfx::physics
