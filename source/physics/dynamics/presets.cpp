module gfx.physics.dynamics;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.interface;
import :xpbd_math;

namespace gfx::physics
{

namespace
{

/// Throws the PhysicsError of an invalid preset.
[[noreturn]] void
reject_preset(std::string_view operation,
              std::string problem,
              std::string suggestion)
{
    throw PhysicsError({.backend = PhysicsSubsystem::SOFT_BODIES, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::move(suggestion)});
}

/// Throws unless @p value is finite and positive.
void
require_positive(float value,
                 const char* what,
                 std::string_view operation)
{
    if (!(value > 0.0F) || !std::isfinite(value))
    {
        reject_preset(operation, std::format("{} {} is not finite and positive", what, value), std::format("give {} greater than 0", what));
    }
}

/// Throws unless @p value is finite and not negative.
void
require_non_negative(float value,
                     const char* what,
                     std::string_view operation)
{
    if (!(value >= 0.0F) || !std::isfinite(value))
    {
        reject_preset(operation, std::format("{} {} is not finite and at least 0", what, value), std::format("give {} of at least 0", what));
    }
}

/// @p direction normalized; throws if it is not finite or of length 0.
[[nodiscard]] auto
unit_direction(Vector3 direction,
               const char* what,
               std::string_view operation) -> Vector3
{
    const float size = length(direction);
    if (!(size > GEOMETRIC_TOLERANCE) || !std::isfinite(size))
    {
        reject_preset(operation, std::format("the {} ({}, {}, {}) has no direction", what, direction.x, direction.y, direction.z), "give a non-zero direction");
    }
    return direction / size;
}

/// A constraint between two particles.
[[nodiscard]] auto
distance_constraint(SoftBodyConstraintKind kind,
                    std::uint32_t first,
                    std::uint32_t second,
                    float compliance) noexcept -> SoftBodyConstraint
{
    return {.kind = kind, .particles = {first, second, 0, 0}, .compliance = compliance, .rest_value = {}};
}

} // namespace

// ===================================================================================================== cloth

auto
cloth_preset(const ClothPresetDescription& description) -> SoftBodyDescription
{
    constexpr std::string_view OPERATION = "cloth_preset";

    // The description checked, its directions normalized.
    if (description.columns < 2 || description.rows < 2)
    {
        reject_preset(OPERATION, std::format("a cloth of {} × {} particles", description.columns, description.rows), "give at least 2 columns and 2 rows");
    }
    require_positive(description.spacing, "the spacing", OPERATION);
    require_positive(description.area_density, "the area density", OPERATION);
    require_non_negative(description.stretch_compliance, "the stretch compliance", OPERATION);
    require_non_negative(description.shear_compliance, "the shear compliance", OPERATION);
    require_non_negative(description.bending_compliance, "the bending compliance", OPERATION);
    require_non_negative(description.particle_radius, "the particle radius", OPERATION);
    const Vector3 along = unit_direction(description.column_direction, "column direction", OPERATION);
    const Vector3 down = unit_direction(description.row_direction, "row direction", OPERATION);
    if (length(cross(along, down)) < 1.0e-3F)
    {
        reject_preset(OPERATION, "the row direction is parallel to the column direction", "give two different directions");
    }

    // The particles: the cloth's mass shared equally.
    const std::uint32_t columns = description.columns;
    const std::uint32_t rows = description.rows;
    const float area = static_cast<float>(columns - 1) * static_cast<float>(rows - 1) * description.spacing * description.spacing;
    const float particle_mass = description.area_density * area / static_cast<float>(columns * rows);
    SoftBodyDescription cloth{.particle_radius = description.particle_radius > 0.0F ? description.particle_radius : description.spacing * 0.5F};
    cloth.particles.reserve(static_cast<std::size_t>(columns) * rows);
    for (std::uint32_t row = 0; row < rows; ++row)
    {
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            const Vector3 position = description.origin + along * (static_cast<float>(column) * description.spacing) + down * (static_cast<float>(row) * description.spacing);
            cloth.particles.push_back({.position = position, .inverse_mass = 1.0F / particle_mass});
        }
    }

    // The grid's edges; the cells' diagonals; bending two particles apart, or between the cells' triangles.
    const auto at = [columns](std::uint32_t column, std::uint32_t row) { return cloth_particle(columns, column, row); };
    for (std::uint32_t row = 0; row < rows; ++row)
    {
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            if (column + 1 < columns)
            {
                cloth.constraints.push_back(distance_constraint(SoftBodyConstraintKind::STRETCH, at(column, row), at(column + 1, row), description.stretch_compliance));
            }
            if (row + 1 < rows)
            {
                cloth.constraints.push_back(distance_constraint(SoftBodyConstraintKind::STRETCH, at(column, row), at(column, row + 1), description.stretch_compliance));
            }
            if (column + 1 < columns && row + 1 < rows)
            {
                cloth.constraints.push_back(distance_constraint(SoftBodyConstraintKind::SHEAR, at(column, row), at(column + 1, row + 1), description.shear_compliance));
                cloth.constraints.push_back(distance_constraint(SoftBodyConstraintKind::SHEAR, at(column + 1, row), at(column, row + 1), description.shear_compliance));
            }
        }
    }
    if (!description.dihedral_bending)
    {
        for (std::uint32_t row = 0; row < rows; ++row)
        {
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                if (column + 2 < columns)
                {
                    cloth.constraints.push_back(distance_constraint(SoftBodyConstraintKind::BENDING, at(column, row), at(column + 2, row), description.bending_compliance));
                }
                if (row + 2 < rows)
                {
                    cloth.constraints.push_back(distance_constraint(SoftBodyConstraintKind::BENDING, at(column, row), at(column, row + 2), description.bending_compliance));
                }
            }
        }
        return cloth;
    }

    // Each cell is two triangles split along its diagonal (column, row)–(column + 1, row + 1): the diagonal, and the
    // grid's inner edges, each join two triangles.
    const auto dihedral = [&](std::uint32_t first, std::uint32_t second, std::uint32_t wing, std::uint32_t other_wing) {
        cloth.constraints.push_back(
            {.kind = SoftBodyConstraintKind::DIHEDRAL, .particles = {first, second, wing, other_wing}, .compliance = description.bending_compliance, .rest_value = {}});
    };
    for (std::uint32_t row = 0; row + 1 < rows; ++row)
    {
        for (std::uint32_t column = 0; column + 1 < columns; ++column)
        {
            dihedral(at(column, row), at(column + 1, row + 1), at(column + 1, row), at(column, row + 1));
        }
    }
    for (std::uint32_t row = 1; row + 1 < rows; ++row)
    {
        for (std::uint32_t column = 0; column + 1 < columns; ++column)
        {
            dihedral(at(column, row), at(column + 1, row), at(column, row - 1), at(column + 1, row + 1));
        }
    }
    for (std::uint32_t row = 0; row + 1 < rows; ++row)
    {
        for (std::uint32_t column = 1; column + 1 < columns; ++column)
        {
            dihedral(at(column, row), at(column, row + 1), at(column - 1, row), at(column + 1, row + 1));
        }
    }
    return cloth;
}

// ===================================================================================================== rope

auto
rope_preset(const RopePresetDescription& description) -> SoftBodyDescription
{
    constexpr std::string_view OPERATION = "rope_preset";

    // The description checked.
    if (description.segments < 1)
    {
        reject_preset(OPERATION, "a rope of no segment", "give at least one segment");
    }
    require_positive(description.length, "the length", OPERATION);
    require_positive(description.mass, "the mass", OPERATION);
    require_non_negative(description.stretch_compliance, "the stretch compliance", OPERATION);
    require_non_negative(description.bending_compliance, "the bending compliance", OPERATION);
    require_non_negative(description.particle_radius, "the particle radius", OPERATION);
    const Vector3 direction = unit_direction(description.direction, "direction", OPERATION);

    // The particles along the line, the rope's mass shared equally; segments, then bending every other particle.
    const std::uint32_t count = description.segments + 1;
    const float segment = description.length / static_cast<float>(description.segments);
    SoftBodyDescription rope{.particle_radius = description.particle_radius > 0.0F ? description.particle_radius : segment * 0.5F};
    rope.particles.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        rope.particles.push_back({.position = description.origin + direction * (static_cast<float>(index) * segment),
                                  .inverse_mass = static_cast<float>(count) / description.mass});
    }
    for (std::uint32_t index = 0; index + 1 < count; ++index)
    {
        rope.constraints.push_back(distance_constraint(SoftBodyConstraintKind::STRETCH, index, index + 1, description.stretch_compliance));
    }
    for (std::uint32_t index = 0; index + 2 < count; ++index)
    {
        rope.constraints.push_back(distance_constraint(SoftBodyConstraintKind::BENDING, index, index + 2, description.bending_compliance));
    }
    return rope;
}

// ===================================================================================================== volumes

auto
tetrahedral_box(const TetrahedralBoxDescription& description) -> TetrahedralMesh
{
    constexpr std::string_view OPERATION = "tetrahedral_box";

    // The description checked.
    require_positive(description.half_extents.x, "the half extent along x", OPERATION);
    require_positive(description.half_extents.y, "the half extent along y", OPERATION);
    require_positive(description.half_extents.z, "the half extent along z", OPERATION);
    const auto [cells_x, cells_y, cells_z] = description.cells;
    if (cells_x < 1 || cells_y < 1 || cells_z < 1)
    {
        reject_preset(OPERATION, std::format("a box of {} × {} × {} cells", cells_x, cells_y, cells_z), "give at least one cell along each axis");
    }

    // The grid's vertices, at the box's pose.
    TetrahedralMesh mesh;
    const auto vertex = [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (z * (cells_y + 1) + y) * (cells_x + 1) + x; };
    for (std::uint32_t z = 0; z <= cells_z; ++z)
    {
        for (std::uint32_t y = 0; y <= cells_y; ++y)
        {
            for (std::uint32_t x = 0; x <= cells_x; ++x)
            {
                const Vector3 local{.x = description.half_extents.x * (2.0F * static_cast<float>(x) / static_cast<float>(cells_x) - 1.0F),
                                    .y = description.half_extents.y * (2.0F * static_cast<float>(y) / static_cast<float>(cells_y) - 1.0F),
                                    .z = description.half_extents.z * (2.0F * static_cast<float>(z) / static_cast<float>(cells_z) - 1.0F)};
                mesh.positions.push_back(transform_point(description.pose, local));
            }
        }
    }

    // Six tetrahedra per cell around its diagonal from corner 0 to corner 7 (corner bits: x 1, y 2, z 4), each made
    // positive.
    constexpr std::array<std::array<std::uint32_t, 4>, 6> KUHN{{{0, 1, 3, 7}, {0, 1, 5, 7}, {0, 2, 3, 7}, {0, 2, 6, 7}, {0, 4, 5, 7}, {0, 4, 6, 7}}};
    for (std::uint32_t z = 0; z < cells_z; ++z)
    {
        for (std::uint32_t y = 0; y < cells_y; ++y)
        {
            for (std::uint32_t x = 0; x < cells_x; ++x)
            {
                std::array<std::uint32_t, 8> corners{};
                for (std::uint32_t corner = 0; corner < 8; ++corner)
                {
                    corners[corner] = vertex(x + (corner & 1U), y + ((corner >> 1U) & 1U), z + ((corner >> 2U) & 1U));
                }
                for (const std::array<std::uint32_t, 4>& tetrahedron : KUHN)
                {
                    std::array<std::uint32_t, 4> indices{corners[tetrahedron[0]], corners[tetrahedron[1]], corners[tetrahedron[2]], corners[tetrahedron[3]]};
                    if (xpbd::six_volume(mesh.positions[indices[0]], mesh.positions[indices[1]], mesh.positions[indices[2]], mesh.positions[indices[3]]) < 0.0F)
                    {
                        std::swap(indices[2], indices[3]);
                    }
                    mesh.tetrahedra.push_back(indices);
                }
            }
        }
    }
    return mesh;
}

auto
volume_preset(const TetrahedralMesh& mesh,
              const VolumePresetDescription& description) -> SoftBodyDescription
{
    constexpr std::string_view OPERATION = "volume_preset";

    // The description and the mesh checked.
    require_positive(description.density, "the density", OPERATION);
    require_non_negative(description.edge_compliance, "the edge compliance", OPERATION);
    require_non_negative(description.volume_compliance, "the volume compliance", OPERATION);
    require_non_negative(description.particle_radius, "the particle radius", OPERATION);
    const auto count = static_cast<std::uint32_t>(mesh.positions.size());
    if (mesh.tetrahedra.empty())
    {
        reject_preset(OPERATION, "the mesh has no tetrahedron", "give at least one tetrahedron");
    }

    // Each tetrahedron's mass to its corners; its six edges, once each; its volume.
    std::vector<float> masses(count, 0.0F);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
    SoftBodyDescription volume;
    for (std::size_t index = 0; index < mesh.tetrahedra.size(); ++index)
    {
        const std::array<std::uint32_t, 4>& tetrahedron = mesh.tetrahedra[index];
        for (const std::uint32_t corner : tetrahedron)
        {
            if (corner >= count)
            {
                reject_preset(OPERATION, std::format("tetrahedron {} names vertex {} of {}", index, corner, count), "name vertices of the mesh");
            }
        }
        const float six = xpbd::six_volume(mesh.positions[tetrahedron[0]], mesh.positions[tetrahedron[1]], mesh.positions[tetrahedron[2]], mesh.positions[tetrahedron[3]]);
        const float edge_scale = distance(mesh.positions[tetrahedron[0]], mesh.positions[tetrahedron[1]]);
        if (!(std::abs(six) > 1.0e-6F * edge_scale * edge_scale * edge_scale))
        {
            reject_preset(OPERATION, std::format("tetrahedron {} is flat", index), "give tetrahedra with a volume");
        }
        for (const std::uint32_t corner : tetrahedron)
        {
            masses[corner] += description.density * std::abs(six) / 6.0F / 4.0F;
        }
        for (std::uint32_t first = 0; first < 4; ++first)
        {
            for (std::uint32_t second = first + 1; second < 4; ++second)
            {
                edges.emplace_back(std::min(tetrahedron[first], tetrahedron[second]), std::max(tetrahedron[first], tetrahedron[second]));
            }
        }
        volume.constraints.push_back({.kind = SoftBodyConstraintKind::VOLUME, .particles = tetrahedron, .compliance = description.volume_compliance, .rest_value = {}});
    }
    std::ranges::sort(edges);
    const auto [last, end] = std::ranges::unique(edges);
    edges.erase(last, end);

    // The particles (vertices of no tetrahedron do not move); the edges; the radius.
    volume.particles.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        volume.particles.push_back({.position = mesh.positions[index], .inverse_mass = masses[index] > 0.0F ? 1.0F / masses[index] : 0.0F});
    }
    float shortest = std::numeric_limits<float>::infinity();
    for (const auto& [first, second] : edges)
    {
        volume.constraints.push_back(distance_constraint(SoftBodyConstraintKind::STRETCH, first, second, description.edge_compliance));
        shortest = std::min(shortest, distance(mesh.positions[first], mesh.positions[second]));
    }
    std::ranges::rotate(volume.constraints, volume.constraints.end() - static_cast<std::ptrdiff_t>(edges.size()));
    volume.particle_radius = description.particle_radius > 0.0F ? description.particle_radius : shortest * 0.5F;
    return volume;
}

// ===================================================================================================== pins and attachments

auto
pin_to_world(const SoftBodyDescription& description,
             std::uint32_t particle,
             float compliance) -> ParticlePin
{
    if (particle >= description.particles.size())
    {
        reject_preset("pin_to_world", std::format("the pin holds particle {} of {}", particle, description.particles.size()), "pin a particle of the soft body");
    }
    return pin_to_world(particle, description.particles[particle].position, compliance);
}

auto
add_long_range_attachments(SoftBodyDescription& description,
                           float compliance) -> std::uint32_t
{
    // The particles pinned to the world, and every pinned one.
    std::vector<std::uint32_t> anchors;
    std::vector<std::uint8_t> pinned(description.particles.size(), 0);
    for (const ParticlePin& pin : description.pins)
    {
        if (pin.particle < pinned.size())
        {
            pinned[pin.particle] = 1;
            if (!pin.body)
            {
                anchors.push_back(pin.particle);
            }
        }
    }
    std::ranges::sort(anchors);
    const auto [last, end] = std::ranges::unique(anchors);
    anchors.erase(last, end);

    // A tether from each free movable particle to each anchor.
    std::uint32_t added = 0;
    for (std::uint32_t particle = 0; particle < description.particles.size(); ++particle)
    {
        if (pinned[particle] != 0 || !(description.particles[particle].inverse_mass > 0.0F))
        {
            continue;
        }
        for (const std::uint32_t anchor : anchors)
        {
            description.constraints.push_back(distance_constraint(SoftBodyConstraintKind::TETHER, particle, anchor, compliance));
            ++added;
        }
    }
    return added;
}

} // namespace gfx::physics
