# gfx physics and camera: a decoupled physics abstraction over Box3D, and a camera module — architecture, decisions and plan

Status: **approved 2026-10-24** (every §D question as recommended, plus the math rule recorded there); **PH0 committed**
(636478b, §B "PH0 as built", deviations accepted); **PH1 committed** (527b32c); **PH2 committed** (e970d83); **PH3 committed** (e33daf0); **PH4 committed** (db6ecce); **PH5
committed** (d2b2c8e); **PH6 committed** (7856f19); **PH7 committed** (92728b6); **PH8 implemented** (§B "PH8 as built"), awaiting validation: every phase is built. This
document answers the request
"C++23 physics abstraction library … Box3D … camera system". It follows the platform and UI designs' form: §0 lists
where the request meets the existing project (each with a recommendation and a question in §D), §A gives the
architecture and decisions, §B the implementation plan, §C the API as proposed, §D the questions.

Facts checked (2026-10-24):

- **Box3D exists**: Erin Catto's 3D engine, announced on 2026-06-30 ([box2d.org](https://box2d.org/posts/2026/06/announcing-box3d/),
  [github.com/erincatto/box3d](https://github.com/erincatto/box3d)), **MIT**, library in **C17** (samples C++20), **alpha**
  ("the API will change"), tag **v0.1.0** (61 commits), pull requests disabled, CMake presets for Windows, Linux, macOS
  and **MinGW**. From its public headers (`include/box3d/types.h`, `box3d.h`):
  - shapes: sphere, capsule, convex hull (boxes through `b3BoxHull`; no box, cylinder or cone type), triangle mesh, height
    field and baked compound (**the last three static only**); several shapes per body;
  - joints: parallel, distance, filter, motor, prismatic, revolute, spherical (cone and twist limits, spring, motor),
    weld (linear and angular springs), wheel; limits, motors, springs, friction; constraint force and torque getters;
    `forceThreshold`/`torqueThreshold` raise **joint events** (no automatic breaking);
  - surface material per shape: friction (one coefficient), restitution, rolling resistance; filtering by 64-bit category
    and mask bits plus a group index; custom filter, pre-solve, friction and restitution callbacks;
  - sensors (begin/end touch events, off by default), contact begin/end/hit events, body move and sleep events;
  - queries: ray cast, shape cast, overlap, character mover casts;
  - bodies: static, kinematic, dynamic; damping, gravity scale, motion locks, per-body sleep threshold, bullet (continuous)
    flag, fast rotation; "Soft Step" sub-stepping solver, continuous collision, island sleeping, warm starting toggle;
  - threading: `workerCount`, `enqueueTask`/`finishTask` callbacks (Box2D v3's task-system hooks); cross-platform
    determinism; recording and replay; a large-world variant with double positions;
  - **no soft bodies, cloth or ropes** (the weld joint's springs "mimic soft-body simulation"; the distance joint is
    "useful for ropes"), **no generic six-degree-of-freedom joint**, **no custom shapes, solver, integrator or broad phase**.
- Alternatives: **Jolt 5.6.0** (MIT, C++17; MSYS2 packages 5.3.0) has XPBD soft bodies (cloth, volumes), six-DOF and
  cone/swing-twist constraints and user shapes; **Bullet 3.25** (zlib, MSYS2) has soft bodies; PhysX 5 (BSD-3). None is
  installed here. **glm 1.0.3** (MIT, C++17 header library, MSYS2) ships a module interface since 1.0.0.
- GCC 16 evaluates `std::sqrt`, `sin`, `tan`, `atan2`, `abs` and `fma` in constant expressions (tested, even with
  `-pedantic-errors`); C++23 does not require it (C++26 makes part of `<cmath>` constexpr), so MSVC and Clang will not.
- The repository has **no audio, asset-loader or LOD abstraction**; its libraries are core, graphics, platform, the
  platform–graphics bridge and UI.

---

## 0. Where the request meets the existing project

| # | Request | Situation | Recommendation |
|---|---|---|---|
| 1 | A GitHub repository (`commercial-physics-abstraction`), git commands to push | Project rule: you commit and push; no `gh` here. Core, toolchain workarounds, CMake modules and test infrastructure live in this repository | **`source/physics/` in this repository** (targets `gfx::physics` and `gfx::camera`, namespaces `gfx::physics` and `gfx::camera`), as was decided for the UI; the requested layout is kept inside `source/physics/`. A separate repository would duplicate core and the toolchain workarounds and fetch gfx anyway. If you want one, I prepare it and give you the commands. |
| 2 | "Fully decoupled from graphics, audio, UI, asset-loader, LOD" | Physics and camera can depend on **gfx::core** only (enumeration machinery, errors, handles), as the platform library does | **Depend on core only**, and **prove it**: a CMake check fails the build if the physics or camera targets link (directly or not) graphics, platform or UI, and a source check refuses `import gfx.` of anything but `gfx.core*`, `gfx.physics*`, `gfx.camera*`. |
| 3 | `template<class Tag> struct Handle { std::uint32_t value; };` | Core has `class Handle<Tag>` (private 32-bit value: 24-bit slot, 8-bit generation; issued by backends through `HandleKey`) — your decision: a class, never `struct Handle` | **Core's `Handle<Tag>`**, one tag per resource kind. It is trivially copyable, constexpr-constructible, comparable, hashable, formattable, has no lookup structure inside, and detects stale handles by generation. |
| 4 | `PhysicsError : std::runtime_error`; `BackendCapabilityError` a `PhysicsError` subclass | Core has one hierarchy: `Error : std::runtime_error`, and `BackendCapabilityError : Error` is `final` (unchanged for the UI) | **`PhysicsError : gfx::Error`** naming the world, body, collider, joint, operation, backend, problem and suggestion; capability gaps throw **core's `BackendCapabilityError`** with a physics backend identity (`[gfx::physics::BOX3D] …`, the same context lines), so one `catch (const gfx::BackendCapabilityError&)` works across libraries. It is then not a `PhysicsError` subclass. |
| 5 | Queries return `std::expected`; unimplemented queries `std::unexpected(PhysicsError{…})` | A `PhysicsError` owns strings: building one allocates, in a hot path | **`std::expected<T, QueryFailure>`**: `QueryFailure` is a small value (a `QueryFailureKind`, the handles concerned, a static detail), no allocation; `failure.error()` builds the full `PhysicsError` for callers that want to throw it. A ray that hits nothing is a failure of kind `NO_HIT`. |
| 6 | `glm` for math | Core has small value types (`Vector2`, `Extent`, …). glm is a C++ header library: like msdfgen's headers (§6.11 of the project memory) its standard-library includes are expected to conflict with `import std` in a module unit (not tested); its module interface is unverified with GCC 16 | **Own constexpr math in core** (`gfx.core.math`: `Vector3`, `Vector4`, `Quaternion`, `Matrix3`, `Matrix4`, `Transform`), with `if consteval` implementations of square root and trigonometry so that compile-time use is portable (GCC folds `<cmath>`, MSVC and Clang do not yet). An optional header converting to and from glm can come later. |
| 7 | All enumerations in one `enums.cppm`; the camera "in its own module" but its enumerations "in the root `enums.cppm`" | Project rule: one enumerations file per library | **One library root `source/physics/`** holding both modules: `enums.cppm` (module `gfx.physics.enums`) has the physics **and** camera enumerations; the camera module (`gfx.camera`) imports only the enumerations, handles and geometric primitives, never a physics interface. Three targets: `gfx::physics_common` (enumerations, handles, errors, primitives), `gfx::camera` and `gfx::physics`, both on `physics_common`. |
| 8 | Box3D as the backend; "if the intended library is Bullet/Jolt/PhysX/Box2D-derived, treat it as that" | Box3D is real (above): MIT, C, alpha, no soft bodies, a fixed joint set, nothing pluggable | **The real Box3D, pinned** (tag v0.1.0 or a later commit, by URL and SHA-256), its C headers only in the backend's global module fragments. What it lacks throws `BackendCapabilityError` (§A11), except what the abstraction provides itself (next row). Jolt is the natural second backend later. |
| 9 | Soft bodies (PBD/XPBD, cloth, rope, tetrahedral) on the Box3D backend | Box3D has none | **The abstraction's reference XPBD solver** simulates soft bodies next to a Box3D world, colliding with its bodies' colliders through the abstraction's own narrow phase, two-way (impulses applied to Box3D bodies). The alternative reading — Box3D throws for soft bodies — leaves cloth, rope and volumes unusable until a second backend exists. |
| 10 | Sequential-impulse solver, broad phase, narrow phase and integrator as pluggable components; Box3D lets none be replaced | A component interface nothing can plug into is untestable | A **reference backend** composed only of the abstraction's components (`ReferenceWorld<BroadPhase, NarrowPhase, Solver, Integrator>`): sequential impulses, the spatial structures, GJK/EPA, the integrators. It proves the component contracts end to end and is the real template for custom backends; the null backend stays the template of the error contract. Proposed as a phase of its own (§B). |
| 11 | "Custom user backend must always be possible", a `PHYSICS_BACKEND_*` CMake hook, and "no system-wide globals" | The graphics and platform factories import compiled-in backends; a runtime registry would be a global | **Two ways, no global**: (a) a custom backend derives from the interfaces and the application constructs it directly; (b) the CMake option `GFX_PHYSICS_CUSTOM_BACKEND_TARGET` names a target that provides module `gfx.physics.backends.custom` (`unavailable_reason()`, `create_world()`), which the factory imports for `PhysicsBackendKind::CUSTOM`. Options `GFX_PHYSICS_BACKEND_BOX3D` and `GFX_PHYSICS_BACKEND_NULL`. |
| 12 | `PhysicsDesc`, `CameraDesc`, `create_camera(CameraDesc, CameraBackendKind)` | Naming rule: full words, `<Thing>Description` | `PhysicsWorldDescription`, `CameraDescription`, `create_physics_world(description, kind)`, `create_camera(description, kind)`; `Aabb`, `Obb`, `Bsp`, `Gjk`, `Epa`, `Xpbd` are acronyms (allowed). |
| 13 | CI on Linux, Windows and macOS | Only Windows/MinGW GCC 16 here; the project's step 6 is CI with MSVC, Clang and Linux | **No workflow file now** (it could not be run here); physics joins the project-wide CI of step 6. |
| 14 | "Compile time over run time" | — | Two layers for every component: **concepts** for compile-time composition (the reference backend and the spatial structures are templates: no virtual call in hot loops) and **virtual interfaces** for run-time selection (the factory, user plug-ins); shape and joint descriptions, convex hulls from point sets, ragdoll topologies and structure parameters are `constexpr`/`consteval`-checked when known at compile time. |

---

## A. Architecture and design decisions

### A1. Why decoupled, and how it is consumed

Physics answers questions about bodies in space; it has no reason to know how they are drawn, heard or streamed. Each
coupling would make a headless server, a tool or a test pull in a GPU device, a window or an audio mixer. So:

- `gfx::physics` and `gfx::camera` link `gfx::core` (and their own `gfx::physics_common`) and nothing else of gfx; the
  graphics, platform and UI libraries do not link physics. The checks of §0 row 2 fail the build otherwise.
- What crosses into other systems is plain values: transforms (`Transform`: position + unit quaternion), matrices
  (`Matrix4`, column-major floats), contact and trigger events. A renderer copies a camera's `view_projection()` into its
  push constants; a game copies body transforms into its scene. Neither library calls back into the engine.
- The camera does not depend on physics either: it imports the enumerations, the handles and the primitives (`Frustum`,
  `Ray`, `Aabb`, `Sphere`, `Obb`) of `physics_common`, which are geometry, not simulation.

### A2. Object model

```mermaid
classDiagram
    direction LR
    class PhysicsWorld {
        +step(seconds)
        +begin_step(seconds) / wait_step()
        +create_rigid_body / soft_body / collider / joint / constraint / trigger
        +ray_cast / shape_cast / overlap / contacts
        +events() TriggerEvent, ContactEvent, JointEvent
        +broad_phase() / narrow_phase() / solver() / integrator()
        +deleter() / profiler()
    }
    PhysicsWorld "1" *-- "n" RigidBody
    PhysicsWorld "1" *-- "n" SoftBody
    RigidBody "1" *-- "n" Collider
    Collider --> Shape : shares
    PhysicsWorld "1" *-- "n" Joint
    Joint --|> Constraint
    PhysicsWorld "1" *-- "n" Constraint
    PhysicsWorld "1" *-- "n" Trigger
    Trigger --|> Collider
    PhysicsWorld o-- BroadPhase
    PhysicsWorld o-- NarrowPhase
    PhysicsWorld o-- Solver
    PhysicsWorld o-- Integrator
    BroadPhase o-- SpatialStructure
```

- **`PhysicsWorld`** owns the simulation state of one world; several worlds coexist (no globals). The application owns
  every object it creates through `std::unique_ptr` (RAII, §A15); the world outlives them.
- **`Shape`** is immutable geometry, shareable between colliders (a hull used by a thousand crates is built once).
  **`Collider`** attaches a shape to a body with a local transform and a material (friction, restitution, rolling
  friction, density), a collision filter (layers, mask, group), the trigger flag and the constraint compatibility
  (whether joints may connect to it). A **`Trigger`** is a collider that reports overlaps instead of colliding.
- **`Constraint`** is what the solver orders: contacts, joints, soft-body attachments and user constraints, each with a
  `ConstraintKind`. **`Joint`** is the two-body constraint the application configures (`JointKind`: distance, hinge, ball
  and socket, slider, fixed, cone-twist, six-DOF, …; §A8).
- **`BroadPhase`, `NarrowPhase`, `Solver`, `Integrator`** are the world's pipeline. A backend reports its own (Box3D:
  `BroadPhaseKind::BVH`, `SolverKind::SOFT_STEP`, `IntegratorKind::SEMI_IMPLICIT_EULER`) and refuses replacements it
  cannot honor; the reference backend accepts any. **`SpatialStructure`** (BVH, octree, quadtree, BSP, uniform grid, sweep
  and prune) is what a spatial broad phase is built on, and is usable alone (game queries, culling).

### A3. Handles

- Core's `Handle<Tag>`, one tag per resource: `WorldTag`, `RigidBodyTag`, `SoftBodyTag`, `ColliderTag`, `ShapeTag`,
  `ConstraintTag`, `JointTag`, `TriggerTag`, `SpatialStructureTag`, `SpatialProxyTag` (an item inside a structure),
  `BroadPhaseTag`, `NarrowPhaseTag`, `SolverTag`, `IntegratorTag`, `ContactManifoldTag`, `CameraTag`,
  `CameraControllerTag`; aliases `RigidBodyHandle`, …; concepts `PhysicsHandle` (any of them), `BodyHandle` (rigid or soft),
  `ColliderHandle`-like and so on, as the request lists.
- **No maps**: a handle is a slot index and a generation into the issuing world's tables (core's thread-safe
  `HandleTable`, lock-free lookups). A stale handle is detected by its generation and reported (`InvalidHandleError` in
  operations, `QueryFailureKind::INVALID_HANDLE` in queries).
- **Backend internals stay private**: the Box3D backend's records hold `b3BodyId`, `b3ShapeId`, `b3JointId` (Box3D's own
  generation-checked ids); they appear in no exported declaration. User data pointers of Box3D carry our slot index, so
  events map back to handles without a search.
- Contact manifolds, events and query results carry handles, never pointers into backend storage.

### A4. Queries and errors in hot paths

`ray_cast`, `shape_cast`, `overlap`, `closest_point`, `contacts_of` return `std::expected<T, QueryFailure>` (`T`: a hit,
a span of hits written into caller storage, or a count). Failures: `NO_HIT`, `INVALID_HANDLE`, `STEP_IN_PROGRESS`,
`UNSUPPORTED` (with the capability), `CAPACITY_EXCEEDED`. Queries never throw and never allocate (results go into caller
spans or callbacks). Everything else throws (A16).

### A5. Broad phase

- **Contract**: `insert(collider, Aabb, CollisionFilter) → proxy`, `remove(proxy)`, `move(proxy, Aabb, displacement)`,
  `update()` (once per step), `pairs()` → the candidate pairs whose fattened bounds overlap and whose filters accept each
  other, **sorted** by (smaller collider handle, larger) — reproducible whatever the structure or the thread count;
  `new_pairs()` / `ended_pairs()` for contact bookkeeping.
- **Filtering**: 64-bit category and mask bits plus a group (Box3D's model: a positive shared group always collides, a
  negative one never), and an optional custom filter callable.
- **Incremental**: bounds are fattened (margin and displacement prediction), so a proxy moves in the structure only when
  it leaves its fattened box.
- **Any structure**: `SpatialBroadPhase<Structure>` adapts any `SpatialStructureConcept` (A7); the structure enumerates
  overlapping proxy pairs (its own traversal: tree against tree for BVH and BSP, per cell for grids, octrees and
  quadtrees, the sweep for SAP), the adapter filters, deduplicates and sorts.
- **Parallel**: the pair search is split into tasks over the task scheduler (A14); each task writes its own buffer, the
  buffers are merged in task order and sorted — the same pairs for one thread or sixteen.

### A6. Narrow phase

- **Convex shapes** satisfy `ConvexShape`: `support(direction) → point` and a `margin` (the radius of rounded shapes:
  sphere, capsule, rounded hull), plus their bounds. GJK works on the margin-free core.
- **GJK** answers intersection and distance (with closest points and the simplex); **EPA** grows the GJK simplex into
  the penetration depth and normal when the cores overlap; with margins, shallow contacts come from GJK distance alone
  (the common case, and the fast one).
- **Manifolds**: from the separating normal, the reference and incident features (faces of hulls, segments of capsules)
  are clipped against each other to give up to four points, each with a **feature id** (the pair of features that made
  it). The manifold cache keeps last step's points by feature id with their accumulated impulses for warm starting, so a
  box resting on the ground keeps the same four contacts frame after frame.
- **Analytic fast paths** where they are exact and cheaper: sphere–sphere, sphere–capsule, capsule–capsule,
  sphere–box, box–box (SAT: the one place a separating-axis test is the reference), segment–triangle. **SAT is otherwise
  a fallback** (triangle against triangle in mesh–mesh).
- **Meshes**: a BVH over the triangles; convex–mesh runs GJK against each candidate triangle; mesh–mesh traverses both
  BVHs and tests triangle pairs; height fields enumerate the cells under the other shape's bounds; compounds recurse
  into their children (through their own BVH when large).
- **Cylinders and cones** are convex shapes with exact support functions in the abstraction; a backend without them
  (Box3D) receives a hull approximation whose segment count the collider description sets.
- `NarrowPhase` is the interface (`collide(pair, previous manifold) → manifold`, queries per shape pair); the reference
  implementation is the abstraction's; a backend that has its own reports it.

### A7. Spatial structures

All six are written against the abstract primitives only (`Aabb` for storage; `Ray`, `Segment`, `Sphere`, `Obb`,
`Frustum` for queries through the primitives' intersection functions), so they are the same code under Box3D, the
reference backend, culling or game logic.

| Structure | Storage | Updates | Notes |
|---|---|---|---|
| BVH | dynamic AABB tree, fattened leaves, surface-area insertion, rotations | incremental | the default broad phase; also built bottom-up for static meshes |
| Octree | loose octree, `constexpr` depth and looseness | incremental | uniform 3D distributions |
| Quadtree | loose quadtree over two axes (`constexpr` plane: XZ by default), the third axis ignored | incremental | flat worlds |
| BSP | splitting planes chosen by surface-area heuristic, straddling items kept at the node | incremental inserts and removals, rebuilt when unbalanced | static-heavy scenes |
| Uniform grid | hashed cells of `constexpr` size | incremental | many similar small objects; soft-body self-collision |
| Sweep and prune | sorted endpoints on one or three axes, insertion sort per update | incremental, coherent | small, coherent scenes |

`SpatialStructureConcept` requires `insert`, `remove`, `update`, `query(Aabb | Ray | Segment | Frustum | Sphere, visitor)`,
`iterate(visitor)` and `pairs(visitor)`; `SpatialStructure` is the virtual interface wrapping any of them for run-time
choice. A test runs the same scene through all six and compares their pairs and query results with brute force.

### A8. Solver and constraints

- **Reference solver: sequential impulses** with accumulated impulses and **warm starting** (from the manifold cache and
  last step's joint impulses), a configurable velocity iteration count, **position correction** by non-linear Gauss–Seidel
  iterations (no Baumgarte energy gain), restitution above a **restitution threshold**, friction with **static** and
  **dynamic** coefficients (the static one while the sliding speed is under a threshold) and **rolling** friction (an
  angular constraint), **sleeping** by islands whose velocities stay under per-body thresholds for a time.
- **Determinism**: constraints are ordered by (`ConstraintKind`, smaller handle, larger handle, feature id) before
  iterating; islands are found in handle order; parallel solving is by graph colouring with a fixed colour order.
- **One `Joint` interface, many kinds**: distance (modes **rod**, **spring**, **rope** (a maximum length only), **stub**
  (meaning to confirm, §D), hinge (1 DOF), ball and socket (3 DOF), slider (1 DOF), fixed, cone-twist (2 DOF limits),
  generic six-DOF (per-axis free, limited or locked), and motor/servo drives on any free axis (velocity or position
  target with maximum force); **limits** per axis with softness; **breakable** joints (force and torque thresholds: the
  joint is disabled, reported by a `JointEvent`, and its object stays valid until the application destroys it).
- `Solver` exposes the reference (`SolverKind::SEQUENTIAL_IMPULSE`) and reports a backend's (`SOFT_STEP` for Box3D), with
  `SolverDescription` (iterations or sub-steps, warm starting, thresholds); per-constraint settings live on the joint.

### A9. Soft bodies

- **Reference path: XPBD** (extended position-based dynamics with compliance, so stiffness does not depend on the
  iteration count or time step): particles with inverse masses; stretch and shear distance constraints, bending
  (dihedral), tetrahedral volume constraints, long-range attachments; sub-steps; damping.
- **Volumetric**: a tetrahedral mesh given by the application (positions and tetrahedra), with volume preservation and
  per-tetrahedron compliance. A backend with native volumes (Jolt) may run it natively; Box3D cannot, so the reference
  path runs it.
- **Cloth and rope are presets**, not separate APIs: `cloth_preset(columns, rows, spacing)` and `rope_preset(segments,
  length)` produce a `SoftBodyDescription` (particles, constraints, default compliances) that the application may still
  edit.
- **Pinning**: a particle pinned to a point of the world or to a point of a rigid body (an attachment constraint);
  **collision with rigid bodies**: particle spheres against the colliders through the abstraction's narrow phase,
  friction, two-way (the impulse is applied to the body through the backend); **self-collision** toggled per soft body
  (a uniform grid over the particles); stiffness given as compliance, damping per body.

### A10. Rigid bodies and colliders

- `BodyKind`: static, kinematic, dynamic. Mass from the colliders' densities or given; **inertia tensor** computed
  (principal moments and frame) or given; **center of mass** computed or given; linear and angular damping, gravity scale,
  sleep thresholds, `MotionQuality` (discrete or continuous), per-axis motion locks.
- Colliders (the "tools layer"): sphere, capsule, cylinder, cone, box, **convex hull built from points** (quickhull;
  `consteval` for point sets known at compile time, with the hull's capacity as a template argument), triangle mesh,
  height field (`std::mdspan` of heights), compound. Each carries friction (static, dynamic, rolling), restitution,
  density, filter, trigger flag and constraint compatibility.
- **Triggers** emit `ENTER`, `STAY` and `EXIT` events into the world's `TriggerEvent` queue (read after a step; `STAY`
  is produced every step while an overlap lasts — Box3D reports begin and end only, the backend keeps the overlap set).

### A11. Integrators

- A separate interface because integration is a choice independent of collision and constraints (a soft body's
  stability, a particle system's cost), and because some backends want one supplied. `Integrator` works on spans of
  particle or body states (structure of arrays: positions, velocities, inverse masses, accumulated forces, previous
  positions): `predict(state, seconds)` and `finalize(state, seconds)` — one step for explicit schemes, the prediction and
  velocity update around constraint projection for XPBD — plus `requirements()` (previous positions, force evaluations
  per step).
- Implementations: **semi-implicit Euler**, **Verlet** (position Verlet), **RK4** (with a force-field callable evaluated
  four times), **XPBD** (prediction and velocity update; projection is the soft-body solver's).
- **Backends that do not take one still conform**: a Box3D world's `integrator()` describes Box3D's own integration
  (`IntegratorKind::SEMI_IMPLICIT_EULER`, `is_external() == false`); asking it to integrate rigid bodies with another
  throws `BackendCapabilityError`. The reference soft bodies under Box3D accept any integrator meeting their requirements.

### A12. The Box3D backend

| Request | Box3D | In the backend |
|---|---|---|
| Rigid bodies (kinds, mass, damping, sleep, CCD, locks) | yes | mapped one to one (`isBullet` for continuous) |
| Sphere, capsule, hull, box | yes (box = `b3BoxHull`) | mapped; boxes as box hulls |
| Cylinder, cone | no type | hulls built by the abstraction (segment count in the description) |
| Triangle mesh, height field, compound | **static only** (runtime compounds = several shapes on a body) | static: mapped; on a dynamic or kinematic body: `BackendCapabilityError` (suggestion: convex decomposition into hulls, or a compound of hulls) |
| Static and dynamic friction | one coefficient | equal coefficients accepted; different ones: `BackendCapabilityError` |
| Rolling friction, restitution, restitution threshold | yes | mapped |
| Layers, masks, groups, custom filter | yes (64-bit) | mapped |
| Distance (rod, spring, rope), hinge, ball and socket, slider, fixed | distance, revolute, spherical, prismatic, weld | mapped |
| Cone-twist | spherical joint with cone and twist limits | mapped |
| Generic six-DOF | no | `BackendCapabilityError` (suggestion: the joint kinds above, or a backend with six-DOF joints) |
| Motors, servos, limits, springs | yes | mapped per joint kind |
| Breakable joints | thresholds raise joint events | the backend disables the joint on its event and reports `JointEvent::BROKEN` |
| Triggers | sensors (begin and end) | mapped; `STAY` synthesized |
| Ray, shape and overlap queries | yes | mapped; plus the abstraction's own (BVH + GJK) on request |
| Soft bodies | no | the abstraction's XPBD next to the Box3D world (§0 row 9) |
| Replacing broad phase, narrow phase, solver, integrator | no | the backend reports its own; replacing throws `BackendCapabilityError` |
| Determinism, task hooks | yes | `enqueueTask`/`finishTask` forwarded to the world's task scheduler |

A custom backend implements the same interfaces (the null backend shows every operation and the error it throws when
unsupported) and is selected as §0 row 11 says. If Box3D keeps Box2D's process-wide settings (allocator, assertion
handler), the backend leaves them at their defaults (to verify when it is wrapped).

### A13. Camera module

- **Abstract `Camera`** with a standard implementation and room for custom ones (off-axis stereo, oblique near planes,
  jittered projections); `create_camera(description, CameraBackendKind::STANDARD)`; every matrix function is also a
  `constexpr` free function usable without any camera object.
- **Conventions**: right-handed world and view space; the camera looks along −Z with +Y up in view space; orientation is
  a **unit quaternion** (Hamilton convention, w first; rotations compose right to left, `q2 * q1` applies `q1` first;
  vectors rotate as `q v q*`), renormalized after each change. Yaw (about +Y), pitch (about +X) and roll (about −Z) are
  **read from** and **built into** quaternions (`set_orientation_from(yaw, pitch, roll)`, `rotate(axis, angle)`,
  `look_at(target, up)`); no Euler state is kept, so no gimbal lock can appear in the camera itself.
- **Projections**: perspective (vertical field of view, aspect ratio, near, far or infinite far, reverse depth) and
  orthographic (width and height or the six bounds, near, far, reverse depth). Clip conventions are the caller's choice
  in the description — depth range `ZERO_TO_ONE` (default: Vulkan, Direct3D, Metal, WebGPU) or `NEGATIVE_ONE_TO_ONE`
  (OpenGL), normalized-device y up (default) or down (Vulkan without a flipped viewport) — so the camera serves any
  renderer without knowing it.
- **Outputs**: `view`, `projection`, `view_projection`, `inverse_view`, `inverse_projection`, `inverse_view_projection`
  (`Matrix4`, column-major); `frustum()` (six world-space planes, normals inward, extracted from the view-projection
  matrix with the depth convention applied); culling helpers for spheres, AABBs and OBBs (`OUTSIDE`, `INTERSECTING`,
  `INSIDE`); `screen_to_world_ray(point, viewport)` (unprojects the near and far points; parallel rays for orthographic
  cameras; reverse and infinite depth handled).
- **Unit-agnostic**: no unit is assumed anywhere (near and far are whatever the caller's unit is).
- **`CameraController`** (optional; free-fly, orbit, first-person) updates a camera from a `CameraControllerInput` of plain
  values (movement, look deltas, zoom, seconds) — not from the platform library's events, which the application
  translates — and is where limits such as pitch clamping live.

### A14. Threading

- **Asynchronous stepping**: with `PhysicsWorldDescription::asynchronous_stepping`, the world owns one `std::jthread`
  worker (stopped through its `std::stop_token` when the world is destroyed); `begin_step(seconds)` hands the step to it
  and returns at once, `wait_step()` waits for its end, `step(seconds)` does both. Without it, steps run on the caller.
- **Deferred edits**: every mutation (create or destroy a body, set a transform or a velocity, add a force or an impulse,
  change a joint) is **recorded and applied at the start of the next step**, in the order recorded — one rule whatever
  the timing, so a replay with the same edits gives the same result. New objects get their handles at once; getters
  return the state of the last completed step.
- **Parallel hooks**: `TaskScheduler` (the application's job system: `parallel_for(count, minimum_range, task)` and
  `wait(group)`) given in the description; the reference broad and narrow phases split their work over it with
  deterministic merges; Box3D's `enqueueTask`/`finishTask` are forwarded to it; without one, work runs on the stepping
  thread. Single-threaded backends conform by ignoring it.
- **Read-only queries from any number of threads while no step runs**, through a reader handshake: a query increments
  the world's reader count and then reads the stepping flag; `begin_step` sets the flag and then waits for the reader
  count to reach zero. Both pairs of operations are `memory_order_seq_cst` (a store-then-load on each side, which weaker
  orders do not order), so a query that started before a step finishes before the step mutates anything, and a query
  that starts during a step returns `STEP_IN_PROGRESS` without blocking. The end of a step is a release store; queries
  and getters acquire it, so they see every effect of the completed step.
- **No globals**: every piece of state belongs to a world (or a camera); two worlds step concurrently on two threads.

### A15. RAII and deferred deletion

Every object is created by its parent and owned by the application (`std::unique_ptr`): destruction destroys. During a
step (or while queries hold the world), destruction is queued to the world's **deleter**, drained at the start of the
next step after the deferred edits. Objects hold a weak lifetime token of their world: after the world is gone their
operations throw `PhysicsError` ("the world was destroyed") instead of touching freed memory. Joints whose body was
destroyed are detached: their handle no longer resolves, and their operations throw naming the destroyed body. Objects
are non-copyable and movable where a moved-from state is meaningful (descriptions and value types; world objects are
held by `unique_ptr`).

### A16. Errors

- `PhysicsError : gfx::Error`: message `[gfx::physics::BOX3D] RigidBody::set_mass_properties: <problem>` followed by
  `World: World(index 0, generation 1)`, `Body: RigidBody(index 12, generation 3)`, `Collider: …`, `Reason`, `Suggestion`,
  `Location`; accessors for the handles.
- Capability gaps: core `BackendCapabilityError` with the physics backend identity, the capability name (an enumerator of
  `PhysicsCapability`), the same context lines and a suggestion (§0 row 4).
- Queries: `QueryFailure` (§A4). Destructors never throw.

### A17. Modules and layout

```
source/physics/                      gfx::physics_common, gfx::physics, gfx::camera
  enums.cppm                         gfx.physics.enums — every physics and camera enumeration and constant
  handles.cppm                       gfx.physics.handles — tags, aliases, concepts
  errors.cppm                        gfx.physics.errors — PhysicsError, QueryFailure, throw helpers
  primitives/                        gfx.physics.primitives — aabb, sphere, obb, convex_hull, ray, segment, frustum (+ plane)
  camera/interface/                  gfx.camera — camera.cppm, camera_controller.cppm (+ standard implementations)
  camera_factory.cppm                gfx.camera.factory — create_camera()
  interface/                         gfx.physics.interface — physics_world, rigid_body, soft_body, collider, shape,
                                     constraint, joint, broad_phase, narrow_phase, solver, integrator,
                                     spatial_structure, trigger, query (+ deleter, profiler, task_scheduler)
  narrow_phase/                      gfx.physics.narrow_phase — gjk, epa, manifold (+ sat, analytic, mesh)
  spatial/                           gfx.physics.spatial — bvh, octree, quadtree, bsp, uniform_grid, sweep_and_prune
  dynamics/                          gfx.physics.dynamics — integrators, sequential impulses, XPBD (reference components)
  backends/box3d/, backends/null/    private file sets, imported only by the factory
  physics_factory.cppm               gfx.physics.factory — create_physics_world()
source/core/math.cppm                gfx.core.math — vectors, quaternion, matrices, transforms (constexpr)
```

`.cppm` modules throughout (`import std;`), headers only where a module cannot serve (Box3D's C headers in the backend's
global module fragments, as SDL's are). `import gfx.physics;` re-exports everything public except the backends;
`import gfx.camera;` re-exports the camera and the primitives it uses.

### A18. Naming

As the project: snake_case functions, PascalCase types and concepts, `m_` members of classes (none in description
structs), SCREAMING_SNAKE_CASE enumerators and constants in `enums.cppm` only, full words (`PhysicsWorldDescription`,
`MAXIMUM_`, `minimum_range`), acronyms allowed (`Aabb`, `Obb`, `Bsp`, `Gjk`, `Epa`, `Xpbd`, `Bvh`), names that platform
headers define as macros avoided.

---

## B. Implementation plan

Each phase is built, tested (Debug and Release, every test in CTest), reported and validated before the next, as before.

| Phase | Content | Tests |
|---|---|---|
| **PH0 Foundations** | `gfx.core.math`; `source/physics` targets and options; enumerations, handles, errors, `QueryFailure`; primitives with constexpr constructors and intersection tests (point, Aabb, Sphere, Obb, Ray, Segment, Frustum, Plane); the decoupling checks | math and primitives at compile time (`static_assert`) and run time; compile-failure tests (handles of different kinds, decoupled imports) |
| **PH1 Camera** | `Camera`, standard perspective/orthographic implementation, clip conventions, frustum extraction, culling, `screen_to_world_ray`; controllers (free-fly, orbit, first-person); `camera_factory` | matrices against hand-computed values for every convention; round trips (project then unproject); culling of boxes on both sides of every plane; constexpr cameras |
| **PH2 Shapes and narrow phase** | convex shapes and support functions, quickhull (`consteval` variant), GJK, EPA, analytic pairs, SAT box–box, manifolds with feature ids and the manifold cache; mesh BVH, convex–mesh, mesh–mesh, height fields, compounds | against analytic answers (distances, depths, normals) over randomized poses; manifold stability (feature ids kept across small motions) |
| **PH3 Spatial structures and broad phase** | BVH, octree, quadtree, BSP, uniform grid, sweep and prune; `SpatialBroadPhase<Structure>`, filtering, deterministic pairs, parallel pair search | every structure against brute force (pairs and queries) over thousands of moving boxes; identical pairs with 1 and N threads |
| **PH4 Interfaces, null backend, threading** | every interface with throwing defaults; null backend; factory; deferred edits, deleter, asynchronous stepping, reader handshake, task scheduler | the error contract on every operation; edits applied in order; queries from threads during and between steps |
| **PH5 Box3D backend** | Box3D fetched and pinned; worlds, bodies, colliders, joints, triggers (`STAY`), events, breakable joints, queries, task hooks; capability errors of §A12 | simulations with known outcomes (free fall, resting stacks, pendulums against analytic periods, joint limits), determinism (two runs, same bits), each capability error |
| **PH6 Integrators and soft bodies** | semi-implicit Euler, Verlet, RK4, XPBD; XPBD soft bodies (cloth, rope, tetrahedral volumes), pinning, rigid coupling with Box3D, self-collision | integrators against analytic orbits and springs (energy drift bounds); cloth hanging from two pins settles; a rope's length holds; two-way coupling moves a Box3D body |
| **PH7 Reference backend** | `ReferenceWorld<BroadPhase, NarrowPhase, Solver, Integrator>` with sequential impulses (warm starting, friction models, restitution threshold, sleeping), its joints, the factory's `REFERENCE`… or `CUSTOM` slot (per your answer) | the PH5 scenarios on the reference backend; swapping each component |
| **PH8 Sample and documentation** | `examples/physics` (headless: the request's usage example, asserting the step, the query and the camera math; CTest); README sections; THIRD_PARTY_LICENSES.md (Box3D); architecture overview with diagrams | the sample under CTest |

### PH0 as built (2026-10-24)

**Math in core** — module `gfx.core.math` (`source/core/math.cppm`, in target `gfx::core`, not in the `gfx.core` umbrella:
`import gfx.core.math;`). Conventions: right-handed, column vectors, column-major matrices (as graphics APIs and cglm),
Hamilton quaternions, yaw/pitch/roll = intrinsic Y-X-Z (roll applied first). Types: `Vector3` (12 bytes), `Vector4`,
`Quaternion` (identity by default), `Matrix3`, `Matrix4` (identity by default), `YawPitchRoll`, `Transform` {position,
orientation}. Every function is `constexpr` and has two paths through `if consteval`:

- **compile time**: own code — `square_root` (Newton in double), `sine`/`cosine`/`tangent`/`arc_tangent`/`arc_sine`/
  `arc_cosine` (range reduction + series), cofactor inverses — so constant expressions work with every compiler, not
  only with GCC's folding of `<cmath>`;
- **run time**: `<cmath>` for scalars and **cglm 0.9.6** for the 4-wide types (`Matrix4` products, matrix × vector,
  transpose, determinant, inverse, rigid inverse; `Matrix3` products; quaternion products and normalization; `Vector4`
  arithmetic, dot, minimum/maximum), the values copied to and from cglm's arrays with `std::bit_cast` (same layouts,
  static-asserted). cglm's `static inline` functions are included in the module's global module fragment
  (`CGLM_ALL_UNALIGNED`): GCC 16 warns that exported inline functions expose TU-local entities; verified to compile, link
  and run correctly, so `-Wno-expose-global-module-tu-local` is set on `math.cppm` only.
- **batches** (SIMD over many elements): `transform_points` / `transform_vectors` (by a `Matrix4` or a `Transform`) and
  `multiply_add` (floats, `Vector3`s), on kernels of our own written with **SIMDe 0.8.2**'s SSE intrinsics in a plain
  translation unit (`math_kernels.cpp` behind the std-free `math_kernels.hpp`: SIMDe's headers include `<stdio.h>` and
  `<memory>`, which cannot share a unit with `import std`, §6.11 of the project memory). Short outputs throw
  `InvalidUsageError`; constant expressions take the scalar path.

`GFX_SIMD_INSTRUCTION_SET` (cache variable: `SSE2` default, `AVX2`, `NATIVE`) feeds the INTERFACE target
`gfx::simd_options`, linked PUBLIC by core so inline SIMD code is compiled alike in consumers. It also sets
**`-ffp-contract=off`**: no fused multiply-add is formed behind our back, so the scalar and SIMD paths (and builds with
different instruction sets) compute the same bits — the batch tests compare them exactly.

**Physics foundations** — `source/physics/`, option `GFX_BUILD_PHYSICS` (ON), target **`gfx::physics_common`**
(`gfx_physics_common`: PUBLIC `gfx::core`, PRIVATE SIMDe); `gfx::camera` and `gfx::physics` come with PH1 and PH4.

- `gfx.physics.enums` (`enums.cppm`, generated): 22 enumerations — the C1 excerpt plus `PhysicsSubsystem` {GEOMETRY,
  SPATIAL, NARROW_PHASE, DYNAMICS, SOFT_BODIES, WORLD} (with `PhysicsBackendKind`, the backend identities of errors:
  `[gfx::physics::GEOMETRY]`), `PhysicsCapability` (16: what a `BackendCapabilityError` names), `BroadPhaseKind`,
  `NarrowPhaseKind`, `FrustumPlane`, `PlaneSide`, `AxisPlane`, `LayerMask` (64-bit bitmask `LAYER_0`..`LAYER_63`) — and the
  constants `ALL_LAYERS`, `MAXIMUM_MANIFOLD_POINTS`, `DEFAULT_RESTITUTION_THRESHOLD`, `GEOMETRIC_TOLERANCE`. The camera's
  enumerations join this file in PH1.
- `gfx.physics.handles`: 17 tags (world, rigid body, soft body, collider, shape, constraint, joint, trigger, spatial
  structure and proxy, broad phase, narrow phase, solver, integrator, contact manifold, camera, camera controller) with
  their `Handle` aliases; concepts `PhysicsHandle`, `BodyHandle`, `CollisionObjectHandle`, `ConstraintObjectHandle`,
  `SpatialHandle`, `CameraObjectHandle`.
- `gfx.physics.errors`: `PhysicsError : gfx::Error` (`PhysicsErrorDescription` {backend, operation, problem, suggestion,
  `PhysicsErrorContext` {world, rigid_body, soft_body, collider, shape, joint, trigger}}; one message line per object
  concerned), `throw_unsupported(backend, operation, PhysicsCapability, reason, suggestion, context)` → core
  `BackendCapabilityError`, `QueryFailure` {kind, operation, detail (static text), backend, capability, context;
  `message()`, `raise()`} and `QueryResult<T> = std::expected<T, QueryFailure>`.
- `gfx.physics.primitives` (partitions; re-exports `gfx.core.math` and the enumerations): `Plane` (unit normal, distance;
  `from_point_normal`, `from_points`, `from_coefficients` — a vector without direction gives a plane everything is in
  front of), `Aabb` (`empty`, `from_center`, `from_points`, `merged`, `expanded`, `closest_point`, `transformed` by a matrix
  (Arvo) or a transform, …), `Sphere`, `Obb` (center, half extents, orientation), `Ray`, `Segment` (+ `closest_points` of
  two segments), `ConvexHullView` / `FixedConvexHull<V, F>` (constexpr; `box_hull()`) / `ConvexHull` (owns its arrays,
  validates the faces against the vertices), `Frustum` (`from_matrix(matrix, DepthRange, reversed_depth)`: Gribb–Hartmann
  planes facing in; an infinite far plane becomes a plane without direction), the concept **`ConvexShape`** (support,
  margin, bounds) with `support_point()`, and the tests: `intersects` (box–box, sphere–sphere, box–sphere, oriented boxes by
  the 15-axis separating axis test, oriented box–box/sphere, segment–box/sphere), `classify` (plane or frustum against a
  box, sphere or oriented box → `PlaneSide` / `FrustumIntersection`), `ray_distance` (box, sphere, oriented box, plane,
  hull; optional maximum) → `std::optional<float>`.
- **Batch culling**: `classify(frustum, span<const Aabb>, span<FrustumIntersection>)` and the `Sphere` overload, on SIMDe
  kernels (`primitive_kernels.cpp`, plain translation unit): the six planes in two registers of four lanes (two dummy
  lanes everything is in front of), one element per iteration, the same operations in the same order as the scalar
  `classify()` — identical results, volumes exactly on a plane included. The batches use one thread; callers that cull
  in parallel give each job its own part of the spans (the functions keep no state). Multithreaded work inside the
  library (pair search, islands, stepping) comes with the task scheduler (PH3, PH4).
- **Decoupling, proven**: `gfx_require_decoupled(gfx_physics_common FORBIDDEN gfx gfx_platform gfx_platform_graphics
  gfx_ui gfx_ui_platform gfx_ui_atlas_tools)` (`cmake/GfxDecoupling.cmake`) walks the target's whole link closure at the
  end of configuration (deferred, so targets declared later count; generator expressions are searched for target names)
  and stops the configuration naming the path to a forbidden target (checked once by linking one through a
  `$<$<CONFIG:Debug>:…>` expression). `cmake/scripts/GfxCheckModuleImports.cmake` scans sources for `import gfx…;`: CTest
  `PhysicsDecoupling.SourcesImportOnlyCoreAndPhysics` (allowed prefixes `gfx.core`, `gfx.physics`, `gfx.camera`, matched
  on whole name components) and `PhysicsDecoupling.ForbiddenImportsAreFound` (a fixture importing `gfx.platform` and
  `gfx.camerawork` must be reported).

Tests: `gfx_core_tests` + 8 `MathTest.*` (compile-time values recomputed at run time on the cglm/SIMDe paths, conventions,
batches against one element at a time, short outputs); **`gfx_physics_common_tests`** (20: enumerations spelled and
parsed, handles, errors and query failures; 75 `static_assert`s on primitives and intersections; oriented boxes against the projections
of their corners on the 15 axes (4000 random pairs), segment closest points against a sampled reference, hull validation,
frustums from matrices in both depth ranges, reversed and infinite; frustum classification against the corners, rays
against hulls and surfaces, segments against rays; batches equal to the scalar results for three frustums × 4125 boxes
and spheres, every length 0–9 at four offsets, short outputs); the two decoupling tests; compile-failure tests
`PhysicsHandlesOfDifferentKindsDoNotCompare`, `ConvexShapesNeedAMargin`.

Deviations from the proposal, for validation:

1. **Concept names**: the request names concepts `ColliderHandle`, `ConstraintHandle` and `CameraHandle`, but those
   names are the handle aliases (`using ColliderHandle = Handle<ColliderTag>`); the concepts are
   `CollisionObjectHandle` (collider or trigger), `ConstraintObjectHandle` (constraint or joint), `CameraObjectHandle`
   (camera or controller).
2. **`ConvexShape` lives in the primitives** (it needs `Vector3` and `Aabb`), not in `handles.cppm`.
3. **`Vector3` stays scalar** (12 bytes, no padding: arrays of positions stay packed); SIMD reaches it through the batch
   functions. cglm's own 3-component functions are scalar too.
4. **Quaternions are stored x, y, z, w** (cglm's layout) — the constructors take named fields, so the order shows only
   in memory.
5. **Decoupled imports are checked by a CTest scan**, not a compile-failure test (a failing compile would only show that
   one file fails, not that every source keeps to the rule).
6. **`ConvexHull` takes vertices and face planes** in PH0 (validated); building a hull from a point set (quickhull, and
   the `consteval` `convex_hull<N>(points)` of §C2) is PH2's, as planned.

### PH1 as built (2026-10-24)

Target **`gfx::camera`** (`gfx_camera`, PUBLIC `gfx::physics_common`), `import gfx.camera;`, namespace `gfx::camera`, in
`source/physics/camera/`:

| Module | Content |
|---|---|
| `gfx.camera` (camera.cppm) | umbrella: the interface and the factory |
| `gfx.camera.interface` (interface/) | re-exports gfx.core.math, gfx.core.types, gfx.core.errors, gfx.physics.enums, gfx.physics.errors, gfx.physics.primitives; partitions `:projection`, `:camera`, `:controller`; implementation units camera.cpp, controller.cpp |
| `gfx.camera.factory` (camera_factory.cppm/.cpp) | `CameraBackendAvailability`, `camera_backend_availability()`, `available_camera_backends()`, `create_camera(description = {}, kind = STANDARD)`, `create_camera_controller(description, kind = STANDARD)` |
| `gfx.camera.backends.standard` (backends/standard/, private file set) | `StandardCamera` and the three controllers, declared in their implementation units; imported only by the factory's implementation |

- **Enumerations** (in `source/physics/enums.cppm`, namespace `gfx::camera`, generated by the PH1 script):
  `CameraBackendKind` {STANDARD, CUSTOM} (BACKEND_LIBRARY "gfx::camera": `[gfx::camera::STANDARD] ...`),
  `CameraProjectionKind`, `CameraControllerKind` {FREE_FLY, FIRST_PERSON, ORBIT}, `NormalizedDeviceYAxis` {UP, DOWN},
  `ReferenceFrame` {WORLD, LOCAL}; constant `INFINITE_FAR_PLANE`. The primitives the camera uses are named in its namespace
  too (`using physics::Frustum;` … `Ray`, `Aabb`, `Sphere`, `Obb`, `Plane`, `DepthRange`, `FrustumIntersection`, `FrustumPlane`).
- **Values and constexpr functions** (`:projection`): `PerspectiveProjection` {vertical_field_of_view, aspect_ratio,
  near_plane, far_plane}, `OrthographicProjection` {left, right, bottom, top, near_plane, far_plane} + `orthographic(
  OrthographicSize)`, `CameraProjection = std::variant<…>`, `ClipConventions` {depth_range, reversed_depth, y_axis},
  `CameraDescription` {projection, clip, position, orientation}, `ScreenViewport` {x, y, width, height}; `perspective_matrix`,
  `orthographic_matrix`, `projection_matrix`, `view_matrix`, `view_projection_matrix`, `camera_transform`,
  `orientation_toward`, `look_at_orientation` (→ optional), `camera_frustum` (from a matrix and conventions, or a
  description), `screen_to_normalized_device`, `screen_to_world_ray`, `world_to_screen` (→ optional: pixels and clip depth),
  `camera_problem` (→ optional `CameraProblem` {problem, value, suggestion}). One depth formula covers every convention:
  clip depth `(A z + B) / -z` (perspective) or `A z + B` (orthographic) with A and B solved so that the near and far planes
  land on the convention's depths, and the limit as the far plane goes to infinity; +Y down negates the y row, and
  `camera_frustum` swaps the bottom and top planes back so that plane names stay true.
- **`Camera`** (`:camera`): non-copyable, `backend()`; pure virtual state (`description`, `position`, `orientation`,
  `projection`, `clip_conventions`), setters (`set_position`, `set_orientation` (normalizes), `set_pose`, `set_projection`,
  `set_clip_conventions`, `set_aspect_ratio` — throwing definitions in the base) and matrices (`view`, `projection_matrix`,
  `view_projection`, `inverse_view`, `inverse_projection`, `inverse_view_projection`, `frustum`); library code built on them:
  `transform`, `forward`/`right`/`up`, `yaw_pitch_roll`, `set_orientation_from`, `rotate(axis, angle, ReferenceFrame)`,
  `translate(offset, ReferenceFrame)`, `look_at(target, up)`, `classify` (Aabb, Sphere, Obb, and the SIMD batches),
  `screen_to_world_ray`, `world_to_screen`; `validate_camera_description()` for derived cameras. The standard camera
  validates every change (a refused one leaves it unchanged), then recomputes all matrices and the frustum at once, so
  getters are copies and concurrent readers need no lock; inverses are composed (exact rigid inverse × one 4×4 inverse).
- **Controllers** (`:controller`): `CameraControllerInput` {movement (+X right, +Y up, +Z forward), look (radians: x yaw,
  positive turns right; y pitch, positive looks up), roll (positive banks right), zoom, seconds}; descriptions
  `FreeFlyControllerDescription` {speed}, `FirstPersonControllerDescription` {speed, up, pitch limits},
  `OrbitControllerDescription` {target, distance, yaw, pitch, distance and pitch limits, zoom_rate, pan_speed, up} (its
  state too), `CameraControllerDescription = std::variant<…>`, `controller_kind()`; `CameraController` {`kind`,
  `description`, `set_description` (same kind only), `update(Camera&, input)`}; `validate_controller_description()`,
  `validate_controller_input()`. First person and orbit measure yaw and pitch in the frame that takes +Y to their up axis
  (any up axis works: +Z for modeling tools).

Tests: **`gfx_camera_tests`** (16: projection matrices against hand-computed A and B for the 8 depth conventions and +Y
down, orthographic likewise, view matrices, look-at orientations, a camera evaluated entirely at compile time — 32
`static_assert`s; near and far depths under every convention; frustums and screen rays identical under every convention;
pixel → ray → point → pixel round trips; look-at orientations stay level; problems; the factory; matrices equal to the
functions and inverting each other; setters that validate and leave the camera unchanged; turning, moving, looking at;
boxes, spheres and oriented boxes inside, straddling and outside each of the six planes, perspective and orthographic,
under every convention, batches equal to single calls; controllers: signs, clamps, level walking, a +Z up axis, orbit
distance/zoom/pan/state, validation); camera enumerations in `gfx_physics_common_tests`; CTest
`PhysicsDecoupling.CameraImportsNoPhysicsInterface` (the camera's sources import only gfx.core, the physics enumerations,
handles, errors and primitives, and gfx.camera) and `PhysicsDecoupling.CameraImportsOfThePhysicsInterfaceAreFound` (a
fixture); `gfx_require_decoupled(gfx_camera FORBIDDEN … gfx_physics)`. The import scan had a bug the camera's longer
prefixes revealed (a prefix longer than the module name made CMake's SUBSTRING fail): fixed.

Deviations from the proposal, for validation:

1. **`near_plane` and `far_plane`**, not C4's `near`/`far`: `<windows.h>` defines `near` and `far` as empty macros, so a
   designated initializer `.near = 0.1F` would not compile in a translation unit that includes it.
2. **Roll is about +Z**, as core's `YawPitchRoll` (one convention in the code base), not about −Z as §A13 says;
   `CameraControllerInput::roll` is positive banking right (the image turns counter-clockwise).
3. **`CameraBackendKind::CUSTOM` is never built by the factory**: an application's camera derives from `Camera` and is
   constructed directly (`camera_backend_availability(CUSTOM)` says so; `create_camera(…, CUSTOM)` throws a
   `BackendCapabilityError` with that reason). A CMake hook like the physics one (§0 row 11) seemed of no use for
   cameras; say if you want one.
4. **No `perspective()` helper** (C4): `PerspectiveProjection{…}` is the value; `orthographic(OrthographicSize)` gives the
   width-and-height form of an orthographic projection.
5. **Modules**: `gfx.camera.interface`, `gfx.camera.factory` and the private `gfx.camera.backends.standard` under the
   umbrella `gfx.camera` (as graphics: `gfx.interface`, `gfx.backend_factory`, private backends, `gfx`); §A17 named only
   `gfx.camera` and `gfx.camera.factory`.
6. **The camera handles are unused**: cameras are objects owned through `std::unique_ptr`, with no issuer of handles;
   `CameraHandle` and `CameraControllerHandle` stay for an owner that names cameras later (a scene, a world attaching
   cameras to bodies).
7. **Added beyond §A13**: `world_to_screen()` (projection to pixels and clip depth: round trips, labels, picking),
   `set_pose()` (one recomputation for a controller's move and turn), `translate()`, `set_aspect_ratio()`,
   `camera_problem()` usable in constant expressions, an orthographic near plane that may be negative (behind the camera).

### PH2 as built (2026-10-24)

Target **`gfx::physics`** (`gfx_physics`: PUBLIC `gfx::physics_common`, PRIVATE SIMDe; it never links the camera), which
the later phases grow. Two modules:

**`gfx.physics.shapes`** (`source/physics/shapes/`):

- `:convex` — `SphereShape`, `CapsuleShape` (axis +Y), `CylinderShape`, `ConeShape` (apex +Y), `BoxShape` (with a rounding
  `radius`), `TriangleShape`: constexpr values, each a `ConvexShape` (support of the margin-free core, `margin()`, `bounds()`)
  and a **`FacedConvexShape`** with `supporting_face(direction)` → `SupportingFace` (a polygon counter-clockwise around its
  outward normal, a segment or a point, with a stable `face` index for feature ids). Curved shapes switch to a face within
  about 5.7°: a cylinder's cap or a cone's base as an octagon, a cylinder's side line, a cone's slant, a capsule's segment.
- `:hull` — `HullView` (vertices, face planes, face polygons as `HullFace` ranges of uint16 indices, radius),
  `FixedHull<V, F, I>` and **`convex_hull<V, F, I>(points)`** (consteval quickhull: a hull known at compile time is built by
  the compiler; a call to `convex_hull_rejected_the_points()` or `convex_hull_capacity_is_too_small()` in the diagnostic
  names the problem), `HullShape` (owning; `from_points()` runs the same quickhull at run time; descriptive PhysicsErrors
  for fewer than 4, coincident, collinear, coplanar or infinite points). Quickhull computes in double precision with the
  tolerance 3 · FLT_EPSILON · (largest |x| + |y| + |z|), finds horizons by depth-first search over triangle neighbors, and
  merges coplanar adjacent triangles (normals within 0.00001, vertices within 8 tolerances) into polygons whose planes keep
  every vertex behind them.
- `:mass` — `MassProperties` {mass, center of mass, inertia about it}: closed forms for sphere, box, capsule, cylinder and
  cone (rounding not counted), the covariance method for hulls and closed meshes (`surface_mass_properties()` over any range
  of triangles), `transformed()`, `combined()` (parallel axis theorem), `principal_axes()` (cyclic Jacobi, right-handed
  frame) — all constexpr.
- `:static_bvh` — `StaticBvh`: built once (binned SAH, 16 bins, leaves of 4, median splits below depth 32 so a stack of 64
  holds the traversal), queries by box, by segment and of item pairs between two hierarchies (the second posed), testing each
  item's own box.
- `:mesh` — `TriangleMeshShape` (`TriangleMeshDescription`: vertices, index triples, `two_sided`, `active_edge_cosine`;
  triangles' BVH; **active edges** from edges sorted by their vertices: an edge between triangles that meet flat or folded
  inward is inactive; `closed()`), `HeightFieldShape` (`HeightFieldDescription`: a `std::mdspan` of heights rows × columns,
  scale; two triangles per cell facing +Y; active edges from the grid on demand; cell-range queries).
- `:shape` — **`Shape`**: immutable geometry, `ShapeGeometry = std::variant<SphereShape, CapsuleShape, CylinderShape,
  ConeShape, BoxShape, HullShape, TriangleMeshShape, HeightFieldShape, CompoundShape>` in `ShapeKind`'s order, sizes checked
  when made, `kind()`, `is_convex()`, `get_if<>()`, local and posed `bounds()` (tight for convex shapes, from six supports),
  `mass_properties(density)`; `make_shape()` → `std::shared_ptr<const Shape>`; `CompoundShape` (children: shared shapes at
  poses, a BVH over their boxes); `convex_bounds()`.
- SIMD: `HullShape::support()` runs a SIMDe kernel from 16 vertices on (`shape_kernels.cpp`: four vertices per step, per-lane
  first maximum, lanes reduced to the smallest index among equals) — the same vertex as the scalar loop.

**`gfx.physics.narrow_phase`** (`source/physics/narrow_phase/`):

- `:contact` — `ContactFeature` {sub-shape of each side, 32-bit local id: reference face, incident face, incident vertex or
  (incident edge × reference side) or reference corner, and which side is the reference; or a point contact's two features},
  `ContactPoint` {position midway between the surfaces, separation along the normal, feature, normal and tangent impulses},
  `ContactManifold` {normal from the first shape to the second, up to 4 points, sub-shapes, `ContactAlgorithm`},
  `CollisionSettings` {speculative_distance 0.02, merge_cosine 0.999, maximum_manifolds 8}, `flipped()`,
  `reduce_contact_points()` (deepest, farthest from it, widest triangle, farthest on the other side).
- `:gjk` — `MinkowskiDifference`, `gjk()` (Ericson's Voronoi-region closest points on the simplex, barycentric witness
  points, relative tolerance 1e-5, at most MAXIMUM_GJK_ITERATIONS), `GjkResult` with the final simplex.
- `:epa` — `epa()` from GJK's simplex (grown to a tetrahedron along directions that give volume), fixed storage
  (MAXIMUM_EPA_VERTICES, MAXIMUM_EPA_FACES), horizon by edges seen once, `EpaResult` {status, normal, depth, deepest points}.
- `:clipping` — `posed_face()`, `face_manifold()` (reference = the polygon better aligned with the normal, the first's on a
  tie; Sutherland–Hodgman against its side planes with feature tags; a segment clipped parametrically; points up to the
  speculative distance), `point_manifold()`, `contact_manifold()` (faces, else one point).
- `:analytic` — sphere–sphere, sphere–capsule, capsule–capsule (two points when parallel and overlapping), sphere–box
  (27 regions as features), and the separating axis test for box–box and triangle–triangle (face axes kept unless an edge
  axis separates by 5 % more, edge contacts at the edges' closest points).
- `:convex` — **`collide_convex(first, pose, second, pose, settings)`**: the analytic pairs in either order at compile time,
  else `collide_convex_general()` (GJK on the cores; separated within the speculative distance: GJK's normal; overlapping:
  EPA; then the manifold); `convex_distance()` → `ShapeDistance`. constexpr: the tests collide two boxes at compile time.
- `:collide` — **`collide(ShapeInstance, ShapeInstance, ContactCollector&, settings)`** for any two shapes: compounds recurse
  into the children under the other's box (manifolds name the child); a convex shape against a mesh or height field
  collides with each triangle under its box in the triangles' frame (one-sided unless two-sided; a manifold whose every
  point is on the face or on inactive edges takes the face's normal); two meshes or height fields collide triangle by
  triangle; manifolds whose normals agree merge (points reduced to 4), at most maximum_manifolds kept, the deepest.
  `ContactCollector` keeps its vectors between calls; `collide()` is pure (one collector per thread). `shape_distance()` for
  convex shapes.
- `:manifold_cache` — `ManifoldKey` {two `ColliderHandle`s, two sub-shapes}, `ManifoldCache` (pimpl: open addressing,
  doubling past half full, end_step() keeping the step's entries without tombstones): each step `update()` the fresh
  manifolds (impulses copied by feature id), solve, `store()` the solved manifolds, `end_step()` (unused pairs forgotten).

**Toolchain defect found** (§6.13 of the project memory): a handle whose tag one module declares, instantiated by a second
module (`std::optional` members of `PhysicsErrorContext`) and by a partition of a third (a braced default initializer or a
defaulted comparison of a struct holding one), makes the partition's module file unreadable ("failed to read compiled module
cluster N: Bad file data"), and reading on, crashes GCC in an implementation unit. Workaround: every physics handle is
explicitly instantiated at the end of `handles.cppm`, so importers share one instantiation. Detector:
`CompilationFailure.HandleInstantiatedInSeveralModulesCorruptsModuleFile` (`tests/toolchain/defects/handle_instantiation_*`).

Tests: **`gfx_physics_tests`** (22): supporting faces, the compile-time cube and wedge hulls, mass properties of hulls and
formulas (all `static_assert`ed); random hulls holding their points, every vertex on its faces, Euler's formula, the SIMD
support against the scalar one; hulls rejected; a 256-sided hull against the cylinder, capsule and sphere formulas,
principal axes of a turned box, two boxes against one; a flat grid's and a closed cube mesh's active edges and the cube's
mass; a height field's triangles, active edges, queries; shapes' checks; a compound's bounds, mass, children; the BVH against
brute force (box, segment and pair queries). GJK and EPA: sphere–box distances against the box's closest point, capsule–box
against sampling (500 random poses), box–box penetrations against the separating-axis minimum (1000 poses; the normal
separates them by the depth), curved and touching shapes; at compile time too. Manifolds: points per resting pair (box 4,
sphere 1, capsule lying 2 and standing 1, cylinder standing 4 and lying 2, cone 4, hull cube 4, rounded box 4, all at the
right depth, midway), feature ids through 100 nudged steps, speculative contacts, reversed orders, analytic against general
paths, the reduction of 9 points, a manifold at compile time. Collision of shapes: a box sliding over a triangulated floor
(one manifold of four points, the floor's normal at every seam — the test fails with the active-edge correction disabled),
one- and two-sided meshes, a height-field ridge (two normals; the deepest kept with one manifold), a table compound on the
ground (16 leg contacts merged into 4, a leg named), mesh against mesh, checked inputs, a collector that keeps its storage;
the manifold cache (impulses by feature, end of steps, growth). Plus the detector and the new enumerations' round trips.

Deviations from the proposal, for validation:

1. **Shapes are values** (`SphereShape` … `CompoundShape`) inside an immutable `Shape` shared through
   `std::shared_ptr<const Shape>`; the world's `ShapeHandle`-named shape objects (§A2) come with the interfaces (PH4) and
   will wrap these.
2. **Rounded boxes and hulls** (`radius`, the margin) are supported in collision; mass properties count the core only.
3. **Curved faces as octagons**: a cylinder standing or a cone on its base touches through an octagon of its cap or base
   (reduced to 4 points); the hull approximations for Box3D (§A6) are PH5's.
4. **Mesh contacts are one-sided by default** (`TriangleMeshDescription::two_sided`); triangle–triangle contacts (mesh against
   mesh) use neither one-sidedness nor active edges.
5. **The narrow phase is not yet behind an interface**: `collide()` and `collide_convex()` are the reference implementation;
   the `NarrowPhase` interface (§A6) arrives with the other interfaces in PH4. Ray and shape casts against shapes come with
   the queries (PH3, PH4).
6. **Manifold cache API**: update (warm start) → solve → `store()` → `end_step()`, keyed by colliders and sub-shapes;
   single-threaded (the narrow phase itself is pure and can run in parallel).

### PH3 as built (2026-10-24)

In `gfx::physics`, two more modules:

**`gfx.physics.tasks`** (`source/physics/tasks/`): `TaskScheduler` (`parallel_for(count, minimum_range, task)` — ranges
of at least the minimum, possibly concurrent, returning when all are done and rethrowing the first exception;
`concurrency()`), `TaskFunction = std::function<void(begin, end)>`, the library's `InlineTaskScheduler` (on the caller) and
`ThreadTaskScheduler` (std::jthread workers taking ranges by an atomic counter, the caller among them; the caller returns
only when every range is done and no worker is still taking ranges).

**`gfx.physics.spatial`** (`source/physics/spatial/`), every structure written against the primitives only:

| Structure | As built |
|---|---|
| `Bvh` | dynamic AABB tree: leaves beside the sibling of least surface-area cost (Box2D's branch and bound), AVL rotations; mutations in bvh.cpp |
| `Octree<Depth = 8, Looseness = 2>`, `Quadtree<Plane = XZ, Depth = 8, Looseness = 2>` | one `LooseTree<Dimensions, Plane, Depth, Looseness>`: region at construction (default ±1024), a proxy at the deepest level whose loose cells hold it, in its center's cell; cells hashed per level; queries visit per level the cells in range, or every used cell when fewer; too large or outside the region: level 0, always visited |
| `Bsp` | axis-aligned planes by binned surface-area heuristic over a node's proxies (16 bins on the axis of widest spread); proxies a plane cuts stay at its node; leaves split past 8; node boxes for queries; rebuilt after as many changes as proxies or when a split would go past 2 · bit_width(n / 8 + 1) + 4 |
| `UniformGrid<CellSize = 1>` | hashed cubic cells; a proxy in every cell it covers up to 64, larger ones in a list every search visits; each pair or hit reported by the cell of the smallest corner of the overlap (const, concurrent queries) |
| `SweepAndPrune<Axes = 1>` | proxies sorted by lower end on x (or on each axis: 3), moved by insertion; pairs by a sweep (along the axis of widest spread with three) |

`SpatialStructureConcept` (`KIND`, insert, remove, update → bool, key, box, size, queries by `Aabb`, `Ray` + maximum,
`Segment`, `Sphere`, `Frustum` with visitors returning false to stop — a ray's visitor also gets the entry distance into
the box —, pairs, iterate) — the static_assert in spatial.cppm checks all seven. Shared: `ProxyStore` (slots issuing
`SpatialProxyHandle`s; stale handles throw PhysicsError [gfx::physics::SPATIAL]), `CellMap` (open addressing, backward-shift
removal).

**Broad phase**: `CollisionFilter` {category, mask, group} with `should_collide()` (Box3D's rule), `BroadPhasePair` (smaller
handle first, ordered), `BroadPhaseDescription` {margin 0.1, prediction 2, proxies_per_task 128}, `SpatialBroadPhase<Structure>`
(insert with filter, remove, `move(proxy, box, displacement)` → refits only when the box leaves its fat box: margin plus
the predicted motion ahead, `set_filter`, `set_custom_filter`, `update(scheduler)` → sorted `pairs()`, `new_pairs()`,
`ended_pairs()`); `DefaultBroadPhase = SpatialBroadPhase<Bvh>`, `FlatWorldBroadPhase = SpatialBroadPhase<Quadtree<XZ, 8>>`.

Tests (in `gfx_physics_tests`, + 12): each of the seven structures through 1500 boxes moved, removed and added over 8
steps — pairs (each once) and box, sphere, segment, ray (entry distances too) and frustum queries equal brute force,
iteration, stopping visitors, stale handles; a BVH of 4096 sorted inserts at most 20 high, a BSP at most 24 deep, loose
octree levels; filters, fat boxes, new and ended pairs; 3000 moving boxes over 6 steps give identical pairs on every
structure with the structure's own search and with 1, 2 and 8 threads; the schedulers (every index once over 50 rounds,
ranges of the minimum, exceptions to the caller).

Deviations from the proposal, for validation:

1. **Structures keep the boxes they are given**; fattening (margin and predicted displacement) is the broad phase's.
2. **The BSP splits by axis-aligned planes** (a k-d tree): general planes fit polygon soups, not boxes; nodes keep boxes.
3. **The parallel pair search is per-proxy queries** (the pair kept by its smaller collider) split over tasks; the
   structure's own pair search runs single-threaded. Both give the same sorted pairs (tested).
4. **The task scheduler arrived now** (§A14 placed it with PH4): needed by the pair search; `ThreadTaskScheduler` is a
   library pool for applications without a job system.
5. **The virtual `SpatialStructure`** (run-time choice of a structure, §A7) comes with the interfaces in PH4.
6. **The broad phase indexes filters by the collider handle's slot**: colliders must not be null, one proxy per collider; a
   custom filter must be thread-safe when a scheduler is given.

### PH4 as built (2026-10-25)

Two targets now: **`gfx::physics_components`** (everything a backend is composed of: shapes, narrow phase, task schedulers,
spatial structures, the interface, the framework) and **`gfx::physics`** (the factory, the umbrella module `gfx.physics`, the
backends in a private file set). A custom backend links `gfx::physics_components` (no cycle) and is named by the CMake cache
variable `GFX_PHYSICS_CUSTOM_BACKEND_TARGET`: its public module `gfx.physics.backends.custom` exports
`gfx::physics::custom_backend::unavailable_reason()` and `create_physics_world(description)`, and the factory then serves
`PhysicsBackendKind::CUSTOM` (checked in a separate build directory with a scratch backend).

**`gfx.physics.interface`** (`source/physics/interface/`), every operation pure virtual with a definition that throws a
`BackendCapabilityError` naming the operation and the capability (queries: a `QueryFailure` of kind UNSUPPORTED):

| Partition | Content |
|---|---|
| `:descriptions` | `Friction`, `SurfaceMaterial`, `SolverDescription`, `SleepDescription`, `PhysicsWorldDescription` (gravity −9.80665 y, asynchronous_stepping, task_scheduler, solver, sleep, collision, hit_event_speed, debug_name); `RigidBodyDescription`, `RigidBodyState`, `ColliderDescription` (`std::shared_ptr<const Shape>`, pose, material, density, filter, contact_events, round_shape_segments), `TriggerDescription`; joints: `JointLimits`, `JointMotor`, `JointSpring`, `JointBreaking`, seven descriptions in `JointDescription` (a variant in `JointKind`'s order), `JointState`, `joint_kind()`, `joint_bodies()`, `hinge_joint()` …; soft bodies: `SoftBodyParticle`, `SoftBodyConstraint`, `ParticlePin`, `SoftBodyDescription`; queries: `QueryOptions` {filter, engine}, `RayHit`, `ShapeCastHit`, `ClosestPointHit`, `ContactInfo`; events: `ContactEvent`, `TriggerEvent`, `JointEvent`, `SleepEvent`, `PhysicsEvents` (spans), `StepStatistics` |
| `:object` | `PhysicsObject : HandleIssuer` (backend(), protected throw_unsupported / throw_not_implemented) |
| `:objects` | `Collider`, `RigidBody` (state, mass properties, add_collider, motion, forces and impulses, configuration, sleep), `Joint` (description, state, update, set_enabled), `Trigger`, `SoftBody` (positions/velocities into caller storage, pins, particles, settings) |
| `:pipeline` | `BroadPhase`, `SpatialBroadPhaseComponent<Structure>`, `NarrowPhase`, `GjkNarrowPhase`, `Solver` (kind only until PH7), `Integrator` + `IntegrationState` + `IntegratorRequirements` + `ForceField` (implementations in PH6) |
| `:world` | `PhysicsWorld`: objects, settings, step / begin_step / wait_step / is_stepping / step_count, events, statistics, the seven queries (noexcept, `QueryResult`), the pipeline and its replacement |

**`gfx.physics.world`** (`source/physics/world/`): the framework every library backend is built on. `SteppedWorld` (final,
a `PhysicsWorld`) composes a **`SteppedWorldBackend`**, which implements `identity()`, `pipeline()`, `check(edit,
RecordedWorld)` (capability errors when an edit is recorded), `apply(edits, pending)`, `simulate(seconds, pending)` and, if
it has them, its own queries (optional results: none means the abstraction answers) and pipeline replacements. The framework:

- **issues handles at once** (slot lists with 8-bit generations; a slot freed only when the step that applied the
  destruction is published), checks every value when it is recorded (descriptions, finite poses, orientations and axes
  normalized, bodies alive, a joint's bodies distinct, limits ordered, soft-body indices …: `PhysicsError` naming the
  backend, the operation and the objects), records **`WorldEdit`** (a variant of 45 edit kinds, `:edits`) under one mutex,
  from any thread;
- **applies the edits at the start of the next step in the order recorded**: first to the pending records (`:snapshot`:
  one record per object — its configuration and its state —, events, contacts, statistics; mass properties of dynamic bodies
  from their colliders' shapes at density 1 scaled and moved, `move_kinematic` as velocities), then hands them to the
  backend, which simulates and writes its results into the pending snapshot; the framework then computes the colliders',
  triggers' and soft bodies' world boxes;
- **destroys by edits**: an object's destructor records its destruction; a body's records its colliders', triggers' and
  joints' first (their objects then throw "destroyed with its body"); pins to a destroyed body are released;
- **publishes in `wait_step()`**: pending → published (whole records whose configuration changed, otherwise the state), slots
  released, the query tree updated; getters read the published snapshot under a shared lock (an object created since shows
  its creation values) — **getters work during steps**, only queries are refused;
- **steps** on the caller, or on one `std::jthread` with `asynchronous_stepping`; a step's exception is rethrown by
  `wait_step()` (nothing published; the edits it applied stay in the pending records);
- **the reader handshake** (§A14): a query increments the readers then reads the stepping flag; `begin_step` sets the flag
  then waits (atomic wait) for no reader; both `seq_cst`; publication happens while the flag is still set;
- **the abstraction's queries** (`QueryEngine::ABSTRACTION`, and `BACKEND` for backends without their own): a dynamic BVH
  over the published colliders' world boxes, then the narrow phase's new queries against each candidate; ties go to the
  smaller handle; nothing allocates (results into caller spans, capacity exceeded = the nearest or smallest kept);
- objects of a destroyed world throw "the world was destroyed" (weak link to the shared state).

**Narrow phase** (`:queries`): `ray_cast_shape`, `cast_shape` (convex moving shape), `shapes_overlap`, `closest_point_on_shape`
— conservative advancement over GJK distances for convex shapes (each step stops half the touching distance 1e-4 short of the
plane through the closest point, so GJK's normals stay exact; within GJK's accuracy, 1.6e-3, a normal that turns away is
checked by a probe further along the motion), spheres and triangles exactly, meshes through their hierarchy, height fields
by pieces of the ray in order, compounds through their children. **Spatial** (`:spatial_structure`): `SpatialStructure`
(virtual, `std::function` visitors), `SpatialStructureAdapter<S>`, `make_spatial_structure(kind)`.

**Null backend** (`gfx.physics.backends.null`): everything that would make or change something throws
`BackendCapabilityError` (WORLD_OBJECTS); queries fail as UNSUPPORTED; `description()` returns what it was given, events and
statistics are empty, `wait_step()` does nothing. **Factory** (`gfx.physics.factory`): `physics_backend_availability()`,
`available_physics_backends()` (simulating backends first), `create_physics_world(description, kind)`; BOX3D (PH5) and
REFERENCE (PH7) report that they are not built yet.

**Robustness fixes in the PH2 narrow phase**, found by the new tests' samples and kept as regression tests:

1. **GJK slivers**: a cylinder's rim gave three nearly equal support points, and the needle tetrahedron they made with a
   fourth "held" the origin (shapes 0.436 apart reported overlapping). A tetrahedron whose volume is under 1e-4 of the cube
   of its longest edge no longer decides: its nearest face does (`GjkEpaTest.SliversOfNearlyRepeatedPointsDoNotMakeSeparatedShapesOverlap`, exact poses in hex floats).
2. **EPA's start**: a sliver from GJK is regrown from one point; when GJK's simplex leaves the origin on the start's boundary,
   the octahedron of the six axes' support points starts instead when it holds the origin and is convex.
3. **EPA's expansion**: faces the new point is almost in the plane of (cylinder caps and rims) are not removed (a visibility
   tolerance; the expanded face always is), and the nearest distance — a lower bound that only grows on a convex polytope —
   is watched: should it drop, the best face before stands. Points inside a cylinder had come out at depth 0.

Tests (+ 18): `gfx_physics_world_tests` (new, 15: the error contract of every default definition — classes generated from the
same list of operations as interface.cpp's definitions —, factory, null backend, edits applied in order, edits from six
threads each in its order, deferred and cascading destruction with slot reuse, objects of a destroyed world, asynchronous held
and failing steps, 4 reader threads during 200 steps never seeing a torn state, the abstraction's queries, joints/triggers/
soft bodies, the pipeline, capability checks at record time, invalid values, kinematic targets) on a recording backend
(module `gfx.tests.physics_world_support`); in `gfx_physics_tests`: `ShapeQueryTest.*` (5: convex ray casts against exact
signed distance functions sampled along each ray, meshes and height fields against brute force, compounds, shape casts against
the distance along the motion, overlaps and closest points), the run-time spatial structures, the GJK regression test. The
random tests were also run with ten times the samples and with other seeds (Release) before being kept at their sizes.

Deviations from the proposal, for validation:

1. **Colliders take `std::shared_ptr<const Shape>`** (`make_shape()`); there is no `PhysicsWorld::create_shape()` returning a
   `ShapeHandle`-named world object (§A2, §C4). A backend converts a shape once and caches it by its pointer (the shared
   pointer keeps it alive).
2. **Backends implement `SteppedWorldBackend` hooks, not the object interfaces**: one framework gives every backend the same
   deferred edits, checks, objects, threading and queries (the interfaces remain for a world written from scratch).
3. **Getters work during a step** (they read the published snapshot); only queries return `STEP_IN_PROGRESS`.
4. **Queries find colliders only** (`QueryOptions` has no "include triggers"): triggers report through `TriggerEvent`s.
5. **User constraints** (`ConstraintHandle` objects) and the solver's operations come with the reference backend (PH7);
   `Solver` has its kind only. The integrators' implementations come with PH6.
6. **A joint changes by `update(description)`** (same kind and bodies) and `set_enabled()`, not by one setter per property.
7. **The null backend's getters report an empty world** (and `wait_step()` does nothing) instead of throwing; every operation
   that would make or change something throws.
8. **World handles are numbered by a process-wide atomic counter** (the one piece of state outside a world: names in
   errors, not behavior).
9. **Narrow-phase fixes** to GJK and EPA (above) changed PH2 code.

### PH5 as built (2026-10-25)

**Box3D** (Erin Catto, MIT, alpha) at **main f36d061b** (2026-10-20, 41 commits after the v0.1.0 release: fixes to sensors,
joint limits and continuous collision; chosen by the user over the tag), GitHub's source archive (2,884,055 bytes) pinned
by SHA-256 `b035dce6…df17ed`: `gfx_require_box3d()` (GfxDependencies.cmake, static subproject: its samples, tests and
benchmarks are top-level only), option **`GFX_PHYSICS_BACKEND_BOX3D`** (ON) → `GFX_WITH_PHYSICS_BOX3D_BACKEND` for the
factory. Box3D's C headers compile in a module's global fragment next to `import std` (checked).

**`gfx.physics.backends.box3d`** (`source/physics/backends/box3d/`, private file set): `Box3dBackend`, a
`SteppedWorldBackend` over one Box3D world (internal partition `:backend`; units box3d_world.cpp, box3d_shapes.cpp,
box3d_joints.cpp, box3d_queries.cpp):

| Of the interface | In Box3D |
|---|---|
| Rigid bodies (kinds, poses, velocities, damping, gravity scale, locks, sleep, continuous) | `b3BodyDef` one to one; `move_kinematic` = `b3Body_SetTargetTransform`; forces, torques, impulses applied for the next step; mass properties from Box3D (`b3Body_ApplyMassFromShapes`, or `b3Body_SetMassData` for given ones) read back into the published records |
| Sphere, capsule | `b3Sphere`, `b3Capsule` at the collider's pose (a capsule of half height 0: a sphere) |
| Box, hull | `b3MakeBoxHull`, `b3CreateHull` of the hull's vertices, as transformed hulls; rounded ones and hulls of more than 128 vertices refused (`ROUNDED_HULLS`, `LARGE_HULLS`) |
| Cylinder, cone | hulls of `round_shape_segments` sides (at most 64) |
| Triangle mesh | `b3CreateMesh` in the body's frame, on static bodies only (`DYNAMIC_TRIANGLE_MESHES`); two-sided meshes get each triangle twice, once reversed (Box3D culls back sides) |
| Height field | `b3CreateHeightField` (heights quantized to 16 bits between the field's lowest and highest), on static bodies only (`DYNAMIC_HEIGHT_FIELDS`), in the body's frame (`POSED_HEIGHT_FIELDS`) |
| Compound | one Box3D shape per part on the body (Box3D's run-time compounds): dynamic compounds of convex parts work; each shape remembers the top-level child it comes from |
| Materials | one friction coefficient: different static and dynamic ones refused (`SEPARATE_STATIC_FRICTION`); restitution; rolling resistance |
| Filters | 64-bit category and mask, group |
| Triggers | sensor shapes (no mass) on their body, or on a static body of their own; ENTER/EXIT from Box3D's sensor events (the first and last shape of a collider), **STAY** made by the backend every later step |
| Distance joints | rod; spring (with limits); rope = a spring of 0 Hz limited to its length; stub = a spherical joint |
| Hinge, slider, ball and socket, fixed, cone-twist | revolute (about the frames' z), prismatic (along the frames' x), spherical, weld (springs), spherical with cone and twist limits; frames computed from the bodies' poses when the joint is applied; hinge and twist limits within ±0.99π (`WIDE_JOINT_LIMITS`); six-DOF refused |
| Breaking | Box3D's force and torque thresholds raise joint events; the backend destroys the Box3D joint, reports `JointEvent::BROKEN` and keeps the frames to make it again when enabled |
| Events | contacts from Box3D's begin/end events counted per pair of colliders (one touch however many shapes touch; a destroyed collider's touches end at once), hits from its hit events; sleep from body move events (FELL_ASLEEP, WOKE_UP) |
| Contacts | the manifold of each touching pair (`b3Contact_GetData`), in handle order |
| Queries (`QueryEngine::BACKEND`) | `b3World_CastRay` (closest and every hit, one per collider), `b3World_CastShape` and `b3World_OverlapShape` with a proxy (spheres, capsules, boxes rounded or not, hulls), `b3World_OverlapAABB`; the query's filter applied as colliders are (groups included), ties to the smaller handle; cylinders, cones, meshes and closest points left to the abstraction |
| Soft bodies | refused for now (`SOFT_BODIES`): the abstraction's XPBD comes with PH6 |
| Settings | gravity; sub-steps, contact stiffness, damping and push speed, restitution threshold, warm starting; sleep on or off and its speed threshold; hit speed |
| Statistics | phase times from Box3D's profile (pairs, collide, solve), its contact count |
| Threads | Box3D's `enqueueTask`/`finishTask` over the world's `TaskScheduler` (workers: its concurrency) |

**Task scheduler**: `TaskScheduler::start(StartedTask) → TaskTicket` and `finish(ticket)` (inline by default;
`ThreadTaskScheduler` queues the work for its workers, and a waiting `finish()` runs queued work itself): what Box3D's
hooks take (§A14's "wait(group)"). **A PH3 race fixed** (found when repeating the tests): a worker woken late for a loop
that was over could join the next loop with the old loop's range count, overshoot its finished count and leave
`parallel_for()` waiting forever; workers now join only a loop in progress (300 repeats pass).

Framework additions: `check_world_description()` (backends that make their world before the SteppedWorld),
`RecordedWorld::body_colliders()` / `collider_shape()` (Box3D's checks of a body's kind against its colliders' shapes).
New capabilities: `ROUNDED_HULLS`, `LARGE_HULLS`, `POSED_HEIGHT_FIELDS`, `WIDE_JOINT_LIMITS`.

Tests (+ 13): `gfx_physics_box3d_tests` (12: the factory; free fall (exact velocity, drop within 1 %); a stack of five boxes
at rest, asleep, with its contacts; a pendulum's period within 2 % of 2π√(L/g); hinge limits, a rope's length, a weld
breaking; contact begin/hit/end and trigger enter/stay/exit; the same bits in two runs and on four threads through the task
hooks; every capability refused; a dynamic compound on a height field, a ball stopped by a two-sided mesh's back; Box3D's
ray casts, shape cast and overlaps against the abstraction's; a kinematic platform lifting a crate; two asynchronous worlds
stepping at once while four threads query them), `TaskSchedulerTest.StartedWorkRunsOnceAndFinishWaitsForIt`.

Deviations from the proposal, for validation:

1. **Box3D is pinned at a main commit** (f36d061b), not the v0.1.0 release (the user's choice).
2. **Compounds of convex shapes work on moving bodies** (several Box3D shapes per body), where §A12 refused them; only
   their mesh and height-field parts need static bodies.
3. **Four capabilities added** for limits §A12 did not list: rounded hulls, hulls past 128 vertices (and round shapes past 64
   sides), height fields at a pose, hinge and twist limits past ±0.99π.
4. **Two-sided meshes** collide from both sides by holding each triangle twice (Box3D's meshes are one-sided).
5. **Soft bodies are refused until PH6** (the design runs them through the abstraction's XPBD next to Box3D).
6. **Changing a joint (`update`) or enabling it again makes the Box3D joint anew** from its creation frames (its warm start is
   lost for a step); **moving a collider on its body** makes its shapes anew (its touches end, then begin again).
7. **Box3D's own queries differ from the abstraction's in three ways** (documented in the backend's module): they skip
   triggers (both do), ignore back sides of one-sided meshes, and do not report a ray that starts inside a shape (the
   abstraction reports it at distance 0).
8. **Solver and sleep settings Box3D has no counterpart for are hints it ignores**: velocity and position iterations, the
   static friction speed, the angular sleep threshold and sleep time, the collision settings (Box3D's own constants).
9. **The task scheduler gained `start()` / `finish()`**, and its PH3 race was fixed.

### PH6 as built (2026-10-25)

**`gfx.physics.dynamics`** (`source/physics/dynamics/`, in `gfx::physics_components`; re-exported by `gfx.physics.world`,
hence by `gfx.physics`):

| Partition | Content |
|---|---|
| `:integrators` | `SemiImplicitEulerIntegrator`, `VerletIntegrator` (position Verlet: previous positions required, velocities the last displacement over h), `RungeKutta4Integrator` (the force field evaluated four times per step), `XpbdIntegrator` (prediction, then velocities from the positions); `make_integrator(kind)`. Only states with a non-zero inverse mass move; a = gravity + inverse mass × (forces + the field's); `predict()` leaves the step's start positions in `previous_positions` when given; positions advance in one SIMD batch (`multiply_add`) when every state moves; scratch storage reused (no allocation after the first step) |
| `:presets` | `cloth_preset()` (a grid: STRETCH along its edges, SHEAR along the cells' diagonals, BENDING distances two particles apart — or DIHEDRAL angles between the cells' triangles), `cloth_particle()`; `rope_preset()` (STRETCH, BENDING every other particle); `tetrahedral_box()` (six positive tetrahedra per cell around its diagonal: neighbors share faces), `volume_preset()` (each tetrahedron's mass to its corners, STRETCH per edge once, VOLUME per tetrahedron); `pin_to_world(description, particle)` (where it starts), `pin_to_world(particle, point)`, `pin_to_body()`; `add_long_range_attachments()` (a TETHER from every free particle to every particle pinned to the world) |
| `:soft_body_solver` | `SoftBodyModel` (inverse masses, rest positions, constraints with their rest values, greedily colored 64 colors at a time, tethers apart), `CoupledBody`, `CoupledCollider`, `SimulatedSoftBody`, `BodyCorrection`, `XpbdSoftBodySolver` |
| `:xpbd_math` (internal) | six times a tetrahedron's volume; the dihedral angle at an edge and its gradients (Bridson et al. 2003); angles wrapped into (−π, π] |

**The XPBD step** of a soft body, over its sub-steps, one iteration per sub-step (Macklin et al. 2019, "small steps"):
prediction by the world's soft-body integrator (previous positions given as x − v h, for Verlet); rigid pins hold their
particles (a pin of compliance 0 to the world, or to a body corrections do not move, makes the particle kinematic); the
model's colors in order, a color of at least 512 constraints split over the task scheduler (ranges of 128; no two
constraints of a color move one particle, so any thread count gives the same bits); tethers (they move their first
particle only); the other pins; contacts with the colliders — spheres, capsules and boxes (rounded too) analytically,
cylinders, cones and hulls through `closest_point_on_shape()`, meshes and height fields through their nearest triangle
(behind a one-sided one is inside), compounds' leaves each — with Coulomb friction (static while the sliding stays under
the static coefficient times the normal correction, Müller et al. 2020; coefficients the geometric mean of the soft body's
and the collider's); an overlap the sub-step started with is undone without becoming velocity (pre-stabilization, Macklin
et al. 2014: a particle starting inside a shape is moved out, not launched); self-collision (a hashed grid over the
particles at the step's start, pairs within two radii plus twice the step's reach, not pairs that start within two
radii); velocities from the positions' change, damped by (1 − damping)^h.

**Coupling with rigid bodies, both ways**: soft bodies step after the backend's step. Each body moves, as a soft body sees
it, from its pose before the rigid step to its pose after (interpolated per sub-step); a dynamic body with mass is a
proxy that takes part of every correction made at one of its points — pins to it, contacts with its colliders, their
friction — by its inverse mass there (mass, inertia, motion locks), which moves it within the step and accumulates its
impulse. At the end the solver hands each body a `BodyCorrection` (impulses through and about its center of mass, and
the translation and rotation the corrections gave it); the backend moves the body and applies the impulses. Momentum is
exchanged exactly (a soft cube thrown at a floating box: the test's total stays within 0.1 %, Box3D's within 2 %).

**The framework** steps soft bodies after `simulate()` when `SteppedWorldBackend::simulates_soft_bodies()` is false (the
default): models are made at the first step that needs them (and dropped when the soft body is destroyed or its slot
reused), the bodies' poses are kept before the backend's step, `StepPhase::SOFT_BODIES` is timed, and the corrections go
to `SteppedWorldBackend::apply_soft_body_corrections()` (by default on the records). `PhysicsWorld::soft_body_integrator()`
/ `replace_soft_body_integrator()` (an `XpbdIntegrator` by default; replaced between steps). **Box3D** accepts soft bodies:
corrections through `b3Body_SetTransform` and impulses to the center of mass; a sleeping body is woken only by a
correction whose impulse would move it faster than its sleep speed (a cloth resting on a sleeping box lets it sleep).

Interface changes: `SoftBodyConstraintKind` gains `DIHEDRAL` and `TETHER` (`BENDING` is now a distance only);
`SoftBodyConstraint::rest_value` is a `std::optional<float>` (a dihedral rest angle may be negative, so "negative means
from the start" could not stay); `soft_body_constraint_particles(kind)`; `SoftBodyParticle::velocity`; constraints naming
a particle twice are refused when recorded.

Tests (+ 22): `gfx_physics_dynamics_tests` (17: a spring's energy within each scheme's bound and without drift, Runge–Kutta
4 on the exact trajectory; a circular orbit's radius, energy and angular momentum; immovable states and invalid states;
Runge–Kutta's four evaluations and exactness under constant forces; the presets' particles, constraints and masses;
colors that never share a particle; a rope's length held with and without tethers; a cloth hanging from two pins settles,
with distance and dihedral bending; a bent hinge returns to its rest angle (0 and 1 radian); a tetrahedral volume rests on
a floor with its volume; a particle rests on every kind of shape, comes out of a one-sided mesh it starts behind and stays
behind a two-sided one; friction holds on a 30° slope at 0.8 and lets slide at 0.2 with the analytic distance; self-
collision; momentum exchanged with a dynamic body; every integrator predicts; the same bits on one and four threads;
invalid steps), `Box3dSoftBodyTest.*` (4: a cloth falls onto Box3D colliders and rests; a rope holds a 4 kg Box3D box up
— it falls when unpinned; a soft cube pushes a Box3D body with the momentum it loses; the same bits on one and four
threads), `SteppedWorldTest.TheFrameworkStepsSoftBodiesForBackendsWithoutTheirOwn`.

Deviations from the proposal, for validation:

1. **Soft bodies step after the backend's step** (partitioned coupling, the bodies' motion interpolated), not inside one
   solver with the rigid bodies, and the backend receives **`BodyCorrection`s — impulses and displacements**: handing over
   impulses only (the first version) lost each step the motion the rope had given a body, and a 4 kg box hanging from a
   0.2 kg rope gained energy until it rose at 3.9 m/s.
2. **New interface**: `PhysicsWorld::soft_body_integrator()` / `replace_soft_body_integrator()`;
   `SteppedWorldBackend::simulates_soft_bodies()` / `apply_soft_body_corrections()`; `SoftBodyParticle::velocity`; an
   optional rest value; constraint kinds `DIHEDRAL` and `TETHER`.
3. **Long-range attachments are `TETHER` constraints** added by `add_long_range_attachments()` after the pins (anchors: the
   particles pinned to the world), not a setting of the soft body.
4. **Soft bodies do not collide with each other** (only with colliders and, when enabled, with themselves); self-collision
   has no friction.
5. **Soft bodies step one after the other**; the threads split a soft body's large colors (an integrator keeps scratch
   storage: one state at a time).
6. **`pin_to_world(description, particle)`** pins a particle where it starts (§C4's `pin_to_world(0)` cannot know where that
   is); `pin_to_world(particle, point)` takes a point.
7. **Integrators' contract made explicit**: `predict()` leaves the start positions in `previous_positions`; soft bodies
   make their velocities from their positions themselves, so any integrator (custom ones too) predicts them.
8. **Contact pre-stabilization**: an overlap a sub-step starts with is undone without velocity.
9. **Box3D's sleeping bodies** wake only for a correction whose impulse exceeds their sleep speed.

Known limits: a body much heavier than the particles it hangs from stretches them (XPBD's mass ratios: more sub-steps or
long-range attachments help); a tether moves only its first particle.

### PH7 as built (2026-10-25)

**The solver's operations** (`gfx.physics.interface:pipeline`): `SolverBody` (center of mass, orientation, velocities,
inverse mass, world inverse inertia with locked axes at 0, linear freedom), `SolverContact` (bodies, a manifold whose
points carry last step's impulses in and this step's out, the pair's friction and restitution, a rolling impulse),
`SolverJoint` (bodies — the second may be `NO_SOLVER_BODY`, the world —, a joint description or a `UserConstraint`, its
frames about the bodies' centers of mass, its impulses per row, its force and torque out), `SolverStep`, and
`Solver::solve(step)` (the bodies' velocities already include the step's forces: the solver solves velocities, moves the
bodies, corrects positions). **User constraints** (deferred to this phase by PH4): `ConstraintRow` (EQUALITY, INEQUALITY,
MOTOR or SPRING — `ConstraintRowKind` — with a full Jacobian on both bodies and an error), `ConstraintFrame`, the
application's `UserConstraint` (`row_count()`, `rows(first, second, rows)`), `ConstraintDescription`, a `Constraint`
object, `PhysicsWorld::create_constraint()`; edits, records, slots, cascades (a body's constraints go with it), checks in the
framework; `JointEvent::constraint`, `PhysicsErrorContext::constraint`. Box3D refuses them (`USER_CONSTRAINTS`).
`SteppedWorldBackend::broad_phase_component()` (and the other three) let a backend show its real components through
`PhysicsWorld::broad_phase()` …; every `replace_*()` is refused during a step.

**`SequentialImpulseSolver`** (`gfx.physics.dynamics:sequential_impulses`): warm starting from the impulses carried in;
velocity iterations over the joints' rows then the contacts — Coulomb friction at each point (sticking while the
impulse that stops the sliding fits the static cone, sliding at the dynamic coefficient beyond), rolling resistance (the
relative turn opposed up to the coefficient, a lever in meters, times the push), normal impulses with speculative
contacts (a point still apart lets the bodies approach until they touch); restitution for points that hit faster than the
threshold (Box2D v3's pass after the iterations); the bodies moved; position iterations (non-linear Gauss–Seidel, no
velocity bias): overlaps beyond `LINEAR_SLOP` and joint errors, `POSITION_CORRECTION_FACTOR` of each per iteration, at
most `MAXIMUM_LINEAR_CORRECTION` / `MAXIMUM_ANGULAR_CORRECTION`. **Each run of equality rows** (a point, an orientation:
up to six) is solved as one system (Cholesky), for velocities and positions, as Box2D solves its point constraints;
springs are soft constraints (frequency, damping ratio); motors are bounded by force × time. Joints and contacts are
**colored** by the dynamic bodies they move; a color of 128 or more runs on the task scheduler: the same bits on any
thread count. **`joint_rows()`** (public) makes every joint kind's rows: points as three world-axis rows, alignments and
orientations from the relative quaternion with **exact Jacobians** (d(2v)/dt = Ω · (w e + v × e), and the closed form for
2 atan2(v, w) angles), limits as INEQUALITY pairs, the slider's and six-DOF's translations with the first lever reaching
the second anchor; hinges turn about their frames' z, sliders along x, cone-twist joints twist about z; distance springs
of frequency 0 leave the length free between the limits.

**The reference backend** (`gfx.physics.backends.reference`, `source/physics/backends/reference/`, always built): a
`SteppedWorldBackend` of the abstraction's components — `SpatialBroadPhaseComponent<Bvh>`, `GjkNarrowPhase`,
`SequentialImpulseSolver`, `SemiImplicitEulerIntegrator` —, each replaceable between steps (a new broad phase gets every
live collider). Its records are the truth (it reads the pending records each step, so the framework's soft-body
corrections reach it as records). A step:

1. bodies an edit woke wake; impulses apply at once; forces and gravity are kept for the sub-steps;
2. collider boxes (continuous bodies' stretched by their motion) move in the broad phase; its pairs — of different bodies,
   one dynamic, not kept apart by a joint, not both at rest (then last step's manifolds stay) — collide on the scheduler's
   threads in chunks of 32 pairs, merged in order; the manifold cache gives returning points their impulses;
3. islands (union-find over dynamic bodies joined by contacts, joints and constraints; touching a moving kinematic body
   keeps one awake) wake as wholes;
4. **sub-steps** (`SolverDescription::sub_steps`, 4 by default, as Box3D's): the forces through the integrator, then the
   contact points moved with their bodies (their surface points kept in the bodies' frames), then the solve;
5. poses and velocities back to the records; impulses into the cache; joint forces, torques and breaking (BROKEN events,
   the joint disabled);
6. islands whose bodies all stayed slow for the sleep time fall asleep;
7. contact BEGIN (with HIT above the hit speed), END, the touching pairs' contacts; trigger ENTER, STAY, EXIT (overlap tests
   against the colliders' boxes where the step left them).

Tests (+ 20): `gfx_physics_reference_tests` (15: the factory; free fall; a stack at rest, asleep, its contacts, woken
through its island; a pendulum's period within 2 %; hinge limits, a rope, a breaking weld, a hinge motor's speed; contact
and trigger events; a kinematic platform lifting a crate; the same bits on one and four threads; six-DOF joints keeping
their locked axes and a limited turn; a dynamic mesh resting on its face, static friction holding on a 20° slope and
dynamic friction letting a pushed box slide; a bounce of 0.8² of the height, rolling resistance stopping a ball;
continuous bodies stopped by a 5 cm wall a discrete one passes; user constraints holding, carrying the weight, breaking;
each component replaced — the broad phase on sweep and prune and on an octree, the narrow phase, the solver, the
integrator —; a rope holding a body), `gfx_physics_dynamics_tests` + 3 (every joint kind's Jacobians against the measured
rates of its errors; an elastic collision exchanging velocities; a body hanging from a point carrying its weight),
`SteppedWorldTest.UserConstraintsAreRecordedAndGoWithTheirBodies`, and `CMakeDefect.GoogleTestDiscoveryNamesNoTarget` (below).

**Build**: CMake 4.4.0-rc2 writes no target name into GoogleTest's POST_BUILD discovery scripts, so the test executables of
a directory list their tests into one JSON file; with six physics test executables linking at once, the Release build
stopped on a clobbered listing. `gfx_add_test()` now links a directory's test executables one at a time (a Ninja job pool
per directory; directories still link in parallel), and a detector test passes while the defect lasts.

Deviations from the proposal, for validation:

1. **`ReferenceWorld<BroadPhase, NarrowPhase, Solver, Integrator>` is not a class template**: the backend composes its
   components at run time, each replaceable between steps through `PhysicsWorld::replace_*()` — a virtual call per
   component per sub-step (per candidate pair for the narrow phase); the hot loops are compile-time templates inside the
   components (`SpatialBroadPhaseComponent<Structure>`, the narrow phase's dispatch).
2. **The solver's operations and user constraints added to the interface** (`Solver::solve`, `SolverBody`/`Contact`/
   `Joint`/`Step`, `ConstraintRow`, `UserConstraint`, `Constraint`, `create_constraint`, `joint_rows()`), and
   `SteppedWorldBackend::*_component()`.
3. **The reference sub-steps** (`SolverDescription::sub_steps`): collided once, solved per sub-step. With one solve of eight
   iterations per step, a stack of five boxes rocked and its top box ended 2 cm aside.
4. **Static or dynamic friction is decided by the friction cone**, not by `static_friction_speed`: the sliding speed a
   solver sees always includes the step's gravity (a box on a slope never started static). `static_friction_speed`,
   `contact_hertz`, `contact_damping_ratio` and `maximum_push_speed` are Box3D's settings; the reference corrects
   positions with non-linear Gauss–Seidel and its constants.
5. **A motor's speed is the second body's relative to the first** (as Box3D's): a body joined to the world turns at minus
   it. Documented on `JointMotor`.
6. **Continuous collision is speculative**: a continuous body's contacts reach as far as it moves toward the other in the
   step (no time of impact).
7. **Triggers test every collider whose box overlaps theirs** (no tree): triggers × colliders box tests per step.
8. **A dynamic body whose colliders give no mass** (open meshes) moves with a unit mass and inertia.
9. **Replacing a component is refused during a step**, on every backend.
10. **Fixed in earlier code**: inertia tensors were declared singular below a determinant of 10⁻⁹ — a 2 cm sphere's is
    10⁻¹² — in the soft bodies' coupling (PH6) and the framework's default corrections; the test is now against float's
    smallest normal value.

### PH8 as built (2026-10-25)

**The sample** (`examples/physics/physics_example.cpp`, target `gfx_physics_example`, linking `gfx::physics` and
`gfx::camera` only): §C4's usage example on the API as built. A static floor; a crate whose hull is built at compile
time (`convex_hull<16, 10, 48>()` of a box of 1 × 0.8 × 1 m with its vertical edges cut off, an octagonal prism —
`static_assert`s check its 16 vertices and 10 faces) falls from 4 m; a 30 kg lid on a hinge along the crate's back edge
(limits [−1.2, 0] radians, breaking above 800 N: more than the lid's weight, far less than stopping it at the landing); a
24 × 24 cloth pinned at its two top corners (`cloth_preset`, `pin_to_world(description, particle)`); a trigger sphere of
2 m. The world steps asynchronously on a `ThreadTaskScheduler`; during the first step a perspective camera (whose
description is also a `constexpr` value: `look_at_orientation()`, `view_projection_matrix()` and `world_to_screen()` are
checked by a `static_assert`) and an orthographic map camera are made. After the steps it prints and checks: the crate
entered the zone once and stays (`TriggerEvent`s), rests upright on the floor; the hinge broke in the step the crate
landed (`ContactEvent` BEGIN, `JointEvent` BROKEN, `JointState`); the cloth's pins hold and the cloth hangs below them;
the ray through the screen's center hits the crate's surface (through `QueryEngine::ABSTRACTION`: its point on the plane
of a face of the compile-time hull, its normal that face's) and the backend's own ray cast agrees; the hit point projects
back to the center; the camera object's run-time matrices equal the compile-time ones; the frustums classify the crate,
the zone, a box behind the view and one off the map; the map's ray through the crate's pixel falls straight down onto it.
Options `--backend box3d|reference`, `--steps`, `--threads`; exit code 1 when a check fails. CTest
`PhysicsExample.{BOX3D,REFERENCE}` (`--steps 240 --threads 4`, label `example`). `examples/` is now added whenever
`GFX_BUILD_EXAMPLES` is on; each example checks the libraries it needs.

**Found by the sample, fixed**: on the reference backend the hinge never broke. A joint's force was the **last sub-step's**:
the landing is stopped within one sub-step (rigid contacts, non-linear Gauss–Seidel), so by the last one the hinge carried
only the lid's weight. A joint's force and torque are now the **step's average** — the impulse it gave over the whole step
per second, the mean of its sub-steps' forces — for `JointState` and for breaking (16 830 N at the landing; Box3D reports
6 357 N for the same landing, its soft contacts spreading the stop over several steps). Test
`ReferenceTest.JointsBreakUnderAShockWhateverTheSubSteps` (1, 4 and 8 sub-steps: the hinge breaks in the step the crate
lands or the next, not before; a block hanging from a ball joint reports its weight on any number of sub-steps); with the
old code it fails on 4 and 8 sub-steps.

**Found by the full test run, fixed**: the first Debug run counted 1007 tests for 975 — the Box3D and reference tests were
registered twice. GoogleTestAddTests.cmake deletes an executable's test list before appending the tests it finds; on
Windows `file(REMOVE)` of a file another process holds open fails **silently** (checked: a held file survives it without
an error), so a list being read during the build (VS Code's CMake Tools refreshes its tests after builds; a `ctest -N`)
got a second copy. `gfx_add_test()` now empties the list (`file(WRITE)`, which works where deleting does not) in a
POST_BUILD step just before the discovery (`cmake/scripts/GfxEmptyTestLists.cmake`); a relink with the list held open
writes one copy. Detector `CMakeDefect.FileRemoveIgnoresHeldFiles` (`cmake/scripts/GfxCheckFileRemove.cmake`: a file held
by `file(LOCK)` survives `file(REMOVE)`; Windows only).

**Documentation**: README — the libraries' table and status, a "Physics" section (a usage example compiled with
`-Werror` and run in a temporary target, the shapes, stepping, queries, events, joints, soft bodies, components, the
capability table of Box3D against the reference backend, custom backends), the example in "Examples", the threading
table, the options and the layout; `docs/architecture.md` §7 "Physics and camera libraries" with seven diagrams (targets
and modules, worlds/objects/backends/components, a step, the reference backend's step, collision, soft bodies and their
coupling, cameras) and a threads table, §1 and §8 updated (all 29 diagrams render in Mermaid 10.9.1 and 11.4.1);
`.vscode/launch.json` "gfx: Physics example" (asks for the backend). THIRD_PARTY_LICENSES.md already listed cglm, SIMDe
and Box3D (PH0, PH5). The comment of `QueryFailureKind::STEP_IN_PROGRESS` said queries wait for the step's end; they
return at once (the comment, and the README, now say so).

Deviations from the proposal, for validation:

1. **The scene differs from §C4 where the API differs** (§B "PH1 as built" 1 and 4, "PH6 as built" 6): `near_plane`/
   `far_plane`, `PerspectiveProjection{…}` instead of `perspective()`, `make_shape()` instead of `world->create_shape()`,
   `material.friction` with two coefficients, `.breaking = {.force = 800}`, `pin_to_world(description, particle)`;
   the crate is an octagonal prism rather than a wedge (a lid needs a flat top), and the cloth hangs behind it.
2. **The reference backend's joint forces are the step's average** (above): a change of PH7's behavior, for breaking and for
   `JointState::force`/`torque`.

### Follow-up after PH8: contacts of spinning and fast bodies (2026-10-25, found by the sandbox)

The physics sandbox's scripted shot (a continuous ball at 60 m/s into a pyramid of crates) scattered the crates on the
reference backend without a single contact event of the ball, and the ball sank into the pyramid.

1. **Contact points turned with spinning bodies.** Between sub-steps the reference backend moved each contact point as a
   point fixed on each body's surface, and the sequential-impulse solver's position stage did the same. A sphere spinning
   after a glancing hit (up to about 240 rad/s, a radian per sub-step) carried that point away from the contact: the
   separation grew by up to the radius (0.25 m measured), so the pair never counted as touching and positions were not
   corrected. Contact points are now kept on the shapes' **cores** (a sphere's center, a capsule's segment, a rounded box's
   inner box), which turn with their bodies, and the **margins** are added along the current normal — exact for spheres, for
   capsules about their axis, unchanged for shapes without a margin. Public addition: `SolverContact::first_margin` and
   `second_margin` (0 by default: a solver that ignores them behaves as before); the reference backend fills them from the
   shape (or the compound's child) each manifold is on.
2. **Hits resolved within a step were not reported.** With the points tracked correctly, the ball hit the front crate and
   rebounded (restitution 0.2) inside one step: its points never ended a step within the slop. A pair now touches in a step
   when the solver pushed at one of its points in any sub-step (`PairManifold::pushed`) or a point is within the slop, as
   Box2D v3 reports hits from the points that applied an impulse.

Tests (+ 2, `gfx_physics_reference_tests`): `SpinningBallsKeepTheirContactsUnderThem` (a ball spinning at 200 rad/s dropped
on the floor: BEGIN, touching at every step once landed, no sinking beyond 1.5 cm — with the margins at 0 it sinks to
0.199 m and reports nothing), `HitsThatBounceOffWithinAStepAreReported` (a bouncy continuous ball at 60 m/s against a wall:
BEGIN, HIT and END — without the pushed points none of them). In the sandbox the reference backend now reports the shot 13
frames after it (Box3D: 14).

### Follow-up after PH8: shallow overlaps of large shapes (2026-10-26, found by the sandbox's tour)

The sandbox's tour (every preset on both backends, each scene checked) found two of the showcase's bodies, a cylinder and
the compile-time hull, 1.3 m under the floor on the reference backend after landing among the other shapes. The narrow
phase gave the resting cylinder (2.9 mm deep in the floor, a box 100 m wide and 1 m thick) a manifold whose normal pointed
down with a separation of -1.64 m, the way out through the floor's bottom face, and the solver pushed it there within one
step.

**GJK took a shallow overlap for a sliver.** Its last tetrahedron held the origin, but it was 100 m across and millimeters
high: its volume (46.9) was under 1e-4 of its longest edge cubed (286), the test PH4 added for slivers (three support points
of a cylinder's rim almost together, whose face signs are noise). Its nearest face, 0.8 mm above the origin, then made the
shapes apart, with a normal pointing away from the overlap, and face clipping picked the floor's bottom face. A sliver is
now a tetrahedron whose volume is within the rounding error of its corners: the determinant's first-order error, each
corner off by float's epsilon times the largest coordinate (times the faces' parallelogram areas), plus epsilon times the
product of the three edges, with a safety factor of 16 (`is_sliver()` in `gjk.cppm`). PH4's sliver (its regression test
with exact poses) still is one; thin tetrahedra of exact corners no longer are. Twenty sweeps of other seeds over the
GJK/EPA, contact, collision and shape-query tests found no failure (only two samples where the random rays hit a hull
fewer times than the test asks for coverage).

Tests (+ 2): `GjkEpaTest.ShallowOverlapsOfALargeBoxAreOverlaps` (the cylinder's exact pose: GJK reports an overlap, the
distance is -2.88 mm with the normal up, the manifold's points are on the top face) and
`ReferenceTest.FallingConvexShapesStayOnTheFloor` (the showcase's eighteen falling shapes, none goes into the floor). Both
fail with the old test.

---

## C. The API as proposed (for validation, not final code)

### C1. Enumerations (excerpt of `source/physics/enums.cppm`)

```cpp
enum class PhysicsBackendKind : std::uint8_t { BOX3D, REFERENCE, NULL_BACKEND, CUSTOM };   // BACKEND_LIBRARY "gfx::physics"
enum class CameraBackendKind : std::uint8_t { STANDARD, CUSTOM };                         // BACKEND_LIBRARY "gfx::camera"
enum class BodyKind : std::uint8_t { STATIC, KINEMATIC, DYNAMIC };
enum class ShapeKind : std::uint8_t { SPHERE, CAPSULE, CYLINDER, CONE, BOX, CONVEX_HULL, TRIANGLE_MESH, HEIGHT_FIELD, COMPOUND };
enum class JointKind : std::uint8_t { DISTANCE, HINGE, BALL_AND_SOCKET, SLIDER, FIXED, CONE_TWIST, SIX_DEGREES_OF_FREEDOM };
enum class DistanceMode : std::uint8_t { ROD, SPRING, ROPE, STUB };
enum class ConstraintKind : std::uint8_t { CONTACT, JOINT, ATTACHMENT, USER };
enum class SolverKind : std::uint8_t { SEQUENTIAL_IMPULSE, SOFT_STEP, XPBD };
enum class IntegratorKind : std::uint8_t { SEMI_IMPLICIT_EULER, VERLET, RUNGE_KUTTA_4, XPBD };
enum class SpatialStructureKind : std::uint8_t { BVH, OCTREE, QUADTREE, BSP, UNIFORM_GRID, SWEEP_AND_PRUNE };
enum class MotionQuality : std::uint8_t { DISCRETE, CONTINUOUS };
enum class TriggerEventKind : std::uint8_t { ENTER, STAY, EXIT };
enum class QueryFailureKind : std::uint8_t { NO_HIT, INVALID_HANDLE, STEP_IN_PROGRESS, UNSUPPORTED, CAPACITY_EXCEEDED };
enum class CameraProjectionKind : std::uint8_t { PERSPECTIVE, ORTHOGRAPHIC };
enum class CameraControllerKind : std::uint8_t { FREE_FLY, ORBIT, FIRST_PERSON };
enum class DepthRange : std::uint8_t { ZERO_TO_ONE, NEGATIVE_ONE_TO_ONE };
enum class FrustumIntersection : std::uint8_t { OUTSIDE, INTERSECTING, INSIDE };
inline constexpr std::uint32_t MAXIMUM_MANIFOLD_POINTS = 4;
inline constexpr float DEFAULT_RESTITUTION_THRESHOLD = 1.0F;
```

### C2. Primitives and the compile-time path

```cpp
inline constexpr Aabb crate_bounds = Aabb::from_center({0, 1, 0}, {0.5F, 0.5F, 0.5F});
static_assert(crate_bounds.contains(Vector3{0, 1.2F, 0}));

// A hull known at compile time: built, and its face count checked, by the compiler.
inline constexpr auto wedge = convex_hull<8>(std::array{Vector3{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}});
static_assert(wedge.face_count() == 5);

template<class Shape>
concept ConvexShape = requires(const Shape& shape, Vector3 direction) {
    { shape.support(direction) } -> std::same_as<Vector3>;
    { shape.margin() } -> std::same_as<float>;
    { shape.bounds() } -> std::same_as<Aabb>;
};
// GJK over any two convex shapes in any poses; no backend involved.
template<ConvexShape A, ConvexShape B>
constexpr auto gjk_distance(const A& a, const Transform& pose_a, const B& b, const Transform& pose_b) -> GjkResult;
```

### C3. Spatial structures and integrators

```cpp
template<class Structure>
concept SpatialStructureConcept = requires(Structure& structure, const Structure& reader, ColliderHandle key, Aabb box,
                                           Ray ray, Frustum frustum, SpatialProxyHandle proxy) {
    { structure.insert(key, box) } -> std::same_as<SpatialProxyHandle>;
    structure.remove(proxy);
    { structure.update(proxy, box) } -> std::same_as<bool>;          // true when the proxy moved in the structure
    reader.query(box, [](ColliderHandle) { return true; });         // visitors return false to stop
    reader.query(ray, [](ColliderHandle, float) { return true; });
    reader.query(frustum, [](ColliderHandle) { return true; });
    reader.pairs([](ColliderHandle, ColliderHandle) {});
    reader.iterate([](ColliderHandle, const Aabb&) {});
};
using DefaultBroadPhase = SpatialBroadPhase<Bvh>;
using FlatWorldBroadPhase = SpatialBroadPhase<Quadtree<AxisPlane::XZ, 8>>;   // structure parameters at compile time

class Integrator : public PhysicsObject
{
public:
    [[nodiscard]] virtual auto kind() const noexcept -> IntegratorKind = 0;
    [[nodiscard]] virtual auto is_external() const noexcept -> bool = 0;     // false: the backend integrates itself
    [[nodiscard]] virtual auto requirements() const noexcept -> IntegratorRequirements = 0;
    virtual void predict(const IntegrationState& state, float seconds) = 0;  // throws BackendCapabilityError by default
    virtual void finalize(const IntegrationState& state, float seconds) = 0;
};
```

### C4. World, camera and the requested usage example

```cpp
import std;
import gfx.physics;
import gfx.camera;

using namespace gfx::physics;

int main()
{
    std::unique_ptr<PhysicsWorld> world = create_physics_world({.gravity = {0, -9.81F, 0}, .asynchronous_stepping = true},
                                                               PhysicsBackendKind::BOX3D);

    // A static floor, a dynamic body with a hull built at compile time.
    auto floor = world->create_rigid_body({.kind = BodyKind::STATIC});
    auto floor_collider = floor->add_collider({.shape = world->create_shape(box_shape({50, 0.5F, 50})), .friction = {.dynamic = 0.8F}});
    auto crate = world->create_rigid_body({.kind = BodyKind::DYNAMIC, .pose = {.position = {0, 4, 0}}});
    auto crate_collider = crate->add_collider({.shape = world->create_shape(hull_shape(wedge)), .density = 500});

    // A cloth pinned at two corners (the reference XPBD path next to Box3D).
    SoftBodyDescription cloth = cloth_preset({.columns = 24, .rows = 24, .spacing = 0.05F, .origin = {-0.6F, 3, 0}});
    cloth.pins = {pin_to_world(0), pin_to_world(23)};
    auto flag = world->create_soft_body(cloth);

    // A breakable hinge between the crate and a lid; a trigger volume.
    auto lid = world->create_rigid_body({.kind = BodyKind::DYNAMIC, .pose = {.position = {0, 4.6F, 0}}});
    auto lid_collider = lid->add_collider({.shape = world->create_shape(box_shape({0.5F, 0.05F, 0.5F})), .density = 300});
    auto hinge = world->create_joint(hinge_joint({.first = crate->handle(), .second = lid->handle(), .anchor = {0, 4.55F, -0.5F},
                                                  .axis = {1, 0, 0}, .limits = {{-1.2F, 0}}, .break_force = 800}));
    auto zone = world->create_trigger({.shape = world->create_shape(sphere_shape(2)), .pose = {.position = {0, 1, 0}}});

    // An asynchronous step; meanwhile, a camera; then a GJK ray cast through the abstraction's BVH narrow phase.
    world->begin_step(1.0F / 60.0F);
    std::unique_ptr<gfx::camera::Camera> camera = gfx::camera::create_camera(
        {.projection = gfx::camera::perspective({.vertical_field_of_view = 1.0F, .aspect_ratio = 16.0F / 9.0F, .near = 0.1F, .far = 500}),
         .position = {0, 3, 8}},
        CameraBackendKind::STANDARD);
    camera->look_at({0, 1, 0}, {0, 1, 0});
    std::unique_ptr<gfx::camera::Camera> map = gfx::camera::create_camera(
        {.projection = gfx::camera::orthographic({.width = 100, .height = 100, .near = 0, .far = 200}), .position = {0, 100, 0}},
        CameraBackendKind::STANDARD);
    world->wait_step();
    const Ray ray = camera->screen_to_world_ray({960, 540}, {1920, 1080});
    const QueryResult<RayHit> hit = world->ray_cast(ray, 100, {.engine = QueryEngine::ABSTRACTION});   // BVH + GJK
    for (const TriggerEvent& event : world->events().triggers) { /* ENTER / STAY / EXIT */ }
    return hit ? 0 : 1;
}
```

---

## D. Questions

Each has a recommendation; "as recommended" answers them all.

**Answers (2026-10-24)**: "do as recommended" — every question below as recommended — with a rule for the math: *what
cannot run at compile time runs on SIMD when available, through existing MIT libraries, parallelized where possible*.
Asked next and answered: the libraries are **cglm** (C, MIT: matrices, quaternions, 4-component vectors) and **SIMDe**
(MIT: portable SSE/AVX intrinsics for kernels of our own), both downloaded and pinned. So question 3 now reads: own
constexpr math in core for the compile-time paths, cglm and SIMDe for the run-time ones (§B "PH0 as built").

1. **Repository**: this repository (`source/physics`, §0 row 1) or a separate one? *Recommended: this repository.*
2. **Dependencies**: physics and camera on gfx::core only, with build checks proving it (§0 row 2)? *Recommended: yes.*
3. **Math**: own constexpr math in core (`gfx.core.math`, portable compile-time paths) instead of glm (§0 row 6)?
   *Recommended: own math; a glm conversion header later if wanted.*
4. **Handles**: core's `Handle<Tag>` class instead of `struct Handle { value; }` (§0 row 3)? *Recommended: yes.*
5. **Errors**: `PhysicsError : gfx::Error`; capability gaps throw core's `BackendCapabilityError` (so not a
   `PhysicsError` subclass) with the same context (§0 row 4)? *Recommended: yes.*
6. **Queries**: `std::expected<T, QueryFailure>` with an allocation-free failure convertible to `PhysicsError`, rather than
   `std::unexpected(PhysicsError{…})` (§0 row 5)? *Recommended: yes.*
7. **Layout**: one root `source/physics` with one `enums.cppm` for physics and camera, three targets (`physics_common`,
   `camera`, `physics`) (§0 row 7)? Or the camera as its own library with its own enumerations file? *Recommended: the
   former, as the request's layout says.*
8. **Box3D**: the real Box3D (erincatto/box3d, MIT, alpha), pinned, with the capability errors of §A12 (dynamic meshes,
   height fields and compounds; different static and dynamic friction; six-DOF joints; replacing components)?
   *Recommended: yes.* (I will ask before downloading it, with its size.)
9. **Soft bodies under Box3D**: the abstraction's XPBD simulation, two-way coupled with Box3D bodies (§0 row 9), or
   `BackendCapabilityError`? *Recommended: XPBD in the abstraction.*
10. **Reference backend** (PH7): a backend composed only of the abstraction's components (sequential impulses, the
    spatial structures, GJK/EPA, the integrators)? *Recommended: yes, after Box3D; it is what makes the component
    interfaces real.*
11. **Custom backends**: direct construction, plus the CMake option `GFX_PHYSICS_CUSTOM_BACKEND_TARGET` for the factory's
    `CUSTOM` kind, no run-time registry (§0 row 11)? *Recommended: yes.*
12. **Edits**: always applied at the start of the next step, whatever the timing, getters showing the last completed step
    (§A14)? Or applied at once when no step runs? *Recommended: always at the next step (deterministic).*
13. **Camera defaults**: right-handed, −Z forward, +Y up; depth `ZERO_TO_ONE`, y up in normalized device coordinates;
    reverse depth and infinite far plane as options (§A13)? *Recommended: yes.*
14. **Precision**: single precision now; double positions (Box3D's large-world variant) later? *Recommended: later, as an
    option.*
15. **"Stub" distance joints**: what is a "stub"? *My reading: a rod of zero length (two points held together); tell me
    if you meant something else.*
16. **CI**: no workflow now; physics joins the project-wide CI of step 6 (§0 row 13)? *Recommended: yes.*
17. **Phases**: PH0–PH8 in the order of §B, starting with PH0 (foundations) and PH1 (camera, which needs nothing else)?
    *Recommended: yes.*
