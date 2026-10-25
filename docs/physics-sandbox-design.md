# gfx physics sandbox: an interactive application over every library — decisions and plan

Status: **answers given 2026-10-25** (below); **S1 implemented** (§C "S1 as built"), awaiting validation.

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
