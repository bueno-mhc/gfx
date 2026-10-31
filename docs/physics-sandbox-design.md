# gfx physics sandbox: an interactive application over every library — decisions and plan

Status: **answers given 2026-10-25** (below); **S1 committed** (c5d0ac9); **S2 committed** (da1c14e); **S3 committed** (378ee8c); **S4 committed** (7727d29); **S5 committed** (abbb007); **S6 implemented** (§C "S6 as built"), awaiting validation: every step of the plan is built.

The request (2026-10-25): *a visual test combining all abstractions, for the physics simulation — an application where I
can interact and see the physics being calculated and executed, with graphics.* It is `examples/physics_sandbox`: a window
(platform library) showing a physics world (physics library, Box3D or the reference backend) drawn with Vulkan (graphics
library) through cameras (camera library), with panels (UI library) to change and inspect the simulation while it runs.

## A. Answers (user, 2026-10-25)

| Question | Answer |
|---|---|
| Where turning a world into geometry lives | **A renderer-independent physics module**, `gfx.physics.debug_draw` (core only, as Box3D's b3DebugDraw or Jolt's DebugRenderer): shapes as triangle meshes, colored lines; the example draws them with Vulkan |
| What the application lets you do | **All four groups**: interaction (orbit and fly cameras, pick and drag bodies, shoot balls, spawn and delete objects); scene presets (stacks and pyramids, dominoes, joint chains such as a ragdoll-like figure and a rope bridge, cloth over shapes, soft volumes, friction slopes, bouncing balls, triggers); live controls (pause, single step, slow motion, gravity, sub-steps and iterations, Box3D/reference at run time, the reference backend's broad phase and integrator swapped); debug views (contacts and normals, bounding boxes, the broad phase's structure, joints, sleeping bodies and islands in colors, statistics with per-phase timing plots) |
| How it looks | **Lit solids**: directional and ambient light, 4x MSAA, a grid floor, colors by body state, an optional wireframe overlay (no shadows) |
| How CTest checks it | **A scripted interaction** on each backend with the validation layers: a pick, a drag and a shot, their outcomes checked (the dragged body moved, the ball hit), no validation message |

## B. Plan

Each step is built, tested in Debug and Release, reported, and validated before the next.

| Step | Content | Checked by |
|---|---|---|
| **S1 Debug drawing** | `gfx.physics.debug_draw`: every shape kind as a mesh in its own frame; `DebugLines` | meshes against their shapes (surface, normals, closed, volumes converging), lines against their definitions |
| **S2 Skeleton** | `examples/physics_sandbox`: window, Vulkan device and swapchain, depth and 4x MSAA, lit instanced meshes (one mesh per shape, cached; per-collider poses and colors in a bindless storage buffer), the debug lines, a grid floor, an orbit camera, a first scene stepped asynchronously at 60 Hz, a statistics panel | a CTest run of N frames with the validation layers on each platform backend |
| **S3 Interaction** | picking through `screen_to_world_ray()` and `ray_cast()`, dragging, shooting balls, spawning and deleting; free-fly camera; a scripted mode for CTest | the scripted pick, drag and shot, their outcomes |
| **S4 Scenes** | the presets; switching backends at run time (the scene rebuilt from its preset) | each preset steps on each backend without errors |
| **S5 Controls and debug views** | the panels and overlays of §A | the scripted run toggles each view and control |
| **S6 Documentation** | README, architecture overview, this record; CTest on both physics backends | the docs' snippets compiled, diagrams rendered |

Defaults chosen without asking (for validation; each can change):

1. **Threads**: the main thread owns the platform, the input system, the UI and the world; the world steps on its own thread
   (`asynchronous_stepping`) at a fixed 60 Hz, rendering interpolates poses between the last two completed steps; the frame is
   drawn on the main thread (the UI editor example already shows a render thread).
2. **Dragging**: a kinematic "hand" body that follows the mouse ray at the picked depth, joined to the picked body by a
   distance joint in SPRING mode (both backends have it; Box3D has no user constraints).
3. **Backend switch**: worlds cannot move between backends, so a switch destroys the world and rebuilds the current
   preset on the other backend (the camera stays).

## C. As built

### S1 as built (2026-10-25)

Module **`gfx.physics.debug_draw`** (`source/physics/debug_draw/`, in `gfx::physics_components`, re-exported by
`gfx.physics`):

- **`tessellate(shape, description)`** → `DebugMesh` (vertices: position and outward unit normal in the shape's frame;
  indices counter-clockwise seen from outside; `two_sided` for two-sided triangle meshes), or appended to a mesh.
  `TessellationDescription::segments` (32) around a full circle. Spheres, capsules, cylinders and cones are revolved
  profiles (smooth normals on curved parts, flat caps); boxes and hulls are their faces, and when rounded, a strip of
  quads along each edge (the arc between its faces' normals) and a spherical patch at each corner (rings out to the arcs
  between its faces' normals) — the exact rounded surface at every vertex, with the arcs computed from the face of the
  smaller index so faces, strips and patches share their vertices exactly (the mesh is watertight); triangle meshes and
  height fields are their triangles with flat normals; compounds their children at their poses. Triangles are wound by
  their vertices' normals and triangles without area are dropped (poles, edges between coplanar faces).
- **`enclosed_volume(mesh)`** (divergence theorem).
- **`DebugLines`**: `add_line`, `add_box` (axis-aligned or oriented), `add_axes`, `add_arrow`, `add_cross`, `add_circle`,
  `add_contacts` (a cross per point, an arrow along the normal); `DebugColor` with `packed()` (RGBA8, red lowest);
  `clear()` keeps the storage.

Tests (+ 7, in `gfx_physics_tests`, `tests/physics/debug_draw_test.cpp`): for a sphere, capsule, cylinder, cone, box,
rounded box, hull and rounded hull — closed (every edge, by its ends' positions, runs once each way), every vertex on the
surface (`closest_point_on_shape()` within 2·10⁻⁴), unit normals pointing out, triangles facing out, bounds inside the
shape's and reaching 98 % of them, enclosed volumes within 3 % at 32 segments and 0.2 % at 128 (exact formulas; Steiner's
formula for the rounded box and hull) and converging; an unrounded box is 12 triangles and 24 vertices; triangle meshes and
height fields keep their triangles, order and heights; compounds are their children (vertices on the compound's surface,
volumes adding up); too few segments are refused; every line helper. Checked: without the corner patches, the closedness
and volume tests fail for both rounded shapes.

### S2 as built (2026-10-25)

`examples/physics_sandbox` (target `gfx_physics_sandbox`; links the platform bridge, the UI and its platform adapter, the
physics and camera libraries): three units of its own and four shaders.

- **`physics_sandbox.renderer`** (`renderer.cppm`/`.cpp`): `SceneRenderer` — every shape tessellated once
  (`gfx.physics.debug_draw`, 32 segments) when an instance first names it, its vertices (six words: position, normal) and
  indices copied by the graphics library's `Uploader` into two device-local buffers every mesh shares (32 MiB / 16 MiB; a
  mesh waits a frame when a frame's 16 MiB of staging is used up); a frame's instances (nine words: translation,
  orientation, packed color, flags) and debug lines (four words) in a host-visible `TransientBufferAllocator` region;
  `render()` records one pass — a 4x multisampled color target (when the device has 4 samples for color and depth)
  resolved into the frame's image, a D32 depth target cleared to 0 for reversed depth (GREATER_OR_EQUAL), both transient
  and made again when the window's size changes — with one instanced `draw_indexed()` per mesh (instances sorted by mesh,
  one-sided meshes with back faces culled, two-sided ones without) and the lines last (depth tested, not written).
  Shaders (`gfx_add_shaders()`, module `physics_sandbox.shaders`): `mesh.vert` pulls vertices and instances from the
  bindless storage buffers (no vertex input), turns positions and normals by the instance's quaternion, converts sRGB
  colors to linear; `mesh.frag` lights them (a directional light, a sky/ground ambient), draws the floor's grid (flag 1:
  lines every meter and stronger every five, antialiased with `fwidth`) and a highlight tint (flag 2, for S3);
  `line.vert`/`line.frag` for the lines.
- **`physics_sandbox.scene`** (`scene.cppm`/`.cpp`): `Scene` — the world (`asynchronous_stepping`, on a
  `ThreadTaskScheduler`) and `SceneBody`s (the body, its colliders with their shapes and poses, kind, color, flags, the
  poses of the last two completed steps, awake). `advance(seconds)`: completes the step begun by the previous call, runs
  the steps due beyond one at once (a frame longer than 0.25 s is not caught up), begins the next on the world's thread —
  it runs while the frame is drawn —; `drawn_pose()` interpolates the last two completed poses (core math's
  `interpolate(Transform, Transform, fraction)`). `build_showcase()`: a 80 × 80 m floor with the grid, a pyramid of 21
  crates, three of each convex kind (sphere, capsule, cylinder, cone, a hull built at compile time — a plinth —, a
  compound dumbbell) falling from staggered heights, a static ramp with three balls, a static height-field mound.
- **`physics_sandbox.cpp`**: platform and window, Vulkan device and `WindowSwapchain`, `FrameTimeline` and
  `FrameCommandPools`, the input system (right drag orbits, middle drag pans, the wheel zooms, Escape quits; ignored while
  the UI wants the mouse), a perspective camera (vertical field of view 0.9, near 0.05 m, no far plane, reversed depth,
  +Y down in normalized device coordinates for Vulkan) turned by the camera library's orbit controller, the UI (Noto Sans
  atlas built by `gfx_add_ui_atlas()`, 15 points) with a statistics panel (backend, threads, step, bodies awake, pairs,
  contact points, each step phase's time, a plot of the last 180 steps' times, the frame time and rate, instances, draws,
  lines, meshes). A frame: events → actions → camera → `Scene::advance()` → the scene's instances (sleeping bodies
  darker) and the world's axes → the UI → the scene's pass, then the UI's pass over the resolved image (loaded) → present.
  Options `--backend`, `--physics`, `--device`, `--threads`, `--frames` (one step per frame: the same frames on any
  machine), `--validation`, `--capture <file.ppm>` (the last frame read back; swapchain images made copyable for it).

CTest: `PhysicsSandbox.{SDL3,GLFW}` (`--frames 240 --validation`, label `example`). Checked by hand: Box3D on NVIDIA with
SDL3, the reference backend on Intel with GLFW, Box3D on Intel with SDL3 — no validation message; the captures show the
lit shapes, the grid, the panel.

Deviations from the plan, for validation:

1. **Two passes**: the UI renderer's pipeline has no depth format, and a pipeline drawing in a pass must match the pass's
   depth attachment, so the scene's pass (multisampled, depth) is resolved into the frame's image and the UI draws in a
   second pass that loads it (the UI's text is antialiased by its own distance fields).
2. **`--capture`** (not planned): the last frame written to a PPM file, used to check the rendering here and, in S3, by the
   scripted run.

### S3 as built (2026-10-25)

Module **`physics_sandbox.interaction`** (`interaction.cppm`/`.cpp`) and changes to the scene and the program:

- **`FrameCommands`**: what a frame asks — the pointer (framebuffer pixels) and whether it is over the scene, grab
  pressed/held/released, push (meters along the pointer's ray while dragging), shoot, spawn (a `SpawnKind`), remove,
  toggle fly, look, pan, fly movement, zoom. The program makes them from the input system's actions (`commands_from()`:
  the mouse ignored while the UI wants it, the keyboard while the UI types; the wheel pushes a dragged body, else zooms) or
  takes them from the script: **the same code runs either way**.
- **`CameraRig`**: the orbit controller and a first-person one (`FirstPersonControllerDescription`, 8 m/s: yaw about the
  world's up, no roll, W A S D on the horizontal plane, E and Q up and down); F switches — flying starts from the camera's
  pose, orbiting again turns about the point ahead of it at the orbit's distance (yaw and pitch from the camera).
- **`Interaction`** (between steps: after `Scene::finish()`, before `Scene::advance()`, since a query during a step
  returns STEP_IN_PROGRESS): a ray through the pointer (`Camera::screen_to_world_ray()`) cast against the world
  (`PhysicsWorld::ray_cast()`, the backend's queries) finds the dynamic body hovered (highlighted, flag 2); grabbing keeps
  the point grabbed in the body's frame and its distance along the ray; **dragging applies a spring-damper force at that
  point** (4 Hz, critically damped, scaled by the body's mass, carrying its weight, at most 40 g) and damps its spin
  (`apply_force_at()`, `apply_torque()`); shooting throws a ball (radius 0.25 m, 3000 kg/m³, continuous collision) at 60 m/s
  along the ray; spawning puts a body of the kind asked 0.9 m off the surface pointed at (or 10 m ahead); removing destroys
  the hovered body. The drag's spring and the point pointed at are drawn as lines.
- **`Scene`**: a `ShapeLibrary` (one shape per spawn kind and the ball, shared with the showcase: one mesh each),
  `spawn()`, `shoot()` (at most 40 balls: the oldest goes), `remove_body()`, `find()` by body handle, `body_of()` by
  collider, the contact events of the steps completed since `clear_events()`; `build_showcase()` returns a `Showcase` (the
  pyramid's crates, its top and middle crates, the first sphere) for the script.
- **`Script`** (`--script`, 420 frames of one step): grab the pyramid's top crate (frame 60), drag it 2.5 m aside and 2 m up
  over a second, hold, release (150); shoot at the pyramid's middle (180); spawn a dumbbell on the floor (270); remove the
  body under the first sphere's pixel (300); fly forward for a second (330 to 390); then six checks: the top crate grabbed;
  dragged up (> 1 m) and aside (> 1.5 m), within 1.2 m of the pointer's target; a contact event between the ball and a
  crate; the dumbbell at rest within 1.5 m of the point; one body fewer and the removed one gone; 8 m flown (±0.6), orbiting
  again. The exit code is 1 when one fails.
- The panel: a summary (backend, step, bodies, pairs, contact points, step and frame times, the plot of step times with the
  slowest, the camera's mode, what the pointer points at, the interaction's counts) and sections that open and close (step
  phases, rendering, controls — open); the window sizes itself. `--capture-frame <index>` captures another frame than the
  last one.

CTest: `PhysicsSandbox.SDL3` (Box3D) and `PhysicsSandbox.GLFW` (the reference backend) run the script with the validation
layers. Both pass every check on NVIDIA; GLFW with the reference backend on Intel too.

**Found by the sandbox, fixed in the reference backend** (physics design, §B "Follow-up after PH8"): a fast shot ball pushed
into the pyramid without any contact event. Contact points were moved between sub-steps (and in the solver's position
stage) as points fixed on each body's surface, so a sphere spinning after a glancing hit (up to about 240 rad/s here, a
radian per sub-step) carried its point away from the contact: its separation grew by up to its radius, the pair never
counted as touching, and the ball sank into what it hit. Contact points are now kept on the shapes' cores with the margins
along the normal (`SolverContact::first_margin`/`second_margin`). A hit resolved and bounced off within one step then still
went unreported: a pair now touches in a step when the solver pushed at one of its points in a sub-step, or when a point is
within the slop.

Deviations from the plan, for validation:

1. **Dragging is a force, not a joint**: a spring-damper force at the grabbed point instead of a kinematic hand body joined
   by a SPRING distance joint. A distance joint of length 0 acts along one line only, which turns when the two points meet
   (the reference backend's row direction falls back to +Y at zero length), and damps only along it; the force damps in
   every direction, needs no extra body, and behaves the same on both backends.
2. **Flying is first person** (yaw about the world's up, no roll, horizontal movement with E/Q for height) rather than the
   free-fly controller, whose mouse turns about the camera's own axes and so rolls the view as it goes.
3. **`--capture-frame`** (not planned), to check a frame in the middle of the script.
4. **Two fixes to the reference backend** (above), with a public addition: `SolverContact::first_margin`/`second_margin`
   (0 by default: a solver that ignores them behaves as before).

### S4 as built (2026-10-26)

Module **`physics_sandbox.presets`** (`presets.cppm`/`.cpp`) and changes to the scene, the renderer and the program:

- **Nine presets** (`Preset`, `preset_information()`, `find_preset()`, `build_preset(preset, scene)` → `PresetResult`: the
  view the orbit starts with, the showcase's handles for the script): **showcase** (S2's scene); **stacks** (a pyramid of
  eight levels, a tower of ten crates, a column of rounded crates, a wall of staggered bricks); **dominoes** (one every
  half meter along a sine, each turned along it, the first tipped over); **chains** (fourteen capsules on ball-and-socket
  joints hanging from a point, a rope bridge of sixteen planks on hinges sagging between two posts with three crates
  dropped on it, a figure of ten parts — cone-twist neck, shoulders and hips, hinged elbows and knees with limits, a
  six-degree-of-freedom waist — and a wrecking ball on a distance joint swinging into a stack); **cloth** (two cloths of
  22 × 22 particles falling flat onto a sphere and a box, a curtain hanging by its top row from a bar with long-range
  attachments); **soft volumes** (four tetrahedral jelly cubes from rigid to soft edges, a rope pinned to the world holding
  a crate pinned to its end); **friction** (crates of friction 0 to 0.6 on a 25° slope of friction 1 — mixed as square
  roots: the first three slide —, a crate of static friction 0.6 and dynamic 0.05 that sticks until pushed, balls rolling
  with rolling resistance 0, 0.03, 0.15); **bouncing** (eight balls of restitution 0 to 0.95, bouncy crates, balls on a
  tilted plate); **triggers** (three zones — two boxes, a sphere — and an arm on a motorized hinge sweeping sixteen bodies
  around, balls falling into each zone).
- **What a backend refuses**: a preset asks for what it shows and catches `BackendCapabilityError`; the backend gets the
  nearest thing it has and the scene keeps a note of what was refused (capability and the reason's first line) and what
  was made instead, shown in the panel and printed. Box3D refuses three: rounded crates (sharp ones), separate static
  friction (0.05 for both), the six-degree-of-freedom waist (a cone-twist one).
- **Scene**: soft bodies (`add_soft_body(description, surface, color)`: the triangles drawn, or none for a sphere at each
  particle; their particles' last two positions, interpolated like poses), joints (`add_joint()`; distance joints drawn as
  a line between their anchors while both bodies exist), triggers (`add_trigger(name, shape, pose, color)`: the colliders in
  each from ENTER and EXIT events, those of removed bodies dropped, the ENTER count; drawn as lines, brighter while
  occupied), notes, `add_floor()`, `palette()`. `add_body()` makes no body when a collider is refused.
- **Renderer**: `add_surface(key, indices, positions, color)` — a moving surface's indices stored once per key (a mesh
  without vertices), its vertices and area-weighted normals written each frame into the frame's buffer, one two-sided draw
  each (an identity instance after the meshes'; `MeshPush::vertex_word` says where its vertices start). `clear_meshes()`
  forgets the surfaces too.
- **Switching**: the panel's Scene section picks the preset (a combo) and the physics backend (radio buttons, the backends
  built), with a reset button (and R); any of them builds the scene again at the start of the next frame — the step in flight
  finishes, the device waits idle, the interaction, the scene and the renderer's meshes go, the preset is built into a new
  world, the camera aims at its view. Each preset's view is shifted left by 0.3 of its distance so the scene sits right of
  the panel; the controls section starts closed so the panel fits.
- **`--preset <name>`** starts with another scene; **`--tour`** builds every preset on every physics backend in turn (90
  frames each, one step per frame) through the same rebuilding and checks each scene before leaving it: every dynamic body's
  and particle's position and velocity finite, none lower than 1 m under the floor's top, none faster than 30 m/s, the steps
  all taken — the lowest body or particle is named in the details. `--script` stays in the showcase.

CTest: `PhysicsSandbox.Tour` runs the tour on the first platform backend with the validation layers (eighteen scenes, about
9 s in Debug on NVIDIA); the scripted runs are unchanged and pass. Captures of every preset on both backends were looked at.

**Found by the tour, fixed in the narrow phase** (physics design, §B "Follow-up after PH8: shallow overlaps of large
shapes"): two of the showcase's bodies went through the floor on the reference backend. GJK's sliver test called a
tetrahedron 100 m across and millimeters high a sliver, its nearest face made the shapes apart with a normal pointing down,
and the contact pushed the body out through the floor's bottom face. A sliver is now a tetrahedron whose volume is within the
rounding error of its corners.

Deviations from the plan, for validation:

1. **More than the presets listed**: the showcase is a preset; the chains preset adds a hanging chain and a wrecking ball, the
   cloth preset a curtain, the soft volumes a rope holding a crate, the friction preset rolling resistance.
2. **Refusals become substitutes and notes** (not planned): a preset still loads on a backend that lacks a feature.
3. **`--tour`** and its CTest are how "each preset steps on each backend without errors" is checked.
4. **A fix to GJK** (above), with two regression tests.
5. Soft bodies cannot be grabbed (ray casts find colliders only), and they do not collide with each other (the framework's
   XPBD solver collides particles with colliders and, optionally, with their own body's), so the jelly cubes are dropped
   apart.

### S5 as built (2026-10-31)

Module **`physics_sandbox.controls`** (`controls.cppm`/`.cpp`), changes to the scene, the renderer and the program, and
an addition to the physics library (below).

- **Simulation** (`SimulationSettings`, `SimulationControl`): pause (P), single step (N; `Scene::hold()` completes the step
  in flight and begins none, drawing the latest poses; `Scene::step_once()` begins exactly one), slow motion (a time scale
  from 0.05 to 4: the frame's seconds scaled before the fixed-step accumulator), gravity, the solver's sub-steps, velocity
  and position iterations and warm starting, sleeping, and on a backend that takes components the broad phase (BVH,
  octree, quadtree, BSP, uniform grid, sweep and prune on one or three axes: `make_broad_phase()`) and the rigid bodies'
  integrator (semi-implicit Euler, Verlet, Runge–Kutta 4). `SimulationControl::apply()` sends what changed since the last
  frame, between steps (after `Scene::finish()`, before the next step begins); a change of gravity wakes every dynamic body
  (a sleeping one would not notice); what the backend refuses (Box3D runs its own broad phase and integrator) goes back in
  the settings and is kept for the panel. `adopt()` takes a new scene's world as it is, so the settings carry over to
  every scene built after.
- **Views** (`ViewSettings`, `DebugViews`): the meshes' **wireframe** (a pipeline of PolygonMode::LINE, culling none,
  depth tested without writing, biased toward the camera; the fragment shader draws edges in one dark color and leaves
  the floor out: its 80 m diagonal crossed the scene), **bounding boxes** (colliders by awake, asleep, static; soft
  bodies; triggers), **contacts** (each dynamic body's `contacts_of()`, a pair of dynamic bodies drawn once: points and
  normals), the **broad phase**'s nodes or cells (`BroadPhase::structure_nodes()`, colored by depth, dimmer when listing
  no proxy; Box3D's refusal shown instead), **joints** (anchors on both bodies, the line between, the axis of hinges,
  sliders, cone-twists and six-DOF joints; red once broken — `Scene` now keeps joints' anchors and axes in their bodies'
  frames and their JointEvents), and **colors** by body (darker asleep), by sleep (awake, asleep, static) or by **island**
  (dynamic bodies joined by their contacts with each other and by joints: a union-find in the sandbox, since worlds do
  not expose islands). The lines are gathered between steps from the last completed step and drawn over the frame.
- **Panel**: sections Simulation, Views (with what the views found: boxes, contact points, nodes, joints, islands) and
  Step phases (each phase's time with its plot over the last 180 steps); Scene stays open, the others closed. Keys P and
  N. **`--views <list>`** starts with views on (`wireframe,boxes,contacts,broad_phase,joints` and `sleep` or `islands`).
- **Script**: `--script` now runs 620 frames: after the interaction's 420, `ControlScript` pauses (frame 420), steps once
  (450), runs at a quarter of the speed for 40 frames (455), lets a ball go without gravity at (6, 5, 6) (496) and gives
  gravity back after 30 frames, sets 8 sub-steps, 12 velocity and 4 position iterations (557), asks for an octree and
  Verlet (560), turns every view on with island colors and holds the ball by a ball-and-socket joint (570); nine checks:
  no step while paused, one single step, 10 steps in 40 slow frames, the ball still (within 1 cm, under 1 cm/s) without
  gravity and falling with it, the solver's settings in the world's description, the components replaced (reference) or
  refused with both refusals noted and the settings back (Box3D), every view drew something (boxes, contact points,
  nodes or the refusal, a joint, wireframe draws), at least two islands. All fifteen checks pass on Box3D with SDL3 and
  on the reference backend with GLFW, with the validation layers; the tour still passes. Captures of the views on both
  backends and of the panel's sections were looked at.

**Library addition** (physics design, §B "Follow-up after PH8: structures shown to debug views"): `SpatialNode`, the
structures' `nodes()` (now in `SpatialStructureConcept`), the run-time `SpatialStructure::nodes()`, and
`BroadPhase::structure_nodes()`; seven new typed tests check that the nodes cover every proxy.

Deviations from the plan, for validation:

1. **A library addition** for the broad phase view (above): a structure had no way to show what it is made of.
2. **Islands are the sandbox's own** (contacts and joints between dynamic bodies), not the backends' (Box3D keeps its
   islands inside; the reference backend's union-find is internal to its step).
3. **Changing gravity wakes every body**: otherwise sleeping bodies ignore it.
4. **`--views`** (not planned), to start with views on and to capture them.
5. **The floor has no wireframe**, and Box3D's panel labels the iterations unused (its soft step has none).
6. **The control script extends `--script`** (the same CTest runs, now 620 frames) rather than a separate run.

### S6 as built (2026-10-31)

Documentation of the sandbox as built; no code changed.

- **Architecture overview** ([architecture.md](architecture.md)): the sandbox among the applications (§1); §7.5 says how
  GJK tells slivers from shallow overlaps and that structures show their nodes; a new **§8 "The physics sandbox: every
  library in one window"** — §8.1 its modules and what each uses (diagram), §8.2 a frame across the main thread, the
  world's thread and the GPU (sequence diagram) and why everything that reads the world runs between `finish()` and
  `advance()`, §8.3 drawing a world (diagram: meshes once per shape, instances, surfaces and lines per frame, the scene
  pass's draws, the resolve, the UI pass), §8.4 what CTest checks and the three library defects the sandbox found; Build
  and tests is now §9, with the sandbox's runs and the physics suites' new coverage. All 32 diagrams render in Mermaid
  10.9.1 and 11.4.1 (the local check page); the new ones were looked at in 11.4.1, where a long subgraph title ("scene
  pass: 4x MSAA, reversed depth") was cut short — shortened, its detail moved into the first node.
- **README**: a second physics snippet — what a renderer needs to draw a world for debugging (meshes once per shape,
  bounding boxes, contacts, a component broad phase's nodes) — compiled with the project's warnings as errors and run
  (one mesh, 56 lines) from a temporary target removed after; the sandbox's entry in the layout; the paragraph on the
  sandbox no longer says "in progress".
- **THIRD_PARTY_LICENSES.md**: the sandbox embeds a Noto Sans atlas.
- **This record**: S5 and S6 as built.

CTest runs the sandbox on both physics backends (`PhysicsSandbox.SDL3` with Box3D, `PhysicsSandbox.GLFW` with the reference
backend, `PhysicsSandbox.Tour` with both), as the plan asked.
