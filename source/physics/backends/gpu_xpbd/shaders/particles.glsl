// The soft bodies' particles as the passes read them (GpuSoftBodies in gpu_soft_bodies.cppm): what a particle keeps
// between edits, its soft body's settings. Included after the pass's push constants (PARAMETERS names them).
#ifndef GPU_XPBD_PARTICLES_GLSL
#define GPU_XPBD_PARTICLES_GLSL

// A particle (GpuParticle): its inverse mass (bits), flags (GpuBodyFlag's: dynamic, or kinematic when pinned rigidly to
// a point it follows), soft body, and the body its kinematic pin follows (NO_BODY: the world's point); its starting
// position (self-collision ignores pairs that start within two radii); its kinematic pin's point (in that body's frame
// about its center of mass, or in the world).
const uint PARTICLE_VECTORS = 3U;

uvec4 particle_header(uint particle)
{
    return gpu_word_vectors[PARAMETERS.particles].values[PARTICLE_VECTORS * particle];
}

float particle_inverse_mass(uint particle)
{
    return uintBitsToFloat(particle_header(particle).x);
}

// A soft body (GpuSoftBody): gravity scale, damping per second, particle radius, friction; iterations per substep,
// flags (bit 0: self-collision), first particle, particle count.
const uint SOFT_BODY_VECTORS = 2U;

#endif
