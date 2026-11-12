// The velocity updates of an XPBD substep, for xpbd_velocity_update.comp (bodies), particle_velocity_update.comp
// (particles) and velocity_update_fused.comp (both in one dispatch: GpuOptimization::KERNEL_FUSION). Included after
// step.glsl and particles.glsl.
#ifndef GPU_XPBD_VELOCITY_UPDATE_GLSL
#define GPU_XPBD_VELOCITY_UPDATE_GLSL

// A body (Müller et al. 2020, Algorithm 2): each awake dynamic body's velocities become what its pose did over the substep
// (what the constraint projections between the passes moved included), from the displacement and rotation vector the
// passes accumulated (xpbd_integrate.comp says why not from two poses); kinematic bodies keep the velocities they were
// given.
void update_body_velocity(uint slot)
{
    if (slot >= PARAMETERS.body_count)
    {
        return;
    }
    const uint flags = gpu_words[PARAMETERS.body_flags].words[slot];
    if (!body_moves(flags) || (flags & BODY_DYNAMIC) == 0U)
    {
        return;
    }
    const float h = PARAMETERS.gravity.w;
    gpu_vectors[PARAMETERS.body_linear_velocities].values[slot].xyz = gpu_vectors[PARAMETERS.body_displacements].values[slot].xyz / h;
    gpu_vectors[PARAMETERS.body_angular_velocities].values[slot].xyz = gpu_vectors[PARAMETERS.body_rotations].values[slot].xyz / h;
}

// A particle: its velocity becomes what its position did over the substep (the projections' corrections included), a
// dynamic one's damped by its soft body's damping, (1 − d)^h for d lost per second (as the abstraction's soft-body solver
// damps).
void update_particle_velocity(uint particle)
{
    if (particle >= PARAMETERS.particle_count)
    {
        return;
    }
    const uvec4 header = particle_header(particle);
    if (!body_moves(header.y))
    {
        return;
    }
    const float h = PARAMETERS.gravity.w;
    float factor = 1.0;
    if ((header.y & BODY_DYNAMIC) != 0U)
    {
        const float damping = gpu_vectors[PARAMETERS.soft_bodies].values[SOFT_BODY_VECTORS * header.z].y;
        factor = damping >= 1.0 ? 0.0 : pow(1.0 - damping, h);
    }
    const vec3 displacement = gpu_vectors[PARAMETERS.particle_displacements].values[particle].xyz;
    gpu_vectors[PARAMETERS.particle_velocities].values[particle].xyz = displacement * (factor / h);
}

#endif
