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

/// Throws the PhysicsError of a constraint the model cannot be made of.
[[noreturn]] void
reject_model(std::string problem,
             std::string suggestion)
{
    throw PhysicsError(
        {.backend = PhysicsSubsystem::SOFT_BODIES, .operation = "SoftBodyModel::SoftBodyModel", .problem = std::move(problem), .suggestion = std::move(suggestion)});
}

} // namespace

SoftBodyModel::SoftBodyModel(const SoftBodyDescription& description)
{
    // The particles.
    const auto count = static_cast<std::uint32_t>(description.particles.size());
    m_inverse_masses.reserve(count);
    m_rest_positions.reserve(count);
    for (const SoftBodyParticle& particle : description.particles)
    {
        m_inverse_masses.push_back(particle.inverse_mass);
        m_rest_positions.push_back(particle.position);
    }

    // Each constraint checked, its rest value from the starting positions unless given; tethers apart.
    std::vector<Constraint> colored;
    colored.reserve(description.constraints.size());
    for (std::size_t index = 0; index < description.constraints.size(); ++index)
    {
        const SoftBodyConstraint& constraint = description.constraints[index];
        const std::uint32_t used = soft_body_constraint_particles(constraint.kind);
        Constraint prepared{.kind = constraint.kind, .particles = {}, .compliance = constraint.compliance, .rest = 0.0F};
        for (std::uint32_t corner = 0; corner < used; ++corner)
        {
            const std::uint32_t particle = constraint.particles[corner];
            if (particle >= count)
            {
                reject_model(std::format("constraint {} ({}) names particle {} of {}", index, constraint.kind, particle, count), "name particles of the soft body");
            }
            if (std::ranges::find(prepared.particles.begin(), prepared.particles.begin() + corner, particle) != prepared.particles.begin() + corner)
            {
                reject_model(std::format("constraint {} ({}) names particle {} twice", index, constraint.kind, particle), "name different particles");
            }
            prepared.particles[corner] = particle;
        }
        const auto& [a, b, c, d] = prepared.particles;
        switch (constraint.kind)
        {
            case SoftBodyConstraintKind::STRETCH:
            case SoftBodyConstraintKind::SHEAR:
            case SoftBodyConstraintKind::BENDING:
            case SoftBodyConstraintKind::TETHER:
                prepared.rest = constraint.rest_value.value_or(distance(m_rest_positions[a], m_rest_positions[b]));
                break;
            case SoftBodyConstraintKind::VOLUME:
            {
                // Six times the volume, signed as the starting tetrahedron.
                const float six = xpbd::six_volume(m_rest_positions[a], m_rest_positions[b], m_rest_positions[c], m_rest_positions[d]);
                prepared.rest = constraint.rest_value ? 6.0F * *constraint.rest_value * (six < 0.0F ? -1.0F : 1.0F) : six;
                break;
            }
            case SoftBodyConstraintKind::DIHEDRAL:
                prepared.rest = constraint.rest_value.value_or(xpbd::dihedral(m_rest_positions[a], m_rest_positions[b], m_rest_positions[c], m_rest_positions[d]).angle);
                break;
        }
        (constraint.kind == SoftBodyConstraintKind::TETHER ? m_tethers : colored).push_back(prepared);
    }

    // Colors, greedily in order: the lowest one none of a constraint's particles has yet (64 at a time).
    std::vector<std::uint32_t> colors(colored.size(), 0);
    std::vector<std::uint32_t> remaining(colored.size());
    std::ranges::iota(remaining, 0U);
    std::vector<std::uint32_t> next;
    std::vector<std::uint64_t> taken(count, 0);
    std::uint32_t color_count = 0;
    for (std::uint32_t base = 0; !remaining.empty(); base += 64)
    {
        std::ranges::fill(taken, std::uint64_t{0});
        next.clear();
        for (const std::uint32_t index : remaining)
        {
            const Constraint& constraint = colored[index];
            const std::uint32_t used = soft_body_constraint_particles(constraint.kind);
            std::uint64_t mask = 0;
            for (std::uint32_t corner = 0; corner < used; ++corner)
            {
                mask |= taken[constraint.particles[corner]];
            }
            if (mask == ~std::uint64_t{0})
            {
                next.push_back(index);
                continue;
            }
            const auto bit = static_cast<std::uint32_t>(std::countr_one(mask));
            colors[index] = base + bit;
            color_count = std::max(color_count, base + bit + 1);
            for (std::uint32_t corner = 0; corner < used; ++corner)
            {
                taken[constraint.particles[corner]] |= std::uint64_t{1} << bit;
            }
        }
        remaining.swap(next);
    }

    // The constraints color after color, in order within each.
    m_color_starts.assign(color_count + 1, 0);
    for (const std::uint32_t color : colors)
    {
        ++m_color_starts[color + 1];
    }
    std::partial_sum(m_color_starts.begin(), m_color_starts.end(), m_color_starts.begin());
    m_constraints.resize(colored.size());
    std::vector<std::uint32_t> cursor(m_color_starts.begin(), m_color_starts.end() - 1);
    for (std::size_t index = 0; index < colored.size(); ++index)
    {
        m_constraints[cursor[colors[index]]++] = colored[index];
    }
}

} // namespace gfx::physics
