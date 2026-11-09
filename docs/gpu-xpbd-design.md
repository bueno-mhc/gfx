# gfx physics: a GPU-exclusive XPBD backend on a Karras BVH — architecture, decisions and plan

Status: **answers given 2026-10-31: every question of §D as recommended.** **G0 accepted** with its 5 deviations (§B "G0
as built"; committed a0414c5), **G1 accepted** with its 4 (§B "G1 as built"; committed 3e666ee), **G2 accepted** with its 5
(§B "G2 as built"; committed 6858652), **G3 accepted** with its 5 (§B "G3 as built"; committed c3623c9), **G4 accepted**
with its 5 (§B "G4 as built"; committed ac87393), **G5 accepted** with its 9 (§B "G5 as built"; committed c51d323), **G6
accepted** with its 7 (§B "G6 as built"; committed cad9b8e), **G7 accepted** with its 6 (§B "G7 as built"; committed
31de7c4), **G8 accepted** with its 7 (§B "G8 as built"; committed 66af4ca), **G9 accepted** with its 7 (§B "G9 as built";
committed fb58b7e; the 64-bit atomics setting stays a reported capability, as recommended). **G10 accepted** with its 3 (§B "G10 as
built": documentation; committed 66dbbd0). Every phase of the plan is built and accepted.

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

### G1 as built (2026-10-31)

**`GpuPrimitives`** (exported partition `gfx.physics.gpu_xpbd:primitives`, pimpl; units `gpu_primitives.cpp`): recorded into
the caller's `ComputeRecorder` on storage buffers named by `GpuSpan` {bindless index, first element}; a `GpuCount` is a
constant maximum, or a word a pass wrote (clamped to the maximum, which sizes every dispatch). `reserve(maximum)` sizes the
scratch (called before recording; at most `GPU_XPBD_MAXIMUM_ELEMENTS`, 65535 tiles of 512). Each primitive orders its passes
and ends with a barrier to later compute passes (to indirect dispatches for `dispatch_arguments()`); the caller orders what
wrote the inputs. Misuses (a count above the reserve, a sort into an input's buffer, 0 or more than 32 key bits, arguments
without a count word) throw `InvalidUsageError` when recorded.

- **`exclusive_scan(input, output, count, total)`**: reduce-then-scan over levels of 512-element tiles (at most three levels
  for 33.5 M words): `scan_reduce` sums each tile into the level above, the top level (one tile) is scanned in place by one
  workgroup, which writes the total, then `scan_downsweep` scans each level's tiles from the offsets above. In place allowed.
- **`reduce_bounds(boxes, result, count)`**: boxes of two vectors (minimum, maximum), reduced level by level; the empty box
  (+∞, −∞) for none.
- **`sort(keys, values, sorted_keys, sorted_values, count, key_bits)`**: stable, out of place. Up to
  `GPU_XPBD_RANK_SORT_LIMIT` (4096) keys, **by rank** in one pass (each key counts the keys before it, a tile of keys at a time
  in shared memory); beyond, **LSD radix**, `GPU_XPBD_RADIX_BITS` (8) per pass, ⌈key_bits / 8⌉ passes alternating between the
  scratch and the outputs so the last writes the outputs: `radix_histogram` (shared atomics; histogram digit-major,
  `digit · tiles + tile`) → the scan, in place → `radix_scatter` (the tile in rounds of 64 elements in element order; an
  element's rank = the earlier invocations of its round with its digit, counted in shared memory; each digit's next place
  advances once the round has read it). Keys are below 2^key_bits (a precondition: fewer bits, fewer passes).
- **`compact(flags, values, output, output_count, count)`**: the flags (0 or 1) scanned into places, the total as the count,
  then `compact_scatter` writes each flagged value (or index) at its place.
- **`dispatch_arguments(count, arguments, per_workgroup)`**: (⌈count / per workgroup⌉, 1, 1) for an indirect dispatch.

**Variants.** The workgroup operations (sum, exclusive sum, box minimum and maximum: `shaders/workgroup.glsl`) have a subgroup
variant (subgroup arithmetic within subgroups, then across them by the first subgroup) and a portable one (Hillis–Steele
scan, tree reductions in shared memory). They are **two SPIR-V modules per pass** — `scan_reduce.comp` and
`scan_reduce_subgroups.comp` include the same body, the latter after `#define GPU_XPBD_SUBGROUPS` — because a module declaring
the subgroup capabilities is invalid on a device without them even if a specialization constant never takes that branch.
The subgroup variant now needs `BASIC | ARITHMETIC` and subgroups of at least `GPU_XPBD_MINIMUM_SUBGROUP_SIZE` (8: a workgroup
of 64 then has no more subgroups than the first subgroup has lanes); both GPUs here qualify. Integer sums and floating-point
minimums and maximums are exact, so the variants give the same bits.

**Shaders.** Includes split: `common.glsl` (extensions — the subgroup ones under `GPU_XPBD_SUBGROUPS` —, specialization
constants, typed bindless aliases), `step.glsl` (was `gpu_xpbd.glsl`: step parameters, patches, the step's push constants,
body helpers), `workgroup.glsl`, `primitives.glsl` (count resolution, blocked and striped element indices). Specialization
constants 6–8: the invalid bindless index, the elements per invocation, the radix bits. New constants in `enums.cppm`:
`GPU_XPBD_ITEMS_PER_INVOCATION` 8, `GPU_XPBD_TILE_SIZE` 512, `GPU_XPBD_MAXIMUM_ELEMENTS`, `GPU_XPBD_RADIX_BITS` 8,
`GPU_XPBD_RANK_SORT_LIMIT` 4096, `GPU_XPBD_MINIMUM_SUBGROUP_SIZE` 8. The step does not use the primitives yet (G2's BVH
does; the step from G3).

**Deviations (for validation).**
1. **`GpuPrimitives` is public** (exported by `gfx.physics.gpu_xpbd`): the tests reach it there, and tools may sort, scan and
   compact on a gfx device with it; the design had the primitives internal.
2. **The radix sort's in-tile ranking is the same in both variants** (counted in shared memory; no ballot-based multisplit):
   the subgroup variant speeds up its scans only. Measured: a million 32-bit keys in about 3 ms on the RTX 5060, 27–38 ms on
   the Intel GPU (Debug, validation layers, recording and waiting included), the two variants alike. A ballot multisplit can
   come if a later phase needs it.
3. **Variants are separate SPIR-V modules**, not specialization constants (above); and the variant needs no ballot (G0 asked
   for `BALLOT` and did not check the subgroup size).
4. **Dispatches are sized by the maximum** even when the count is a GPU-written word (passes past the count do nothing);
   `dispatch_arguments()` is there for callers that want indirect dispatches.

**Tests** (`gpu_primitives_test.cpp` in `gfx_physics_gpu_xpbd_tests`; each on both GPUs × both variants with the validation
layers): scans of 0 to 600 001 words (one, two and three levels; wrapping and small values; in place) against serial sums,
with nothing written past the count; box reductions of 0 to 300 001 boxes against serial minimums and maximums (exact);
sorts of 0 to 200 003 pairs (rank and radix paths, 32-, 30-, 10-, 4- and 2-bit keys, already sorted input, values = indices)
equal to `std::ranges::stable_sort`; a million keys sorted (time printed); compactions with values and with indices; counts
from GPU words (scan, sort by radix and by rank, a word above the maximum clamped, dispatch arguments for 5555 and 0
elements); the variant asked for is the one running; misuses refused.

### G2 as built (2026-10-31)

**`GpuBvh`** (exported partition `gfx.physics.gpu_xpbd:bvh`, pimpl; unit `gpu_bvh.cpp`): a tree of boxes (two vectors each,
as `reduce_bounds()` reads them) recorded into the caller's `ComputeRecorder`, on the caller's `GpuPrimitives` (sort,
scans, reduction). `GpuBvhDescription` {`layout` (`GpuBvhLayout` BINARY or FOUR_WIDE), `treelet_rounds`, `debug_name`};
`reserve(maximum)` before recording (the primitives too); `build(boxes, count)` (a `GpuCount`: a constant or a GPU word);
`refit(boxes)` (same leaves, moved boxes); `find_pairs(pairs, pair_count, capacity)` within the tree and
`find_pairs(other, …)` between two trees; `cast_rays(rays, hits, count)`; the buffers for reading back (header, nodes,
annotations, four-wide nodes, sorted codes); the free function `structure_nodes(header, nodes, output)` lists a tree read
back as the spatial structures list theirs (`BroadPhase::structure_nodes()`). Misuses (a refit, a search or rays before a
build, a count above the reserve) throw `InvalidUsageError` when recorded.

- **Layout.** n leaves: 2n − 1 nodes of 32 bytes in one array — the internal nodes 0 … n − 2 (node 0 the root when n ≥ 2),
  then the leaves in the sorted order. A node holds its box's minimum and maximum (float bits) and two words: its children
  (an internal node's index, or `GPU_XPBD_BVH_LEAF_FLAG` | the leaf's position), or for a leaf its primitive. The nodes are
  read and written as word vectors, so an index's bits never pass through a float (a NaN or denormal pattern a driver might
  canonicalize or flush). Beside them: a parent per node, an **annotation** per node (leaves under it, the last leaf
  position under it, its surface-area-heuristic cost), a counter per internal node, the header {leaf count, root, `GpuBvhFlag`
  bits, reserved}, and for FOUR_WIDE a 128-byte node per internal node (children's boxes by axis, children, their last leaves).
- **Build** (`bvh_morton`, the sort, `bvh_hierarchy`, `bvh_bounds`): the box around every box (`reduce_bounds`), each
  center's 30-bit Morton code in it (10 bits per axis, `GPU_XPBD_MORTON_BITS`), a stable sort of (code, index) by 30 bits,
  the hierarchy of Karras 2012 (figure 4) with **δ the common prefix length** (32 + that of the indices when codes are equal,
  §0 row 15), then the bounds bottom up: one invocation per leaf climbs, counting itself on each parent's counter with
  acquire-release semantics (queue-family scope, the Vulkan memory model, coherent views of the nodes and annotations); the
  second to arrive writes the union and the annotation. Costs: leaf `GPU_XPBD_BVH_LEAF_COST` (1) × area, internal
  `GPU_XPBD_BVH_INTERNAL_COST` (1.2) × area + (cost of its children), areas 2(xy + yz + zx) — every operation `precise` (no
  fused multiply-add) and in one association everywhere, so a node's cost is the same bits whoever computes it.
- **Treelets** (`bvh_treelet`, Karras and Aila 2013): round r climbs again (counters zeroed by `bvh_reset`); a node over at
  least `GPU_XPBD_TREELET_LEAVES` (7) · 2^r leaves forms its treelet (its children, then the internal subtree of largest area
  opened, the first on ties, until 7 subtrees), weighs every topology over the subtrees by dynamic programming over their
  2⁷ subsets (each split enumerated once, the first least cost kept), and writes the best one into the treelet's own
  internal nodes when it is strictly cheaper; other nodes get their annotation again. The current topology's cost is
  evaluated exactly as the DP evaluates topologies, so "strictly cheaper" never depends on rounding, and the result does not
  depend on the order the GPU runs the invocations in.
- **Four-wide** (`bvh_wide`): a binary node rooting a four-wide node takes its two children and opens the internal child of
  largest area (the first on ties) until four hang there; internal children root four-wide nodes of their own, at their
  binary index. Each internal node finds out alone whether it roots one, by repeating the openings from the root along its
  path — no level-by-level dispatches, nothing shared between invocations.
- **Pairs** (`bvh_pairs`, run twice): each leaf of the query tree traverses the target tree (stack of
  `GPU_XPBD_BVH_STACK_SIZE` 64 entries; a Karras tree is at most 63 internal nodes deep) and counts its overlapping leaves;
  the counts are scanned into offsets (their sum is the pair count); the second run writes each leaf's pairs from its offset
  in the same order. Within a tree, each pair once: a leaf pairs only with later leaves, and subtrees whose last leaf is not
  later are skipped (the annotation's last leaf). Pairs past the capacity are counted, not written. A traversal deeper than
  the stack sets `GpuBvhFlag::STACK_OVERFLOW` in the target's header (never seen in the tests).
- **Rays** (`bvh_rays`): the nearest leaf box each ray enters (nearer children first, subtrees entered beyond the nearest hit
  skipped, ties to the smaller primitive), for G8's ray queries and for the measurements.
- **Refit**: counters reset, the bounds climb over the existing hierarchy with the new boxes, the four-wide nodes again.

**Determinism.** The same boxes give the same codes, tree, annotations, four-wide nodes and pairs in the same order, bit for
bit, twice and with either variant of the primitives (tested). Unions are exact, costs are evaluated in one fixed way, the
four-wide and treelet choices break ties by position, pairs land at scanned offsets.

**Measurements** (`GpuBvhMeasurementTest`: 100 000 boxes, Release, validation layers off, recording, submission and waiting
included, best of five; "uniform": boxes spread evenly, "scene": a stress case — most boxes in sixteen clusters, many on a
floor, a tenth of them large, half sizes 5–20 m):

| | RTX 5060 | Intel RaptorLake |
|---|---|---|
| plain build, binary (uniform / scene) | 0.37 / 0.32 ms | 6.5 / 3.0 ms |
| four-wide nodes on top | +0.2–0.4 ms | within the noise (4.6 / 5.0 ms) |
| first treelet round on top | +75–87 ms | +69–78 ms |
| second, third round | +55–80, +40–62 ms | +38–46, +20–29 ms |
| SAH cost after 1 / 2 / 3 rounds (uniform) | −5.3 / −5.7 / −5.7 % | the same trees |
| SAH cost after 1 / 2 / 3 rounds (scene) | −20 / −23 / −24 % | the same trees |
| pairs, uniform (56 485), binary → four-wide | 0.79 → 0.49 ms | 7.4 → 3.1 ms |
| pairs, scene (4.1 M), binary → four-wide | 23.4 → 12.7 ms | 70.7 → 36.8 ms |
| 100 000 rays, scene, binary → four-wide | 4.8 → 3.4 ms | 15.8 → 15.1 ms |
| 100 000 rays, scene, four-wide, 0 → 1 → 3 rounds | 3.4 → 2.5 → 2.5 ms | 15.1 → 10.1 → 9.2 ms |

**Defaults decided** (§D13): **FOUR_WIDE** traversal (pairs 38–59 % faster, rays faster or equal, the nodes cost less than a
millisecond per build); **no treelet rounds** for a tree rebuilt every step (the first round costs two to three hundred
plain builds on the RTX 5060, eleven to twenty-six on the Intel GPU). A tree rebuilt rarely (the static tree) may take one
round when its ray queries matter (rays 26–39 % faster in the scene): G3 and G8 decide with the world's numbers. The world's
settings (`GpuXpbdDescription`) gain these fields with G3, when a world first uses a tree. In Debug with the validation
layers the plain builds were slower (2 ms on the RTX 5060, 5–8 ms on the Intel GPU), the treelet rounds as slow, and the
four-wide nodes as much faster.

**Found.** The RTX 5060's driver lost writes to the fourth component of a vector of an array indexed at run time
(`minimums[axis][member]`: the fourth child's box stayed empty; Intel's was right). Found by comparing the four-wide nodes
with the CPU's bit for bit. The four-wide passes now gather per child and build each row from constant components, and the
traversals read a row's component through `lane()` (selections).

**Deviations (for validation).**
1. **A component, not yet in the world.** `GpuBvh` is public and tested on its own, like `GpuPrimitives`. The world keeps
   refusing colliders until G3 brings contacts — accepting them without collisions would ignore collisions silently. So the
   world's static and moving trees and `structure_nodes()` of a running world come with G3, the particle tree with G7; every
   piece they need (build, refit, pairs between trees, `structure_nodes()` of a read-back tree) is here and tested.
2. **Treelet rounds, not top levels**: `treelet_rounds` (round r restructures nodes over at least 7 · 2^r leaves), the knob
   Karras and Aila's method has, instead of the design's `treelet_levels` (the n top levels). One invocation weighs a whole
   treelet (the paper uses a warp per treelet, its subsets in shared memory): simpler, and too slow for per-step rebuilds
   as measured (above). A warp-parallel version can come if a static tree's queries need treelets.
3. **Annotations beside the 32-byte nodes** (leaves, last leaf, cost: 16 bytes per node): the self-pair search skips
   subtrees without later leaves, the treelet pass needs the costs, and the tests check them; traversal reads them only
   within a tree.
4. **Rays now**: the box-level nearest-hit traversal (`cast_rays()`) is in G2 (for the measurements); G8 adds the shapes.
5. **Each tree has its own pipelines** (six to eight): sharing them between the world's trees can come with G3 if creation
   time matters.

**Constants and enumerations** (`enums.cppm`): `GpuBvhLayout` {BINARY, FOUR_WIDE}, `GpuBvhFlag` {NONE, STACK_OVERFLOW};
`GPU_XPBD_MORTON_BITS` 10, `GPU_XPBD_BVH_LEAF_FLAG` 2³¹, `GPU_XPBD_BVH_STACK_SIZE` 64, `GPU_XPBD_TREELET_LEAVES` 7,
`GPU_XPBD_BVH_INTERNAL_COST` 1.2, `GPU_XPBD_BVH_LEAF_COST` 1 — specialization constants 9–15 (the costs by their bits).
Shaders: `bvh.glsl` (layout, boxes, areas, costs, `lane()`, ray entry), `bvh_climb.glsl` (the climb, the union of children),
`bvh_morton`, `bvh_hierarchy`, `bvh_bounds`, `bvh_reset`, `bvh_treelet`, `bvh_wide`, `bvh_pairs`, `bvh_rays`;
`common.glsl` gained word-vector views. `record_pass()` moved to `:resources` (shared by the primitives and the tree).

**Tests** (`gpu_bvh_test.cpp` in `gfx_physics_gpu_xpbd_tests`, on both GPUs × both variants with the validation layers;
the fixture now shared with the primitives' tests in `tests/physics/gpu_test.hpp`): Karras trees of 0 to 100 000 boxes —
codes within a cell of the CPU's, equal codes in the boxes' order, **the internal nodes the CPU builds from the same codes**,
each leaf and primitive reached once, unions exact, annotations and costs bit for bit, contiguous leaf ranges; treelet rounds
1–3 on uniform and clustered boxes **equal bit for bit to the CPU's restructuring of the GPU's plain tree**, costs never
higher; four-wide nodes of 2 to 30 000 boxes equal to the CPU's collapse; pairs within trees of 0 to 20 000 boxes and between
two trees equal to brute force in both layouts with and without treelets, the same order twice, a short capacity still
counting every pair; rays (from inside boxes, along axes, short) entering the nearest box within 1 mm; refits (the same
children, moved boxes, their pairs); a count from a GPU word giving the same tree as the constant; the same bits twice and
in the other variant; `structure_nodes()`; misuses. `GpuBvhMeasurementTest` prints the table above for each GPU.

### G3 as built (2026-10-31)

**`GpuNarrowPhase`** (exported partition `gfx.physics.gpu_xpbd:narrow_phase`, pimpl; unit `gpu_narrow_phase.cpp`), recorded
into the caller's `ComputeRecorder` like the primitives and the tree:

- **Shape table**: `add_shape(const Shape&)` takes the six convex kinds (a 48-byte record each: kind, sizes, margin; a
  hull's vertices, faces with their planes and polygons appended to shared arrays) and refuses triangle meshes, height
  fields and compounds with a `BackendCapabilityError` naming G4; `upload_shapes(recorder)` records the copies of the whole
  table through a staging buffer (before the passes that read it, as `reserve()`); a pass recorded with shapes not uploaded
  throws `InvalidUsageError`.
- **`bounds(instances, boxes, count, expansion)`**: the box of each `GpuInstance` (position, shape index, orientation;
  32 bytes) as `convex_bounds()` computes it — the core's reach along each world axis from its support function, plus the
  margin — grown by the expansion: what `GpuBvh::build()` reads.
- **`collide(instances, pairs, manifolds, count, settings)`**: one invocation per candidate pair (a `GpuPair` of instance
  indices, as `GpuBvh::find_pairs()` writes them), one 160-byte `GpuManifold` at the pair's index (instances, point count,
  algorithm, normal, four points with their positions, separations and `ContactFeature`s; no points: no contact).
  `to_contact_manifold()` turns one into the CPU's `ContactManifold`.
- **`match(previous, previous_count, current, count, predecessors)`** — the contact cache: the previous manifolds sorted by
  pair (two stable radix sorts: second instance, then first instance gathered in that order; manifolds without points sort
  last), each current point's predecessor found by a binary search of its pair and a comparison of features: the previous
  manifold's index × 4 + its point, or 0xFFFFFFFF. `reserve(maximum)` sizes its scratch.

**The CPU's narrow phase, ported.** The shaders (`narrow_phase.glsl`: the table, poses, support functions and supporting
faces of the six kinds, the Minkowski difference, features, manifolds; `gjk.glsl`; `epa.glsl`; `manifold.glsl`: clipping,
reduction to four points, single-point manifolds, the general path; `analytic.glsl`: sphere–sphere, sphere–capsule,
capsule–capsule, sphere–box, box–box separating axes; passes `narrow_phase_collide`, `shape_bounds`, `contact_keys`,
`contact_match`) follow `gfx.physics.shapes` and `gfx.physics.narrow_phase` operation by operation: the dispatch of
`collide_convex()` (analytic pairs in either order, flipped), GJK with Ericson's Voronoi regions and the sliver test, EPA
with its octahedron restart, horizon and best-face guard in fixed storage, Sutherland–Hodgman clipping with feature tags,
`reduce_contact_points()`. The literal tolerances are the CPU's literals; the named limits come as specialization
constants 16–38 (shape kinds, contact algorithms, `MAXIMUM_FACE_VERTICES`, `MAXIMUM_GJK_ITERATIONS`, the EPA storage,
`NO_SUB_SHAPE`, the shapes' `FACE_ALIGNMENT`, `SIDE_ALIGNMENT`, `ROUND_FACE_VERTICES`, `GEOMETRIC_TOLERANCE`, the clipped
polygons' capacity). Large working storage (the EPA polytope, the clipped polygons, the supporting faces) is private memory
of the invocation.

**How it agrees with the CPU** (`ManifoldsAreTheCpusOnEveryPairOfShapes`: 300 random poses of each of the 64 ordered pairs of
eight shapes — sphere, capsule, cylinder, cone, box, rounded box, hull, rounded hull — apart, touching and overlapping, with
the default speculative distance and with none; about 9 500 contacts per run, every algorithm used): **99.9 % of the pairs
give the CPU's manifold at the same poses** — the same algorithm, point count and feature ids, normals within 1.8°, points
and separations within 5 mm (EPA's within its convergence: 2.5°, 2.5 cm, 2 mm — it stops at a depth within 10⁻⁴ of its
reach, which leaves a curved surface's normal free by about a degree) — **and every other pair (19 to 24 per run) gives the
manifold the CPU gives at a pose of either shape 1 µm to 0.2 mm (or µrad to 0.2 mrad) away.** None disagrees. Those
nearby-pose cases are the CPU algorithm's own knife edges, where rounding decides: support points tied along a face normal
(the separating-axis fallback names any corner of the face), a clipped segment running along a reference side plane, EPA in
deep overlaps of curved shapes (a ring of equally deep normals; the polytope's best face). GPU divisions, square roots,
sines and fused operations are not the CPU's, so the two meet at those edges differently.

**Deviations (for validation).**
1. **A component, still not in the world — and the world waits for G5, not G3.** G2's report said the world's trees came with
   G3. But G3 makes contacts and nothing responds to them until G5 (the solver): colliders accepted now would let bodies pass
   through each other silently. So colliders stay refused until G5, which wires bounds, trees, pairs, manifolds and the cache
   into the step (and `structure_nodes()` of a running world).
2. **A manifold per candidate pair, in place.** `collide()` writes a slot per pair (empty when apart); compacting the pairs
   with contacts into a dense list is G5's, which knows what the solver needs.
3. **The cache finds predecessors; what it carries comes later**: the static-friction anchors with G5, the touching age and
   the BEGIN/END events with G8.
4. **One shader for every pair kind**, each invocation running the path its pair needs (analytic, separating axes, GJK, EPA,
   clipping): divergence costs, measured below; sorting pairs by kind or separate dispatches can come if G5's step shows the
   need.
5. **Triangles wait for G4**, with the meshes they come from (the CPU's `TriangleShape` and triangle–triangle separating
   axes).

**Measured** (`GpuNarrowPhaseMeasurementTest`: 192 000 pairs, 3000 random poses of each ordered pair, every algorithm —
about half deep overlaps through EPA, so a heavier mix than resting contacts; recording, submission and waiting included,
best of five): **13.9 ms on the RTX 5060 (14 pairs per microsecond) and 75.8 ms on the Intel GPU** in Release without the
validation layers — the same as in Debug with them (13.5 and 77 ms): the GPU's work decides.

**Found.** glslang checks a constant index into an array sized by a specialization-constant *expression* as if it held one
element (`2 * MAXIMUM_FACE_VERTICES`: the clipped polygons' capacity is a constant of its own); `half`, `input`, `output`,
`centroid`, `shared` and `flat` are reserved words in GLSL.

**Tests** (`gpu_narrow_phase_test.cpp` in `gfx_physics_gpu_xpbd_tests`, on both GPUs with the validation layers): the
manifolds above; instance boxes equal to `Shape::bounds(pose)` grown (within 0.1 mm); the same bits twice; the cache (pairs
shuffled, dropped and added, shapes moved by up to a millimeter, the previous count from a GPU word: every predecessor the
one the manifolds read back name, most points matched); **bounds → `GpuBvh` → pairs → manifolds** for 1200 shapes in a 12 m
cube against every pair on the CPU (the same 2039 contacts; every CPU contact's pair from the tree; none on the GPU between
shapes the CPU finds apart); misuses (a mesh, a negative speculative distance, an infinite expansion, a cache above its
reserve, a shape not uploaded). `GpuNarrowPhaseMeasurementTest` prints the throughput.

### G4 as built (2026-10-31)

**Every shape kind in `GpuNarrowPhase`.** `add_shape()` now takes triangle meshes, height fields and compounds: the shape
table holds 96-byte records (kind, flags, compound depth; two ranges; a static hierarchy's first node, item and item box;
sizes and margin; the shape's own box) and eleven shared arrays (vertices of hulls and meshes, hull faces and corners, mesh
triangles with absolute vertices and their active-edge bits, hierarchy nodes with absolute children or items, hierarchy items
and item boxes, height-field samples as given, each height-field triangle's active edges, compound children with their
poses); a table buffer names the arrays' bindless indices for every pass (`load_tables()`). A compound's children are added
with it, a child shared within one addition stored once.

**`collide()` as `physics::collide()` does it**, in four passes recorded into the caller's command buffer:
1. **Mid phase, counted** (`narrow_phase_pairs.comp`, one invocation per candidate pair): the pair becomes its sub-pairs in
   the order collide.cpp's `Gatherer` visits them — a compound descends into the children its partner's box overlaps, the
   first shape's compounds before the second's, through the CPU's own static hierarchy and stack order (explicit levels,
   `2 × GPU_XPBD_COMPOUND_DEPTH`); a convex shape meets the triangles of a mesh or height field under its box in their frame
   (resumable soup queries: a mesh's hierarchy without zero-area triangles; a height field's cells under the box, row by row,
   two triangles each); two meshes or height fields meet triangle by triangle (the first's under the second's box, then the
   second's near each). Each sub-pair records its kind, its sub-shapes, both pieces and their poses.
2. The counts **scanned** (`GpuPrimitives::exclusive_scan`) into each pair's first slot; the sum is the `total` word.
3. **Mid phase, written** at those slots, up to the capacity.
4. **One manifold per sub-pair** (`narrow_phase_collide.comp`): two convex shapes through the convex dispatch; a convex shape
   against a triangle in the triangles' frame — no contact from behind a one-sided triangle, the face normal on inactive
   edges (`correct_inactive_edges`), each point naming its triangle, moved into the world, flipped when the triangles are the
   pair's first; two triangles by separating axes in the first's frame (`collide_separating_axes` now serves boxes and
   triangles), each point naming both.
5. **Merge** (`narrow_phase_merge.comp`, one invocation per pair): the pair's manifolds in order, each merged into the first
   earlier one whose normal is within the merge cosine (points reduced to four; the deeper names the sub-shapes), the
   deepest `maximum_manifolds` kept by a stable insertion sort, the rest of the pair's slots emptied, the pair's range
   written.

Output: `GpuManifoldSpans {manifolds, ranges, total, capacity}` — `GpuManifold` slots (176 bytes: G3's plus the manifold's
`sub_shape_first`/`second`), per pair its first slot and manifold count, the slots needed. `reserve(maximum_pairs,
maximum_manifolds)`; `reserved_pairs()`, `reserved_manifolds()`. The cache (`match()`) reads the new layout and searches every
manifold of a pair for a point of the same feature (sub-shapes and local features), the first in slot order.

**How it agrees with the CPU.** The CPU's own collide cases hold on both GPUs (`TheCpusCollideCasesHold`): a box sliding over
an 8 × 8 floor's seams keeps one manifold of four points with the floor's normal at all 61 positions, the floor first, a ball
under one-sided and two-sided floors, a box across a height field's ridge (two normals; the deeper alone with one manifold), a
table's four legs merged into one manifold naming a leg, two floors lowered onto each other triangle by triangle. **Random
overlapping poses** (`ManifoldsAreTheCpusWithMeshesHeightFieldsAndCompounds`: 30 of each of the 161 ordered pairs with a rough
one-sided mesh, a flat two-sided mesh, a closed box mesh, a height field, a dumbbell compound, a compound nesting it, or a
compound holding a mesh; default settings, no speculative distance, one manifold): about 4 000 of 4 830 pairs in contact per
run, 3 000 of them with several manifolds; **96–98 % give the CPU's manifolds at the pose** (order, algorithms, sub-shapes,
points with their features), **1.6–3.5 % the CPU's at a nearby pose, and 1 to 3 pairs per run (0.06 %) differ at ties no
nearby pose separates** — points that coincide because neighboring triangles share an edge or a vertex (rounding decides which
triangle's point the reduction keeps, and its feature names that triangle), manifolds sharing their deepest point (their
order when more than `maximum_manifolds` merge), EPA's support point on a flat face. Such ties move with the pose, so the
nearby-pose test cannot separate them; the tests allow 0.25 % and print each. Convex pairs remain held to G3's standard (every
pair at the pose or a nearby one). End to end (1200 shapes, a quarter of any kind: bounds → `GpuBvh` → pairs → manifolds),
the GPU and the CPU find the same 2 377 contacts; 0 (Intel) and 2 (NVIDIA) of 9 632 examined pairs differ at ties.

**Deviations (for validation).**
1. **The CPU's static hierarchies, uploaded — not built on the GPU** (§A7 said the GPU builder would build them). A mesh's
   or compound's `StaticBvh` (nodes, items, item boxes) is copied as built, so the GPU visits triangles and children in the
   CPU's order: the merged manifolds (which points and sub-shapes survive) depend on that order. The shapes build their
   hierarchy anyway; a Karras tree would visit them in another order.
2. **Compounds descended in the narrow phase, not flattened into child colliders** (§A7). The candidate pair stays a pair of
   instances and its compounds are descended as collide() does, so a compound's contacts merge across its children (the
   CPU's single manifold for a table's four legs). The descent's fixed storage limits nesting to `GPU_XPBD_COMPOUND_DEPTH`
   (4) per side: `add_shape()` refuses deeper compounds with a `BackendCapabilityError` suggesting to flatten them.
3. **`collide()` changed shape**: ranges per pair instead of a slot per pair (G3's deviation 2), `GpuManifold` 176 bytes,
   `reserve()` takes pairs and manifold slots. Pairs whose slots pass the capacity are cut short (none of their manifolds
   written, nothing past the capacity); the `total` word tells the slots needed, which G5 sizes from.
4. **A change to the CPU's narrow phase: `TRIANGLE_FACE_TOLERANCE`** (enums.cppm, 10⁻³). `TriangleShape::supporting_face()`
   took the triangle's front when `dot(front, direction) >= 0`; a triangle met edge-on — beside a box resting on a mesh floor,
   which every box on a floor has — got its face from the sign of rounding in GJK's normal (±10⁻⁵), so the CPU and the GPU named
   the incident face, hence the points' features, differently (one seam step failed). Directions within 10⁻³ of the triangle's
   plane now take the front, on both. Only feature ids change (front and back are the same polygon); the CPU tests all pass.
5. **Ties allowed for shapes with triangles or children** (0.25 %, above), where G3 allowed none: their coincident points are
   the CPU algorithm's own knife edges, and they move together.

**Measured** (`GpuNarrowPhaseMeasurementTest`, best of five, recording, submission and waiting included). In Release without
the validation layers: 192 000 convex pairs in **14.0 ms on the RTX 5060** (G3: 13.9) and 87.7 ms on the Intel GPU (G3: 75.8 —
the mid phase, scan and merge cost about 12 ms there); 32 200 overlapping pairs with triangles or children (783 360 sub-pairs)
in **38.5 ms** and 185 ms. Debug with the layers: 14.7 / 89 ms and 40 / 187 ms.

**Found.**
- **GLSL inlines every call**: the sub-pair kernel first called the convex dispatch in three branches (and the separating axes
  twice), compiling GJK, EPA and clipping three times; on the RTX 5060 the kernel ran 5.5 times slower (84 ms for the convex
  pairs). One call site restored G3's speed.
- **Fused multiply-adds**: the drivers fuse `a * b - c` unless told not to. In `reduce_contact_points()` the area spanned by a
  point coinciding with the farthest one is exactly 0 on the CPU (`cross(base, base)`), a signed residue on the GPU, which
  then kept a duplicate as the fourth point: 36 of 4 830 pairs disagreed. The reduction's distances and areas and the merge's
  cosine are now `precise` (no contraction, IEEE products and sums as on the CPU).
- `buffer` is a GLSL reserved word too.

**Tests** (`gpu_narrow_phase_test.cpp`, both GPUs with the validation layers; fifteen test shapes, eight convex): convex pairs
as in G3; the overlapping pairs above; the CPU's collide cases; instance boxes of every kind equal to `Shape::bounds(pose)`
grown; the same bits twice (convex and other pairs); **pairs past the capacity cut short** (the total counts every slot, pairs
that fit are whole, pairs past it have none, the slot after the capacity untouched); the cache over pairs with several
manifolds; the end-to-end run above; misuses (compounds five deep, reserves above the maximum, a table not uploaded, invalid
settings, counts and capacities above the reserve, a cache above its reserve). `GpuNarrowPhaseMeasurementTest` prints both
throughputs.

### G5 as built (2026-11-01)

**Colliders in the world.** `add_collider()` is accepted for every shape kind (a compound nested deeper than
`GPU_XPBD_COMPOUND_DEPTH` is refused when recorded, through the new `GpuNarrowPhase::check_shape()`). The backend keeps each
distinct shape once in the narrow phase's table (by address, the shape kept alive), a 80-byte `GpuCollider` per collider
(its pose relative to its body's center of mass, its body, shape, filter words and material) and two lists of collider
slots — colliders of dynamic and kinematic bodies, colliders of static bodies — rebuilt from the records each step and
uploaded only when their bytes changed; the static tree is rebuilt only when a static collider or a static body's pose
changed (compared as bytes).

**`GpuContactSolver`** (internal partition `:solver`, unit `gpu_solver.cpp`; it owns its `GpuPrimitives`, a `GpuNarrowPhase`
and two `GpuBvh`s) records a step's contact work into the backend's command buffer:
1. **Detection** — the instances (`collider_poses`: the moving list, then the static one, at their bodies' poses), their
   boxes (`GpuNarrowPhase::bounds`), the moving tree (every step) and the static tree (when it changed), their pairs (within
   the moving tree, then between the two, at fixed regions), **filtered** (`pairs_filter`: different bodies, at least one
   dynamic, at least one awake or kinematic, `should_collide()`), compacted in order, the manifolds (`collide()`), each point's
   predecessor (`match()`).
2. **Contact constraints** (`contact_prepare`, one per manifold slot): bodies, normal, materials mixed as the reference
   backend mixes them (friction √(a·b), rolling and restitution the larger), per point the anchors on both bodies and the
   friction anchors — the matched point's of the previous step when static friction held it, else new.
3. **Islands** (`islands`): label propagation over constraints between dynamic bodies (atomic minimum, pointer jumping; the
   smallest body slot labels its island), islands with an awake body (or touching a moving kinematic one) wake their asleep
   bodies at rest.
4. **Graph and colors**: the body→constraint entries (two per slot) sorted by body (each body's constraints in slot order),
   their ranges; **Jones–Plassmann** in `GPU_XPBD_COLORING_ROUNDS` (24) rounds from one color buffer into the other (each
   round decides from the previous round's colors only; priorities hash the colliders and sub-shapes), constraints still
   uncolored — or needing a 33rd color — form the overflow set; lists by color (a stable sort), indirect dispatch arguments
   per color.
5. **Each substep**: G0's prediction (now also keeping the velocities at the substep's start), `position_iterations`
   iterations of `contact_solve` per color then the overflow set (Jacobi: corrections per side, then `jacobi_gather` averages
   each body's in graph order — no floating-point atomics), G0's velocity update, `velocity_iterations` iterations of
   `contact_velocity` the same way.
6. **Sleep** (`sleep`): each awake dynamic body's timer grows while it is slower than the thresholds (and may sleep), its
   island keeps the smallest (atomic minimum of non-negative float bits); islands past `SleepDescription::time` that touch no
   moving kinematic body fall asleep without velocity. The CPU reports FELL_ASLEEP / WOKE_UP from the flags read back, in slot
   order.

**The contact model** (Müller et al. 2020 §3.5–3.6 on bodies moved through their substep displacement and rotation, as G0's
velocity update reads them): non-penetration Δλ = (−C − α̃λ)/(w + α̃), λ ≥ 0, the depth at most `maximum_push_speed`·h,
compliance from `contact_hertz` (α̃ = w/(ωh)², 0 Hz rigid); **static friction** positional at the friction anchors while the
tangential multiplier stays within μs·λ (beyond: the point slips and its anchors renew next step — so a box held on a slope
does not creep); **dynamic friction** as an impulse of at most μd·λ/h per substep through the tangential generalized inverse
mass; **restitution** setting the normal relative speed to −e times the approach speed at the substep's start (e = 0 below
`restitution_threshold`); **rolling resistance** as an angular impulse up to the rolling coefficient (a lever) times the
normal impulse, as the reference solver has it.

**Overflow is replayed.** The step's start (after its patches) is copied to backups; when the counts read back show more
pairs or manifold slots than reserved, more colors than the passes were recorded for, or islands still changing in the last
round, the capacities grow to what was counted (with `GPU_XPBD_CAPACITY_HEADROOM` 50 %), the backups are restored and the
step runs again (at most `GPU_XPBD_MAXIMUM_REPLAYS` 4 times; then a `PhysicsError`). Results are never truncated. A step's
forces are now cleared at its start (not its end) so a replay keeps them.

**Deviations (for validation).**
1. **`contact_damping_ratio` is not used** (§A9 mapped it to XPBD damping). With the default ratio 10, XPBD's damping term
   (γ = 2ζ/(ωh) ≈ 25) left about 1/26 of each correction: a column of six boxes leaned 0.48 m in 4 s and a pile of 400 balls
   sank 0.57 m below the floor; without it the column settles 1.5 cm (its springs' compression) and stands, the pile rests
   3 mm deep. The velocity pass's restitution sets each contact's normal speed, which is the damping these contacts need.
   `contact_hertz` is honored as the compliance.
2. **Dynamic friction bounded as an impulse.** Müller's equation compares h·μd·f with a speed; taken literally (as written
   first) it made friction about the body's mass times too strong — a box on a 30° slope with μ = 0.2 slid 5 mm in 0.75 s
   instead of 1.5 m.
3. **One speculative margin per step**: the world's speculative distance plus the farthest a moving body gets in the step
   (its speed and its turn at its colliders' reach, gravity's growth), from the CPU's mirror — for every pair's boxes and
   speculative contacts. Per-body margins can come later.
4. **Sleeping islands wake one contact layer per step**: pairs of two asleep bodies are not detected, so a push on a
   sleeping stack's top wakes the box under it in the same step and the next one in the next step.
5. **`static_friction_speed` is not used**: static friction is positional (held within μs·λ), without a speed threshold.
6. **Colors recorded per step** = the colors the previous step used + 2 (at least `GPU_XPBD_INITIAL_COLORS` 8), a replay when
   a step needs more — rather than all 32 in every iteration (each recorded color is a dispatch per iteration).
7. **Contact events and `contacts_of()`** wait for G8 (statistics `pairs` and `contacts` are filled).
8. **Shapes stay in the table until the world ends** (a world making a new shape object for every collider grows it).
9. **`velocity_iterations` respected** (default 8, the sequential-impulse default): each is (colors + 2) dispatches per
   substep, most of a small scene's step; one is what Müller uses.

**Graphics and enumerations.** `GpuBodyFlag::SLEEPS`; `GpuSolverMode` {GRAPH_COLORING, JACOBI}; constants
`MAXIMUM_GPU_XPBD_COLORS` 32, `GPU_XPBD_COLORING_ROUNDS` 24, `GPU_XPBD_INITIAL_COLORS` 8, `GPU_XPBD_MAXIMUM_REPLAYS` 4,
`GPU_XPBD_CAPACITY_HEADROOM` 0.5; `GPU_XPBD_STORAGE_BUFFERS` 64 → 160 (the solver's arrays); `GpuXpbdDescription::solver_mode`
and `bvh_layout` (FOUR_WIDE by default, G2's measurements); specialization constants 46–47; `GpuArray` takes an extra buffer
usage (INDIRECT for the color dispatches). Shaders: `solver.glsl`, `collider_poses`, `pairs_filter`, `contact_prepare`,
`islands`, `graph`, `ranges`, `color`, `contact_solve`, `contact_velocity`, `jacobi_gather`, `sleep` (`.comp`); `patch_bodies`
restarts a patched body's sleep timer; `xpbd_integrate` keeps the substep's starting velocities.

**Measured** (`GpuXpbdRigidMeasurementTest`: a pyramid of 210 boxes, 1600 contact points, sleep off, default solver settings
— 4 substeps of 3 position and 8 velocity iterations; recording, submission, waiting and readback included; Release without
the validation layers): **graph coloring 3.9 ms per step on the RTX 5060, 4.2 ms on the Intel GPU; Jacobi 1.3 and 3.7 ms.**

**Found.**
- **A race between transfers** (introduced here, caught by G0's `IdenticalEditsGiveIdenticalBits`, 1 run in 10 on the RTX 5060):
  a growing array's copy of its old contents and the fill clearing the step's forces were recorded with no barrier between
  them; when the fill ran first, a body kept the previous step's force. A transfer-to-transfer barrier now follows the arrays'
  growth (`TRANSFER_TO_TRANSFER`, recorded by `GpuContactSolver::record_uploads()`), before the fills and the colliders'
  upload (which had the same hazard against a new array's zeroing). 30 of 30 runs identical after it.
- `active` is a GLSL reserved word too (renamed).
- Under a full parallel CTest run the NVIDIA driver once refused a device (`vkCreateDevice` → `VK_ERROR_INITIALIZATION_FAILED`)
  to one of the new GPU tests; it passes alone (3 of 3).
- Unrelated to this phase: the UI Automation platform test (`AUiAutomationClientFindsTheNodesAndInvokesAButton/SDL3`) hung
  again in a Release run (8 hours; its client thread blocked inside one UI Automation call while the main thread kept pumping).

**Tests** (`gpu_xpbd_rigid_test.cpp` in `gfx_physics_gpu_xpbd_tests`, each on both GPUs in both solver modes with the
validation layers): a box dropped turned lands on a face 0.5 m up and stops; a column of six boxes leans less than 1 cm and
falls asleep (six FELL_ASLEEP); a pyramid of 21 boxes moves less than 2 cm in 4 s; static friction holds a box on a 20° slope
(μ 0.6) within 1 cm over 3 s; dynamic friction slides one on 30° (μ 0.2) as g(sin θ − μ cos θ) predicts (within 10 %); a ball
dropped from 2 m with e = 0.5 rebounds to e²·2 m (within 15 %; measured 0.481 m) and with e = 0 does not; a sleeping stack
woken by a push wakes within two steps, a lone box stays asleep; eight shape kinds (sphere, capsule, cylinder, cone, box,
rounded box, hull, compound) come to rest on a triangle mesh and on a height field; two worlds with the same 40-body pile give
the same bits every 30 steps; 400 balls dropped at once (replays) end finite and above the floor. `GpuXpbdTest`'s refusals
now name the compound depth instead of colliders.

### G6 as built (2026-11-01)

**Joints in the world.** `create_joint()` is accepted for every joint kind; user constraints stay refused (`USER_CONSTRAINTS`,
§A9). A joint's **frames** are fixed when it is created, from its bodies' poses, as the reference backend fixes them (a
hinge's and a cone-twist's axis is their z, a slider's their x; a distance joint's anchors are in its bodies' frames) and kept
by joint slot; each step the backend rebuilds from the records a 96-byte `GpuJoint` per joint slot (bodies — the second
`NO_SOLVER_BODY` for the world —, `GpuJointFlag::ACTIVE` while enabled, its frames about its bodies' current centers of mass,
its breaking force and torque, its parts), the parts, and the sorted pairs of bodies joined without `collide_connected`, and
uploads them only when their bytes changed. Creating, updating or enabling a joint wakes its bodies.

**Each joint kind is a list of parts** (`GpuJointPart`, 48 bytes: a `GpuJointPartKind`, an axis of the first frame, bounds,
a spring), made on the CPU by `append_joint_parts()` (new unit `gpu_joints.cpp`) and projected in order by one shader: motors
first, then springs, angular parts, and the anchors last (the last projected is the best kept).

| Kind | Parts |
|---|---|
| distance | ROD: DISTANCE [length, length]; ROPE: DISTANCE [minimum, length]; SPRING: DISTANCE [length, length] soft (when its frequency is not 0) and DISTANCE [minimum, maximum] (when bounded); STUB: ANCHORS locked |
| hinge | ANGULAR_MOTOR z, ANGLE z [0, 0] soft (its spring), ALIGNED_AXIS z, ANGLE z [lower, upper] (its limits), ANCHORS locked |
| ball and socket | ANCHORS locked |
| slider | LINEAR_MOTOR x, ANCHORS x [0, 0] soft (its spring), ORIENTATION, ANCHORS x within its limits, y and z locked |
| fixed | ORIENTATION soft by its angular spring, ANCHORS locked soft by its linear spring |
| cone-twist | SWING (the angle between the frames' z axes at most the limit), ANGLE z (its twist limits), ANCHORS locked |
| six DOF | a motor per axis that has one; ORIENTATION when the three rotations are locked, else ANGLE per locked (0) or limited axis; ANCHORS per axis: locked 0, limited, free unbounded |

Angles are measured as the reference backend measures them (`2 atan2(v·e, w)` of the second frame in the first, with its exact
gradient), so limits and motors mean the same on both backends; motors drive the second body relative to the first.

**Constraints numbered joints first.** `GpuContactSolver` became **`GpuConstraintSolver`**: constraints 0 … J − 1 are the joint
slots, then the manifold slots, through the graph, the islands (joints join islands: a push on a chain's last link wakes the
first), the Jones–Plassmann coloring (a joint's priority hashes its slot) and the Jacobi gather. A constraint's **color key** is
its color × 2 + its kind (joints 0, contacts 1); the lists by key give each pass one color's joints or contacts. Each position
iteration runs, per color, `joint_solve` and `contact_solve` without a barrier between them (a color's joints and contacts move
different bodies), then the overflow sets and the gather; the velocity iterations stay contacts-only (joints have no velocity
pass). `pairs_filter` leaves out pairs of bodies a joint keeps apart (a binary search of the joined pairs).

**`joint_solve.comp`**, one invocation per joint per color: each part from the geometry the earlier parts left, XPBD with
the multipliers reset at each substep (§A2), Δλ = −(A⁻¹(C + γ∇C·Δx) + aλ)/(1 + γ + a) along the part's one to three axes
— A = BᵀKB the bodies' generalized inverse mass matrix along them, a spring's compliance α̃ = aA with a = 1/(ωh)² (a frequency
whatever the masses), γ = 2ζ/(ωh) its damping, ∇C·Δx the error's change by the bodies' substep motion; one axis is XPBD's
scalar update. Motors drive the relative motion over the substep (the substep's displacement and rotation accumulators) to the
target speed × h, the multiplier within ± the largest force × h². **`joint_forces.comp`** after the last substep: a joint
solved in the step reports the impulses it gave the second body over the step ÷ (substeps × h²) — the step's average force
and torque (about the second body's center; the world's origin for the world), as the reference backend reports them since
PH8 — and BROKEN past its thresholds. The CPU reads the results back (two vectors per joint slot after the counts) into
`JointState`, and a BROKEN joint is disabled and reported as a `JointEvent` in slot order.

**Deviations (for validation).**
1. **Multi-axis parts are solved as blocks** (ANCHORS, ALIGNED_AXIS, ORIENTATION), not along their error's direction as
   Müller et al. 2020 §3.3 write them (one scalar along Δx/|Δx|). Along one direction, the correction's split between moving and
   turning a body follows that direction's generalized inverse mass: a small body far from its anchor (the pendulum's 0.67 kg bob
   1 m below its hinge: turning it about 6000 times cheaper than moving it) took nearly radial corrections as turns that moved
   its anchor sideways — the bob drifted to 1.27 m from the hinge and its period ran 4 % long. Solved with A = BᵀKB, the anchors
   move exactly by their error (to first order).
2. **Angular parts turn the bodies about the pivot** of a joint whose anchors are locked rigidly (ball, hinge, cone-twist, rigid
   weld, six DOF with its translations locked): the anchors' block is eliminated (its Schur complement I₁⁻¹ + I₂⁻¹ − GᵀK⁻¹G, each
   angular correction q with the anchors' impulse −K⁻¹Gq that keeps them together). Turned about their centers, small bodies far
   from their pivot gave the turn back to the anchors at their next projection: a cone's limit converged by under 1 % per
   iteration and a swing reached 0.37 rad past a limit of 0.3 (the reference solver, too, solves a limit's row apart from its
   point's block).
3. **Breaking is decided on the GPU, applied by the next upload**: `joint_forces` flags BROKEN in the joint's result; the CPU
   disables the record and the next upload makes the joint inactive (rather than the GPU clearing the joint's enabled bit), so a
   replayed step starts from the same joints. As on the reference backend, the joint still acted in the step it broke.
4. **Motors drive the substep's relative motion** (the target is where the substep started plus speed × h), not a target
   advanced from step to step: a motor held back by its force limit does not fall ever further behind.
5. **Joints have no velocity pass**: springs damp through XPBD's γ term in the projection (as §A2's joints bullet says); the
   interface has no other joint damping.
6. **Forces in the overflow set** (every joint in the Jacobi mode, joints left without a color otherwise): the gather averages a
   body's corrections over its overflow constraints, while a joint's force counts its whole multiplier — a joint sharing a body
   with other overflow constraints reports up to that many times its load. Joints alone on their bodies (the tests' hooks)
   report it exactly. (Resolved in G7 by mass splitting: §B "G7 as built", Found.)
7. **A joint not solved in a step** (its bodies asleep, or none of them dynamic) keeps its last force and torque (a sleeping
   hanging body still reports its weight).

**Enumerations and constants.** `GpuJointPartKind` {ANCHORS, DISTANCE, ALIGNED_AXIS, ORIENTATION, ANGLE, SWING, LINEAR_MOTOR,
ANGULAR_MOTOR}, `GpuJointFlag` {ACTIVE, SOLVED, BROKEN} (bitmask); specialization constants 48–59 (the part kinds,
`NO_SOLVER_BODY`, the joint flags); `GPU_XPBD_STORAGE_BUFFERS` 160 → 176 (six joint arrays). Shaders: `joint_solve`,
`joint_forces` (new); `solver.glsl` (joint layouts, `Constraint` / `load_constraint()`, the world as an absent body,
`correct_rotation()`, color keys, `hash_words()` moved there), `graph`, `color`, `islands`, `jacobi_gather`, `contact_solve`,
`contact_velocity`, `contact_prepare` (counts the constraints), `pairs_filter` (joined bodies).

**Measured** (`GpuXpbdJointMeasurementTest`: 50 chains of 20 boxes on 1000 ball joints swinging from the world, sleep off,
default solver settings; Release without the validation layers): **graph coloring 2.1 ms per step on the RTX 5060, 5.0 ms on
the Intel GPU; Jacobi 1.4 and 4.1 ms.** G5's pyramid did not slow down (coloring 3.6 / 4.2 ms, Jacobi 1.3 / 2.3 ms).

**Found.**
- The two coupling problems of deviations 1 and 2 (by the pendulum and cone-twist tests).
- `std::minmax` of two temporaries returns references to them (GCC 16's `-Wdangling-reference` caught it).

**Tests** (`gpu_xpbd_joint_test.cpp` in `gfx_physics_gpu_xpbd_tests`, each on both GPUs in both solver modes with the validation
layers): the reference backend's scenarios — a pendulum's period (within 2 %) and length (1 cm); a hinge's limit (0.05 rad), a
rope's length, a weak weld breaking (BROKEN event, disabled, the block falls), a wheel's motor speed (−3 rad/s within 0.05);
a crate's lid breaking its hinge in the landing step on 1, 4 and 8 substeps while a hook reports its block's weight within 1 %;
a six-DOF joint's locked axes and its limited angle (0.3 rad within 0.03) — then a slider's motor force (1000 N on 64 kg) and
speed, its limit and axis; cone-twist swing and twist limits (0.3 and 0.2 rad within 0.03, reached); springs at their frequencies
(a distance spring's and a hinge spring's periods 1 s within 3 %, the hinge's amplitude ω₀/ω, a soft weld's sag g/ω² within
10 %); rods and stubs within 1 cm; joined bodies kept apart (no contact) unless `collide_connected`; a chain of four boxes
sleeping together and woken through its joints by a push on its last link; the same jointed scene (hinge chain with a motor, a
rope, a slider) giving the same bits twice. `GpuXpbdTest`'s refusals: user constraints instead of joints.

---

### G7 as built (2026-11-01)

**Soft bodies in the world.** `create_soft_body()` is accepted. The backend keeps, per soft body slot, the abstraction's
`SoftBodyModel` (rest values from the starting positions, inverse masses, the static greedy coloring of its constraints) and
its constraints for the GPU color after color: the model's colors, then its **tethers'** (a tether moves its first particle
only: two conflict when they move the same particle or one moves the other's anchor; colored greedily as the model colors the
others, so the usual long-range attachments to a pin take one or two colors, not one each). When soft bodies are made or
destroyed, their particles are laid out again (each soft body's after the previous one's, in slot order), their constraints
gathered color by color (a color of every soft body in one pass) and the particles' state uploaded from the records (which
hold what the GPU computed, exactly); otherwise the GPU keeps them. Each step the particles (inverse masses, pins) and the
soft bodies' settings are rebuilt from the records and uploaded when their bytes changed.

**Particles are constraint nodes.** The solver's nodes are the body slots, then the particles (node = body count + p): a
particle is a body without orientation or inertia to `load_body()`/`store_body_*()`, the graph, the coloring, the joints,
the contacts and the Jacobi gather, which therefore needed no new code beyond reading the node's arrays. Each particle has a
**sphere collider** (one shape per radius) among the moving colliders, after the colliders' slots, with its soft body's
filter and friction: the moving tree, the pairs, the narrow phase (G3–G4: every shape kind, meshes, height fields,
compounds), the contact cache and the contact constraints (G5) give particle–collider contacts — two-way by construction,
the correction shared by the generalized inverse masses — and, for a soft body that collides with itself, particle–particle
contacts. `pairs_filter` keeps two particles' spheres only within one soft body that collides with itself and not when they
start within two radii (the CPU solver's rule); soft bodies do not collide with each other. A particle's contacts have no
restitution nor rolling resistance, two particles' no friction (as the CPU solver has them).

**`GpuSoftBodies`** (internal partition `:soft_bodies`, unit `gpu_soft_bodies.cpp`) owns the particle arrays — positions,
velocities, substep displacements, starting velocities, `GpuParticle` (48 B: inverse mass, flags, soft body, the body a
kinematic pin follows, starting position, pin point), `GpuSoftBody` (32 B: gravity scale, damping, radius, friction,
iterations, flags, particle range), the soft constraints (32 B) with a multiplier each, backups for replays — and records
three passes per substep: `particle_integrate` after the bodies' prediction (gravity times the soft body's scale; a particle
pinned rigidly follows its point, of the world or of its body where the body's prediction placed it), `soft_solve` per color
(STRETCH, SHEAR, BENDING, TETHER — its first particle only, beyond its rest length only —, VOLUME, DIHEDRAL with Bridson's
gradients, as the CPU solver's functions, Δλ = (−C − α̃λ)/(Σwᵢ|∇ᵢC|² + α̃), α̃ = compliance/h², the multipliers reset at each
substep), `particle_velocity_update` (the substep's displacement over h, damped by (1 − d)^h). Each iteration of a substep runs
the soft colors, then the joints and contacts: a soft body's sub-steps become ⌈its sub-steps / the world's substeps⌉
iterations per substep (§D9), the world's `position_iterations` the dynamic constraints'.

**Pins.** A pin rigid (compliance 0) to the world, or to a body that takes no correction (static, kinematic, without mass),
makes its particle **kinematic**: it follows the point (as the CPU solver holds it). A pin to a dynamic body, or a compliant
one, is a **joint of one `PIN` part** (`GpuJointPartKind::PIN`: the anchors together along the world's axes with an XPBD
compliance c, its block's compliance (c/h²) I) from the particle's node to the body's point (about its center of mass) or the
world's, after the joints' slots: two-way like any joint, colored with the joints and contacts.

**Deviations (for validation).**
1. **No separate particle tree**: particles are spheres of the moving tree, rebuilt every step like it (§A6 planned a particle
   tree refit every step and rebuilt every `particle_rebuild_period` steps; no such setting was added).
2. **Particle contacts are rigid contacts** (G5's): found once per step with the speculative margin, soft at `contact_hertz`,
   with static friction at their anchors and dynamic friction in the velocity pass. The CPU solver finds them at every sub-step,
   rigid, and undoes overlaps a sub-step starts with without velocity. A volume of four particle layers rests up to about 3 mm
   into the floor (each bottom particle holds the column above it: g/ω² = 0.3 mm per particle's weight at 30 Hz, and the
   Gauss–Seidel residual).
3. **One-sided meshes collide from their front only**, for particles as for rigid shapes (the narrow phase's rule); the CPU
   solver pushes a particle behind a one-sided triangle out in front.
4. **Self-collision skips the pairs that start within two radii** (the CPU solver's rule) rather than the pairs a constraint
   joins (§A3).
5. **Soft constraints keep XPBD multipliers** within a substep (the CPU solver, one iteration per sub-step, needs none: the
   same update for one iteration).
6. **Bodies touching or pinned to particles stay awake**: particles are in no island (soft bodies never sleep), so their
   constraints keep the rigid side's island awake.

**Found.**
- **G5's Jacobi gather lost momentum; now mass splitting** (Tonge et al. 2012). The overflow set's constraints were each
  solved with the whole bodies and every body took the *average* of its corrections: a body with more overflow constraints than
  its partners took a fraction of the impulses they took — in the Jacobi mode, a soft cube thrown at a floating box (the box
  touched by about nine particles) kept 18 % of the momentum (1.96 of 10.8 kg·m/s). Now `jacobi_gather` counts each node's
  overflow constraints after the coloring (joints and touching contacts for the position passes, touching contacts for the
  velocity pass: next item), the overflow passes solve with each side's part (inverse mass and inertia times its count:
  `split_node()`), and the gather divides the sum of the parts' corrections by the same count — the sum of the impulses its
  constraints gave it: momentum is kept (within float rounding). G6's deviation 6 no longer holds: a joint in the overflow
  set reports its own load.
- **Split among the touching contacts only.** Counting every overflow constraint, mass splitting converged more slowly than
  the plain average (a contact moves both sides C/(n₁ + n₂) instead of C/2n₁ and C/2n₂), and most of a pile's candidates
  never touch (the speculative margin is the farthest motion of any body in the step): G5's 400 balls dropped in a pile, in
  the Jacobi mode, sank to 0.12 m (13 cm into the floor) and were back to only 0.18–0.20 m after 3 s (the plain average:
  0.16 m, back to 0.239 m). `contact_prepare` now flags a contact **touching** when a point overlaps at the step's start or
  will within the step at its bodies' approach speed and half gravity's pull (`CONTACT_TOUCHING`); the counts are the joints
  and the touching contacts, and the gather divides by the same counts (momentum only needs the split and the divisor to
  agree; a contact that touches unforeseen corrects with a larger part, as a Jacobi step does): 0.185–0.19 m, back to 0.24 m
  after 1.5 s. Tried first and dropped: over-relaxing the averaged positions by 1.5 (both sides alike, so momentum held) —
  the multipliers then no longer equal the impulses: a hook reported 1/1.5 of its block's weight, a particle and a box
  held by static friction slid.
- **Pairs within their own reach; manifolds sized by the kept pairs** (G5's sizing, found by the cloth measurement: out of
  device memory on both GPUs). The trees grow every box by the step's speculative distance — the world's plus the farthest
  any body or particle gets in the step — and the manifold slots were sized from the trees' pair slots: a 64 × 64 cloth
  whose edges whip past a sphere at 9 m/s gave every particle a 15 cm margin, millions of tree pairs, and manifolds and
  contacts (176 and 304 bytes per slot, two halves each) past 1.4 GB. Now `pairs_filter` keeps a pair only when the gap
  between its boxes (shrunk back) is within the two colliders' own reach — each one's speed, its turn at its box's farthest
  corner from its body's center, gravity's pull — plus the world's speculative distance; the kept pairs have their own
  capacity (`pair_capacity`, grown by their count with a replay, like the others), and the manifold slots follow theirs
  (no longer at least the trees' pairs). The cloth's world holds 726 MiB (the test asserts under 1 GiB).
- **Light ropes hold heavy bodies loosely at the default substeps**: Gauss–Seidel couples a 0.2 kg rope and the 4 kg box it
  holds slowly (each projection moves the box by the mass ratio), and the sag left falls with the substep's square; a soft
  body's sub-steps taken as iterations converge only linearly. At the world's default 4 substeps the box hangs 3.5 cm low, at
  8 substeps 1.9 cm (the CPU solver, 8 true sub-steps of the rope, holds it within 3 cm). The test runs the world at 8
  substeps.
- `PARAMETERS` names the push constants of the pass: helpers reading particles live in `particles.glsl`, included after them.
- Bash heredocs in this shell turn `\\n` into a newline (patch scripts through the Write tool).

**Tests** (`gpu_xpbd_soft_body_test.cpp` in `gfx_physics_gpu_xpbd_tests`, each on both GPUs in both solver modes with the
validation layers): a rope pinned at one end swings down and settles under its pin, stretching under 3 % (1 % with long-range
attachments), in a world without rigid bodies; a cloth hanging from two pins settles in its plane (distance and dihedral
bending); a bent hinge returns to its rest angle (0 and 1 rad within 0.01); a tetrahedral volume rests on the floor keeping its
volume within 2 %; a particle comes to rest on each of ten shape kinds a radius above (within 1 mm), a two-sided mesh pushes an
overlapping particle out below, a one-sided one does not touch it; friction holds a particle on a 30° slope at μ 0.8 and lets it
slide as g(sin θ − μ cos θ) predicts at 0.2 (within 5 %); self-collision keeps two particles thrown at each other apart, and
without it they pass; a soft cube thrown at a floating 2 kg box pushes it and the momentum is kept within 1 %; a rope holds a
4 kg box at its end and lets it fall when unpinned; a cloth falls onto a table and rests on it, never through it; edits reach
the particles (gravity scale, a moved particle, damping, a pin to a kinematic body followed); two worlds of a cloth and a rope
holding a body give the same bits. `GpuXpbdTest`'s refusals no longer include soft bodies.

**Enumerations and constants.** `GpuJointPartKind::PIN`; `GPU_XPBD_STORAGE_BUFFERS` 176 → 192 (ten particle arrays);
specialization constants 60 (PIN) and 61–66 (`SoftBodyConstraintKind`); contact flag `CONTACT_TOUCHING`.
`GpuStepParameters` is 128 bytes (the particle arrays' indices, the particle count); `GpuSolverParameters` 240 bytes
(`node_count`, `overflow_counts`, `world_speculative_distance`, `boxes`, `pair_capacity`); collider flags `GPU_COLLIDER_LIVE`,
`GPU_COLLIDER_PARTICLE`.
Shaders: `particle_integrate`, `particle_velocity_update`, `soft_solve` (new), `particles.glsl` (new); `rotate_by()` moved to
`step.glsl`; node support in `solver.glsl` (`is_particle()`, `node_center()`, `node_orientation()`, particles in
`body_flags_of()`, `load_body()`, `store_body_*()`), `collider_poses`, `contact_prepare`, `contact_velocity`, `pairs_filter`,
`islands`, `jacobi_gather`, `joint_solve` (PIN).

**Measured** (`GpuXpbdSoftBodyMeasurementTest`: a 64 × 64 cloth — 4096 particles, self-collision on — falling onto a sphere on a
floor; Release without the validation layers): **graph coloring 14.2 ms per step on the RTX 5060, 84 ms on the Intel GPU;
Jacobi 7.5 and 74 ms** (about 28 000 contact points, 726 MiB of device memory). G6's chains did not slow down (coloring 2.8 /
4.5 ms, Jacobi 1.7 / 2.2 ms); G5's pyramid takes 3.9–4.8 / 1.4–1.8 ms on the RTX 5060 (G6: 3.6 / 1.3 — the pairs' reach and
the touching flags), and 5.1–11.2 / 2.3–7.5 ms on the Intel GPU from one run to the next (its clocks: G6's 4.2 / 2.3 sit at
the low end).

### G8 as built (2026-11-02)

**Contacts.** After the substeps, `contacts_report` flags each manifold slot that **touches** — the solver pushed its bodies
apart in a substep (`CONTACT_PUSHED`, set by `contact_solve` when a point's normal multiplier is positive) or its deepest
point started within `LINEAR_SLOP` (the reference backend's rule: a hit that bounces off within one step is reported) —
between two colliders (a particle's sphere is no collider of the interface: its contacts are not reported, though their
points count in the statistics), compacts them and writes a `GpuContactReport` per touching slot (192 B: the colliders'
slots, the fastest approach of its points at the step's start — `contact_prepare` keeps it —, then the manifold as the
narrow phase wrote it). The readback copies them; more than the reports hold → their capacity grows and the report pass runs
again in a second submission (the step's contacts are still on the GPU: no replay). The CPU (`gpu_events.cpp`) turns them
into the touching pairs, each from the smaller collider handle to the larger (the manifold flipped when the narrow phase had
them the other way), sorted, and their `ContactInfo`; the framework's `contacts_of()` reads them. **BEGIN** for a pair that
did not touch after the last step, **HIT** with it when its approach reached `hit_event_speed`, **END** for a pair of the last
step not reported now — unless its colliders still exist and its bodies all rest (static, or dynamic and asleep): the GPU no
longer looks at such pairs (G5), so they and their contacts are **carried over**, and a sleeping stack keeps its contacts and
makes no END. Colliders whose contact events are off still touch (their contacts are there) but make no event. A destroyed
collider's touches end at the next step and the dynamic bodies it touched wake (what rested on it may fall).

**Triggers.** `create_trigger()` is accepted (shapes of every kind; a compound too deep for the narrow phase is refused when
recorded, as a collider's is). Triggers are `GpuCollider` entries with `GPU_COLLIDER_TRIGGER` after the particles' spheres;
their instances follow the static colliders'. At the end of a step, when the world has triggers: the colliders' final poses,
their boxes (no speculative growth), the **moving tree rebuilt** from them, a **trigger tree**, the pairs of the trigger tree
with the moving and with the static tree, `triggers_filter` (mode 0: the collider live, not a particle's, not on the
trigger's body, both filters agree — `should_collide()`, moved into `solver.glsl`; mode 1: the kept pairs' instances), the
narrow phase with **no speculative distance**, then mode 2: a pair overlaps when one of its manifolds has a point, appended to
the results. The CPU sorts the overlaps by trigger and collider and makes **EXIT**, **ENTER** and **STAY** in each trigger's
colliders' order, trigger after trigger in slot order (the reference backend's rule, so touching counts: a zone 5 cm above
the floor does not hold it, one touching it does). A destroyed collider leaves its triggers at once (EXIT). Overflowing
trigger capacities replay the step like the solver's.

**Sleep events** come from G5 (`FELL_ASLEEP` and `WOKE_UP` from the bodies' flags read back; the rigid tests count them).

**Timestamps.** `StepTimer` (in `:resources`) writes a timestamp between phases when the queue supports them: each mark ends
the phase begun at the mark before (BROAD_PHASE: poses, boxes, trees, pairs, filter; NARROW_PHASE: manifolds, the contact
cache; SOLVER: preparation, islands, coloring, the rigid parts of each iteration, joint forces, sleep; SOFT_BODIES: the soft
colors of each iteration and the particles' passes, marked in turn with SOLVER inside the substeps; EVENTS: reports,
triggers, the readback copies), up to `GPU_XPBD_TIMESTAMPS` (256) per step (later marks time with the phase before), ticks
masked by the queue family's valid bits. After the step each phase's GPU time goes to `StepStatistics::phase_seconds`; the
CPU's reading of the events is added to EVENTS; EDITS stays the framework's (the CPU writing the edits). Without timestamps,
SOLVER is the GPU step's wall time.

**Queries.** Every `QueryEngine::BACKEND` query runs on the GPU (`gpu_queries.cpp`, `query.comp`): one at a time (a mutex), in
a command pool and buffer of its own, submitted and waited for on the calling thread. The moving tree must hold the
published poses: the step leaves it so when it built the trigger tree, otherwise the first query after a step rebuilds it
(`record_query_tree`). Mode 0 (one invocation) walks the moving and static trees — for a closest point first a **bound**
walk (the smallest distance from the point to any box's farthest corner, which some collider lies within) — and collects
the colliders whose boxes meet the query's region (the ray's box clipped to its maximum, the swept shape, the overlapping
shape's or the box) and pass its filter, and writes the region as a box record and instance. Then the narrow phase's **mid
phase alone** (`GpuNarrowPhase::pieces()`, new: the pairs' sub-pairs — convex shapes, compound children, triangles under the
region — without manifolds); mode 1 tests each piece; mode 2 keeps each collider's best piece. The CPU reads them back and
picks the nearest or sorts, as the abstraction does (ties to the smaller handle, `keep_smallest`). A piece's answer is the
CPU query's: rays on spheres, plain boxes (new, below) and triangles exact; every other answer — rays on the other convex
kinds, casts, overlaps, closest points — by the same conservative advancement from **one call site of `convex_distance()`**
in one loop (see Found). A cast or overlapping shape is written into a reserved record of the shape table
(`GpuNarrowPhase::write_primitive()`: sphere, capsule, cylinder, cone, box), or named when it is a shape of the table
(`m_shape_indices`); a hull not in the table, more than `GPU_XPBD_QUERY_PAIRS` (4096) colliders or `GPU_XPBD_QUERY_PIECES`
(16384) pieces, a walk past its stack, or a query before the first step → the backend returns nothing and the abstraction
answers (exact, as Box3D's backend does for its proxies). A GPU failure (a lost device) fails the query, `UNSUPPORTED`, and the
next step reports the error (§A13).

**Deviations (for validation).**
1. **Events are made on the CPU** from the reported contacts and overlaps (sorted, compared with last step's lists), not by an
   `events` pass (§A15 named one): the GPU compacts what touches; the comparison is a merge of two sorted lists.
2. **Resting pairs are carried over by the CPU**: the GPU skips pairs whose bodies all rest (G5), so their contacts are those
   of the step before they rested — what `contacts_of()` shows for a sleeping stack.
3. **Triggers go through the narrow phase** (a manifold with a point = overlapping, no speculative distance), with a trigger
   tree and the moving tree rebuilt from the final poses at the end of each step that has triggers (§A6/§A10 planned trigger
   pairs among the step's pairs: those use the start's poses grown by the motion).
4. **Queries one at a time**, with one set of buffers (§A12 planned slots taken atomically): concurrent queries wait for each
   other (they are short: see Measured).
5. **What the GPU cannot hold, the abstraction answers** (more than 4096 colliders or 16384 pieces, a hull not in the table,
   before the first step), as the Box3D backend does; a GPU failure fails the query (§A13).
6. **`broad_phase().structure_nodes()` is refused** (as Box3D's): the trees are not read back between steps (§A9); planned with
   the sandbox's GPU backend in G9.
7. **CPU queries changed** (they are the reference the GPU answers are compared with; Found): flat meshes and compounds'
   rays, exact rays on plain boxes.

**Found.**
- **CPU rays missed flat meshes** (`mesh_ray_cast`, `compound_ray_cast`): the ray was clipped to the shape's box, which for a
  flat mesh has no thickness, so the segment ended exactly on its plane and a triangle's own distance an ulp further was
  rejected — rays onto a level roof or a flat ground mesh missed it now and then (the GPU hit it; the CPU hit the floor 7 m
  further). The height field already widened its box by `GEOMETRIC_TOLERANCE`; now the mesh's and the compound's are widened
  too, and `StaticBvh::query_segment()` widens each node's and item's box (a flat triangle's box met where the segment ends).
  Test `ShapeQueryTest.FlatMeshesAreHitWhereverRaysCrossThem` (a square of two triangles, one-sided, two-sided, a compound's
  child, level and tilted: 2000 rays; it fails from its second ray with the old code — checked).
- **Rays on plain boxes are exact** (`box_ray_cast()`: the slabs in the box's frame, the entered face's normal; from inside a
  hit at 0 against the ray), on the CPU and the GPU (`box_ray()`): conservative advancement's normal is GJK's at the touching
  distance, whose error grows with the shape — 4–5° on the scene's 100 m floor box, one way on the CPU, another on the GPU, so
  the two never agreed within 0.99. Rounded boxes keep conservative advancement. Test
  `ShapeQueryTest.BoxRayCastsHitFacesExactly` (a 100 m floor and a small turned box: the face normal within 1e-5).
- **A query kernel with several `convex_distance()` call sites hung the RTX 5060's driver** (the device lost at the 44th
  closest-point query of a run, depending on the build): GLSL inlines every call, and GJK + EPA inlined for the ray, the cast,
  the probe, the overlap and the closest point made a kernel the driver once never finished. `test_piece()` now has one call
  in one loop (each query kind's state — travel, limit, tolerance, the probe — around it); sphere and triangle rays stay exact.
  (G4 found the same for the collide kernel's speed.)
- A query that caught the lost device and handed every later query to the abstraction let the agreement tests pass
  unchanged: the ray and shape query tests now also require some answers to differ from the CPU's in their last bits (the GPU
  answered), and a GPU failure fails the query.
- GLSL reserved words met: `filter`, `cast`.

**Tests** (`gpu_xpbd_event_test.cpp` in `gfx_physics_gpu_xpbd_tests`, each on both GPUs in both solver modes with the
validation layers): a ball landing begins (one BEGIN, a HIT at 7 m/s), its contact from the ball's side points down, its
collider destroyed ends the touch; a sleeping stack keeps its contacts (4 points per pair) without END and touches again once
pushed awake; a bouncy ball stopped and returned by a wall within one step reports BEGIN, HIT and END; a collider without
contact events touches silently; the same scene on the reference backend and on the GPU begins the same pairs within two
steps and touches the same pairs at the end; a ball through a zone enters once, stays, exits once, and leaves a zone at once
when its collider goes; a trigger on a kinematic body sweeps a row of boxes in order, skipping its own body's collider and a
filtered one; zones of several shapes crossed by kinematic bodies give the reference backend's trigger events at the same
steps, in the same order; with timestamps every phase takes some GPU time and they sum to less than the step; 400 random rays
(`ray_cast` and `ray_cast_all`) and 200 random casts, overlaps (shapes and boxes) and closest points on a scene of every shape
kind (turned convex shapes, a nested compound, a two-sided ramp, a one-sided roof, a height field, resting bodies) give the
abstraction's answers; a box only one layer sees hides from a query that masks that layer. `GpuXpbdTest` refuses a trigger
whose compound is too deep (renamed `WhatTheGpuCannotRunIsRefused`). `GpuXpbdEventMeasurementTest` (both GPUs, both modes).

**Enumerations and constants.** `GPU_XPBD_TIMESTAMPS` 256, `GPU_XPBD_QUERY_PAIRS` 4096, `GPU_XPBD_QUERY_PIECES` 16384;
specialization constant 67 (`LINEAR_SLOP`). `GpuSolverParameters` 304 bytes (reports: capacity, flags, indices, array;
triggers: count, four capacities, seven arrays), `GpuSolverCounts` 76 bytes (+ reported, touching points, five trigger
counts), `GpuContactReport` 192 bytes, `GpuTriggerOverlap` 8 bytes, `GpuQuery` 208 bytes, `GpuQueryResult` 48 bytes; collider
flag `GPU_COLLIDER_TRIGGER`; contact flag `CONTACT_PUSHED`. Shaders `contacts_report`, `triggers_filter`, `query` (new);
`contact_prepare` (the approach speed), `contact_solve` (pushed), `pairs_filter`, `collider_poses` (triggers),
`solver.glsl` (`should_collide()`, the counts' words 12–18).

**Measured** (`GpuXpbdEventMeasurementTest`: 400 balls and boxes dropped through 100 zones onto a floor; Release without the
validation layers): **a step 1.93 ms (graph coloring) / 1.37 ms (Jacobi) on the RTX 5060, 2.12 / 3.36 ms on the Intel GPU,
its events phase 0.30 / 0.49–0.54 ms** (the reports, the trigger trees and narrow phase, the CPU's events; about 1000
contact points, most of the pile resting). **A query (a ray onto the pile, a closest point beside it) takes 0.14 ms on the
RTX 5060 and 0.33–0.50 ms on the Intel GPU, against 0.001 ms for the abstraction's CPU query** on the same scene: one
submission and its wait per query dominate. A synchronous `BACKEND` query pays off only when the CPU's would test many
pieces; many queries at once need the batch API §D7 left for later (one submission for many rays). The other phases'
measurements held: G5's pyramid 4.1 ms (coloring) on the RTX 5060, G6's chains 2.5 / 1.4 ms, G7's cloth 15.1 / 7.7 ms (Intel
GPU 99 / 62 ms, which varies from run to run with its clocks).

### G9 as built (2026-11-08)

**The conformance suite** (`gfx_physics_conformance_tests`: `conformance_test.hpp` and four files of scenarios) runs the
same scenarios on every simulating backend of the build: Box3D, the reference backend, and the GPU backend on each GPU in
two configurations — **full** (a device with 64-bit atomics where the GPU has them, the automatic variants, graph coloring)
and **portable** (a device without 64-bit atomics, the portable variants forced: shared memory instead of subgroups, two
32-bit passes; the Jacobi solver mode) — with the Vulkan validation layers (any message fails the case). §0 row 12's fourth
configuration, a device without a compute queue, stays `GpuXpbdTest`'s capability test on the graphics tests' fake device
(no simulation runs there). Each backend's refusals are listed with the suite (`refused_capabilities()`: Box3D's limits,
the GPU's user constraints, the pipelines of both); a scenario needing a refused capability runs its operation through
`supports()`, which then requires the BackendCapabilityError naming the capability and the backend, and stops there; one
the backend does not refuse must succeed. The scenarios come from the Box3D, reference and Box3D soft-body tests, with the
tolerances they shared:
- rigid bodies: a world names its backend and refuses an invalid description; free fall (g, ½ g t² within 1 %, the
  published mass); a five-box stack at rest within 3 cm, asleep, its bottom box touching two colliders with three points or
  more, woken by a push within six steps; a kinematic platform reaching its targets with a crate on it; every convex kind
  dropped tilted, none into the floor; a ball spinning at 200 rad/s keeping its contact; restitution 0.8 and rolling
  resistance; a continuous ball stopped by a 5 cm wall at 150 m/s; a compound on a height field and a ball under a two-sided
  mesh; a closed mesh on a moving body and separate static and dynamic friction (where not refused); 60 thrown bodies giving
  the same bits twice and on four threads;
- joints: a pendulum's period within 2 % (eight sub-steps); a hinge's limit, a rope's length, a breaking weld, a motor's
  speed; a lid's hinge breaking at the landing whatever the sub-steps (1, 4, 8) while a hook reports its block's weight;
  six-DOF joints keeping their locked axes and user constraints holding and breaking (where not refused);
- events, queries, threads: a ball through a zone onto the floor (BEGIN, HIT, ENTER, STAY, EXIT, its contact from its side,
  END when its collider goes); a ball bouncing off a wall within a step (BEGIN, HIT, END); the backend's queries against the
  abstraction's (rays, every hit along a ray, a cast, an overlap); two asynchronous worlds answering four threads' rays with
  completed steps only; a destroyed body's collider out of the simulation; the broad phase's structure (every collider
  listed, its center in a node listing colliders; Box3D's refused); every capability honored or refused when recorded;
- soft bodies: a cloth onto a table, never through it, at rest; a rope holding a 1 kg body and letting it fall when
  unpinned; a soft cube pushing a floating body with the momentum kept within 2 %; a cloth and a rope giving the same bits
  twice and on four threads.

**Found by the suite** (its first run: 143 of 156 cases passed; the four scenarios below failed, each fixed and checked
by it):
- **A spinning ball sank through the floor on the GPU** (200 rad/s: −0.42 m, every configuration): the contacts' anchors
  were material points of the bodies, so a ball's point under it turned away within a substep (0.83 rad) and stopped
  pushing — the reference backend's defect of S3 (physics-design.md §B "Follow-up after PH8: contacts of spinning and fast
  bodies"). Now as the reference: `contact_prepare` reads the shapes' margins where they touch from the narrow phase's table
  (a convex shape's own, a compound child's, none for triangles; `GpuSolverParameters::tables`), anchors the
  non-penetration on the cores (the point moved in by the margin) and `contact_solve`/`contact_velocity` add the margins back
  along the normal (`contact_levers()`; `GpuContact` has a twentieth vector, the margins); the friction anchors stay
  material points on the surfaces (a ball rolling without slipping is not held back).
- **A continuous ball at 150 m/s went through a 5 cm wall on the GPU** (it ended at x = 5.29 m, the wall at 4.975–5.025):
  the pair was found and the contact made, but (1) the depenetration bound (`maximum_push_speed · h` per projection) applied
  to overlap the step's motion made, not only to the overlap a point started with, and (2) the contact was soft
  (`contact_hertz`: against 30 Hz a ball at 150 m/s sinks v/ω = 0.8 m). Now the bound covers the overlap the point started
  the step with only, and a point that started the step farther apart than `LINEAR_SLOP` is held rigidly (a speculative
  contact: its bodies approach until they touch, Box2D's rule, as the reference backend and Box3D do); resting contacts
  (within the slop) stay soft as before. The ball stops at 4.925 m (the reference's 4.925 m).
- **Static friction never held when μs > μd on the GPU** (a box at rest on a 20° slope, μs 0.8, μd 0.2, slid 1.7 m in 1.5 s):
  static friction was checked point by point against each point's share of the push, while a face's first point removes
  the whole body's drift; with the soft contact's partial push in the first substeps every check failed, the box slid, and
  the drift a sliding body makes never fitted the cone again (with μs = μd the velocity pass's dynamic friction stopped it,
  which is why G5's slopes held). Now `contact_solve` projects non-penetration at every point first, then holds static
  friction while the manifold's tangential multipliers stay within μs times its whole push (Coulomb's cone over the contact
  patch). The box moves 0.3 mm, then sleeps.
- **Box3D's rolling resistance was a quarter to a third of the reference's** (a ball with rolling resistance 0.02 still
  rolled at 0.73 m/s after 5 s; the reference stops it in 3.6 s): the interface's coefficient is a lever in meters (PH7: "the
  relative turn opposed up to the coefficient, a lever in meters, times the push"; the GPU's likewise), while Box3D multiplies
  its coefficient by the larger radius of the shapes. The Box3D backend now sets each shape's coefficient to the lever over
  the radius Box3D multiplies by (`set_surface_material()`: a sphere's or capsule's radius, a quarter of a hull's inner
  radius; meshes and height fields, which Box3D gives no radius, keep the lever, multiplied by the other shape's radius).
  `Friction::rolling`'s documentation now says what it is.

**Deviations (for validation).**
1. **Two GPU configurations per GPU, not four**: "full" and "portable" (§0 row 12's device without 64-bit atomics and its
   forced portable variants together, in the Jacobi mode — the other solver mode, so each configuration runs one); the device
   without a compute queue stays `GpuXpbdTest`'s capability test on the fake device (no simulation can run there).
2. **The 64-bit atomics variant is unused**: no pass uses 64-bit atomics since G8 picks a query's nearest hit on the CPU (§0
   row 6 planned them for that). `GpuXpbdVariants::integer64_atomics` and `GpuXpbdCapabilities::integer64_atomics` are reported
   only; a device without them runs the same passes (the portable configuration shows it). To decide: keep the setting as a
   report (recommended: the batch query API of §D7 may use them), or remove it.
3. **The shared scenarios moved into the suite**: the Box3D and reference test files keep what only their backend shows (Box3D:
   its factory's world, a 0.2 kg rope holding a 4 kg body; the reference: its components, its integrator's exact fall,
   discrete bodies tunneling, user constraints in detail, components replaced); the Box3D soft-body cloth, cube and determinism
   tests and the reference's shared scenarios run in the suite on every backend.
4. **The sandbox's GPU worlds share its device** (the direct entry; the example uses the factory's own device): the sandbox now
   requests a compute queue beside its graphics queue; `Scene` takes a `WorldFactory`.
5. **The GPU's contacts changed** (Found): non-penetration anchored on the shapes' cores; a point starting the step apart
   rigid; the depenetration bound on the start's overlap only; static friction per contact patch. G5–G8's tests all still
   pass (284 cases).
6. **The Box3D backend's rolling resistance converted**, and `Friction::rolling`'s documentation says what it is (a lever in
   meters): an interface comment, no interface change.
7. **The GPU's broad phase shows its trees** (closes G8's deviation 6): `PhysicsWorld::broad_phase()` is the GPU's own object
   now, its kind still `BACKEND`, its component operations refused as before.

**Tests.** The suite: 162 cases (27 scenarios × 6 backends — Box3D, the reference backend, and the GPU backend full and
portable on the RTX 5060 and on the Intel GPU), all passing after the fixes; G5–G8's GPU tests unchanged and passing (284
cases); 28 tests left the Box3D and reference executables (their scenarios are the suite's); CTest `PhysicsExample.GPU_XPBD`
and `PhysicsSandbox.GPU_XPBD` added. Full runs: Debug 1453 of 1453 (1205 s); Release 1452 of 1453, `PhysicsSandbox.Tour`
stopped by its then 300 s timeout, then passing in 174 s with 900 s. Found while running them: a build directory configured
with `cmake --preset` (compilers named `g++`) made the next `build.py` (which names them `g++.EXE`) delete the cache, and
CMake's re-run configure dropped `GFX_PHYSICS_BACKEND_GPU_XPBD=ON`: a first Debug run had no GPU backend (1039 tests).

**The GPU backend in the physics example** (`--backend gpu_xpbd`, through the factory: a device of its own on the most
capable GPU) and its CTest run `PhysicsExample.GPU_XPBD` (`--steps 240 --threads 4`; the CTest GPU resource).

**The GPU backend in the sandbox**: on the sandbox's own device (its direct entry `gpu_xpbd::create_physics_world(description,
device)`: one GPU for the frames and the physics, the steps on a compute queue the sandbox now requests beside its graphics
queue — the graphics family's when the GPU has no other); `Scene` takes a `WorldFactory` (the program's; the factory's
`create_physics_world()` when empty); the panel's backend buttons, `--physics gpu_xpbd` and the tour include it; CTest
`PhysicsSandbox.GPU_XPBD` runs the script on the first platform backend. The tour now visits 27 scenes (2430 frames paced by
the display: 232 s alone in Release on this laptop in its silent power mode, its NVIDIA display at 50 Hz), so
`PhysicsSandbox.Tour` has a 900 s timeout (300 s by default: the first Release run stopped it there). The control script's
check of refused components compares the integrator with the world's own (the GPU's is XPBD, not semi-implicit Euler).

**The GPU's broad phase shows its trees** (G8's deviation 6): the backend returns its own `BroadPhase` (`GpuBroadPhase`:
`PhysicsWorld::broad_phase()`), whose component operations are refused as the framework's description of a backend's own
broad phase refuses them (CUSTOM_BROAD_PHASE), and whose `structure_nodes()` reads the moving and the static collider trees
back between steps (with the queries' command buffer and lock; the moving tree rebuilt from the published poses first when
the step left it otherwise, as for a query) and converts them with G2's `structure_nodes()`: the moving tree's nodes, then
the static tree's, each depth first from its root, one collider per leaf (a soft body's particles are leaves of the moving
tree).

### G10 as built (2026-11-08)

**README** ([README.md](../README.md)): the library table and the status name the GPU backend; the Physics section counts
four backends; a new section **"Physics on the GPU"**: what runs where (the CPU patches, records, waits and reads back; every
phase on the GPU), a usage snippet (the application's device, `gpu_xpbd::capabilities()` and `to_string()`, the direct
entry with substeps and a solver mode, a column of boxes and a cloth, contact events counted, a ray answered by the GPU and by
the abstraction, a phase's GPU time), then the two ways in, the model (substeps, multipliers reset per substep, contacts on
cores plus margins, speculative points held rigidly, static friction over the patch, joints as parts, particles as nodes),
collision (the two Karras trees, treelets off by default, four-wide traversal, pairs counted then written, the narrow
phase's agreement with the CPU's), the solver (coloring, Jacobi with mass splitting, islands, replay on overflow),
capability probing (the requirements and the one error listing all that is missing), the variants, determinism (and its
boundary: other GPUs and drivers), events and queries (synchronous: 0.14 ms against 0.001 ms), refusals, the step times
measured in G5–G8 on both GPUs, and the conformance suite. The capability table has a GPU column and three new rows
(compounds, soft bodies, own queries; six-DOF joints and user constraints split); the options table has
`GFX_PHYSICS_BACKEND_GPU_XPBD`; the examples' paragraphs name `gpu_xpbd` and the sandbox's GPU run; the threading table, the
project layout and the tests' description name the backend. The debug-drawing snippet now catches the refusal of
`structure_nodes()` instead of testing `is_external()`, so it draws the GPU's trees too.

**Snippets compiled and run**: the GPU snippet, the debug-drawing snippet and the first physics snippet extracted into a
temporary target (`examples/readme_snippet_check`, `-Werror`), built, run (GPU: "5 contacts begun; the column's top at 4.993 m
(GPU query), 4.993 m (CPU query); narrow phase 0.043 ms" on the RTX 5060; debug drawing: "1 mesh, 56 lines"), removed.

**architecture.md**: a new **§8 "The GPU physics backend"** with five diagrams — 8.1 target and modules (flowchart), 8.2 a
world's objects (class diagram), 8.3 a step (sequence diagram: one command buffer, the timeline wait, replays, readback,
events), 8.4 the step's passes (flowchart), 8.5 the Karras tree (flowchart) — and 8.6 determinism, events, queries and the
tests; the sandbox is now §9 and build and tests §10 (references renumbered); §1's diagram and table, §7.1, §7.2's class
diagram and the build diagram name the backend. All 37 diagrams render in Mermaid 10.9.1 and 11.4.1 (`graph` is a reserved
node id in flowcharts: a node so named failed to parse in 11.4.1).

**launch.json**: the physics example's and the sandbox's backend choice (`physicsBackend`) offers `gpu_xpbd` (the GPU and
conformance test executables were added in G9).

**Deviations (for validation).**
1. **Code changed in a documentation phase**: the capability summary said a device without 64-bit atomics ran "two 32-bit
   passes for nearest hits", which G9 found no pass does; `to_string()` now prints "64-bit atomics: yes/no (reported only)"
   (and "scans, reductions and sorts" for the subgroup variant), `GpuXpbdTest.CapabilitiesAreReportedAndVariantsChosen` checks
   the new text, and the documentation of `GpuXpbdVariants::integer64_atomics`, `GpuXpbdCapabilities::integer64_atomics` and
   `GpuVariantChoice::PORTABLE` says the same.
2. **The module's documentation** (`gfx.physics.gpu_xpbd`) described phase G0 (colliders, joints, triggers and soft bodies
   refused); it now describes the backend as built.
3. **The overview's new chapter is §8**, before the sandbox (now §9), rather than a subsection of §7: the backend is a target
   of its own with its own dependencies (graphics), and the sandbox uses it.

**Tests.** No test added or removed (1453). Full runs from PowerShell with `--timeout 900`, the physics library rebuilt (a
comment of `source/physics/enums.cppm` changed): Debug 1453 of 1453 (980 s, 4 skipped), Release 1453 of 1453 (968 s, 19
skipped).

### Fix after G10: mass splitting weighted by the constraints' shares (2026-11-09)

**Found by the physics benchmark** (docs/profiler-performance-design.md, PF3: its `mixed` scene): 16 boxes of 1 m (1 000 kg)
under a cloth of 50 176 particles were pushed into the floor (y = −0.35 m by step 60), through it by step 120, and once
things flew apart the trees' pairs passed 2²⁴ and the step threw (overflow after 4 replays). The framework's CPU XPBD held
the same scene.

**Cause.** A box under the cloth touches about 400 particles: beyond the 32 colors those contacts go to the overflow set, and
in the Jacobi mode its floor contact too. G5/G7's mass splitting split every node into as many parts as it had overflow
constraints (joints and touching contacts) and gave each part the same weight: the floor contact moved the box by 1/401 of
its penetration per iteration while gravity pulled it down every substep.

**Fix** (shaders only: `solver.glsl`, `jacobi_gather.comp`, `contact_solve.comp`, `contact_velocity.comp`, `joint_solve.comp`).
A constraint unsplit would move a node by its share s = w / (w + w_partner) of their error (w: linear inverse masses, the
partner's before its own split); the shares' sum S over the node's overflow constraints of the pass is how much the node
would be moved in all. `jacobi_gather` mode 2 now stores S (float bits, positions' and velocities') instead of the counts;
`split_node(body, node, partner_inverse_mass, velocities)` splits the node into **P = 1 + max(S − 1, 0) / s** parts for that
constraint and returns 1 / P, by which the constraint scales the correction it stores; the gather sums them (no division).
Each constraint still gives both sides the same impulse (momentum kept), and the moves a node takes add up to at most its
whole correction (the parts give each constraint s / S of its error). P is the count of constraints against static and
kinematic partners, one less than the count between equal masses (equal splitting: the count), about 1 for a heavy body's
support when its other constraints are light (S near 1), and about 400 for each particle's contact with the box (the
particle sees the box heavy). A first version in proportion to the shares (P = S / s) kept the box up but made each
particle's part of the box as light as the particle: the cloth sank 5 cm into the box.

**Tests.** `GpuXpbdSoftBodyTest.AHeavyBoxUnderAClothStaysOnTheFloor` (a 1 000 kg box, a cloth of 1 600 particles falling onto
it, 3 s: the box within 1 cm of rest and upright each second, the particles on its top) failed on both GPUs in both solver
modes before the fix (the box tipped and went through the floor) and passes after it. The GPU backend's tests, the
conformance suite and the examples: 554 of 554 in Debug. The benchmark: `mixed` passes on the GPU (56.0 ms against Box3D's
44.6), the lattice's worst rod 10.2 % (16.6 % before), the pyramid 85.4 ms (88.6). Full runs (1519 tests: + 4, the new test
on 2 GPUs in 2 modes): Debug 1519 of 1519 (1124 s), Release 1519 of 1519 (1102 s).

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
