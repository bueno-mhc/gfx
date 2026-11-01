// GpuSoftBodies: the particles' arrays (state uploaded whole when the soft bodies change, kept by the GPU between steps,
// backed up for replays), the soft bodies' settings and their constraints by color, and the passes of a substep.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.gpu_xpbd.shaders;
import :resources;
import :soft_bodies;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The push constants of soft_solve.comp.
struct SoftPush
{
    std::uint32_t parameters = 0;  ///< The step parameters' bindless index.
    std::uint32_t constraints = 0; ///< The soft constraints'.
    std::uint32_t multipliers = 0; ///< Their multipliers' (a float each).
    std::uint32_t first = 0;       ///< The color's first constraint.
    std::uint32_t count = 0;       ///< Its constraints.
    std::uint32_t iteration = 0;   ///< The substep's iteration (0: the multipliers reset).
};

} // namespace

struct GpuSoftBodies::State
{
    explicit State(Device& target)
        : device{target}
        , integrate{make_pipeline(target, shaders::particle_integrate_comp, modules)}
        , velocity_update{make_pipeline(target, shaders::particle_velocity_update_comp, modules)}
        , solve{make_pipeline(target, shaders::soft_solve_comp, modules)}
    {
    }

    Device& device;
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::unique_ptr<ComputePipeline> integrate{};
    std::unique_ptr<ComputePipeline> velocity_update{};
    std::unique_ptr<ComputePipeline> solve{};

    // CPU copies uploaded when they change.
    std::vector<GpuVector> state_positions{};
    std::vector<GpuVector> state_velocities{};
    std::vector<GpuParticle> particle_data{};
    std::vector<GpuSoftBody> soft_body_data{};
    std::vector<GpuSoftConstraint> constraint_data{};
    std::vector<std::uint32_t> color_starts{};
    std::uint32_t iterations = 0;
    std::uint32_t particle_count = 0;
    bool state_pending = false;
    bool particles_pending = false;
    bool constraints_pending = false;

    // Buffers.
    HostBuffer upload{"GPU physics: soft-body upload", MemoryLocation::HOST_UPLOAD, BufferUsage::TRANSFER_SOURCE};
    GpuArray positions{"GPU physics: particle positions", sizeof(GpuVector)};
    GpuArray velocities{"GPU physics: particle velocities", sizeof(GpuVector)};
    GpuArray displacements{"GPU physics: particle displacements", sizeof(GpuVector)};
    GpuArray previous_velocities{"GPU physics: particle velocities at the substep's start", sizeof(GpuVector)};
    GpuArray particles{"GPU physics: particles", sizeof(GpuParticle)};
    GpuArray soft_bodies{"GPU physics: soft bodies", sizeof(GpuSoftBody)};
    GpuArray constraints{"GPU physics: soft constraints", sizeof(GpuSoftConstraint)};
    GpuArray multipliers{"GPU physics: soft constraint multipliers", sizeof(float)};
    GpuArray position_backup{"GPU physics: particle position backup", sizeof(GpuVector)};
    GpuArray velocity_backup{"GPU physics: particle velocity backup", sizeof(GpuVector)};
};

GpuSoftBodies::GpuSoftBodies(Device& device)
    : m_state{std::make_unique<State>(device)}
{
}

GpuSoftBodies::~GpuSoftBodies() = default;

void
GpuSoftBodies::set_state(std::span<const GpuVector> positions,
                         std::span<const GpuVector> velocities)
{
    State& state = *m_state;
    state.state_positions.assign(positions.begin(), positions.end());
    state.state_velocities.assign(velocities.begin(), velocities.end());
    state.state_pending = true;
}

void
GpuSoftBodies::set_particles(std::span<const GpuParticle> particles,
                             std::span<const GpuSoftBody> soft_bodies)
{
    State& state = *m_state;
    state.particle_data.assign(particles.begin(), particles.end());
    state.soft_body_data.assign(soft_bodies.begin(), soft_bodies.end());
    state.particle_count = static_cast<std::uint32_t>(particles.size());
    state.particles_pending = true;
}

void
GpuSoftBodies::set_constraints(std::span<const GpuSoftConstraint> constraints,
                               std::span<const std::uint32_t> color_starts)
{
    State& state = *m_state;
    state.constraint_data.assign(constraints.begin(), constraints.end());
    state.color_starts.assign(color_starts.begin(), color_starts.end());
    state.constraints_pending = true;
}

void
GpuSoftBodies::set_iterations(std::uint32_t iterations) noexcept
{
    m_state->iterations = iterations;
}

auto
GpuSoftBodies::particle_count() const noexcept -> std::uint32_t
{
    return m_state->particle_count;
}

auto
GpuSoftBodies::iterations() const noexcept -> std::uint32_t
{
    return m_state->constraint_data.empty() ? 0 : m_state->iterations;
}

void
GpuSoftBodies::record_uploads(ComputeRecorder& recorder,
                              std::vector<std::unique_ptr<Buffer>>& retired)
{
    // The arrays for the particles, soft bodies and constraints (their contents are uploaded whole when they change: a
    // grown array needs nothing kept).
    State& state = *m_state;
    Device& device = state.device;
    const auto grow = [&](GpuArray& array, std::size_t count) { (void)array.reserve(device, recorder, std::max(static_cast<std::uint32_t>(count), 1U), 0, retired); };
    for (GpuArray* array : {&state.positions, &state.velocities, &state.displacements, &state.previous_velocities, &state.particles, &state.position_backup,
                            &state.velocity_backup})
    {
        grow(*array, state.particle_count);
    }
    grow(state.soft_bodies, state.soft_body_data.size());
    grow(state.constraints, state.constraint_data.size());
    grow(state.multipliers, state.constraint_data.size());
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_TRANSFER, 1}});

    // What changed, through one staging buffer.
    struct Piece
    {
        std::span<const std::byte> bytes;
        const GpuArray* target;
    };
    std::vector<Piece> pieces;
    if (state.state_pending)
    {
        pieces.push_back({std::as_bytes(std::span{state.state_positions}), &state.positions});
        pieces.push_back({std::as_bytes(std::span{state.state_velocities}), &state.velocities});
        state.state_pending = false;
    }
    if (state.particles_pending)
    {
        pieces.push_back({std::as_bytes(std::span{state.particle_data}), &state.particles});
        pieces.push_back({std::as_bytes(std::span{state.soft_body_data}), &state.soft_bodies});
        state.particles_pending = false;
    }
    if (state.constraints_pending)
    {
        pieces.push_back({std::as_bytes(std::span{state.constraint_data}), &state.constraints});
        state.constraints_pending = false;
    }
    std::uint64_t total = 0;
    for (const Piece& piece : pieces)
    {
        total += piece.bytes.size();
    }
    if (total == 0)
    {
        return;
    }
    state.upload.reserve(device, total, retired);
    const std::span<std::byte> mapped = state.upload.bytes();
    std::uint64_t offset = 0;
    for (const Piece& piece : pieces)
    {
        if (piece.bytes.empty())
        {
            continue;
        }
        std::ranges::copy(piece.bytes, mapped.begin() + static_cast<std::ptrdiff_t>(offset));
        const std::array regions{BufferCopyRegion{.source_offset = offset, .size = piece.bytes.size()}};
        recorder.copy_buffer(state.upload.buffer().handle(), piece.target->buffer().handle(), regions);
        offset += piece.bytes.size();
    }
    state.upload.buffer().flush_mapped_range();
}

void
GpuSoftBodies::record_backup(ComputeRecorder& recorder,
                             bool restore) const
{
    const State& state = *m_state;
    if (state.particle_count == 0)
    {
        return;
    }
    const std::uint64_t size = std::uint64_t{state.particle_count} * sizeof(GpuVector);
    for (const auto& [live, backup] : {std::pair{&state.positions, &state.position_backup}, std::pair{&state.velocities, &state.velocity_backup}})
    {
        const std::array regions{BufferCopyRegion{.size = size}};
        recorder.copy_buffer(restore ? backup->buffer().handle() : live->buffer().handle(), restore ? live->buffer().handle() : backup->buffer().handle(), regions);
    }
}

void
GpuSoftBodies::record_integrate(ComputeRecorder& recorder,
                                std::uint32_t step_parameters) const
{
    const State& state = *m_state;
    if (state.particle_count > 0)
    {
        record_pass(recorder, *state.integrate, step_parameters, workgroups(state.particle_count));
    }
}

void
GpuSoftBodies::record_iteration(ComputeRecorder& recorder,
                                std::uint32_t step_parameters,
                                std::uint32_t iteration) const
{
    // The colors one after the other (no two constraints of a color move one particle).
    const State& state = *m_state;
    if (iteration >= state.iterations)
    {
        return;
    }
    for (std::size_t color = 0; color + 1 < state.color_starts.size(); ++color)
    {
        const std::uint32_t first = state.color_starts[color];
        const std::uint32_t count = state.color_starts[color + 1] - first;
        if (count == 0)
        {
            continue;
        }
        record_pass(recorder, *state.solve,
                    SoftPush{.parameters = step_parameters,
                             .constraints = state.constraints.index(),
                             .multipliers = state.multipliers.index(),
                             .first = first,
                             .count = count,
                             .iteration = iteration},
                    workgroups(count));
    }
}

void
GpuSoftBodies::record_velocity_update(ComputeRecorder& recorder,
                                      std::uint32_t step_parameters) const
{
    const State& state = *m_state;
    if (state.particle_count > 0)
    {
        record_pass(recorder, *state.velocity_update, step_parameters, workgroups(state.particle_count));
    }
}

auto
GpuSoftBodies::positions() const noexcept -> const GpuArray&
{
    return m_state->positions;
}

auto
GpuSoftBodies::velocities() const noexcept -> const GpuArray&
{
    return m_state->velocities;
}

auto
GpuSoftBodies::displacements() const noexcept -> const GpuArray&
{
    return m_state->displacements;
}

auto
GpuSoftBodies::previous_velocities() const noexcept -> const GpuArray&
{
    return m_state->previous_velocities;
}

auto
GpuSoftBodies::particles() const noexcept -> const GpuArray&
{
    return m_state->particles;
}

auto
GpuSoftBodies::soft_bodies() const noexcept -> const GpuArray&
{
    return m_state->soft_bodies;
}

} // namespace gfx::physics::gpu_xpbd
