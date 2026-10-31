# gfx physics: a GPU-exclusive XPBD backend on a Karras BVH — architecture, decisions and plan

Status: **answers given 2026-10-31: every question of §D as recommended.** **G0 accepted** with its 5 deviations (§B "G0
as built"; not committed yet); G1 in progress.

The request (2026-10-31): a new physics backend, selectable through the existing factory, that simulates rigid and soft
bodies with **extended position-based dynamics (XPBD)** entirely on the GPU through the graphics library's compute
subset (storage buffers, compute pipelines, dispatches, barriers; no CUDA, OpenCL, HIP, vendor extensions or hardware ray
tracing), with a broad phase on a **binary radix tree BVH built on the GPU** (Karras 2012), GJK/EPA narrow phase on the GPU,
graph-colored (Jones–Plassmann) or Jacobi constraint solving, bit-identical results for identical inputs, capability
probing with `BackendCapabilityError`, pipeline variants for optional GPU features, and the existing physics interface
unchanged.

This document follows the shape of [physics-design.md](physics-design.md): §0 where the request meets the code, §A the
architecture and decisions the request asks for, §B the plan, §C the API as proposed, §D questions.

---

## 0. Where the request meets the existing project

| # | Request | Situation in the code | Recommendation |
|---|---|---|---|
| 1 | Implement `PhysicsDesc`, `add_rigid_body`, `remove_*`, `query_ray/query_shape/query_overlap`, `async_step/sync_step`, `set_deferred_edits`, a `PhysicsBackend` concept, a `PhysicsRegistry` | The interface as built is `PhysicsWorldDescription`; objects created by `create_rigid_body()`, `RigidBody::add_collider()`, `create_joint()`, `create_soft_body()`, `create_trigger()`, `create_constraint()` and **removed by destroying them** (RAII); `step()` = `begin_step()` + `wait_step()`; **edits are always deferred** to the next step; queries `ray_cast`, `ray_cast_all`, `shape_cast`, `overlap` (shape, box), `closest_point`, `contacts_of`; backends compiled in, no registry (decided in PH0: no global) | **Implement the interface as it is**; §A9 maps each requested operation to the real one. No line of `gfx.physics.interface` changes. |
| 2 | Add `PhysicsBackendKind::GpuXpbd` and `IntegratorKind::Xpbd` to `enums.cppm` | `IntegratorKind::XPBD` and `SolverKind::XPBD` **already exist** (PH6); enumerators are `SCREAMING_SNAKE_CASE` | **One enumerator: `PhysicsBackendKind::GPU_XPBD`.** The backend's public settings (§C2) need a few more enumerations and constants, which by the single-file rule also go into `source/physics/enums.cppm` (nothing else in the interface changes). |
| 3 | The backend gets a graphics device | `PhysicsWorldDescription` has no device, and adding one would make the physics interface depend on graphics | **Two entries.** Through the factory, the backend **creates its own** Vulkan instance and device (a compute queue, the most capable GPU). Directly, a public module `gfx.physics.gpu_xpbd` makes a world on **the application's `gfx::Device`** (shared with its renderer, and how tests use devices lacking a feature) with backend settings (`GpuXpbdDescription`). This is the existing "custom backends may also be constructed directly" path. |
| 4 | Import "only the physics abstraction and the graphics abstraction" | The physics targets must not link graphics, which the build checks (`gfx_require_decoupled`, `PhysicsDecoupling.*` scanning imports) | **A separate target `gfx::physics_gpu_xpbd`** (graphics + `gfx::physics_components`), option **`GFX_PHYSICS_BACKEND_GPU_XPBD` (default OFF)**. When it is ON, `gfx::physics` links it privately (for the factory) and its decoupling check allows `gfx` for that target only. `physics_common`, `physics_components` and `camera` stay graphics-free, and the import scan allows graphics only under `backends/gpu_xpbd/`. |
| 5 | Probe subgroup support, 64-bit atomics, maximum storage-buffer size, shared memory, storage images, fp16 | The graphics interface reports queue families, workgroup sizes and counts, subgroup **sizes**, `SHADER_FLOAT16/INTEGER16/INTEGER64`, `TIMESTAMP_QUERY`. It does **not** report subgroup **operations**, 64-bit **atomics**, the storage-buffer **range**, **shared memory** per workgroup, or the **Vulkan memory model** (none is wired in the Vulkan backend) | **Small additions to the graphics library** (not physics): `DeviceFeature::SHADER_INTEGER64_ATOMICS`, `DeviceFeature::VULKAN_MEMORY_MODEL` (required by Vulkan 1.3, enabled automatically), `DeviceLimits::subgroup_operations` (a new `SubgroupOperation` bitmask in graphics `enums.cppm`), `maximum_storage_buffer_range`, `maximum_compute_shared_memory_size`, filled by the Vulkan backend. |
| 6 | Pipeline variants: subgroups or shared memory; 64-bit atomics or emulation; storage images if available; fp16 contact manifolds | Nothing in a physics step is image-shaped; half floats have an 11-bit mantissa (≈ 1 mm resolution at 2 m from the origin) | **Two variant axes**: subgroup arithmetic or shared memory (scans, reductions, radix sort), 64-bit atomics or two 32-bit passes (nearest-hit queries). **No storage images and no fp16 variants**: they would not be used, or would cost precision a contact cannot afford. The capability summary still reports them. |
| 7 | "No readback in the frame loop except queries and events" | The interface's getters (`RigidBody::state()`, `Joint::state()`, `SoftBody` positions), `events()`, `statistics()` and `contacts_of()` must show the last completed step after `wait_step()` | **One readback per step** (states of the bodies that moved, joint states, particles, contacts, events, counts) in a single copy at the end of the step's command buffer. With `asynchronous_stepping` (what a game uses) the world's own thread waits for it; the application thread never blocks before `wait_step()`. Without the readback the getters could not work. |
| 8 | Queries asynchronous, results through a staging readback and a fence | The interface's queries are **synchronous, `noexcept`, allocation-free**, concurrent between steps; `QueryEngine::BACKEND` (the backend's) and `QueryEngine::ABSTRACTION` (the framework's own CPU queries, on every backend) | **`QueryEngine::BACKEND` runs on the GPU**: traversal dispatched from preallocated query slots, the caller waits for its fence, results copied into the caller's span. `ABSTRACTION` stays the framework's CPU path (a feature of the abstraction, not a fallback of this backend). An asynchronous batch API (many rays per submission) can come later as an extension of `gfx.physics.gpu_xpbd` (§D7). |
| 9 | λ "reset per substep" (XPBD model, item 1) and "preserved across substeps, reset each step" (solver, item 4) | The two items contradict each other; Macklin et al. (2016, 2019) and Müller et al. (2020) reset λ at the start of each time step, and with substepping each substep **is** a time step | **λ reset at each substep.** Carrying it into the next substep would add the last substep's force to the compliance term of the next. A contact's cache (by feature ID) keeps its **anchor points**, which static friction needs, instead of λ. A joint's force is λ/h², averaged over the step's substeps (as the reference backend reports it since PH8). |
| 10 | Every joint kind, motors, limits, breakable constraints; user constraints implied by `Constraint` | `JointKind`: DISTANCE (rod, spring, rope, stub), HINGE, BALL_AND_SOCKET, SLIDER, FIXED, CONE_TWIST, SIX_DEGREES_OF_FREEDOM. `UserConstraint` computes its rows in **C++ virtual functions** at every iteration | **Every joint kind on the GPU**, with limits, motors, springs and breaking. **User constraints are refused** (`BackendCapabilityError`, `USER_CONSTRAINTS`, as on Box3D): their rows are C++ code the GPU cannot run. Replacing components is refused (`CUSTOM_*`): the pipeline is the backend's own. |
| 11 | "Pass the abstraction's conformance tests" | **No shared conformance suite exists**: Box3D (12 tests), the reference backend (19) and the framework (16) have their own | **Build one** (§B, G9): the backend-independent behavior of the Box3D, reference and framework tests, parameterized over every simulating backend (and every GPU and variant set for this one), each expectation conditional on the backend's capabilities (a refusal must be a `BackendCapabilityError` naming the capability); the null backend keeps its error-contract tests. |
| 12 | Test devices lacking 64-bit atomics, subgroups, a compute queue | Real Vulkan devices always have compute (a graphics family must support compute); subgroup operations are properties, not features one disables | **No 64-bit atomics**: a device created without `SHADER_INTEGER64_ATOMICS`. **No subgroups**: forced through `GpuXpbdDescription::variants`. **No compute queue**: the graphics test library's fake backend (a device whose queues lack COMPUTE): the capability check runs before any GPU work, so the error is checked without a GPU. |
| 13 | `static_assert` buffer sizes against device limits | Device limits are known at run time only | **Layouts at compile time, limits at creation**: GPU structures are C++ aggregates with `static_assert`ed sizes and offsets; workgroup sizes and capacities are constants in `enums.cppm`, passed to shaders as **specialization constants** (one source of truth, still compile-time constants for the driver); the device's limits are checked once at creation. |
| 14 | `BvhNode` of 64 bytes | Traversal is bandwidth-bound | **32-byte nodes** (two 16-byte rows: minimum + left child, maximum + right child, the leaf bit in the child index); parents in their own array (refit only). Half the bandwidth of 64-byte nodes. |
| 15 | δ(i, j) = 63 − clz(morton[i] XOR morton[j]) | That expression is the index of the highest differing bit; Karras's δ is the **length of the common prefix**, with duplicates told apart by their indices | δ(i, j) = clz(k_i ⊕ k_j) for 30-bit codes in 32-bit keys, and 32 + clz(i ⊕ j) when the codes are equal (Karras 2012, §4); −1 outside the range. |
| 16 | Treelet optimization "from the Karras paper" | Treelet restructuring is Karras & **Aila 2013** ("Fast Parallel Construction of High-Quality Bounding Volume Hierarchies"), not the 2012 paper | Implemented as specified (top levels, treelets of 7 leaves), **optional and measured**: a tree rebuilt every step for pair finding may not repay it; ray queries may. Default decided by the measurements (§D13). |
| 17 | `glm` for CPU set-up | Own constexpr math in core (`gfx.core.math`, PH0 decision), cglm/SIMDe at run time | **No glm**, no new dependency; `THIRD_PARTY_LICENSES.md` unchanged. |
| 18 | The file layout of the request (40 `.cppm` files, `rigid_body.cppm`, `collider.cppm`, …) | The framework (`SteppedWorld`) already provides handles, generic objects, validation, edit recording, snapshots, threading and the reader handshake; Box3D and the reference backend implement `SteppedWorldBackend` | **A `SteppedWorldBackend`** (§A1); the request's areas become partitions of one module (§A15). The per-object files are unnecessary: the framework's objects are the ones every backend shares, which is what makes the contract the same. |
| 19 | A GitHub repository, git commands, CI with Lavapipe/WARP and macOS | Project rule: you commit and push; only Windows/GCC 16 here (no Lavapipe, no WARP, no macOS); CI is step 6 of the project | **This repository**, you commit; no workflow file now. CI with a software Vulkan driver (Lavapipe on Linux) belongs to the project's step 6. |
| 20 | Code for everything in one answer | Project rule: step by step, each step compiled, tested, reported and validated | **Phases G0–G10** (§B), each built and tested in Debug and Release on both GPUs before the next. |

---

## A. Architecture and design decisions

### A1. A pure consumer of two abstractions

The backend is a **`SteppedWorldBackend`**, like Box3D and the reference backend. The framework keeps everything that does not
depend on how bodies move: handles, the generic objects the application holds, the checks of every description, the edit
log, the pending and published snapshots, the world's thread, the reader handshake for queries, the abstraction's own
queries. The backend implements `check()` (refusals at record time), `apply()` (edits into GPU buffers), `simulate()` (the
GPU step and its readback into the pending snapshot), `simulates_soft_bodies()` (true) and the `BACKEND` queries. So its
worlds behave as the other backends' do in everything the interface promises, and the conformance suite (G9) checks it.

It imports `gfx.physics.interface`, `gfx.physics.world` (the framework), the CPU-side set-up of `gfx.physics.shapes`,
`gfx.physics.narrow_phase` (contact feature packing), `gfx.physics.dynamics` (soft-body rest values and the static coloring
of their constraints) and the graphics library's public modules — nothing from platform, UI, camera or the other backends.
Nothing graphics leaves the backend: no graphics handle or object appears in the physics interface, and the direct-construction
module takes a `gfx::Device&` in, never gives one out.

### A2. XPBD for rigid bodies

Following Müller et al. 2020 ("Detailed Rigid Body Simulation with Extended Position Based Dynamics") on the GPU:

- **State** per body (structure of arrays): position, orientation (quaternion), linear and angular velocity, previous
  position and orientation, inverse mass, inverse inertia in the body frame (diagonal after the principal axes, with the
  principal rotation folded into the body's frame) — world-space inverse inertia computed when needed: R I⁻¹ Rᵀ.
  `MotionLock` zeroes the locked axes of the generalized inverse masses.
- **Per step**: collision detection once (positions at the step's start, boxes grown by the step's motion: speculative
  contacts), then **S substeps** of h = Δt/S: integrate (velocity from gravity and forces, damping, predicted pose); **I
  iterations** of position projection over the colored constraints; velocities from the pose change; one **velocity pass**
  (dynamic friction, restitution with the threshold, joint damping).
- **Constraint update** (per constraint, per iteration): `Δλ = (−C − α̃λ) / (Σ wᵢ + α̃)`, α̃ = α/h²; positional constraints
  with generalized inverse masses `w = 1/m + (r × n)ᵀ I⁻¹ (r × n)`, angular ones `w = nᵀ I⁻¹ n`; corrections applied to
  positions and orientations (`q ← normalize(q + ½ [Δθ, 0] q)`).
- **Contacts**: non-penetration (λₙ ≥ 0), **static friction** as a positional constraint on the tangential drift of the
  two anchors while |λₜ| < μs λₙ (anchors kept from the last step by feature ID); **dynamic friction** μd and **restitution**
  in the velocity pass; `SolverDescription::maximum_push_speed` bounds the depenetration per substep; rolling resistance as
  an angular velocity damping proportional to λₙ.
- **Joints**: positional parts (anchors together, along an axis, within a range) and angular parts (axes aligned, angles
  within limits: hinge, swing cone, twist range, six-DOF per axis) as in Müller 2020 §3.3–3.4. **Springs** (`JointSpring`
  frequency and damping ratio) become a compliance α = 1/(m_eff ω²) and the XPBD damping term. **Motors** advance a target
  angle or position by `target_speed · h` each substep, their |λ|/h² clamped to `maximum_force`. Rod, rope (inequality),
  spring and stub are the distance joint's modes.
- **Breaking**: force = λ/h², averaged over the step's substeps; past `JointBreaking::force` or `torque` the joint's
  enabled bit clears on the GPU and a BROKEN event is written (§A10). `JointState::force/torque` are the same averages.

### A3. XPBD for soft bodies

- **Particles** (structure of arrays): position, previous position, velocity, inverse mass (0: fixed in place), owner.
- **Constraints**: STRETCH, SHEAR, BENDING, TETHER (distances, TETHER one-sided), VOLUME (tetrahedra), DIHEDRAL (Bridson
  angle), with the rest values the CPU model computes (`SoftBodyModel`). Their topology never changes, so they are **colored
  once on the CPU at creation** (the model's greedy coloring) and uploaded by color; only the constraints that change every
  step (contacts) are colored on the GPU.
- **Pins**: `ParticlePin` to the world or to a body as attachment constraints (with compliance); a pinned-in-place particle
  of inverse mass 0 costs nothing.
- **Coupling** with rigid bodies: particle–collider contacts are XPBD constraints between a particle and a body, in the
  **same colored set** as rigid contacts, so each correction moves both sides (two-way) without write conflicts — no
  separate correction pass as the CPU coupling needs.
- **Self-collision** (`self_collision`): particle–particle contacts from the particle BVH, skipping pairs joined by a
  constraint.
- **Sub-steps**: the world's S substeps advance everything together; a soft body asking for more
  (`SoftBodyDescription::sub_steps`) gets ⌈its sub-steps / S⌉ iterations per substep (§D9).
- Cloth, rope and volumes are the existing presets (`cloth_preset`, `rope_preset`, `tetrahedral_box`, …): nothing new.

### A4. How the interface's `Integrator` and `Solver` are satisfied

`PhysicsWorld::integrator()` returns a description with `kind() == IntegratorKind::XPBD`, `is_external() == false`;
`solver()` reports `SolverKind::XPBD`; `broad_phase()` reports `BroadPhaseKind::BACKEND` and answers `structure_nodes()` by
reading the BVH back (between steps, for debug views); `narrow_phase()` reports `NarrowPhaseKind::BACKEND`. Replacing any of
them throws `BackendCapabilityError` (`CUSTOM_*`). `soft_body_integrator()` throws, as the interface says for backends that
simulate soft bodies themselves. Substeps and iterations come from `SolverDescription` and are respected exactly (§A9).

### A5. The GPU compute model

- **Everything on compute**: bounds, BVH, pairs, narrow phase, coloring, islands, solve, integration, sleeping, events. The
  CPU records commands, applies edits to its upload buffers and parses the readback. There is no CPU path of any of these.
- **Portable feature set** (required): Vulkan 1.3 through the graphics library (storage buffers, 32-bit atomics, the
  Vulkan memory model, timeline semaphores — all required by 1.3 or by the graphics library already), workgroups of
  `GPU_XPBD_WORKGROUP_SIZE` = 64, 16 KiB of shared memory, a queue with COMPUTE. Optional: subgroup arithmetic, 64-bit
  atomics (variants, §0 row 6); timestamps (profiling).
- **Capability probing** at creation: every required capability checked, **all** missing ones reported in one
  `BackendCapabilityError` (required vs reported values); a `GpuXpbdCapabilities` summary (device, queue family, limits,
  variants chosen, optional features) available from `gfx.physics.gpu_xpbd` and printed by the sample.
- **Pipeline variants** by specialization constants (one SPIR-V module per pass; the variant is a constant the driver
  folds), chosen at creation, overridable by `GpuXpbdDescription::variants` (tests, diagnostics).
- **Buffers**: structure of arrays, one storage buffer per array (bodies, particles, colliders, joints by kind, contacts,
  nodes, keys, scratch), suballocated from a few large allocations (`GpuBufferPool`), sized from the counts the CPU knows
  (bodies, colliders, joints, particles) and from last step's counts with headroom for what only the GPU knows (pairs,
  contacts). Overflow is detected on the GPU and the step is **replayed** from its saved start with larger buffers (§D15):
  results are never truncated.
- **Barriers declared once**: the step is a fixed list of passes, each with the buffers it reads and writes; the list
  generates the barriers (compute→compute, transfer→compute for uploads, compute→indirect for indirect dispatch arguments,
  compute→transfer before the readback). §A10 shows it.
- **Indirect dispatches** wherever a count is known only on the GPU (pairs, contacts, colors, islands), so one command
  buffer per step needs no CPU round trip.
- **Bindless**: the graphics library's bindless storage-buffer table and push constants address every buffer (as the UI
  renderer and the sandbox do), so no descriptor set changes between passes.

### A6. The Karras BVH

- **Two trees of colliders**: a **static tree** (static bodies' colliders: floors, terrain, level geometry) rebuilt only when
  a static collider is added, removed or moved, and a **moving tree** (kinematic and dynamic) **rebuilt every step**.
  Pairs = moving × moving + moving × static. Box2D and Box3D split their trees this way for the same reason: most of a
  scene does not move. A **particle tree** (soft bodies) is **refit** every step and **rebuilt** every
  `GpuXpbdDescription::particle_rebuild_period` steps or when particles are added or removed.
- **Morton codes**: centers of the boxes normalized to the tree's bounds (a min/max reduction: exact, so deterministic),
  10 bits per axis interleaved into 30 bits.
- **Radix sort** of (code, index): stable LSD, 8 bits per pass, 4 passes, workgroup histograms → global scan → stable
  scatter (ranks from a workgroup scan, not atomics). Below 4096 keys one workgroup sorts everything in shared memory.
- **Hierarchy**: one invocation per internal node: direction, range by exponential then binary search on δ, split by binary
  search (Karras 2012, Figure 4); leaves are the sorted primitives.
- **Bounds**: bottom-up, Karras's way: from each leaf up, an atomic counter per node; the second invocation to arrive
  computes the union and climbs (device-scope acquire/release, Vulkan memory model). Unions of boxes are exact (min/max),
  so the result does not depend on who arrives second.
- **Treelets** (optional, Karras & Aila 2013): the n top levels restructured by dynamic programming over treelets of 7 leaves.
- **Four-wide** (optional): a collapse pass to nodes of 4 children for traversal; the binary tree stays the reference.
- **Refit**: the same bottom-up pass over an existing topology.
- **Traversal** for pairs, queries and triggers: iterative, an explicit stack of 32-bit node indices in local memory
  (depth bounded by 30 + log₂ n plus duplicates; overflow detected and reported as an overflow, never silent).
  **Pairs are written deterministically**: count per leaf → scan → write into each leaf's own range (no atomic append).
  Query results are sorted by collider handle before they reach the caller.

### A7. The narrow phase on the GPU

- **Shapes**: support functions of every convex kind (sphere, capsule, cylinder, cone, box and hull with their rounding
  radius); hulls with their faces for clipping; triangle meshes and height fields with their own static BVH (built on the
  GPU with the same builder when the shape is first uploaded; shapes shared by colliders are uploaded once); compounds
  flattened into child colliders that report the parent's handle and the child's sub-shape index.
- **Per candidate pair**: analytic fast paths (sphere/capsule pairs) as the CPU narrow phase has them, else **GJK** on the
  cores (fixed iteration bound, the CPU's sliver test), **EPA** with fixed storage when the cores overlap, then the
  **manifold**: clipping of the supporting faces (up to 4 points) as `contact_manifold()` does on the CPU.
- **Meshes and height fields**: the convex shape's box in the mesh's frame traverses the mesh's BVH → candidate triangles →
  per-triangle GJK/EPA with **active-edge** correction (the CPU's seam fix). Mesh–mesh: triangle pairs from the two trees
  (SAT triangle–triangle).
- **Feature IDs** packed exactly as the CPU's `ContactFeature` (shape feature indices of both sides, the sub-shape), so a
  contact is matched to last step's by equality.
- **Cache**: last step's contacts sorted by (pair key, feature); each new point finds its predecessor by binary search and
  keeps its anchors (static friction) and its touching age (events).
- **Tests**: GPU manifolds against the CPU's `collide()` for thousands of random poses of every shape pair (same normal,
  depths within tolerance, same feature IDs).

### A8. The constraint solver

- **Graph**: nodes are dynamic bodies and particles (static and kinematic ones never conflict: they are not written).
  Edges per constraint (contact, joint, particle contact, pin). Built each step as a body → constraints adjacency in CSR
  form (count, scan, fill by sorting (body, constraint) keys: deterministic).
- **Jones–Plassmann coloring** each step: priorities from a hash of the constraint's stable key (a pair of collider
  handles and a feature, a joint handle), so they do not depend on the order of the arrays; each round, an uncolored
  constraint whose priority beats every uncolored neighbor's takes the smallest color its neighbors do not have. Rounds
  are recorded in advance as indirect dispatches (empty once everything is colored).
- **Colors** up to `MAXIMUM_GPU_XPBD_COLORS`; constraints left without a color (rare, very crowded bodies) go to an
  **overflow set solved Jacobi-style** after the colors.
- **Jacobi mode** (`GpuXpbdDescription::solver_mode`): every constraint computes its correction from the same positions
  into its own slot, then each body sums its slots **in its CSR order** (a gather, no atomic float addition) with the usual
  relaxation factor. Deterministic by construction.
- **Order**: soft-body constraints by their static colors, then the dynamic colors (joints, contacts, particle contacts).
- **Islands and sleep**: connected components of the touching and joined dynamic bodies by label propagation (each label
  the smallest body index of its island: deterministic); a body sleeps when every body of its island stayed under the
  thresholds for `SleepDescription::time`; sleeping bodies are excluded through an active list; a contact or joint with
  an awake body wakes the island.

### A9. The interface, operation by operation

| Interface | On the GPU backend |
|---|---|
| `create_physics_world(description, PhysicsBackendKind::GPU_XPBD)` | own instance and device (§0 row 3); `gpu_xpbd::create_physics_world(description, device, settings)` for the application's device |
| `create_rigid_body`, `add_collider`, `create_joint`, `create_soft_body`, `create_trigger`, setters, forces, impulses, destruction | recorded by the framework; at the next step `apply()` writes them to upload buffers: small scattered writes by a `patch_buffers` pass, bulk data (meshes, soft bodies) by copies |
| `create_constraint` (user constraints) | `BackendCapabilityError` (`USER_CONSTRAINTS`) when recorded |
| `set_solver_description` | `sub_steps` = S; `position_iterations` = I per substep; `velocity_iterations` = iterations of the velocity pass; `warm_starting` = reuse of contact anchors; `restitution_threshold`, `static_friction_speed`, `maximum_push_speed` as named; `contact_hertz`/`contact_damping_ratio` → contact compliance and damping |
| `set_sleep_description` | thresholds and time of §A8 |
| `begin_step` / `wait_step` / `step` | the framework's threading: `simulate()` records one command buffer, submits it on the compute queue, waits for its timeline value on the stepping thread, parses the readback |
| `events()`, `statistics()` (phase times from GPU timestamps when available), getters | from the readback, through the framework's snapshot |
| `ray_cast`, `ray_cast_all`, `shape_cast`, `overlap` (shape, box), `closest_point` with `QueryEngine::BACKEND` | GPU traversal + narrow phase from a query slot, synchronous (§0 row 8) |
| `contacts_of` | the framework's, from the contacts read back |
| `broad_phase().structure_nodes()` | the BVH read back between steps |

### A10. One step, its passes and its barriers

Declared once as a table in the backend (the pass list generates the barriers; nothing is scattered):

| # | Pass (shader) | Reads | Writes | Then |
|---|---|---|---|---|
| 0 | uploads, `patch_buffers` | upload buffer | bodies, colliders, joints, particles | transfer/compute → compute |
| 1 | `bounds` | bodies, colliders, particles | boxes, tree bounds | → compute |
| 2 | `morton`, `radix_*` ×4, `bvh_hierarchy`, `bvh_bounds` (+ `bvh_treelet`, `bvh_wide`) for the moving tree; static tree when changed; `bvh_refit` or rebuild for particles | boxes | nodes | → compute |
| 3 | `pairs_count`, scan, `pairs_write` (colliders, particles × colliders, self, triggers) | nodes, boxes | pairs, indirect arguments | → compute + indirect |
| 4 | `narrow_phase` (`gjk`, `epa`, `manifold` stages), `contact_cache` | pairs, shapes, last contacts | contacts | → compute |
| 5 | `graph_build`, `color` rounds, `islands`, `wake` | contacts, joints | adjacency, colors, islands | → compute + indirect |
| 6 | × S substeps: `xpbd_integrate`; × I: per color `xpbd_solve_rigid` / `xpbd_solve_soft`, overflow Jacobi; `xpbd_velocity_update`; velocity pass | everything above | bodies, particles, λ | → compute after each |
| 7 | `joint_forces` (breaking), `sleep_check`, `events` | λ, contacts, last step's lists | events, flags, sleep timers | → compute |
| 8 | `compact_results`, copy to readback | bodies, particles, joints, contacts, events, counts | readback buffer | compute → transfer, signal timeline |

About 40 dispatches outside the substeps, S × (I × colors + 3) inside: with S = 4, I = 3 and ~10 colors, ~170 dispatches per
step, recorded once per step.

### A11. Determinism

**Guarantee**: identical edits, descriptions and step durations give **bit-identical** states, events and contacts on the
same device, driver and variant set. How: no floating-point atomics anywhere; every parallel output goes to a slot computed
by a scan (count → scan → write) or a stable sort, never to an atomic append; colors from priorities that hash stable keys;
Jacobi gathers in a fixed order; bounds unions exact; queries sorted. Variants change the order of integer scans only, so
they are expected to give the same bits too — tested and reported, not promised (§D10). Across different GPUs or drivers
the results differ (transcendental functions and fused operations are the driver's); that is the determinism boundary.
Tests: every scenario run twice and compared byte for byte, on both GPUs and every variant set.

### A12. Threading and synchronization

- **Recording** on the stepping thread only (the world's own thread with `asynchronous_stepping`, the caller's otherwise):
  one command pool per world, reused each step.
- **Completion**: a timeline semaphore per world (value = step number), waited on the stepping thread; no device or queue
  idle wait anywhere in a step.
- **Queries** between steps from any thread: `GPU_XPBD_QUERY_SLOTS` preallocated slots (command buffer, small buffers),
  a slot taken with an atomic, released after its fence; when all are busy the query waits for one.
- **Several worlds**: each owns its buffers and command pools; through the factory each also owns its device; through the
  direct entry they share the application's device and queue (submissions to one queue are serialized by the graphics
  library). Determinism is per world and does not depend on the queue (§A11).
- **Shared hardware queues** (Intel UHD here: one queue for every type): a step never waits on another queue's signal, so
  the graphics library's shared-queue rule cannot deadlock it.

### A13. Errors

- **At creation**: `BackendCapabilityError` from `[gfx::physics::GPU_XPBD]`, capability `WORLD_OBJECTS`, the reason listing
  every missing capability as "*name*: required *x*, reported *y*" (e.g. "a queue with COMPUTE capability: required 1,
  reported 0", "maximum_compute_workgroup_invocations: required 64, reported 32"), the suggestion naming another backend.
- **When recorded**: refusals (user constraints; anything a later phase does not support yet) by `check()`, as Box3D does.
- **In a step**: hot paths do not throw; overflow is replayed (§A5). A graphics failure (device lost, out of memory when a
  buffer grows) becomes a `PhysicsError` naming the world, the step, the pass or buffer, with the graphics error nested
  (`std::throw_with_nested`: `DeviceLostError` stays reachable), rethrown by `wait_step()` as the interface says.
- **Queries**: never throw; a device lost during a query is `QueryFailureKind::UNSUPPORTED` with the reason in its detail.

### A14. RAII and deferred destruction

Every GPU object is owned by a RAII member (`std::unique_ptr` of the graphics interfaces); replaced buffers (growth) go to
the device's `Deleter`, keyed on the queue's progress, so the GPU never sees a freed buffer. The backend's destructor waits
for its last timeline value, then releases (SteppedWorld already waits for a running step first). With the direct entry the
`gfx::Device` must outlive the world (documented on the function).

### A15. Modules and layout

```
source/physics/backends/gpu_xpbd/            target gfx_physics_gpu_xpbd (gfx::physics_gpu_xpbd)
  gpu_xpbd.cppm                gfx.physics.gpu_xpbd: GpuXpbdDescription, GpuXpbdCapabilities, capabilities(device),
                               create_physics_world(description, device, settings)   [public, direct entry]
  gpu_xpbd_backend.cppm        gfx.physics.backends.gpu_xpbd: unavailable_reason(), create_physics_world(description)
                               [imported by the factory only]
  internal partitions          :capabilities, :pipelines, :memory (buffer pool, upload, readback), :passes (the step's
                               pass table and barriers), :broad_phase, :narrow_phase, :solver, :world (GpuXpbdBackend)
  *.cpp                        one implementation unit per area
  shaders/                     morton, radix_histogram, radix_scan, radix_scatter, bvh_hierarchy, bvh_bounds,
                               bvh_treelet, bvh_wide, bvh_refit, bvh_traverse (pairs, queries), pairs_*, gjk.glsl,
                               epa.glsl, manifold.glsl (includes), narrow_phase, contact_cache, graph_build, color,
                               islands, xpbd_integrate, xpbd_solve_rigid, xpbd_solve_soft, xpbd_velocity_update,
                               joint_forces, sleep_check, events, compact_results, patch_buffers, queries — through
                               gfx_add_shaders() (SPIR-V embedded; Release loads only SPIR-V)
```

The factory imports `gfx.physics.backends.gpu_xpbd` under `GFX_WITH_PHYSICS_GPU_XPBD_BACKEND`. Shared GLSL layouts are
checked against the C++ ones by a test (reflected block sizes + a GPU round trip of known values).

### A16. Naming

`PhysicsBackendKind::GPU_XPBD`; `GpuXpbdBackend`, `GpuXpbdDescription`, `GpuXpbdCapabilities`, `GpuXpbdVariants`;
functions snake_case (`create_physics_world`, `capabilities`); members `m_`; enumerations and constants
(`GPU_XPBD_WORKGROUP_SIZE`, `MAXIMUM_GPU_XPBD_COLORS`, `GPU_XPBD_QUERY_SLOTS`, `GpuSolverMode`, `GpuBvhLayout`,
`GpuVariantChoice`) in `source/physics/enums.cppm`; `Gpu`, `Xpbd`, `Bvh` are acronyms (allowed).

---

## B. Plan

Each phase is built, tested in Debug and Release on both GPUs (NVIDIA and Intel), reported and validated before the next.

| Phase | Content | Checked by |
|---|---|---|
| **G0 Foundations** | graphics additions (§0 row 5) in the Vulkan backend; target, option, `GPU_XPBD`, factory glue, direct entry; capability probe, summary and errors; buffer pool, uploads, readback, timeline; a world that applies edits and integrates bodies under gravity on the GPU (no collisions yet) | capability errors (fake device without COMPUTE, devices without features), falling bodies against the analytic solution, bit-identical twice, validation layers silent |
| **G1 GPU primitives** | scans, reductions, stable radix sort (key–value), compaction; both variants | against `std::stable_sort` and serial scans on random inputs, both GPUs, both variants, bit-identical |
| **G2 Karras BVH** | Morton codes, hierarchy, bounds, static/moving/particle trees, refit, treelets, four-wide, traversal, pairs, `structure_nodes()` | tree invariants, pairs equal to brute force, treelet/wide measured, determinism |
| **G3 Convex narrow phase** | support functions, GJK, EPA, manifolds, analytic pairs, feature IDs, cache | GPU manifolds against the CPU's `collide()` on random poses of every convex pair |
| **G4 Meshes, height fields, compounds** | shape BVHs, active edges, mesh–convex, mesh–mesh, height fields, compounds | against the CPU narrow phase; the seam test |
| **G5 Rigid XPBD** | contacts with static and dynamic friction, restitution, substeps and iterations, Jones–Plassmann coloring + overflow, Jacobi mode, islands, sleep | stacks and pyramids at rest, friction slopes (static and dynamic), restitution heights, sleeping islands, both solver modes, determinism |
| **G6 Joints** | every joint kind, limits, motors, springs, breaking, joint states and events | the reference backend's joint tests (angles, limits, motor speeds, breaking under a shock) |
| **G7 Soft bodies** | particles, every soft constraint kind, pins, two-way contacts, self-collision, particle tree | the dynamics tests' scenarios (cloth hanging, rope, volume kept, cloth on a body moving it) |
| **G8 Events, triggers, queries** | contact BEGIN/END/HIT, trigger ENTER/STAY/EXIT, sleep events, `contacts_of`, every `BACKEND` query, GPU timestamps per phase | against the reference backend's events and the abstraction's queries |
| **G9 Conformance** | the backend-parameterized conformance suite (§0 row 11) over BOX3D, REFERENCE, GPU_XPBD × GPUs × variants; the four device configurations; GPU_XPBD in the physics example (`--backend gpu_xpbd`) and in the sandbox (backend switch, tour) | the suite on every backend; the example and the sandbox's scripted runs under CTest |
| **G10 Documentation** | README (model, BVH, capability probing, variants, determinism, conformance), architecture.md section with diagrams, this record "as built", launch.json | snippets compiled, diagrams rendered |

### G0 as built (2026-10-31)

**Graphics library.** `DeviceFeature::SHADER_INTEGER64_ATOMICS` (buffer and shared 64-bit atomics; **on request only**, so a
device can lack it: the Vulkan backend turns `shaderBufferInt64Atomics`/`shaderSharedInt64Atomics` off unless required) and
`DeviceFeature::SHADER_MEMORY_MODEL` (the Vulkan memory model at device scope; automatic, it costs nothing);
`SubgroupOperation` bitmask (BASIC … QUAD) and `DeviceLimits::subgroup_operations` (compute stage only),
`maximum_storage_buffer_range`, `maximum_compute_shared_memory_size`; **`Device::enabled(DeviceFeature)`** (pure virtual: the
Vulkan device had it privately; the fake test device answers what its physical device supports). Tests
`VulkanDeviceTest.ComputeLimitsAndFeaturesAreReported`, `VulkanWideAtomicsTest.WorkgroupsExchangeSixtyFourBitKeysWithinADispatch`
(shader `wide_atomics.comp`: 64-bit `atomicMax` from 1001 workgroups, the last one — found by an acquire-release counter at
device scope — reads the result). **Intel's RaptorLake GPU has no 64-bit buffer atomics**: the portable variant runs on real
hardware here, not only when forced.

**Physics enumerations.** `PhysicsBackendKind::GPU_XPBD`; `GpuVariantChoice` {AUTOMATIC, PORTABLE}; `GpuBodyFlag` bitmask
{SIMULATED, DYNAMIC, KINEMATIC, AWAKE}; constants `GPU_XPBD_WORKGROUP_SIZE` 64, `GPU_XPBD_BODY_LOCK_SHIFT` 8,
`GPU_XPBD_SHARED_MEMORY_SIZE` 16384, `GPU_XPBD_STORAGE_BUFFER_RANGE` 2²⁷, `GPU_XPBD_STORAGE_BUFFERS` 64. The workgroup size
and the flag bits reach the shaders as **specialization constants** (ids 0–5; each pipeline gets the ones its shader declares,
read from its reflected layout), so `enums.cppm` stays the one source.

**Build.** Option `GFX_PHYSICS_BACKEND_GPU_XPBD` (OFF; `build.py` and the presets turn it on for this repository's
development builds); target `gfx::physics_gpu_xpbd` (`gfx::physics_components` + `gfx::graphics`, decoupled from platform, UI
and camera); `gfx::physics` links it and its decoupling check allows `gfx` then only. Import scan:
`PhysicsDecoupling.SourcesImportOnlyCoreAndPhysics` excludes `backends/gpu_xpbd` (the script gained `EXCLUDE`), and
`PhysicsDecoupling.GpuBackendImportsOnlyPhysicsAndGraphics` allows the graphics interface modules there (`gfx.enums`,
`gfx.errors`, `gfx.handles`, `gfx.interface`, `gfx.backend_factory`), nothing of platform, UI or camera.

**Modules** (`source/physics/backends/gpu_xpbd/`): `gfx.physics.gpu_xpbd` (public: `capabilities(device, variants)`,
`to_string()`, `unavailable_reason()`, `create_physics_world(description[, settings])` on its own device,
`create_physics_world(description, device[, settings])`) with the exported partition `:settings` (`GpuXpbdVariants`,
`GpuXpbdDescription`, `GpuXpbdCapabilities`); internal partitions `:probing` (missing capabilities of a physical device or a
device, the step queue, the error), `:resources` (the layouts shared with the shaders, `static_assert`ed by size and offset;
`GpuArray` growable device arrays — copy of the kept elements and zeroed tail recorded in the step that grows them, old buffer
kept until the step completes; `HostBuffer`; `GpuPipelines`; the step's barriers as constants), `:backend` (`GpuXpbdBackend`);
units `gpu_xpbd.cpp`, `capabilities.cpp`, `gpu_resources.cpp`, `gpu_xpbd_world.cpp`; shaders `gpu_xpbd.glsl` (typed aliases of
the bindless storage-buffer binding, the step parameters, quaternion helpers), `patch_bodies.comp`, `xpbd_integrate.comp`,
`xpbd_velocity_update.comp`.

**The world.** A `SteppedWorldBackend` (`simulates_soft_bodies()` true). Bodies live in eleven GPU arrays (centers with the
inverse mass, orientations, velocities with the dampings, inverse inertias and inertias in the body frame, forces, torques,
the substep's displacements and rotation vectors, flags); the CPU keeps the values the last step read back, exactly. Edits
mark what changed; at the step's start each changed body becomes one 176-byte patch (record values where set, the GPU's
otherwise, impulses applied, implicit wakes reported as `WOKE_UP`). One command buffer per step on the device's COMPUTE queue
(else its GRAPHICS queue): growth copies, one barrier, the patch pass, then per substep `xpbd_integrate` (gravity × scale,
force, gyroscopic torque, damping `1/(1 + h c)`, locks; the pose moved) and `xpbd_velocity_update`, then copies into a
readback buffer and the forces cleared; submitted with a timeline signal and waited for on the stepping thread. Graphics
failures become a `PhysicsError` naming the stage with the graphics error nested. The factory's worlds make a Vulkan instance
and device each (the selected physical device first, then the others; one compute queue; 64-bit atomics when the device has
them); `unavailable_reason()` probes the physical devices without making a device.

**Deviations (for validation).**
1. **One public module** `gfx.physics.gpu_xpbd` instead of that plus a separate factory glue module: the factory imports it
   directly (only when the option is on, when `gfx::physics` may depend on graphics anyway).
2. **Velocities from the substep's displacement and rotation vector**, not from the difference of two poses. Found by the
   free-fall test: in single precision the difference form gained 0.16 % of speed falling from 10 m (5.0650 m instead of
   5.0762 m after 1 s; the GPUs gave exactly what a float32 CPU simulation of the difference form gives), and far more at 1000 m.
   With the displacement form the error is 1 × 10⁻⁵ m at 10 m and 7 × 10⁻⁵ m at 1000 m, and a free spin keeps its rate (the
   difference form lost (h ω)²/8 per substep). Constraint projections (G5) add their corrections to the displacement and
   rotation arrays as well as to the pose.
3. **What later phases bring is refused now**, never ignored: colliders (`add_collider`: collisions arrive with G2–G5),
   joints (G6), soft bodies (G7), triggers (G8) throw `BackendCapabilityError` naming the phase; user constraints are refused
   for good. Bodies get their mass from `RigidBodyDescription::mass_properties` until then. Queries are the framework's.
4. An explicit `wake_up()` reports no `WOKE_UP` (as on the reference backend: the framework already set the record awake); a
   force, an impulse, a pose or a velocity given to a sleeping body does.
5. The graphics feature is named `SHADER_MEMORY_MODEL` (API-neutral) rather than `VULKAN_MEMORY_MODEL`; `Device::enabled()` is
   an addition to the graphics interface the design did not list.

**Tests** (`gfx_physics_gpu_xpbd_tests`, every real-device test on both GPUs with the validation layers): capabilities and
variants (`PORTABLE` forces both portable variants; the summary names them); the pipeline is the backend's own (replacements
refused); free fall against substepped Euler (1 × 10⁻⁴ m); a free spin about an offset center of mass (center fixed to
1 × 10⁻⁴ m, rate to 1 × 10⁻⁵); impulses, forces, damping, locks, gravity scale, kinematic and static bodies, a sleeping body
woken by a push; the same edits (360 bodies made, pushed and destroyed over 90 steps) giving the same bits twice and with the
portable variants; refusals naming their phase; a device with a GRAPHICS queue only; the fake device (no queue, no limits, no
bindless table) refused with each missing capability named; the factory's own devices (two worlds at once, one stepping
asynchronously). `world_test.cpp`'s backend list includes `GPU_XPBD` when available.

---

## C. The API as proposed (for validation, not final code)

### C1. `source/physics/enums.cppm` (additions)

```cpp
enum class PhysicsBackendKind : std::uint8_t
{
    BOX3D,
    REFERENCE,
    NULL_BACKEND,
    CUSTOM,
    GPU_XPBD, ///< Extended position-based dynamics on the GPU (graphics library compute), Karras BVH broad phase.
};

enum class GpuSolverMode : std::uint8_t
{
    GRAPH_COLORING, ///< Gauss–Seidel by colors (Jones–Plassmann each step), overflow solved Jacobi-style.
    JACOBI,         ///< Every constraint from the same positions, gathered per body in a fixed order.
};

enum class GpuBvhLayout : std::uint8_t
{
    BINARY,    ///< Karras's binary radix tree.
    FOUR_WIDE, ///< The binary tree collapsed into nodes of four children for traversal.
};

enum class GpuVariantChoice : std::uint8_t
{
    AUTOMATIC, ///< The faster variant the device supports.
    PORTABLE,  ///< The variant every device runs (shared memory, two 32-bit passes).
};

inline constexpr std::uint32_t GPU_XPBD_WORKGROUP_SIZE = 64;   ///< Invocations per workgroup of every pass.
inline constexpr std::uint32_t MAXIMUM_GPU_XPBD_COLORS = 32;   ///< Colors of the dynamic constraints per step.
inline constexpr std::uint32_t GPU_XPBD_QUERY_SLOTS = 4;       ///< Queries in flight at once.
```

### C2. `gfx.physics.gpu_xpbd` (direct entry)

```cpp
struct GpuXpbdVariants
{
    GpuVariantChoice subgroups = GpuVariantChoice::AUTOMATIC;        ///< Scans and sorts with subgroup arithmetic.
    GpuVariantChoice integer64_atomics = GpuVariantChoice::AUTOMATIC; ///< Nearest-hit queries in one pass.
};

struct GpuXpbdDescription
{
    GpuSolverMode solver_mode = GpuSolverMode::GRAPH_COLORING;
    GpuBvhLayout traversal_layout = GpuBvhLayout::BINARY;
    std::uint32_t treelet_levels = 0;           ///< Top levels restructured (0: none; default set by G2's measurements).
    std::uint32_t particle_rebuild_period = 16; ///< Steps between rebuilds of the particle tree (refits between).
    GpuXpbdVariants variants{};
};

struct GpuXpbdCapabilities { /* device name, queue family, limits used, variants chosen, optional features */ };

/// The capabilities of @p device as the backend sees them; throws BackendCapabilityError listing every missing one.
[[nodiscard]] auto capabilities(const gfx::Device& device) -> GpuXpbdCapabilities;

/// A world simulated on @p device (which must outlive it).
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description,
                                        gfx::Device& device,
                                        const GpuXpbdDescription& settings = {}) -> std::unique_ptr<PhysicsWorld>;
```

### C3. The requested usage example, on the real API (application code: no graphics)

```cpp
import std;
import gfx.physics;

using namespace gfx::physics;

auto main() -> int
{
    // A world simulated on the GPU, through the factory (the backend makes its own device).
    const std::unique_ptr<PhysicsWorld> world = create_physics_world({.asynchronous_stepping = true}, PhysicsBackendKind::GPU_XPBD);

    // A static floor, a crate (compile-time hull) on a hinge that breaks above 800 N, a cloth pinned at two corners, a trigger.
    const auto floor = world->create_rigid_body({.kind = BodyKind::STATIC});
    const auto floor_collider = floor->add_collider({.shape = make_shape(BoxShape{.half_extents = {.x = 20, .y = 0.5F, .z = 20}}),
                                                     .pose = {.position = {.y = -0.5F}}});
    const auto crate = world->create_rigid_body({.pose = {.position = {.y = 4}}});
    const auto crate_collider = crate->add_collider({.shape = make_shape(HullShape(CRATE_HULL)), .density = 200}); // as in examples/physics
    const auto hinge = world->create_joint(hinge_joint({.first = crate->handle(), .anchor = {.x = 0.5F, .y = 4.5F}, .axis = {.z = 1},
                                                        .breaking = {.force = 800}}));
    SoftBodyDescription cloth = cloth_preset({.columns = 24, .rows = 24});
    cloth.pins = {pin_to_world(cloth, cloth_particle(24, 0, 0)), pin_to_world(cloth, cloth_particle(24, 23, 0))};
    const auto sheet = world->create_soft_body(cloth);
    const auto zone = world->create_trigger({.shape = make_shape(SphereShape{.radius = 1}), .pose = {.position = {.y = 1}}});

    for (int frame = 0; frame < 240; ++frame)
    {
        world->begin_step(1.0F / 60.0F); // records and submits on the world's thread; returns at once
        // ... the application's frame ...
        world->wait_step();              // results published

        for (const JointEvent& event : world->events().joints)
        {
            std::println("hinge broke at {:.0f} N", event.force);
        }
        const QueryResult<RayHit> hit = world->ray_cast({.origin = {.y = 10}, .direction = {.y = -1}}, 20); // GPU traversal
        if (hit)
        {
            std::println("ray hit at {:.2f} m", hit->distance);
        }
    }
} // objects, then the world: the backend waits for its last submission and releases its buffers through the deleter
```

---

## D. Questions

Each has a recommendation; "as recommended" answers them all.

**Answers (2026-10-31)**: "as recommended" — every question below as recommended.

1. **Interface**: implement the interface as built (§0 row 1, §A9) rather than the request's names? *Recommended: yes.*
2. **Enumerations**: `PhysicsBackendKind::GPU_XPBD` only (the XPBD integrator and solver kinds exist), plus the backend's
   settings enumerations and constants in the same `enums.cppm` (§C1)? *Recommended: yes.*
3. **Device**: the factory makes a device per world, and the direct entry `gfx.physics.gpu_xpbd` takes the application's
   device with backend settings (§0 row 3, §C2)? Or only one of the two? *Recommended: both.*
4. **Build**: target `gfx::physics_gpu_xpbd`, option `GFX_PHYSICS_BACKEND_GPU_XPBD` default OFF, `gfx::physics` allowed to
   link graphics only when it is ON (§0 row 4)? *Recommended: yes.*
5. **Graphics additions**: `SHADER_INTEGER64_ATOMICS`, `VULKAN_MEMORY_MODEL`, `DeviceLimits::subgroup_operations`
   (`SubgroupOperation` bitmask), `maximum_storage_buffer_range`, `maximum_compute_shared_memory_size` (§0 row 5)?
   *Recommended: yes (done in G0).*
6. **Readback**: one readback per step of states, joint states, particles, contacts and events, required by the getters
   (§0 row 7)? *Recommended: yes.*
7. **Queries**: `QueryEngine::BACKEND` on the GPU, synchronous as the interface defines; an asynchronous batch API only if
   you want one later (§0 row 8)? *Recommended: yes; no batch API now.*
8. **λ**: reset at each substep (standard XPBD), contact anchors cached by feature ID, joint forces averaged over the
   substeps — instead of λ kept across substeps (§0 row 9)? *Recommended: yes.*
9. **Settings mapping**: `SolverDescription` as in §A9; a soft body's own sub-steps as ⌈sub-steps / S⌉ iterations per
   substep; the backend-only settings in `GpuXpbdDescription` (the factory uses its defaults)? *Recommended: yes.*
10. **Determinism**: bit-identical on one device, driver and variant set; across variants tested and reported, not
    promised; no floating-point atomics anywhere (§A11)? *Recommended: yes.*
11. **Variants**: subgroups or shared memory; 64-bit atomics or two passes; no storage-image and no fp16 variants
    (§0 row 6)? *Recommended: yes.*
12. **Scope of shapes and features**: everything the interface has (every shape kind, meshes and height fields on moving
    bodies, compounds, six-DOF joints, rounded hulls, separate static and dynamic friction, soft bodies, triggers);
    `MotionQuality::CONTINUOUS` as speculative contacts (no time of impact: very fast bodies against very thin geometry can
    still tunnel, documented); refused: user constraints, component replacement, double precision (`LARGE_WORLDS`)?
    *Recommended: yes.*
13. **BVH**: separate static and moving trees, a refit particle tree, 32-byte nodes, corrected δ (§0 rows 14–16); treelets and
    the four-wide layout implemented, their defaults decided by G2's measurements? *Recommended: yes.*
14. **Sleep**: by islands, as `SleepDescription` says ("its island must stay under the thresholds"), rather than per body?
    *Recommended: islands.*
15. **Overflow**: capacities with headroom, overflow detected on the GPU, the step replayed from its saved start with larger
    buffers (never truncated, deterministic)? Or a second submission every step once the counts are known (simpler, one
    more wait per step)? *Recommended: replay.*
16. **Conformance**: build the backend-parameterized suite in G9 from the existing tests, with the four device
    configurations of §0 row 12? *Recommended: yes.*
17. **Dependencies**: no glm, nothing new (§0 row 17)? *Recommended: yes.*
18. **Repository and CI**: this repository, you commit; no workflow now (§0 row 19)? *Recommended: yes.*
19. **Phases**: G0–G10 in order, each validated (§B)? *Recommended: yes.*
20. **Several worlds through the factory**: each makes its own device (no process-wide state), and applications wanting
    several worlds on one device use the direct entry? *Recommended: yes.*
