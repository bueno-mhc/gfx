# gfx: a profiler, and the GPU XPBD backend made as fast as Box3D or faster — architecture, decisions and plan

Status: **answers given 2026-11-08: every question of §D as recommended.** **PF0 accepted** with its 5 deviations (§B "PF0 as
built"; committed 73cae0d). **PF1 accepted** with its 6 deviations (§B "PF1 as built"). **PF2 accepted** with its 6
deviations (§B "PF2 as built"). **PF3 accepted** with its 8 deviations (§B "PF3 as built"); the GPU defect it found is fixed and accepted (docs/gpu-xpbd-design.md §B
"Fix after G10"; the baseline recorded again). PF4 in progress. The numbers of §0.1 were
measured with a temporary program (removed) and a temporary timing print in the GPU backend (reverted).

The request (2026-11-08) asks for two things that drive each other:

1. **A profiler** that measures CPU and GPU time of every step and flow of the GPU XPBD pipeline and of any code an
   engineer chooses: per-thread lock-free recording, GPU timestamps through the graphics library's `QueryPool` only,
   sessions, traces (Chrome Tracing JSON, a binary format), reports with percentiles and CPU/GPU overlap and idle ratios,
   compiled out to nothing when disabled, at most 200 ns per CPU scope and 80 ns per counter.
2. **A performance overhaul of the GPU XPBD backend** so that its `step()` takes no longer than Box3D's on equivalent
   scenes, and is at least 1.25× faster from 10 000 bodies or 100 000 constraints on: bottlenecks found and removed with the
   profiler, each optimization behind a flag and measured before and after, a benchmark program with six scenes against
   Box3D, a generated report, regression checks.

This document follows the shape of [gpu-xpbd-design.md](gpu-xpbd-design.md): §0 where the request meets the code (with
the baseline measured today), §A the architecture and decisions, §B the plan, §C the API as proposed, §D questions.

---

## 0. Where the request meets the existing project

### 0.1 The baseline, measured today

A temporary program built the request's scenes at their sizes, stepped each 1/60 s through `PhysicsWorld::step()` (the
factory's worlds: the GPU backend on the RTX 5060 Laptop GPU with no validation layers, Box3D with a `ThreadTaskScheduler`
of 16 threads on the Intel Core i9-14900HX, 24 cores and 32 threads), Release build, sleep on, 20–30 warm-up steps then
40–60 measured steps. A temporary print in the GPU backend's `simulate()` split its time (milliseconds per step):

| Scene | Box3D `step()` | GPU `step()` | GPU: outside `simulate()` (the framework) | layouts updated | recording | submit + wait | readback + events | dispatches / barriers |
|---|---|---|---|---|---|---|---|---|
| 10 000 boxes falling onto a floor, one each | 5.9 | 9.0 | — | — | — | — | — | — |
| 50 000 boxes, the same | 36.8 | 25.3–30.7 | 13–15 | 1.8 | 0.8 | 2.9–5.8 | 3.6–4.1 | 631 / 638 |
| a two-dimensional pyramid of 29 890 boxes (base 244) | 64.6 | 41.9–47.6 | 11.5 | 1.1 | 1.1 | 15.3 (solver 12.2 on the GPU) | 13.0 | 805 / 812 |
| 100 chains of 200 links held by rods (20 000 joints) | 13.6 | 12.1–14.8 | 6.2 | 2.1 | 0.8 | 2.6 | 0.4 | 735 / 634 |
| a cloth of 316 × 316 particles (99 856) falling onto a floor | 16.9 (the framework's CPU XPBD; measured falling — landed: 77, PF2's traces) | 20.3–20.8 (landed: 22) | ≈ 0 | 5.7 | 4.2 | 11.6 | 0.3 | 747 / 754 |

Box3D's own broad phase, narrow phase and solver (its profile) take 4.2 of its 36.8 ms at 50 000 boxes, 14.0 of 64.6 ms
in the pyramid and 3.3 of 13.6 ms in the chains: the rest is our Box3D wrapper's capture of states and contacts and the
framework's work, which every backend pays. The GPU world's **first creation took 46 s** (the NVIDIA driver compiling every
pipeline with a cold shader cache); later creations 0.4–2.7 s.

What this shows, before any profiler:

- **The GPU's own work is a minority of the GPU `step()`**: about 2.8 ms of GPU work at 50 000 boxes in a 25–30 ms step.
  The rest is CPU work on the stepping thread.
- **The framework (`SteppedWorld`) costs about 6–15 ms per step at 20 000–50 000 bodies**, outside every backend. What it
  does there: the colliders' world bounds recomputed, every record copied into the published snapshot (contacts
  included), the abstraction's query BVH updated for every collider that moved; how the time divides between them is for
  the profiler to show (PF2). Box3D pays it too: Box3D's own phases are a fifth to a third of its step through gfx.
- **Contacts and events cost 3.6–13 ms on the CPU** (200 000–250 000 reported contact points sorted, merged with the last
  step's, turned into events and `ContactInfo` records), more than the whole GPU step.
- **The GPU solver dominates only in deep stacks** (12 ms in the pyramid: 245 000 contact points; per substep, 3 position
  iterations and 8 velocity iterations by color, 4 substeps).
- **630–810 dispatches and as many barriers per step**: at 50 000 boxes 2.7 ms of GPU time over 631 dispatch-and-barrier
  pairs is mostly launch and barrier cost (a few microseconds each), not arithmetic.
- **Per-step CPU layout work (1–6 ms)**: colliders, joints and soft bodies are laid out and compared with last step's bytes
  every step (5.7 ms for the cloth), and recording takes 0.8–4.2 ms.

So most of the request's optimization list (persistent kernels, warp-level scans, vectorized loads) targets the smallest
part of today's step. The plan (§B) orders the work by what the profiler measures: the CPU side and the framework first, then
the GPU's launch-bound passes, then its arithmetic.

### 0.2 Request against code

| # | Request | Situation in the code | Recommendation |
|---|---|---|---|
| 1 | A profiler depending only on std and the graphics library's `QueryPool`; macros usable from **any** subsystem | Physics, camera, UI-text and core libraries must not link graphics (`gfx_require_decoupled`); a profiler that links graphics could not be used there | **Two targets.** `gfx::profiler` (new library `source/profiler/`, namespace `gfx::profiler`, depends on `gfx::core` only): CPU scopes, counters, markers, async spans, sessions, traces, exporters, reports. **`gfx::profiler_graphics`** (a bridge, like `gfx::platform_graphics`): the GPU timing provider over `QueryPool`. Any library may use the first; only graphics users link the second. The decoupling checks allow `gfx_profiler` everywhere. |
| 2 | `profiler/macros.cppm` exporting `PROFILE_*` macros | **A module cannot export macros** (C++20) | **One small header `gfx/profiler.hpp`** with the `PROFILE_*` macros (the "strictly necessary" header case), expanding to objects of `gfx.profiler` or to nothing; plus a macro-free API (`ProfileScope scope{"name"};` with `std::source_location`) for module code that prefers no macros. |
| 3 | `PROFILER_ENABLED=0` yields zero code and zero data, checked on the disassembly | Every library's switches are constants in its `enums.cppm`, set from a CMake definition (`DEBUG_BUILD`, `SHADER_HOT_RELOAD`) | Constant **`gfx::profiler::PROFILER_ENABLED`** from `GFX_PROFILER_ENABLED`; disabled macros expand to `((void)0)`, the API's objects to empty `if constexpr` bodies. **Test**: a translation unit using every macro is compiled with the profiler off, and `objdump`/`nm` must show no profiler symbol, string or call (MinGW binutils are here). |
| 4 | Handles `template<class Tag> struct Handle { std::uint32_t value; }`, 12 profiler and 7 optimization handle kinds | Core's `Handle<Tag>` is a **class** with a private value (project rule), issued by `HandleIssuer` | Core's `Handle<Tag>` with tags in `source/profiler/handles.cppm`, **only for objects a user names** (session, counter, category, trace, exporter, sink, frame). The optimization "resources" (kernels, arenas, work queues, batches, pipeline variants) are the backend's internals, which never leave it (project rule): **no public handles** for them. |
| 5 | Enumerations and constants in **one** `enums.cppm` | One enumerations file **per library** (core, graphics, platform, UI, physics) | The profiler's in `source/profiler/enums.cppm`; the backend's optimization flags in `source/physics/enums.cppm` (where the GPU backend's are). |
| 6 | `PhysicsDesc::optimization_flags`, refit cadence in `PhysicsDesc` | The physics interface stays unchanged (GPU XPBD decision); backend-only settings live in `GpuXpbdDescription`; substeps and iterations are in `SolverDescription` already | **`GpuXpbdDescription::optimizations`** (bitmask `GpuOptimization`, default: every flag that passed its checks) and its cadence settings; the factory's worlds use the defaults. |
| 7 | `glm` for CPU math; `zstd`/`lz4` for trace compression | Own math in `gfx.core.math` (+ cglm, SIMDe); zstd is already fetched for the UI's atlases | **No glm** (a profiler does no vector math); **zstd only**, optional, for binary traces; no lz4. `THIRD_PARTY_LICENSES.md` unchanged. |
| 8 | GPU timestamps; CPU↔GPU alignment by "query-period calibration" | `QueryPool` (timestamps, `results(wait)`), `timestamp_period_nanoseconds`, `timestamp_valid_bits`; a `GpuProfiler` helper (scopes per frame, read N frames later); the backend's `StepTimer` (≤ 256 timestamps between phases). **No calibrated timestamps** in the graphics interface | Reuse `QueryPool`; **add `DeviceFeature::CALIBRATED_TIMESTAMPS` and `Device::calibrate_timestamps()`** (Vulkan `VK_KHR_calibrated_timestamps`: a GPU tick and a CPU `steady_clock` reading taken together, with their deviation; whether both GPUs' drivers offer it is to verify in PF1). Without it: one offset measured by a submission writing a timestamp, bounded by the wait (approximate, marked so in the trace). `GpuProfiler` and `StepTimer` become users of the new provider. |
| 9 | "Byte-identical traces for identical workloads" | Times differ run to run by nature | **Identical structure**: the same records in the same order with the same names, categories, query indices, counters and markers; times are **excluded** from the comparison (a canonical form without times is what the test compares byte for byte). |
| 10 | ≤ 200 ns per CPU scope, ≤ 80 ns per counter | `steady_clock::now()` on Windows is `QueryPerformanceCounter`, about 20–30 ns | Two readings and two ring writes per scope; measured by a Release-only test (Debug builds are not timed). |
| 11 | `PROFILER_ENABLED` on in Debug and RelWithDebInfo, off in Release | Two presets (debug, release); the benchmarks need timing in an **optimized** build | **Option `GFX_PROFILER`** (on in debug, off in release) and a **third preset `profile`** (Release flags, profiler on, `build/profile`) where the benchmarks and the GPU timing tests run. |
| 12 | Persistent kernels for the BVH build and the solver (a long-running kernel consuming a work queue) | **Vulkan guarantees no forward progress between workgroups**: a kernel whose workgroups wait for each other (a global barrier, a queue filled by others) can deadlock when the device does not run them all at once; the device gives no count of resident workgroups | **Fusion without waiting** where a pass's inputs come only from its own workgroup (one dispatch for several passes on small problems: a whole small BVH or a small island's colors in one workgroup), **flag `PERSISTENT_KERNELS` off by default**, the wide persistent kernel documented as unsafe on this API. |
| 13 | Deferred readbacks, N frames in flight, "no CPU waits in the steady state" | The interface promises that after `wait_step()` the getters, `events()`, `statistics()` and `contacts_of()` show the step just completed; with `asynchronous_stepping` the wait already happens on the world's thread while the application works | **Keep the interface's promise**; cut what is read back (only awake or changed bodies, contacts only when asked for) and overlap through asynchronous stepping; an opt-in latency of N steps would change what the getters mean (question §D8). |
| 14 | Command-buffer prefetch (record the next step while one runs) | A step's commands depend on its edits, known only when it begins; every GPU-side count is already indirect | **Reuse the recorded step**: when no capacity, layout or edit changed what is recorded, submit last step's command buffer again (Vulkan allows it: our command buffers are begun without one-time-submit; whether the abstraction's `submit()` accepts a command buffer twice is to verify, and to add if not) — recording goes from 0.8–4.2 ms to nothing in the steady state. |
| 15 | Descriptor-set rebinding elimination | **Bindless already**: no descriptor set changes between passes (push constants name the buffers) | Nothing to do; the profiler's counters show 0 descriptor binds. |
| 16 | Deterministic slot reduction replacing the Jacobi fallback's atomics | **Already so** (G5, G7): no floating-point atomics anywhere; corrections gathered per body in a fixed order with mass splitting | Nothing to do; recorded as such in the taxonomy. |
| 17 | Graph coloring once per topology change | Contacts change every step; joints' and soft bodies' colors (soft bodies' static colors already) do not | **Coloring warm start**: last step's colors as the first proposal for constraints that persist (matched by the contact cache), conflicts resolved by the usual rounds; joints colored only when joints change; flag `COLORING_CACHE`; determinism kept (same inputs, same colors). |
| 18 | `fp16` storage when quality allows | Half floats have 11-bit mantissas (≈ 1 mm at 2 m, ≈ 6 cm at 100 m) | **Reported, not used** for positions, velocities or contacts; considered only for data bounded and relative (none found today). |
| 19 | Benchmark scenes: `many_boxes` "1 constraint each", `stress_constraints` "1,000 bodies × 10,000 constraints each other-way" | Ambiguous: a box's "constraint" may be its floor contact or a joint; 1 000 × 10 000 may mean 10 million joints (more than this laptop's 16 GB holds for Box3D) | **`many_boxes`**: 50 000 boxes each resting on the floor (one contact manifold each), as measured above. **`stress_constraints`**: 1 000 bodies and 100 000 joints, each body joined to 100 others in both directions (question §D10). |
| 20 | `mixed`: 10 000 rigid bodies and 50 000 coupled particles | Box3D has no soft bodies: the framework's CPU XPBD steps them beside Box3D | Compared as is (Box3D + the framework's soft bodies) and said so in the report. |
| 21 | Acceptance: ≤ Box3D on every scene, ≥ 1.25× faster from 10 000 bodies or 100 000 constraints | Today (§0.1): GPU ahead at 50 000 boxes (1.2–1.4×) and in the pyramid (1.4–1.5×), behind at 10 000 boxes, in the chains and the cloth | Kept as the target, with the comparison **through the gfx interface** (`step()` wall clock, both backends paying the framework), Box3D's own phase times reported beside it (honest about what "Box3D" costs natively), any miss documented with its cause. |
| 22 | 100 warm-up and 1 000 measured frames per scene and backend; CI fails on a regression over 5 % | Six scenes × two backends × 1 100 steps of up to 65 ms ≈ 15 minutes; this laptop's clocks vary (Intel GPU timings varied 2× run to run; ASUS "Silent" mode) | The harness runs as a **CTest test with label `benchmark`**, excluded from the default runs; the 5 % gate compares with a stored baseline **only on the machine that recorded it**, with the power mode noted in the report. |
| 23 | GitHub Actions on Linux/Windows/macOS with Lavapipe/WARP and a self-hosted GPU runner; `git` commands | Project rule: you commit and push; only Windows/GCC 16 here; CI is step 6 of the project, not started | **No workflow file now** (step 6); you commit. |
| 24 | Pipeline caching ("never create a pipeline during a step") | Pipelines are created at world creation, none during steps; **no `PipelineCache` is used**, and each world compiles its own (first creation 46 s cold) | **A pipeline cache per device shared by the backend's worlds** (pipelines created once per device and variant set, not per world), **saved to a file** given by `GpuXpbdDescription::pipeline_cache_path` (empty: none). |
| 25 | Multiple worlds sharing queues and descriptors | Worlds of the direct entry share the application's device and queue; each owns its pipelines and buffers | Shared pipelines (row 24); the benchmark gets a two-world case. |
| 26 | The physics library may not depend on the profiler internals; the XPBD optimizer depends only on physics and graphics | — | The backend links `gfx::profiler` (core only) and `gfx::profiler_graphics`; the framework (`gfx::physics_components`) links `gfx::profiler` for its own scopes (it already depends on core only). |

---

## A. Architecture and design decisions

### A1. Profiler object model

```
Profiler ──creates──▶ Session ──per frame──▶ Frame
   │                     │
   │                     ├── ThreadContext (one per recording thread: an SPSC ring of records)
   │                     │      records: CPU scope begin/end, counter, marker, async begin/end, GPU scope begin/end
   │                     ├── GpuTimingProvider (per device/queue: timestamp queries per frame in flight)
   │                     └── finalize_session() ──▶ Trace (immutable) ──▶ Report (aggregates)
   └── exporters: ChromeTracingJson, BinaryTrace (optionally zstd), Sink (callback, live)
```

- **`Profiler`** (one per process in practice; several allowed): owns the thread contexts' storage, the name table and the
  sessions; `create_profiler(ProfilerDescription)` makes it, `profiler()` returns the one the macros record into (set by the
  application, `nullptr` = not recording: a scope then costs one relaxed load and a branch).
- **`Session`**: a recording of frames (`SessionDescription`: frame count or unbounded, ring sizes, query capacity, category
  filter). `begin_frame()`/`end_frame()` delimit frames; records outside a session are dropped at the cost of one branch.
- **`ThreadContext`**: created the first time a thread records (registration takes a lock once per thread, never again),
  holding a fixed **single-producer single-consumer ring** of 32-byte records, cache-line aligned, sized at creation. The
  producer is the thread; the consumer is the session's collector, which drains every ring at `end_frame()` (from the
  thread that calls it) into the frame's storage. Full ring: the record is dropped and counted (`RECORDS_DROPPED` marker at
  the next drain).
- **Records** carry a timestamp (nanoseconds, `steady_clock`), a **site** (an interned description: name, category, file,
  line — made once per call site by a function-local `static constinit` object, so recording passes a pointer and the site's
  identifier is a compile-time hash of its name and location), a kind, and a 64-bit value (counter value, async token,
  GPU query index).
- **`Trace`**: frames, each with the call tree per thread (from nested begin/end), counters, markers, async spans and GPU
  scopes (resolved ticks converted to the CPU timeline). **`Report`**: per stage mean, p50, p95, p99 (nearest rank), total
  CPU, total GPU, overlap ratio, idle ratio (A6).

### A2. The instrument-anything contract

```cpp
#include <gfx/profiler.hpp>     // macros only; includes nothing from the profiler's internals
import gfx.profiler;            // when using the API directly

void anything()
{
    PROFILE_FUNCTION();                                   // a scope named by the function
    PROFILE_SCOPE("load level");                          // a named scope until the end of the block
    PROFILE_COUNTER("bodies", count);                     // a counter sample
    PROFILE_MEMORY("staging", bytes);
    PROFILE_MARKER("level loaded");
    PROFILE_ASYNC_BEGIN("step", token);                   // spans across threads (submission → completion)
}
```

- **Enabled**: each macro declares a `static constinit ProfileSite` and an RAII object (`ProfileScope`) or a call
  (`record_counter`); category filtering is a bit test against the session's mask.
- **Disabled** (`GFX_PROFILER_ENABLED=0`): every macro is `((void)0)`, the API's objects are empty and their functions
  `if constexpr (PROFILER_ENABLED)` — no code, no site, no string. Checked on the object file (§0.2 row 3).
- **GPU**: `PROFILE_GPU_SCOPE(name, recorder, provider)` opens and closes timestamps around commands recorded in between;
  `PROFILE_DISPATCH(name, recorder, provider, dispatch)` does it around one dispatch. They need `gfx::profiler_graphics`.

### A3. GPU recording

- **`GpuTimingProvider`** (concept in `gfx.profiler`, implemented in `gfx::profiler_graphics` over `QueryPool`): per frame in
  flight, a pool of timestamp queries; `begin_frame(frame, recorder)` resets the frame's queries; `write(recorder, site,
  begin|end)` takes the **next query index** of the frame (recording order: deterministic) and records the timestamp;
  `resolve(frame)` reads the results without waiting once the frame's fence or timeline value is reached (the step's own
  timeline for the physics backend), converts ticks with `timestamp_period_nanoseconds` and the calibration, and hands the
  scopes to the session.
- **Exhausted pool**: the scope is not timed, a `GPU_SCOPE_DROPPED` marker names it; never a silent loss.
- **No timestamps** on a queue (`timestamp_valid_bits == 0`): scopes are recorded with the sentinel `GPU_TIMING_UNAVAILABLE`.
- **Inside one dispatch** nothing can be timed (GJK and EPA run in one kernel): such stages are reported by counters
  (iterations, pieces, points), with the sentinel `FUSED_IN` naming the kernel that contains them (§A5).

### A4. CPU↔GPU alignment

`Device::calibrate_timestamps()` (new, §0.2 row 8) returns a GPU tick, a `steady_clock` reading and the deviation; the
provider calibrates at session start and every second (drift), and converts GPU ticks to the CPU timeline by the nearest
calibration. Without the feature, one offset is measured at session start (a timestamp written by a tiny submission, its
CPU time bounded by submission and wait) and the trace says `APPROXIMATE_ALIGNMENT`.

### A5. XPBD stage coverage

Every stage of the request, mapped onto the backend as built (CPU scopes on the stepping thread, GPU scopes around passes,
counters per stage). Where the request's stage is not a separate pass, the coverage test accepts the sentinel naming why.

| Request's stage | As built | Recorded as |
|---|---|---|
| `step()` entry and exit, deferred-edit apply, submit, timeline wait | `SteppedWorld::run_step`, `apply_to_records`, `backend->apply`, `simulate`, `run_commands` | CPU scopes; the framework's `update_bounds`, `publish`, `update_query_tree` too |
| buffer-pool acquire, staging drain, command begin and end, async readback | `GpuArray::reserve`, `record_uploads`, `record_step`, `read_back`, `read_contacts`, `read_triggers` | CPU scopes + bytes counters |
| descriptor-arena reset | none: bindless | sentinel `STAGE_ABSENT` (reason: bindless) |
| Morton codes, radix histogram ×4, radix scatter ×4 | `bvh_morton`, `radix_histogram`, `radix_scatter` (4 passes of 8 bits for 30-bit keys; rank sort under 4 096 keys) | GPU scopes per pass |
| LCP and split, leaf boxes, internal boxes | `bvh_hierarchy` (one pass), `bvh_bounds` (leaves and internal nodes climbing together) | GPU scopes; sentinels `FUSED_IN` for the parts sharing a pass |
| treelets, four-wide collapse, refit | `bvh_treelet` (0 rounds by default), `bvh_wide`, refit = reset + bounds + wide | GPU scopes; counter "refit or rebuild" |
| pair enumeration, BVH-assisted candidates | `bvh_pairs` (within, between), `pairs_filter`, `narrow_phase_pairs` | GPU scopes; counters pairs, kept pairs, sub-pairs |
| GJK, EPA, manifold, feature IDs | inside `narrow_phase_collide`, `narrow_phase_merge` | one GPU scope each; GJK/EPA as counters (iterations) + `FUSED_IN` |
| warm-start lookup | `contact_keys`, `contact_match` (cache) | GPU scopes; counter matched points |
| coloring, adjacency | `islands`, `graph`, `ranges`, `color` rounds | GPU scopes; counters colors used, overflow constraints |
| rigid and soft solve per substep and iteration | `joint_solve`, `contact_solve`, `soft_solve` by color, `jacobi_gather` | GPU scopes grouped per substep (one scope per iteration, not per color: 600+ scopes per step otherwise) |
| compliance warm start | none: λ reset per substep (decided in GPU XPBD §D8) | sentinel `STAGE_ABSENT` |
| Jacobi accumulation, deterministic reduction | `jacobi_gather` | GPU scope |
| sleep check, break events | `sleep`, `joint_forces` | GPU scopes |
| predict, integrate, velocity update | `xpbd_integrate`, `particle_integrate`, `xpbd_velocity_update`, `particle_velocity_update`, `contact_velocity` | GPU scopes |
| staging copy, fence, poll, event drain | readback copies, timeline wait, `contacts_report`, `triggers_filter`, the CPU's events | GPU and CPU scopes |

Counters per step: bodies, awake bodies, particles, colliders, joints, soft constraints per color, contact points, candidate
and kept pairs, manifolds, BVH nodes and depth (from the header), refit or rebuild, substeps × iterations, colors used,
overflow constraints, replays, dispatches, barriers, workgroups dispatched (direct ones; indirect ones from the counts read
back), and **bytes per array** (each array's element size × the elements a pass touches, a model: no hardware counters are
reachable through the graphics library).

### A6. Traces and reports

- **Chrome Tracing JSON** (`chrome://tracing`, Perfetto): one process, one track per CPU thread, one per GPU queue; scopes as
  complete events, counters as counter events, markers as instant events, async spans as async events.
- **Binary trace**: a versioned header (magic, version, endianness), the site table, then frames of fixed-size records;
  optionally zstd-compressed; read back by the library (round-trip tested).
- **Report**: per stage over the measured frames — mean, p50, p95, p99, minimum, maximum; per frame **CPU time** (the
  stepping thread's step scope), **GPU time** (the union of GPU scopes), **overlap ratio** (time both busy ÷ step wall
  time), **idle ratio** (time the GPU was idle within the step ÷ step wall time). Written as Markdown by the benchmark.

### A7. Determinism, overhead, compile-out

- **Determinism**: query indices allocated in recording order per frame; records drained in thread registration order and
  in each thread's order; the canonical trace (§0.2 row 9) compared byte for byte across two runs of the same workload.
- **Overhead** (Release, `profile` preset): a test times 10 million scopes and counters and fails above 200 ns / 80 ns each.
- **Compile-out**: the object-file test (§0.2 row 3).

### A8. The comparison with Box3D

- **Same scene, same machine, same interface**: each scene is built by one function for both backends through
  `PhysicsWorld`; the same `SolverDescription` (sub-steps, iterations), friction, restitution, sleep settings; Box3D on a
  `ThreadTaskScheduler` of N threads (N reported; §D11), the GPU backend on the factory's device.
- **What is compared**: `step()` wall clock (synchronous stepping), both paying the framework. Reported beside it: the
  stepping thread's CPU time, the GPU time, overlap and idle ratios, and Box3D's own phase times (what Box3D costs natively).
- **Warm-up and measurement**: 100 and 1 000 steps (the request's numbers); for the regression gate a shorter run.
- **Quality**: a step that is fast but wrong is no win. Each scene has a check (the pyramid stands, the chains hang, the
  boxes rest, the cloth lies on the floor) that both backends must pass, and the substep/iteration Pareto front (A10, item 14)
  reports error against time.

### A9. Bottleneck taxonomy

Each item gets its instrumentation in PF2 and its measured cost in the taxonomy table of `PERFORMANCE.md`; the costs
below are today's (§0.1) or first-principles estimates, marked so.

| Area | Cost today | Target |
|---|---|---|
| Framework per step (bounds, publish, query tree) | 6–15 ms at 20 000–50 000 bodies (measured) | < 1 ms at 50 000 bodies |
| Contacts and events on the CPU | 3.6–13 ms (measured) | < 1 ms at 50 000 boxes, < 3 ms in the pyramid |
| Per-step layouts (colliders, joints, soft bodies compared and uploaded) | 1.1–5.7 ms (measured) | only what changed: < 0.2 ms in the steady state |
| Recording | 0.8–4.2 ms (measured) | 0 in the steady state (reused command buffer) |
| Dispatches and barriers | 630–810 per step (measured); launch-bound at 50 000 boxes (estimate: 2–4 µs each) | half, by fusion and by grouping barriers |
| GPU solver in deep stacks | 12 ms in the pyramid (measured) | fewer colors and iterations at the same error (coloring warm start, Pareto) |
| BVH builds | 0.3–1.7 ms (measured, broad phase) | refit with rebuilds by cadence |
| Narrow phase | 0.2–1.0 ms (measured) | unchanged unless measured otherwise |
| Readback volume | every body, every contact (measured implicitly in "readback + events") | awake bodies and requested data only |
| Pipeline creation | 46 s cold, 0.4–2.7 s warm per world (measured) | one creation per device, cache on disk |
| Steady-state allocations | not measured | zero after warm-up (counted like the UI's tests) |

### A10. The optimization program

Each is a bit of `GpuOptimization` in `GpuXpbdDescription::optimizations` (framework items are not GPU flags: they apply
to every backend and are kept only when the conformance suite and the determinism tests pass unchanged). In the order of
§B, highest measured cost first:

| # | Optimization | Mechanism | Flag |
|---|---|---|---|
| 1 | Framework publish and query tree | swap the pending and published buffers of contacts and events instead of copying; world bounds of awake bodies' colliders only; the query tree updated lazily at the first query after a step, with enlarged (fat) boxes so small motions need no update | framework (no flag) |
| 2 | Contacts and events on the GPU | the GPU compacts reported contacts sorted by pair (the radix sort exists) and merges them with last step's touching set (the contact cache already pairs them): BEGIN/END/HIT come out of the GPU, the CPU only copies | `GPU_EVENTS` |
| 3 | Readback of what changed | awake bodies' states and changed joints only (a compacted list), contacts only when the world reports them (`contacts_of()` needs them: kept, but copied once) | `COMPACT_READBACK` |
| 4 | Incremental layouts | colliders, joints and soft bodies re-laid out only when an edit touched them (the framework's edits say which) | `INCREMENTAL_LAYOUTS` |
| 5 | Recorded-step reuse | the command buffer resubmitted when nothing recorded changed (request items 10–11) | `REUSE_RECORDING` |
| 6 | Pipelines per device, cache on disk | (request item: pipeline caching) | `SHARED_PIPELINES` |
| 7 | Kernel fusion | predict + integrate, contact preparation + warm-start lookup, sleep + velocity update, soft colors of one iteration where they fit one workgroup; barriers grouped at pass boundaries | `KERNEL_FUSION` |
| 8 | Refit by default | the moving tree refit each step and rebuilt every `bvh_rebuild_period` steps or when the SAH cost grew by `bvh_rebuild_cost_ratio` | `BVH_REFIT` |
| 9 | Coloring warm start | §0.2 row 17 | `COLORING_CACHE` |
| 10 | Subgroup operations in more passes | ballots and scans in the radix histogram, the coloring and the reductions (the portable shared-memory variants stay) | `SUBGROUP_VARIANTS` |
| 11 | Vectorized loads | `uvec4`/`vec4` loads where a pass still reads scalars (most already read vectors) | `VECTOR_LOADS` |
| 12 | Sleep tuning | thresholds measured against the scenes; islands put to sleep sooner on static-dominant scenes | `SLEEP_TUNING` (sets defaults) |
| 13 | Small-problem single dispatches ("persistent" without waiting) | a whole BVH or solve of a small world in one workgroup | `PERSISTENT_KERNELS` (off by default) |
| 14 | Substep/iteration Pareto front | measured error (stack drift, chain stretch, cloth stretch) against time for S × I; reported, defaults chosen from it | report only |
| 15 | Zero steady-state allocations | counted per step after warm-up (the UI tests' method) | checked by a test |

Kept as they are (§0.2): descriptor reuse (bindless), slot reduction (no atomics), deterministic query allocation,
pipeline variants (subgroups or shared memory). Not done: fp16 storage (row 18), wide persistent kernels (row 12).

### A11. Fallbacks and trace markers

An optimization whose capability is missing (no subgroup arithmetic, a pipeline cache file that cannot be read, a problem
too large for one workgroup) falls back to the baseline path of that stage deterministically, and the trace gets a
`OPTIMIZATION_FALLBACK` marker naming the flag and the reason; `GpuXpbdCapabilities` lists the flags in effect. Results
with every flag off (`optimizations = GpuOptimization::NONE`, "the baseline") and with every flag on must agree within the
tolerances of the conformance suite; most flags give the same bits (they change only where work happens, not the
arithmetic), and the tests say which do.

### A12. Errors

- **`ProfilerError`** (new, in `gfx::profiler`, derived from core's `Error` like every other, origin `[gfx::profiler]`):
  invalid session descriptions, export failures (a file that cannot be written), a binary trace of an unknown version.
- **`BackendCapabilityError`**: as before, from the GPU backend's creation and recording.
- **Hot paths never throw**: recording scopes, counters, GPU timestamps and dispatches do not throw; dropped records are
  counted and marked.

### A13. Modules, layout, naming, RAII

```
source/profiler/                 target gfx_profiler (gfx::profiler): depends on gfx::core only
  enums.cppm                     gfx.profiler.enums: ProfilerCategory, RecordKind, TraceFormat, SessionState,
                                 ProfilerSentinel, Percentile, constants (record size, ring capacity, query capacity)
  handles.cppm                   gfx.profiler.handles: SessionHandle, CounterHandle, CategoryHandle, TraceHandle, ...
  errors.cppm                    ProfilerError
  profiler.cppm                  gfx.profiler (umbrella): Profiler, Session, ProfileScope, record_* functions, Trace,
                                 Report, exporters; internal partitions: ring, site table, collector, call tree,
                                 aggregation; units per area
  include/gfx/profiler.hpp       the PROFILE_* macros
source/profiler_graphics/        target gfx_profiler_graphics: gfx.profiler.graphics (QueryPool timing provider)
source/physics/backends/gpu_xpbd/  optimizations in the existing units (a flag tested where the pass is recorded);
                                 new shaders for fused passes
benchmarks/physics/              gfx_physics_benchmark: the six scenes, the runner, the generated report
docs/PERFORMANCE.md              generated tables + the analysis (§B, PF6)
```

The request's file list (`optimizations/persistent_bvh_kernel.cppm` …, `baseline/*`) is not followed literally: an
optimization lives where its pass is recorded, behind its flag, and "the baseline" is every flag off — two copies of the
backend would diverge. Names follow the project (full words, `m_` members, `snake_case` functions, `PascalCase` types,
`SCREAMING_SNAKE_CASE` enumerators); every resource RAII.

---

## B. Plan

Each phase is built, tested in Debug and Release (and the `profile` preset from PF0), reported and validated before the
next.

| Phase | Content | Checked by |
|---|---|---|
| **PF0 Profiler core** | `gfx::profiler`: enums, handles, errors, sites, thread contexts and rings, scopes, counters, markers, async spans, sessions and frames, the collector, traces, call trees, reports with percentiles; Chrome JSON and binary exporters (zstd optional); `gfx/profiler.hpp`; option `GFX_PROFILER`, preset `profile` | ring and threading tests, overhead test (Release), canonical-trace determinism, compile-out object-file test, Chrome JSON parsed back, binary round trip |
| **PF1 GPU timing** | graphics: `DeviceFeature::CALIBRATED_TIMESTAMPS`, `Device::calibrate_timestamps()` (Vulkan); `gfx::profiler_graphics`: the `QueryPool` provider, deterministic query arenas per frame in flight, resolve on the timeline, dropped-scope markers, sentinels, alignment; `GpuProfiler` and the backend's `StepTimer` rebuilt on it | GPU scopes around known work on both GPUs, alignment within the calibration's deviation, exhausted pools marked |
| **PF2 Instrumentation** | every stage of §A5 (CPU and GPU) and every counter in the GPU backend, the framework (`SteppedWorld`) and the Box3D and reference backends' steps | the stage-coverage test (every stage recorded or its sentinel), traces of the §0.1 scenes |
| **PF3 Benchmark harness** | `gfx_physics_benchmark`: six scenes, quality checks, both backends (and the reference), 100 + 1 000 steps, a Chrome trace per scene, `benchmark_report.md` generated, a stored baseline and the 5 % comparison; CTest label `benchmark` (not in default runs); the two-world case | the harness on both backends; today's numbers as the recorded baseline |
| **PF4 The CPU side** | optimizations 1–6 of §A10 (framework, events on the GPU, compact readback, incremental layouts, recording reuse, shared pipelines and the disk cache), zero steady-state allocations | before/after traces per optimization; conformance, determinism and GPU tests unchanged; flags off = baseline |
| **PF5 The GPU side** | optimizations 7–13 (fusion and barriers, refit cadence, coloring warm start, subgroup passes, vector loads, sleep tuning, small-problem single dispatches) and the Pareto front | the same, per optimization; the acceptance table against Box3D |
| **PF6 Documentation** | `PERFORMANCE.md` (taxonomy with before/after, generated benchmark tables, speed-up per scene, environment, follow-ups for misses), README profiler and performance sections, architecture.md chapter with diagrams, launch.json, records | snippets compiled, diagrams rendered |

### PF0 as built (2026-11-08)

**Target `gfx::profiler`** (`source/profiler/`, `gfx_profiler`, on `gfx::core` and, privately, zstd; `gfx_require_decoupled`
forbids every graphics, platform, UI and physics target): modules `gfx.profiler.enums` (ProfilerSubsystem — errors'
"[gfx::profiler::EXPORT]" —, ProfilerCategory bitmask GENERAL/PHYSICS/GRAPHICS/GPU/UI/PLATFORM/APPLICATION, RecordKind,
TraceFormat, SessionState; constants PROFILER_ENABLED (from `GFX_PROFILER_ENABLED`, a PUBLIC definition of the target),
ALL_PROFILER_CATEGORIES, PROFILER_RECORD_SIZE 32, DEFAULT_PROFILER_RING_CAPACITY 65 536, MAXIMUM_PROFILER_RING_CAPACITY 2²⁶,
PROFILER_CACHE_LINE_SIZE 64, PROFILER_TRACE_MAGIC "GFXTRACE", PROFILER_TRACE_VERSION 1, PROFILER_TRACE_HEADER_SIZE 32),
`gfx.profiler.handles` (SessionHandle, SinkHandle; explicitly instantiated, defect 13), `gfx.profiler.errors` (ProfilerError
{subsystem, operation, problem, suggestion}), and `gfx.profiler` with partitions `:site` (ProfileSite: name, category, file,
line and two 64-bit FNV-1a identifiers — name + file + line, and name alone — made at compile time; `ProfileSite::function()`;
the `record_*` functions; `set_thread_name()`; `is_recording()`; ProfileScope with `end()`), `:trace` (TraceSite, TraceThread,
TraceScope {thread, site, begin, end, depth}, TraceSample, TraceAsyncSpan, TraceFrame, Trace, `canonical_trace()`), `:session`
(ProfilerDescription, SessionDescription {name, frames, categories}, FrameSink concept, Profiler, `create_profiler()`,
`set_current_profiler()`, `current_profiler()`), `:report` (statistics_of, make_report, to_markdown), `:exporters`
(to_chrome_json, write/read_binary_trace, export_trace, import_trace) and internal `:state` (ProfileRecord 32 bytes,
RecordRing, ThreadContext, SessionData, Profiler::Implementation, ProfilerAccess). Units recording.cpp, profiler.cpp,
trace.cpp, report.cpp, chrome_json.cpp, binary_trace.cpp (zstd in its global module fragment), errors.cpp. Header
`source/profiler/include/gfx/profiler.hpp` (macros only; `__COUNTER__` names each site; disabled: `((void)0)`, and
PROFILE_NAMED_SCOPE declares an empty `DisabledProfileScope`).

**Recording**: a record is a relaxed load of the current profiler's categories and a branch when nothing records the site's
category (0.5 ns measured); otherwise an acquire load of the current profiler, a thread-local cache of the thread's
context (registered on its first record: the only lock and allocation), a `steady_clock` reading and a write into the
thread's single-producer single-consumer ring (head and tail on cache lines of their own; full: the record is dropped and
counted). `end_frame()` drains every ring in registration order under the profiler's mutex: scope begins are paired with the
last unmatched begin of the same site (begins whose ends were lost count as unmatched), samples kept in order, spans paired by
(name, token) across threads and frames; a scope or span belongs to the frame it ends in. Sinks get each frame on
`end_frame()`'s thread. `finalize_session()` ends an open frame, stops recording, discards what the rings still hold, counts
scopes and spans left open as unmatched in the last frame.

**Formats**: Chrome Tracing JSON (track 0 the frames, track n + 1 thread n; microseconds with three decimals; JSON-escaped
names; non-finite float samples written as null); the binary trace (header, then the name, sites, threads, frames,
little-endian; zstd level 3 when compressed; every count checked against the bytes left and every index against the sites
and threads before a Trace is returned).

**Build**: option `GFX_PROFILER` (default ON when CMAKE_BUILD_TYPE is Debug), presets debug ON, release OFF and the new
**`profile`** (Release, ON, `build/profile`); `python build.py --profile` (build.py now passes `-DGFX_PROFILER` explicitly).

**Measured** (profile build, `ProfilerOverheadTest`, 2 million records of each kind): **50.7 ns per scope** (two records;
budget 200), **24.6 ns per counter** (budget 80), **0.5 ns per record of a category the session does not record**.

**Deviations (for validation).**
1. **`PROFILE_NAMED_SCOPE(variable, name)`** takes the variable first (the request's `(name, handle)`): the variable is what
   the macro declares, and `variable.end()` ends the scope early.
2. **Scopes keep their depth, not a parent index**: a scope is stored when it ends, and its parent may end in a later frame,
   so frames hold depths and reports rebuild the nesting per thread (a scope inside a scope of the same site counts once).
3. **One recording session per profiler at a time, records into the current profiler only**: `create_session()` refuses a
   second session while one records or waits to be finalized; the macros record into the profiler `set_current_profiler()`
   names (one at a time per process). A thread that records into several profilers in turn registers a context in each
   switch (contexts are freed with their profiler).
4. **GPU macros** (`PROFILE_GPU_SCOPE`, `PROFILE_DISPATCH`) **come with PF1**, which gives them their timing provider.
5. **The README** got the option and the profile build only; its profiler section comes with PF6.

**Tests** (`gfx_profiler_tests`, 26): enumerations, errors and compile-time sites (3); recording (11: nesting and pairing,
sample kinds and values, spans across threads and frames, eight threads recording at once, categories, nothing recorded
without a session or a current profiler, a full ring of 64 dropping and counting 36 of 100, unmatched records, named and
function scopes, frame counts stopping sessions, sinks; misuse refused with descriptive errors; nothing recorded when compiled
out — run in Release); reports (4: nearest-rank percentiles, stages per frame and per call with recursion once, skipped
frames, Markdown); exporters (4: Chrome JSON parsed back by nlohmann/json in a plain translation unit, binary traces read
back equal plain and compressed — 50 frames compress to under a quarter —, corrupt traces refused naming the problem, files
written and read); determinism (the same five-frame workload with a worker thread per frame twice gives the same canonical
trace, a changed counter another); overhead (profile build only). CTest
**`ProfilerCompileOut.MacrosLeaveNoCodeAndNoData`** (cmake/scripts/GfxCheckProfilerCompileOut.cmake: every macro, with
arguments calling an undefined function, compiled with -O2 and GFX_PROFILER_ENABLED=0, gives the same instructions as the
function without them, no symbol and no string). The recording and determinism tests passed 200 repetitions in a row.
Results: Debug 24 passed, 2 skipped (overhead: not optimized; compiled-out: the profiler records); Release 13 passed,
13 skipped (the profiler is compiled out); profile 25 passed, 1 skipped. Full runs (1480 tests: 1453 + 26 + the compile-out
check): Debug 1480 of 1480 (1028 s), Release 1480 of 1480 (964 s); the profile build built every target, its profiler tests
run.

### PF1 as built (2026-11-08)

**Graphics**: `DeviceFeature::CALIBRATED_TIMESTAMPS` (automatic) and **`Device::calibrate_timestamps()`** → `TimestampCalibration`
{gpu_ticks, cpu_time (steady_clock), deviation} (in `:device_memory`; pure virtual with a throwing default; FakeDevice forwards
to it). Vulkan: VK_KHR_ or VK_EXT_calibrated_timestamps when the physical device pairs VK_TIME_DOMAIN_DEVICE with the host
domain (`HOST_TIME_DOMAIN`: QUERY_PERFORMANCE_COUNTER on Windows, CLOCK_MONOTONIC elsewhere;
`VulkanPhysicalDevice::calibrates_timestamps()`); the host value is tied to steady_clock by reading the host clock and
steady_clock right after (plain translation unit `vulkan_host_clock.cpp` behind std-free `vulkan_host_clock.hpp`:
QueryPerformanceCounter needs `<windows.h>`). **Both GPUs here have them**: deviation 2 µs on the RTX 5060, 120–173 µs on
the Intel GPU (Debug, validation layers).

**Core (`gfx.profiler`)**: partition `:gpu` — `GpuScopeRecord` {site, begin, end (steady_clock ns), depth, query, status},
abstract **`GpuTimingSource`** (track_name, alignment, alignment_deviation, collect(scopes, wait), `attached()` counted by the
profilers it is added to) and the concept **`GpuTimingProvider<Provider, Recorder>`** (`provider.scope(recorder, site)`);
enumerations `GpuScopeStatus` {TIMED, DROPPED, UNAVAILABLE, INCOMPLETE} and `GpuClockAlignment` {CALIBRATED, ESTIMATED,
UNAVAILABLE}; handle `GpuSourceHandle`; `Profiler::add_gpu_source()` / `remove_gpu_source()`. At each `end_frame()` the profiler
collects every source without waiting and files each GPU scope into the first frame that ended after the scope began (later ones
wait for their frame); `finalize_session()` collects waiting and files the rest (late ones into the last frame). Traces:
`TraceGpuTrack` {name, alignment, deviation}, `TraceGpuScope` {track, site, begin, end, depth, query, status},
`TraceFrame::gpu_scopes`, `Trace::gpu_tracks()`; canonical form lists GPU tracks and scopes (by track and query); **binary trace
version 2**; Chrome JSON puts the GPU in process 2 (a track per GPU track, timed scopes as complete events, untimed ones as
instants named with their status); reports gain `gpu_stages`, `cpu_time`, `gpu_time` (per-frame busy unions), `overlap_ratio`,
`idle_ratio`, `untimed_gpu_scopes` (and a Markdown line and table). Macros `PROFILE_GPU_SCOPE(name, recorder, provider)` and
`PROFILE_DISPATCH(name, recorder, provider, x, y, z)` (compiled out: `recorder.dispatch(x, y, z)` alone — the compile-out
check covers both).

**Bridge `gfx::profiler_graphics`** (`source/profiler_graphics/`, module `gfx.profiler.graphics`, re-exports gfx and
gfx.profiler): **`GpuTimer`** (a GpuTimingSource; GpuTimerDescription {queue, frames_in_flight 3, scopes_per_frame 1024,
always, wait_limit 2 s, recalibration 1 s, calibrated, name}): one QueryPool, a range of 2 × scopes_per_frame queries per frame
in flight; `begin_frame(recorder)` reads the slot's last frame if nobody did (waiting within the limit), resets its range in
the recorder; `scope(recorder, site)` → `GpuTimerScope<Type>` (timestamps at NONE and ALL_COMMANDS; queries taken in recording
order — the same work uses the same queries); scopes beyond the range DROPPED, a queue without timestamp bits UNAVAILABLE,
timestamps not written within the limit INCOMPLETE (untimed scopes keep their recording time). Timestamps are written only while
a profiler records GPU with the timer added (or `always`); `latest()` gives the last frame read back. Alignment: calibrated
timestamps (recalibrated every second), else an **estimate** from five submissions each writing one timestamp, the query polled
until written (the window between the submission and the first sight of the timestamp; its midpoint, ± half of it). The whole
pool is reset on the GPU at construction (reading a query never reset is invalid usage). One recording thread at a time;
collect() and latest() from others (a mutex).

**Found while testing**: the first estimate waited for the submission's timeline value: **4.5 ms** of deviation on both GPUs
(the wait returned milliseconds after the GPU wrote the timestamp); polling the query brought it to **150 µs**.

**Deviations (for validation).**
1. **`GpuProfiler` (graphics) is not rebuilt on the timer**: the graphics library cannot depend on the bridge (a cycle); it
   stays as it is, and the GpuTimer is the profiler's GPU timing.
2. **The GPU backend's `StepTimer` moves to PF2**, where the backend is instrumented: it feeds `StepStatistics::phase_seconds`
   in every build, which a GpuTimer with `always` can do.
3. **GPU scopes go to the frame their beginning falls in**, arriving one to frames_in_flight frames late (the current frame's
   queries are read only when waited for or reused: it may still record); sinks see a frame before its GPU scopes.
4. **Binary traces are version 2** (GPU tracks and scopes); version 1 traces (PF0's) are refused.
5. **`GpuTimerDescription::calibrated`** (not in the design): turns the estimate on where calibrated timestamps exist, which is
   how the estimate is tested on this machine (both GPUs calibrate).
6. **The timer needs its queue only with timestamps**: on a device without TIMESTAMP_QUERY (the fake device) it records
   UNAVAILABLE scopes whatever its queues.

**Tests**: `gfx_profiler_tests` + 2 (GPU sources filed by time, late delivery, waiting at finalize, categories, attachment;
reports' GPU figures on a hand-built trace) and GPU data in the exporter and canonical tests; **`gfx_profiler_graphics_tests`**
(17: 8 × 2 GPUs with the validation layers — calibrated timestamps against steady_clock over 50 ms (within 0.1 %), scopes of a
32 MiB fill and copy nested and inside their submission (calibrated and estimated), dropped beyond the range, incomplete when
never submitted, the same queries twice, a profiler session with GPU scopes in traces, reports, binary and JSON, the macros —
and the fake device without timestamps). Measured on the way: a 32 MiB fill takes 86–93 µs on the RTX 5060 and 6 ms on the
Intel GPU (validation layers). Results: Debug 1497 of 1497 (965 s; then the estimate's change and its test, 17 of 17 GPU timer
tests), Release 1499 of 1499 (914 s), profile: profiler tests 27 + 1 skipped, GPU timer tests 17, compile-out check; overhead
54.2 ns per scope, 26.5 ns per counter.

### PF2 as built (2026-11-08)

**Profiler additions.** `GpuScopeTiming` {WHEN_RECORDED, ALWAYS}: a scope timed whether or not a profiler records it (the
GPU backend's phases, read back into `StepStatistics::phase_seconds` in every build). `GpuTimer::begin_scope()` /
`end_scope()` (a `GpuScopeToken`) for scopes that do not follow a block, `scope(recorder, site, timing)`,
**`finish_frame()`** (reads the frame just completed: `latest()` gives it, a collecting profiler gets it at its next
`end_frame()`), `latest(vector&)`; resolved scopes are kept for a profiler only while it records. **GPU sources and
profilers know each other** (a library mutex for the links, a mutex per profiler for its sources; add and remove from any
thread): whichever is destroyed first ends the link (`GpuTimingSource::detach()`, called first by `GpuTimer`'s destructor),
`attached_to(profiler)`, and **`attach_to_current_profiler(source)`** (adds the source to the current profiler unless it is
added already). **Stage coverage**: `StageCoverageKind` {RECORDED, FUSED_IN, ABSENT}, `StageCoverage` {stage, kind, site,
reason} and `missing_stages(trace, stages)` (in `:report`). The header's macros take their category from
`GFX_PROFILE_CATEGORY` when a translation unit defines it before including the header (physics units: PHYSICS); the GPU
macros were given back their line continuations.

**The framework** (`gfx::physics_components` links `gfx::profiler`): scopes "World: step", "run the step", "apply the
edits to the records", "backend apply", "backend simulate", "soft bodies", "update the bounds", "wait for the step",
"publish", "copy the records", "copy the events and contacts", "update the query tree"; counters (edits, rigid bodies,
awake bodies, joints, soft bodies, candidate pairs, contact points, touching pairs, contact events); the world's thread is
named "physics world", the `ThreadTaskScheduler`'s workers "physics task N"; the CPU soft-body solver's sub-steps
("Soft bodies (CPU): predict, colored constraints, tethers and pins, contacts, self-collision, velocities"). **Box3D**:
"Box3D: apply the edits", "b3World_Step", "capture the bodies / contacts / triggers / joints", a scope per Box3D task, and
Box3D's own profile and counters as counters ("Box3D: step (ms)", pairs, collide, solve, solver set-up, constraints, sleep
islands, refit; contacts, awake contacts, islands, tasks). **Reference backend**: a scope per stage (edits, bodies, broad
phase, narrow phase, islands, solver set-up, each sub-step with its integration and solve, results, sleep, events) and
counters.

**The GPU backend**: the **`StepTimer` is gone**: the step records into a `GpuTimer` (2 frames in flight,
`GPU_XPBD_TIMED_SCOPES` 2 048 scopes, replacing `GPU_XPBD_TIMESTAMPS`) through a `PhaseTimer` (the phases "GPU XPBD:
broad phase / narrow phase / solver / soft bodies / events" as ALWAYS scopes, marked as the StepTimer marked them) and a
`PassTimer` per component (`set_timer()` on `GpuPrimitives`, `GpuBvh` and `GpuNarrowPhase`, exported; the solver's and the
soft bodies' internal): a GPU scope per primitive and radix pass, per tree build, pair search and pass inside them, per
narrow-phase pass, per group of solver passes (islands, graph, coloring, one per position, velocity and soft iteration —
not per color), per readback. At each step the backend attaches its timer to the current profiler. CPU scopes "GPU XPBD:
apply the edits", "update the soft bodies / colliders / joints", "record the step", "submit", "wait for the GPU", "read
the bodies back", "contacts and their events", "trigger events", "report the contacts again", "query"; counters from the
command buffer (dispatches, barriers, transfers) and from `GpuSolverCounts` (tree pairs, candidates, manifold slots,
contact constraints and points, constraints, colors, overflow constraints, touching contacts and points, trigger
overlaps), a marker per replay. **`gpu_xpbd::stage_coverage()`** (exported): 81 stages of §A5 and the phases, each
RECORDED by a site, FUSED_IN a site with the reason (the common-prefix search and split in `bvh_hierarchy`, leaf boxes in
`bvh_bounds`, GJK, EPA and feature identifiers in `narrow_phase_collide`, joint and contact projections and contact
velocities by color in their iteration's scope) or ABSENT with the reason (descriptor-arena reset: bindless; treelets: 0
rounds by default; refit: rebuilt each step; compliance warm start: multipliers reset each substep, GPU XPBD answer D8).

**Deviations (for validation).**
1. **GPU timings reach a profiler by the source attaching itself** (`attach_to_current_profiler()`, called by the GPU
   backend at each step) rather than the application adding the backend's timer to its session: the physics interface
   has no place to hand a timer out, and the two-way links make either order of destruction safe.
2. **GJK and EPA iterations are not counted**: counting them means atomics in the narrow-phase kernel; their stages are
   FUSED_IN `narrow_phase_collide` with the reason. Counted when the narrow phase becomes a cost worth optimizing.
3. **Counters not recorded yet**: soft constraints per color, BVH nodes and depth (a header readback), refit or rebuild
   (always rebuild today), workgroups dispatched, bytes per array (the traffic model of §A5). They come with the
   optimizations that need them (PF4/PF5: incremental layouts, BVH_REFIT, fusion).
4. **The solve is timed per iteration** (position, velocity, soft), not per color, as §A5 planned.
5. **Two sites share a name** ("jacobi_gather" after the position and after the velocity iterations): reports keep them
   apart by site (file and line), traces show the scope around each.
6. **Exported API added**: `set_timer()` on the three exported GPU components and `gpu_xpbd::stage_coverage()`.

**Tests.** `gfx_profiler_tests` + 1 (sources and profilers leave each other whichever goes first, attaching once, by hand
and to the current profiler); `gfx_profiler_graphics_tests` + 2 (a scope timed ALWAYS around one that is not, tokens,
`finish_frame()`, on both GPUs); new **`gfx_physics_profiling_tests`** (3: the reference backend's and Box3D's traces of a
scene of every object kind — boxes, joints, a cloth, a trigger — cover the framework's stages and theirs, counters and
named task threads; `missing_stages()` names what is missing); `gfx_physics_gpu_xpbd_tests` + 8 (`GpuXpbdProfilingTest` on
both GPUs with the validation layers: the trace covers every stage of `stage_coverage()` and the framework's, every GPU
scope timed, none dropped, counters; every GPU scope inside its submission on the CPU's timeline; the same steps give the
same canonical trace; the phases timed without a profiler, in every build). The scene's cloth has 66 × 66 particles for
the GPU test: below 4 096 keys the sorts take the rank sort, and the radix passes would be missing (the rank sort is a
stage too).

**Traces of the §0.1 scenes** (`profile` build, Release with the profiler; a temporary program, kept in the session's
scratchpad, removed from the tree; Box3D on 16 scheduler threads; GPU on the RTX 5060; 20 warm-up and 40 traced steps;
milliseconds per step, means):

| Scene | Box3D `step()` | GPU `step()` | Framework (bounds + publish; of which the query tree) | Box3D: `b3World_Step` / capture of contacts | GPU: CPU side (layouts + recording + readback + events) / wait / GPU busy |
|---|---|---|---|---|---|
| 10 000 boxes | 7.34 | 7.37 | 2.6–2.7 (1.9) | 3.2 / 1.2 | 2.3 / 2.4 / 2.1 |
| 50 000 boxes | 37.97 | 37.19 | 16.6–17.2 (11.5–12.1) | 8.9 / 10.6 | 13.7 / 6.3 / 5.0 |
| pyramid, 29 890 boxes | 48.93 | 44.37 | 11.1–12.6 (7.5–7.7) | 12.6 / 24.0 | 15.3 / 16.1 / 15.3 |
| 100 chains × 200 links | 12.43 | 13.74 | 6.5–6.6 (5.2–5.3) | 4.5 / — (joints 0.8) | 3.9 / 3.1 / 2.4 |
| cloth, 99 856 particles | 68.24 | 21.20 | 0.3 | — (framework CPU XPBD 67.9) | 9.7 / 11.0 / 10.7 |

What the traces show:
- **The framework is the largest single cost at 50 000 bodies**: 17 ms of a 37–38 ms step on both backends, **the query
  tree 70 % of it** (every moved collider's proxy updated at every publish), then the bounds (2.9 ms) and the copies of
  records (1.5 ms). §0.1 guessed this split; it is now measured.
- **Contacts and events on the CPU**: the GPU backend's 8.5 ms (50 000 boxes) and 11.9 ms (pyramid); Box3D's wrapper
  pays the same kind of cost capturing contacts — 10.6 and 24.0 ms, more than `b3World_Step` itself in the pyramid.
- **The GPU is idle 49–86 % of a GPU step** (overlap 0.14–0.51): the stepping thread waits for the GPU about 0.3–1.3 ms
  longer than the GPU works, and does the rest of the step around it.
- **The GPU's own time goes to the solve**: velocity iterations (8 per substep) cost about twice the position iterations
  (3 per substep): 1.6 against 0.8 ms at 50 000 boxes, 7.6 against 4.1 ms in the pyramid; the broad and narrow phases
  0.3–1.9 and 0.2–1.1 ms; the events phase and the readback copies 0.3–2.0 ms together.
- **Per-step layouts**: colliders 2.1 ms at 50 000 boxes and 4.0 ms for the cloth's particle colliders, soft bodies 1.5 ms,
  joints 1.6 ms for 20 000 joints; recording 0.6–3.9 ms.
- **Correction to §0.1**: the cloth's 16.9 ms for "Box3D" (the framework's CPU XPBD) was measured while the cloth fell; on
  the floor each of its particles meets the floor every sub-step and a step takes 77 ms (the trace shows 15–19 ms per
  step falling, 75–84 ms landed; "Soft bodies (CPU): contacts" 50 ms per step on average over the traced steps). The GPU
  backend's cloth takes 14.5 ms falling
  and 22 ms landed: **3.5 times faster than Box3D's world once landed**.
- Comparison through `step()` today: a tie at 10 000 and 50 000 boxes, GPU 1.10× faster in the pyramid, 0.90× in the
  chains, 3.2× on the cloth (means over the traced steps; this laptop's clocks move such numbers by 10–25 % run to run:
  Box3D's 10 000 boxes took 5.9 ms in §0.1, its pyramid 64.6 ms).

Results: Debug 1513 of 1513 (1093 s; 6 skipped), Release 1513 of 1513 (1039 s; 44 skipped — the profiler is compiled out,
yet `PhasesAreTimedOnTheGpuWithoutAProfiler` runs there: the phases come from the GPU timer in every build), profile: the
68 profiler, GPU timer, profiling and decoupling tests (1 skipped: the compiled-out check), overhead 57.1 ns per scope and
27.1 ns per counter.

### PF3 as built (2026-11-09)

**Target `gfx_physics_benchmark`** (`benchmarks/physics/`; option **`GFX_BUILD_BENCHMARKS`**, on; `benchmarks/CMakeLists.txt`):
module `physics_benchmark.scenes` (scenes.cppm: the scenes, their quality checks), `benchmark.cpp` (options, the runner, the
report, the baselines), `environment.cpp` behind std-free `environment.hpp` (a plain translation unit: the processor's
brand string by cpuid, the operating system by `ver`, the power scheme by `powercfg /getactivescheme` — this laptop's ASUS
"Silent" mode shows there). Arguments: `--scenes`, `--backends box3d,gpu_xpbd,reference` (default Box3D and the GPU
backend), `--warm-up 100`, `--steps 1000`, `--threads 16`, `--scale 1`, `--gpu-device`, `--trace-frames 100`, `--output`,
`--check-baseline <directory>`, `--record-baseline <directory>`, `--sleep`, `--note`, `--only-when-asked`; exit codes 0
passed, 1 failed (a run, a quality check or a regression of a judged backend), 2 bad arguments, 77 not asked.

**Scenes** (one function each, the same for every backend through `PhysicsWorld`; sleep off unless `--sleep`):
`many_boxes` 50 000 boxes (0.8 m) resting on a floor 1.5 m apart; `pyramid` 29 370 boxes as a square pyramid (44 layers, a
44 × 44 base, 1 cm gaps); `cloth` 316 × 316 = 99 856 particles falling 1 m onto a floor; `chain` 100 chains of 200 links held
by rods, starting horizontal; `mixed` 10 000 boxes — 16 boxes of 1 m under the cloth, 9 984 beside it — and a cloth of
224 × 224 = 50 176 particles falling 2 m onto them; `stress_constraints` a 10 × 10 × 10 lattice of balls, each held by rods
to the next 100 in creation order (100 000 rods; 200 per ball) and its top layer hung from the world by 100 more;
`two_worlds` two worlds of 25 000 resting boxes stepped one after the other (the GPU backend's on one device). **Quality
checks** that hold at any moment of a run: every body finite and above y = −1 m; resting boxes within 25 % of their half
height (+ 5 cm) of rest and upright; the pyramid's top box sunk less than 1 m; rods within 30 % of their length (chains and
lattice; the report shows the worst); particles above −5 cm and below their start + 25 cm.

**Runner**: per scene and backend, the world(s) with the shared `ThreadTaskScheduler` (16 threads); the GPU backend on the
benchmark's own device (a Vulkan instance without validation layers, the physical device the graphics library selects or
`--gpu-device`, a compute queue, 64-bit atomics when offered), shared by the worlds of a scene; creation and building
timed, the first step timed apart, the warm-up, then the measured steps as profiler frames (`step()` wall clock taken
around them); the report's GPU busy time, overlap and idle shares; **CPU work** = every thread's outermost scopes summed
per frame (Box3D's tasks on the scheduler's threads included); Box3D's own step time (its counter); a **Chrome trace of the
last 100 frames** per run (`<scene>_<backend>.json`); the quality checks. **`benchmark_report.md`** (generated): the
environment, every measurement (mean, p50, p95, p99, maximum, CPU work, GPU busy, overlap, GPU idle, Box3D native, first
step, quality), the GPU backend against Box3D (mean speed-up, target 1.25× from 10 000 bodies or particles or 100 000
constraints, else 1×), and against the baseline.

**Baselines**: `benchmarks/physics/baselines/baseline-<FNV-1a 64 of "processor | GPU driver | operating system">.tsv`
(machine, settings — build, warm-up, steps, threads, scale, sleep —, date, then each scene and backend's p50, mean, p95,
p99); compared only when the machine and the settings match; a median more than 5 % above the baseline's fails the run —
**for gfx's own backends only** (the GPU and reference backends) and **only when it persists**: a median above the
threshold is measured again (`--confirmations`, 2 by default) and the best run judged. **CTest**: `PhysicsBenchmark.Smoke` (label `example`, in
the default runs: every scene at a hundredth of its size, 2 + 5 steps, on Box3D, the GPU backend and the reference backend)
and `PhysicsBenchmark.Suite` (label `benchmark`; exit 77, skipped, unless `GFX_RUN_BENCHMARKS=1`; RUN_SERIAL, 2 hours;
`--check-baseline` the source tree's baselines; report in `<build>/benchmarks/physics/suite/`).

**Today's baseline** (recorded 2026-11-09 in the profile build: i9-14900HX, RTX 5060 Laptop driver 610.74.0, Windows
10.0.26200, power scheme "Silent"; Box3D on 16 threads; sleep off; 100 + 1 000 steps; mean ms per step):

| Scene | Box3D | GPU | Speed-up | Target | GPU busy | Quality |
|---|---|---|---|---|---|---|
| many_boxes | 22.04 | 41.27 | 0.53× | 1.25× | 7.3 | both pass |
| pyramid | 103.26 | 88.57 | 1.17× | 1.25× | 38.3 | both pass (top box −1.5 / −8.1 cm) |
| cloth | 83.61 | 23.30 | 3.59× | 1.25× | 11.4 | both pass |
| chain | 12.19 | 16.17 | 0.75× | 1.25× | 3.8 | both pass (worst rod 21.0 / 16.2 %) |
| mixed | 45.21 | — | — | 1.25× | — | **GPU fails** (below) |
| stress_constraints | 67.58 | 26.99 | 2.50× | 1.25× | 11.6 | **Box3D fails** (below); GPU passes (16.6 %) |
| two_worlds | 21.02 | 44.11 | 0.48× | 1.25× | 10.0 | both pass |

The target is met on the cloth and the lattice only. Runs against this baseline with the same code moved the GPU's
medians by −1.8 to +4.6 % — once +11.1 % (`two_worlds`) — and Box3D's by −10.6 to +6.2 %: hence Box3D is reported, not
judged, and a suspected regression is measured again (checked: `many_boxes` on the GPU first +6.0 %, then +2.6 %, passed).
Both runs reproduced the failures below identically.

**Found by the benchmark.**
1. **A GPU backend defect** (`mixed`): the boxes under the cloth are pushed into the floor — box 1 at y = −0.35 m by step
   60, through it (−2 m) by step 120 — and once things fly apart the trees' pairs pass 2²⁴ (16 871 629 pairs, of which
   42 986 kept: the trees' boxes all grow by the fastest motion of anything): the step still overflows after 4 replays and
   throws. The framework's CPU XPBD (Box3D's world) holds the same scene. Hypothesis: a box of 1 m touches up to ~400
   particles; beyond the 32 colors those contacts go to the Jacobi overflow set, whose mass splitting divides the box's
   corrections among its touching contacts, and the floor's push loses to the particles'. **Fixed before PF4** (the
   hypothesis confirmed: mass splitting is now weighted by the constraints' shares, docs/gpu-xpbd-design.md §B "Fix after
   G10"); `mixed` then passes on the GPU in 56.0 ms (Box3D 44.6 ms).
2. **Box3D comes apart on `stress_constraints`**: by step 100 a ball is flung 10 m above the anchors, later rods are 4 756 to
   7 774 % off their length; smaller lattices hold (0.04 % at 30 balls, 0.06 % at 100, 2 % at 300). The GPU backend holds the
   full lattice (worst rod 16.6 %).
3. **At rest the GPU backend is about twice as slow as Box3D** (`many_boxes`, `two_worlds`: 41–44 against 21–22 ms; the GPU
   busy 7–10 ms): the CPU side and the framework, as PF2's traces showed.
4. The first GPU world of a profile build took 36 s (the driver compiling every pipeline with a cold cache; 2.5 s warm):
   PF4's shared pipelines and cache.

**Deviations (for validation).**
1. **Sleep off in every scene** (`--sleep` turns it on): the benchmark measures the simulation, not sleeping bodies (as
   Box2D's benchmarks do); the request's sleep tuning is then measured apart.
2. **`pyramid` is a square pyramid of 29 370 boxes**: the wall of 29 890 boxes of §0.1 topples sideways in three dimensions
   within the run (and a box falling 244 m went through the floor on the GPU).
3. **`stress_constraints` hangs from the world** (100 000 rods + 100 to the world, no floor): a lattice landing on a floor
   pushed balls through it on both backends.
4. **`mixed` puts 16 boxes under the cloth, the rest beside it**: the framework's CPU XPBD tests every particle against
   every collider in the cloth's box; 10 000 boxes under it would take minutes per step in Box3D's world.
5. **Only gfx's backends are judged** (failures and regressions); Box3D's are reported. **A regression must persist**: a
   median above 5 % is measured again (2 more runs at most) and the best run judged.
6. **The long run is skipped, not filtered out**: `PhysicsBenchmark.Suite` exits 77 unless `GFX_RUN_BENCHMARKS=1` (CTest has
   no default label filter for plain `ctest` runs); the quick smoke run is in the default runs.
7. **Files**: the scenes in one module, not a file each; the reference backend runs when asked (and in the smoke run);
   rods in the chains and the lattice tolerate 30 % (the chains measured 16–21 % at the bottom of their swing).
8. **The suite failed on this machine** — the GPU's `mixed` (finding 1) — until that defect was fixed (it passes since).

**Baseline recorded again after the fix** (2026-11-09, same machine and settings; mean ms, Box3D / GPU / speed-up):
many_boxes 25.77 / 43.79 / 0.59×, pyramid 106.24 / 85.36 / 1.24×, cloth 81.65 / 23.11 / 3.53×, chain 12.04 / 16.17 / 0.74×,
mixed 44.58 / 55.97 / 0.80×, stress_constraints 67.15 (Box3D fails its check) / 27.10 / 2.48× (worst rod 10.2 %), two_worlds
22.18 / 47.15 / 0.47×; every judged check passes (the benchmark's exit code 0). The target is met on the cloth and the
lattice: PF4 and PF5 start from here.

Results: Debug and Release builds with every test: 1515 of 1515 in Debug (994 s); Release 1513 of 1515 (1017 s), the two
others the benchmark's, not run because its executable failed to compile at the time (a helper declared before the type it
used) — fixed, then `PhysicsBenchmark.Smoke` passes and `PhysicsBenchmark.Suite` is skipped (not asked) in Debug, Release
and profile. Profile suite with `GFX_RUN_BENCHMARKS=1`: fails as expected on the GPU's `mixed` (finding 1); its gate, checked
apart after the confirmation change, passes the other GPU scenes.

---

## C. The API as proposed (for validation, not final code)

### C1. `gfx/profiler.hpp`

```cpp
#if GFX_PROFILER_ENABLED
#define PROFILE_SCOPE(name)                                                                   \
    static constinit ::gfx::profiler::ProfileSite GFX_PROFILE_NAME(site){name, ::gfx::profiler::ProfilerCategory::GENERAL, \
                                                                       __FILE__, __LINE__};   \
    const ::gfx::profiler::ProfileScope GFX_PROFILE_NAME(scope){GFX_PROFILE_NAME(site)}
#define PROFILE_COUNTER(name, value) /* a site + ::gfx::profiler::record_counter(site, value) */
// PROFILE_SCOPE_CATEGORY, PROFILE_FUNCTION, PROFILE_NAMED_SCOPE, PROFILE_COUNTER_FLOAT, PROFILE_MEMORY, PROFILE_MARKER,
// PROFILE_ASYNC_BEGIN, PROFILE_ASYNC_END, PROFILE_GPU_SCOPE, PROFILE_DISPATCH likewise
#else
#define PROFILE_SCOPE(name) ((void)0)
// ... every macro ((void)0)
#endif
```

### C2. `gfx.profiler`

```cpp
struct SessionDescription
{
    std::uint32_t frames = 0;                                   ///< Frames to record (0: until finalized).
    std::uint32_t records_per_thread = 1U << 16;                ///< Ring capacity per thread.
    std::uint32_t gpu_queries_per_frame = 4096;                 ///< Timestamps per frame and provider.
    ProfilerCategory categories = ProfilerCategory::ALL;        ///< Recorded categories.
};

class Profiler
{
public:
    [[nodiscard]] auto create_session(const SessionDescription& description) -> SessionHandle;
    void begin_frame(SessionHandle session);
    void end_frame(SessionHandle session);                      ///< Drains every thread's ring.
    [[nodiscard]] auto finalize_session(SessionHandle session) -> Trace;   ///< Resolves the GPU scopes (waits for them).
    void add_gpu_provider(SessionHandle session, GpuTimingProvider& provider);
};

[[nodiscard]] auto create_profiler(const ProfilerDescription& description = {}) -> std::unique_ptr<Profiler>;
void set_current_profiler(Profiler* profiler) noexcept;        ///< What the macros record into.

[[nodiscard]] auto make_report(const Trace& trace, const ReportDescription& description = {}) -> Report;
void export_trace(const Trace& trace, TraceFormat format, const std::filesystem::path& path);   ///< throws ProfilerError
```

### C3. `GpuXpbdDescription` additions

```cpp
struct GpuXpbdDescription
{
    // ... as today
    GpuOptimization optimizations = DEFAULT_GPU_OPTIMIZATIONS;   ///< Each flag: §A10; NONE = the baseline.
    std::uint32_t bvh_rebuild_period = 30;                        ///< Steps between rebuilds of the moving tree (refit between).
    float bvh_rebuild_cost_ratio = 1.5F;                          ///< Rebuilt sooner when its SAH cost grew so much.
    std::filesystem::path pipeline_cache_path{};                  ///< Pipelines saved and loaded there (empty: not kept).
};
```

### C4. Profiling a step and flipping a flag (the request's usage example, on the proposed API)

```cpp
import std;
import gfx.profiler;
import gfx.physics;
import gfx.physics.gpu_xpbd;
#include <gfx/profiler.hpp>   // (in a module unit: in the global module fragment)

auto run(gfx::physics::GpuOptimization optimizations) -> gfx::profiler::Report
{
    const auto profiler = gfx::profiler::create_profiler();
    gfx::profiler::set_current_profiler(profiler.get());
    const auto session = profiler->create_session({.frames = 300});
    const auto world = gfx::physics::gpu_xpbd::create_physics_world({}, {.optimizations = optimizations});
    build_pyramid(*world);                                       // the benchmark's scene functions
    for (int frame = 0; frame < 300; ++frame)
    {
        profiler->begin_frame(session);
        {
            PROFILE_SCOPE("frame");
            world->step(1.0F / 60.0F);                           // its stages record themselves
        }
        profiler->end_frame(session);
    }
    const auto trace = profiler->finalize_session(session);
    gfx::profiler::export_trace(trace, gfx::profiler::TraceFormat::CHROME_JSON, "pyramid.json");   // open in Perfetto
    return gfx::profiler::make_report(trace);
}
// The report shows "contact_solve" as the hottest GPU stage: run again without COLORING_CACHE and compare.
```

---

## D. Questions

Each has a recommendation; "as recommended" answers them all.

**Answers (2026-11-08)**: "as recommended" — every question below as recommended.

1. **Libraries**: `gfx::profiler` (core only, usable by every library) and the bridge `gfx::profiler_graphics` for GPU
   timing (§0.2 row 1)? *Recommended: yes.*
2. **Macros**: in one header `gfx/profiler.hpp` (modules cannot export macros), with a macro-free API beside it (row 2)?
   *Recommended: yes.*
3. **Handles**: core's `Handle<Tag>`, only for profiler objects a user names; no public handles for the backend's
   internal kernels, arenas or work queues (row 4)? *Recommended: yes.*
4. **Flags**: `GpuXpbdDescription::optimizations` (+ BVH cadence, pipeline cache path), the physics interface unchanged
   (row 6)? *Recommended: yes.*
5. **Trace determinism**: identical structure with times excluded, rather than byte-identical files including times
   (row 9)? *Recommended: yes.*
6. **Build**: option `GFX_PROFILER` (on in debug, off in release) and a third preset `profile` (Release + profiler) for the
   benchmarks and the timing tests (row 11)? *Recommended: yes.*
7. **Priorities**: order the optimization program by measurement — the framework and the CPU side first (§0.1) — and keep,
   re-scope or drop the request's items as §A10 says (persistent kernels only without inter-workgroup waiting and off by
   default; descriptor reuse and slot reduction already there; prefetch replaced by recording reuse; fp16 not used)?
   *Recommended: yes.*
8. **Readback semantics**: keep "after `wait_step()` the getters show the step just completed" (no N-step lag), reducing
   what is read back and overlapping through asynchronous stepping (row 13)? Or an opt-in latency setting?
   *Recommended: keep the semantics; no latency setting now.*
9. **The framework**: optimize `SteppedWorld` (publish, bounds, query tree) as part of this task, knowing it speeds Box3D
   up too and so raises the bar (§A10 item 1)? *Recommended: yes — it is the largest cost, and the comparison stays fair.*
10. **Scenes**: `many_boxes` = 50 000 boxes each resting on the floor; `stress_constraints` = 1 000 bodies and 100 000
    joints (each body joined to 100 others), not 10 million (row 19)? *Recommended: yes.*
11. **Acceptance setup**: Box3D with 16 scheduler threads on this i9-14900HX (or all 32?), the GPU backend on the RTX 5060;
    the Intel GPU measured and reported but not held to the target; the comparison through `step()` with Box3D's own phase
    times reported beside it; misses documented with cause (row 21)? *Recommended: 16 threads, as stated.*
12. **Regression gate and CI**: benchmarks as a CTest label `benchmark` outside default runs, the 5 % gate against a
    baseline stored per machine; no GitHub Actions now (rows 22–23)? *Recommended: yes.*
13. **Pipelines**: shared per device by the backend's worlds and cached to a file named in the settings (row 24)?
    *Recommended: yes.*
14. **Graphics addition**: `DeviceFeature::CALIBRATED_TIMESTAMPS` + `Device::calibrate_timestamps()` (row 8)?
    *Recommended: yes.*
15. **Formats**: Chrome Tracing JSON and the binary trace built in; the live sink as a callback interface (no network
    server); zstd optional for binary traces; no lz4, no glm (rows 7, A6)? *Recommended: yes.*
16. **Plan**: phases PF0–PF6 in that order (§B)? *Recommended: yes.*
