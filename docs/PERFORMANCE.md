# Performance of the physics backends

This document records how fast the GPU physics backend (XPBD in compute shaders, [gpu-xpbd-design.md](gpu-xpbd-design.md))
steps worlds against Box3D, where a step's time goes, and the optimizations that changed it. The decisions and each phase's
full record are in [profiler-performance-design.md](profiler-performance-design.md) (phases PF0–PF6); the profiler that
measured everything is described in the [README](../README.md#profiler) and in [architecture.md](architecture.md#10-the-profiler-and-the-physics-benchmark).

**In short.** On the benchmark's seven large scenes (20 000 to 100 000 bodies, particles or constraints), the GPU backend
steps every scene faster than Box3D: 1.3× to 9.9× with sleeping off, 2× to 107× with sleeping on. Its step time fell by 38
to 78 % from the starting point of the optimizations (PF3's baseline). In worlds of a few thousand moving bodies — piles, walls,
chains — Box3D is faster per step: the GPU backend pays a fixed cost per pass that small worlds do not amortize
([§6](#6-worlds-of-a-few-thousand-moving-bodies)).

## 1. The machine and the method

| | |
|---|---|
| Processor | Intel Core i9-14900HX (24 cores, 32 threads), 16 GB |
| GPU | NVIDIA GeForce RTX 5060 Laptop GPU, driver 610.74.0 |
| Operating system | Windows 11 (10.0.26200), power scheme "Silent" (the laptop's quiet mode: its clocks move by a factor of up to 5 over hours) |
| Compiler | GCC 16.1 (MSYS2 UCRT64) |
| Build | the `profile` configuration: Release flags with the profiler on (`python build.py --profile`, `build/profile`) |

The physics benchmark (`benchmarks/physics`, `gfx_physics_benchmark`) builds each scene with one function for both backends,
steps it through `PhysicsWorld::step()` (1/60 s, the default solver: 4 substeps, 3 position and 8 velocity iterations), 100
warm-up steps then 1 000 measured ones, and checks the result: a step that is fast and wrong is no win. Box3D runs on a
`ThreadTaskScheduler` of 16 threads; the GPU backend on the RTX 5060 through the factory. Both pay the framework
(`SteppedWorld`: edits, bounds, publishing). Each run writes a Chrome trace and a generated `benchmark_report.md` (below:
the run of 2026-11-14).

Run to run, Box3D's means move by up to 25–30 % and the GPU backend's by about 5 % on this laptop. Comparisons between two
versions of the code were therefore made alternately (version A, version B, A, B…), one process per scene, on medians
([profiler-performance-design.md](profiler-performance-design.md) §B "PF5b as built").

## 2. Results

### 2.1 Sleeping off

Mean `step()` wall clock in milliseconds; *GPU busy*: the union of the GPU's timed passes within a step.

| Scene | What | Box3D | GPU | Speed-up | GPU p95 | GPU busy | Quality (GPU) |
|---|---|---|---|---|---|---|---|
| many_boxes | 50 000 boxes resting on a floor | 23.56 | 11.62 | 2.03× | 14.41 | 5.14 | boxes within 0.3 mm of rest |
| pyramid | a square pyramid of 29 370 boxes (base 44) | 109.64 | 45.56 | 2.41× | 48.80 | 36.21 | top box 7.6 cm lower |
| cloth | a cloth of 316 × 316 particles on a floor | 83.36 | 14.43 | 5.78× | 17.24 | 11.05 | flat, no stretch |
| chain | 100 chains of 200 rods (20 000 joints) | 5.60 | 3.49 | 1.60× | 4.34 | 2.20 | worst rod 16.2 % off |
| mixed | 10 000 boxes, 16 of them under a cloth of 50 176 particles | 44.22 | 18.74 | 2.36× | 21.49 | 14.27 | cloth stretch 36.9 % (Box3D's: 32.8 %) |
| stress_constraints | a lattice of 1 000 balls and 100 100 rods, hanging | 75.91 (fails its check) | 7.69 | 9.87× | 10.04 | 4.98 | worst rod 10.2 % off |
| two_worlds | two worlds of 25 000 boxes on one GPU | 16.15 | 12.43 | 1.30× | 14.98 | 6.04 | boxes within 0.3 mm of rest |

The target — no slower than Box3D anywhere, at least 1.25× faster from 10 000 bodies (or particles) or 100 000
constraints — is met on every scene. Box3D's lattice comes apart (a rod 4 756 % off its length), so that scene is not judged.
Box3D has no soft bodies: in `cloth` and `mixed`, the framework's CPU XPBD solver steps the particles beside it.

### 2.2 Sleeping on

| Scene | Box3D | GPU | Speed-up |
|---|---|---|---|
| many_boxes | 12.37 | 1.33 | 9.28× |
| pyramid | 101.30 | 0.95 | 106.52× |
| cloth | 85.89 | 12.81 | 6.70× |
| chain | 6.95 | 3.45 | 2.01× |
| mixed | 51.52 | 16.16 | 3.19× |
| stress_constraints | 85.49 (fails its check) | 7.94 | 10.77× |
| two_worlds | 10.36 | 1.60 | 6.47× |

The resting scenes fall asleep during the warm-up on the GPU backend, which then steps without the GPU
(`RESTING_STEPS`): about 1 ms a step, the framework's and the events' work. Box3D's pyramid never falls asleep in 1 100 steps.

### 2.3 Against the starting point

The GPU backend's mean step before the optimizations (PF3's baseline, recorded 2026-11-09 on this machine) and now:

| Scene | GPU before | GPU now | Change | Box3D before | Box3D now |
|---|---|---|---|---|---|
| many_boxes | 41.3 | 11.6 | −72 % | 22.0 | 23.6 |
| pyramid | 88.6 | 45.6 | −49 % | 103.3 | 109.6 |
| cloth | 23.3 | 14.4 | −38 % | 83.6 | 83.4 |
| chain | 16.2 | 3.5 | −78 % | 12.2 | 5.6 |
| mixed | 56.0 (after the fix of its defect) | 18.7 | −67 % | 45.2 | 44.2 |
| stress_constraints | 27.0 | 7.7 | −72 % | 67.6 | 75.9 |
| two_worlds | 44.1 | 12.4 | −72 % | 21.0 | 16.1 |

Box3D's own code did not change; its column moves with the laptop's clocks and with the framework's improvements, which
every backend shares (PF4, PF5b).

## 3. Where a step's time went

### 3.1 The bottleneck taxonomy, before and after

*Before*: the temporary measurements of §0.1 of the design (2026-11-08, before the profiler existed). *After*: the PF6
benchmark's traces (per step of the GPU backend, milliseconds).

| Area | Before | After | What changed it |
|---|---|---|---|
| The framework per step (bounds, publishing, the query tree) | 6–15 at 20 000–50 000 bodies | 0.4–1.3 | the query tree brought up to date at the first query only, with fat leaves; bounds and copies in parallel; events and contacts swapped, not copied; unmoved colliders' boxes kept (PF4, PF5b) |
| Contacts and events on the CPU | 3.6–13 | 0–6.3 (the pyramid's 125 000 contacts) | sorted 64-bit pair keys, conversion in parallel and in place, side-by-side walks, prefetching (PF4, PF5b) |
| Layouts of colliders, joints and soft bodies | 1.1–5.7 | 0 in the steady state | `INCREMENTAL_LAYOUTS` |
| Recording a step | 0.8–4.2 | 0.5–2.4, hidden behind the GPU's work | `PARTIAL_SUBMISSIONS` (the GPU runs a part while the next is recorded) |
| Dispatches and barriers | 630–810 a step | 324–1 595 (fewer where few colors are used; more in `mixed` and the pyramid, which use many) | `RECORDED_PASSES_BY_USE`: passes only for the colors used |
| The GPU solver in deep stacks | 12 (2D pyramid of 29 890) | 28 (3D pyramid of 29 370: 245 000 contact points at 4 × (3 + 8) iterations) | the velocity iterations are 65 % of it; their count is the application's choice (§5) |
| Tree builds | 0.3–1.7 | 0.07–0.42 | `REACH_BOXES` (fewer candidate pairs), `BVH_REFIT` |
| Readback | every body, every contact | the bodies that changed, in worlds of 4 096 bodies or more | `COMPACT_READBACK` |
| Pipeline creation | 46 s cold, 0.4–2.7 s warm, per world | once per device, from a cache file; a world in 0.09–0.32 s | `SHARED_PIPELINES` |
| Allocations in the steady state | not measured | 0 (counted on every thread, a test) | PF4 |

### 3.2 A GPU step now, scene by scene

Per step (milliseconds; `two_worlds`: per world's step, two a frame). *Wait*: the stepping thread waiting for the GPU after
submitting; *GPU phases*: the GPU's timed phases.

| Scene | Step | Framework | Contacts and events | Layouts | Recording | Wait | GPU solver | GPU phases | Dispatches | Barriers |
|---|---|---|---|---|---|---|---|---|---|---|
| many_boxes | 11.34 | 1.29 | 2.54 | 0 | 1.28 | 5.39 | 3.15 | 5.13 | 326 | 333 |
| pyramid | 43.11 | 1.14 | 6.29 | 0 | 1.62 | 33.40 | 28.33 | 33.44 | 1 107 | 1 114 |
| cloth | 14.65 | 0.91 | 0 | 0 | 1.79 | 11.09 | 5.71 | 11.01 | 438 | 445 |
| chain | 3.33 | 0.42 | 0 | 0 | 0.47 | 2.02 | 1.13 | 2.12 | 338 | 321 |
| mixed | 18.67 | 1.13 | 1.03 | 0 | 2.37 | 13.46 | 10.26 | 14.22 | 1 588 | 1 595 |
| stress_constraints | 7.55 | 0.67 | 0 | 0 | 0.84 | 4.92 | 4.51 | 4.96 | 613 | 584 |
| two_worlds | 6.70 | 0.76 | 1.29 | 0 | 1.07 | 3.10 | 1.84 | 3.00 | 324 | 331 |

Where the GPU's work is large (the pyramid, the cloth, `mixed`), most of a step is the GPU; where it is small (`many_boxes`,
`two_worlds`), half of a step is CPU work around it — contacts and events, recording, publishing.

## 4. The optimizations

Every GPU optimization is a bit of `GpuOptimization` in `GpuXpbdDescription::optimizations`; all are in
`DEFAULT_GPU_OPTIMIZATIONS`, and `GpuOptimization::NONE` runs the backend as it was before them (the benchmark's
`--gpu-optimizations` sets them). Each has a test that fails when its mechanism is broken (checked by breaking it).

| Flag | Phase | What it does | Measured | Bits |
|---|---|---|---|---|
| `INCREMENTAL_LAYOUTS` | PF4 | colliders', joints' and soft bodies' layouts kept unless an edit other than motion changed them | −10 ms a step for the lattice's 100 100 joints, −5.6 ms for the cloth, −2.2 ms for 50 000 boxes | the same |
| `SHARED_PIPELINES` | PF4 | pipelines made once per device through a `PipelineCache` read from and written to `pipeline_cache_path` | a world's creation 0.4–2.7 s → 0.09–0.32 s (this run's worlds) | the same |
| `CHUNKED_GATHER` | PF5 | the Jacobi gather reads lists of the overflow set's entries made once per step; a body with more than 32 sums them in chunks in parallel | `mixed`'s gather 19.1 → 1.2 ms | the same (a heavy body's sum associated by chunks) |
| `NODE_MAXIMA_COLORING` | PF5 | each coloring round compares a constraint with its bodies' atomic maxima instead of scanning their constraints | `mixed`'s coloring 7.3 → 0.4 ms | the same colors |
| `RECORDED_PASSES_BY_USE` | PF5 | solver passes recorded for the colors each kind of constraint used in the previous step; overflow passes only after a step that had an overflow set; the coloring's rounds those the previous step decided constraints in, and two more (since the chains' follow-up); the islands' rounds those the previous step changed labels in, and two more (since 2026-11-15: 8 → 3–4 in most worlds); a replay otherwise | 308–703 fewer dispatches and barriers a step; the chains' 24 rounds → 8 (32 passes) | the same |
| `REACH_BOXES` | PF5 | the broad phase's boxes grown by each collider's own reach, not the fastest body's | `chain`'s 832 000 tree pairs a step gone; its broad phase 1.65 → 0.58 ms | the same pairs, another order |
| `COMPACT_READBACK` | PF5b | the GPU lists and gathers the bodies that may have changed; only in worlds of 4 096 bodies or more of which at most half changed | the readback of resting worlds (with sleep) | the same |
| `RESTING_STEPS` | PF5b | a world at rest (no awake body, no edit, no particle or trigger) steps without the GPU | with sleep: 50 000 boxes 12.4 → 0.9 ms, the pyramid 14.0 → 0.9 ms | the same for the same steps |
| `BVH_REFIT` | PF5b | the moving tree refit for 30 steps while the moving colliders stay the same, then built again | the tree's build in the scenes with motion | the same pairs, another order |
| `KERNEL_FUSION` | PF5b | bodies' and particles' velocity updates in one dispatch | a dispatch and a barrier per substep with particles | the same |
| `PARTIAL_SUBMISSIONS` | PF5b | the step submitted in parts of at least 64 dispatches as it is recorded | recording hidden behind the GPU's work; small worlds 5–24 % faster | the same |
| `REACHABLE_CONTACTS` | after PF6 | a contact none of whose points can meet within the step is no constraint (the pair filter's reach test on the points' gaps) | rods beside a fast ball: 4× fewer constraints; the comparison's wall at size 8 with 4/3/1: 11.5 → 9.3 ms | other bits, the same motion |
| `RELATIVE_SPEED_COLORING` | after PF6 (chains) | a contact that cannot meet at its points' relative speed is solved in the overflow set (Jacobi), not colored | the chains at size 8: 8.5 → 3 colors, 488 → 338 dispatches, the solver's GPU time 1.79 → 1.15 ms | other bits, the same motion |
| `OVERFLOW_NEW_COLORS` | after PF6 (chains) | in a step that records the overflow set's passes anyway, the colors recorded are those the previous step used (no two spare ones) and a new color's constraints join the overflow set instead of a replay | two passes fewer a solver iteration where an overflow set stays (the chains: 3 colors recorded instead of 5) | other bits in the steps whose colors grow |
| `PUSHED_CONTACT_CACHE` | after PF6 (cache) | the contact cache matches only the previous step's contacts that pushed, listed at the end of each step (their exact count read back: one pass below 4 096, no cache when none pushed) instead of sorting every manifold slot | the size-8 chains: the narrow phase 0.43 → 0.08 ms of GPU work, 250 → 231 dispatches | other bits (a contact that never pushed no longer passes on its first anchors) |

Changes that are not flags (every backend, or the GPU backend's CPU side; kept because the conformance suite and the
determinism tests pass unchanged):

- **The framework**: the query tree brought up to date lazily with fat leaves (it was 70 % of the framework's 17 ms at
  50 000 bodies); bounds and published copies in parallel (`for_each_range()`); events and contacts swapped into the published
  snapshot; colliders that did not move keep their boxes.
- **The GPU backend's contacts and events**: sorted by 64-bit pair keys with a byte-wise radix sort, converted once in parallel
  straight into the published list (`SteppedWorldBackend::replaces_contacts()`), events from side-by-side walks of this and last
  step's sorted pairs, reports prefetched 8 places ahead (the pyramid: 20–22 → 12–14 ms on one thread).
- **The step's farthest motion without denormal floats** (4 ms → 0.26 ms a step over a resting cloth: their squares are
  computed about a hundred times slower).
- **`read_back()` in parallel**, each range of bodies storing once (a shared atomic stored by every thread was a regression,
  found by comparing with the previous commit, and fixed); the joints' results too (the chains' 7 684 joints took 0.1 ms on
  one thread).
- **Each body's motion kept** for the step's speculative margin (`BodyMotion`: its speeds and gravity scale, refreshed by the
  read-back and by each edit that patches the body): the step no longer reads every body's record (0.13 → 0.01 ms for 7 684
  bodies). The same arithmetic, the same bits; Debug builds compute it from the records too and throw a `PhysicsError` when the
  two differ.
- **The first part submitted once the broad phase is recorded** (`PARTIAL_SUBMISSIONS`, `GPU_XPBD_FIRST_PART_DISPATCHES` 16): the
  GPU starts while the narrow phase and the constraints are recorded (0.35 → 0.08 ms before the GPU starts, size-8 chains).
- **The contact cache's keys take the instances' bits** (`GpuMatchOptions::instance_count`): an empty manifold sorts at the
  instance count instead of 0xFFFFFFFF, so both sorts need about 14 key bits, not 32 (two radix passes each instead of four;
  13.4 → 9.2 radix passes a step for the chains). The same order, the same bits.
- **`gfx::SharedMutex`** (PF6): the framework's objects lock the published snapshot for every getter. MinGW's
  `std::shared_mutex` (winpthreads' read-write lock) takes about 900 ns to lock and unlock uncontended, so reading the poses
  of 8 000 bodies took 8.4 ms; `gfx::SharedMutex` (`gfx.core.shared_mutex`: a slim reader/writer lock on Windows, 10 ns)
  made it 0.47 ms. Found by the physics comparison ([§6](#6-worlds-of-a-few-thousand-moving-bodies)).

## 5. Measured and not built

| Item | Why not |
|---|---|
| Contacts and events made on the GPU (`GPU_EVENTS`) | the CPU path became 0–6 ms after PF4's sort and in-place conversion; most of what is left is converting 125 000 contacts into the interface's records |
| Reusing a recorded step (`REUSE_RECORDING`) | recording is hidden behind the GPU by `PARTIAL_SUBMISSIONS` in optimized builds. It matters with the validation layers in Debug (a small world's step 46–80 ms, of which recording is about 37 ms), but a replay would write timestamps into another frame's query slots, and knowing a recording may be replayed needs the commands compared |
| Coloring warm start (`COLORING_CACHE`) | the coloring is 0.2–1.7 ms a step since `NODE_MAXIMA_COLORING` |
| Subgroup operations in more passes, vectorized loads | no measured pass is bound by them |
| Persistent kernels | Vulkan guarantees no forward progress between workgroups: a kernel whose workgroups wait for each other can deadlock |
| A small world's substeps in one workgroup (`FUSED_SUBSTEPS`, tried 2026-11-15) | slower wherever it was meant to help. One dispatch of one workgroup ran every substep's passes (prediction, each color of each position iteration, the overflow set and its gather, the velocity update and iterations) as loops of its invocations, the same functions on the same items in the same order, barriers between them, the storage buffers coherent. Substeps' GPU time a step, the benchmark at a twentieth of its size (RTX 5060, 4/3/1): chain (1 000 links, 3 colors) 0.41 ms as passes, 0.64 ms in a workgroup of 256 invocations, 0.71 ms of 1 024; pyramid (1 468 boxes, 4 485 contacts) 2.05 → 5.19 ms; 2 500 resting boxes 0.27 → 1.77 ms. The passes are not bound by their dispatches at these sizes: a joint's or a contact's projection is a long chain of dependent loads and arithmetic, which one multiprocessor runs in more rounds than the whole GPU, its coherent loads past the L1 |
| Sleep tuning | not needed: with the default thresholds the resting scenes sleep within the warm-up |
| Half floats | 11-bit mantissas (about 6 cm at 100 m): not for positions, velocities or contacts |

**The solver settings** (the Pareto front, `benchmarks/physics/pareto.py`: substeps {2, 4, 8} × position iterations
{1, 2, 3} × velocity iterations {1, 2, 4, 8} on the GPU backend): velocity iterations cost the most and buy the least.

| Scene (worst error over the run) | 4/3/8 (defaults) | 4/3/4 | 4/3/2 | 4/3/1 |
|---|---|---|---|---|
| pyramid (top box moved) | 51.9 ms, 0.110 m | 38.9 ms, 0.115 m | 32.0 ms, 0.130 m | 28.9 ms, 0.140 m |
| mixed (cloth stretch) | 19.9 ms, 49.7 % | 16.3 ms, 51.2 % | 13.3 ms, 50.4 % | 12.5 ms, 47.9 % |
| chain (rod stretch) | 4.7 ms, 30.4 % | 4.5 ms, 30.4 % | 4.4 ms, 30.4 % | 4.2 ms, 30.4 % |

The defaults stay 4/3/8 (decided 2026-11-10); an application trades accuracy for time through `SolverDescription`.

## 6. Worlds of a few thousand moving bodies

The physics comparison (`examples/physics_sandbox/physics_comparison.cpp`, `gfx_physics_comparison`) builds one scene into a
Box3D world and a GPU world, steps them in lockstep and draws them side by side. Its scenes are smaller than the benchmark's
and keep moving: a pyramid, a pile poured into a bin, a brick wall hit by a wrecking ball, rows of swinging chains, a cloth.
Median step in milliseconds, 300 steps (sleep on unless said; size 16: 120 steps, sleep off), on the same machine:

| Scene | Size | Bodies or particles | Box3D | GPU | Box3D ÷ GPU |
|---|---|---|---|---|---|
| pyramid | 1 | 2 109 boxes | 2.17 | 0.10 | — (the GPU world sleeps) |
| pyramid | 4 | 8 555 boxes | 18.14 | 0.59 | — (the GPU world sleeps) |
| pyramid | 8 | 16 206 boxes | 41.01 | 0.88 | — (the GPU world sleeps) |
| pyramid | 8, no sleep | 16 206 boxes | 42.57 | 28.03 | 1.52 |
| pyramid | 16, no sleep | 33 511 boxes | 104.16 | 56.26 | 1.85 |
| pile | 1 | 2 000 boxes, balls, capsules | 2.63 | 9.36 | 0.28 |
| pile | 4 | 8 000 | 11.49 | 13.56 | 0.85 |
| pile | 8 | 16 000 | 25.01 | 22.73 | 1.10 |
| pile | 8, no sleep | 16 000 | 24.25 | 18.82 | 1.29 |
| pile | 16, no sleep | 32 000 | 53.21 | 34.53 | 1.54 |
| wrecking | 1 | 1 521 bricks and a ball on a rope | 1.82 | 8.27 | 0.22 |
| wrecking | 4 | 6 113 | 6.10 | 12.02 | 0.51 |
| wrecking | 8 | 12 332 | 11.27 | 24.11 | 0.47 |
| wrecking | 8, no sleep | 12 332 | 11.27 | 21.77 | 0.52 |
| wrecking | 16, no sleep | 24 513 | 26.76 | 22.50 | 1.19 |
| chains | 1 | 960 capsule links, 960 ball joints | 0.68 | 1.28 | 0.53 |
| chains | 4 | 3 840 links and joints | 1.45 | 3.34 | 0.43 |
| chains | 8 | 7 684 links and joints | 2.28 | 4.35 | 0.52 |
| chains | 8, no sleep | 7 684 links and joints | 2.39 | 5.09 | 0.47 |
| chains | 16, no sleep | 15 360 links and joints | 4.78 | 3.83 | 1.25 |
| cloth | 1 | 6 400 particles | 7.50 | 2.16 | 3.48 |
| cloth | 4 | 25 600 particles | 13.56 | 3.39 | 3.99 |
| cloth | 8 | 51 076 particles | 20.37 | 5.15 | 3.95 |
| cloth | 8, no sleep | 51 076 particles | 21.27 | 5.27 | 4.04 |
| cloth | 16, no sleep | 102 400 particles | 21.16 | 8.53 | 2.48 |

What it shows:

- **The GPU backend has a fixed cost per pass.** At 4 substeps × (3 + 8) iterations, a step runs 300–1 600 dispatches,
  each followed by a barrier: a few microseconds each whatever the work. At 8 000 bodies of the pile, the 32 velocity
  iterations take 6.2 ms of the GPU's 9.6 ms; Box3D steps the same pile in 4.2 ms of its own (`b3World_Step`).
- **Moving piles, walls and chains of a few thousand bodies are Box3D's ground; from about 16 000 the GPU's.** Piles and
  pyramids cross over between 8 000 and 16 000 bodies (16 000: the pile 1.1–1.3×, the pyramid 1.5×). A wall hit by a ball
  (12 000 bricks, 117 000 contact points) and chains of links (7 700) are still about twice as fast on Box3D at size 8 —
  many contacts per body, or many joints in few colors, solved color by color with each pass's fixed cost — and cross over
  at size 16 (1.2× and 1.25×). Cloths are the GPU's at any size: 2.5–4× (Box3D has no soft bodies: the framework's CPU XPBD
  steps them).
- **With sleeping on, a scene at rest costs the GPU backend almost nothing**, while Box3D's large pyramids never fall asleep:
  compare scenes in motion with `--no-sleep` (or the panel's "sleeping").
- **Our Box3D wrapper's contact capture is most of Box3D's step**: 7.8 ms of an 11.4 ms step at 8 000 bodies (37 000 contact
  points), against `b3World_Step`'s 4.2 ms.

### 6.1 The GPU world as fast as Box3D's

The comparison first ran the GPU world with the library's solver defaults, 4 substeps × (3 position + 8 velocity
iterations). Those defaults are the sequential-impulse solver's: in XPBD the velocity pass, which applies dynamic friction
and restitution, is one pass per substep (Müller et al. 2020), and on the GPU each velocity iteration is a pass over every
contact color, the Jacobi set and its gather — 60–70 % of the GPU's solver time in the pile and the wall (8.0 and 9.8 ms a
step at size 8). Box3D makes about two passes a substep. Two changes (2026-11-14):

- **The comparison runs its GPU world with one velocity pass a substep (4/3/1)** — `--gpu-solver S/P/V` and the panel's "GPU
  solver" set it, 4/3/8 (the library's defaults) included. The library's defaults are unchanged. In the benchmark's Pareto
  front this setting made the pyramid 1.8× faster for 27 % more drift of its top box (0.110 → 0.140 m), the cloth and the
  chains no worse.
- **`GpuOptimization::REACHABLE_CONTACTS`** (on by default): a contact none of whose points can meet within the step is no
  constraint. The narrow phase keeps points within the step's speculative distance, the farthest any body gets, so one fast body
  (a wrecking ball, a bullet) made every pair of nearby bodies a constraint: more colors, a larger Jacobi set. The test is the
  pair filter's, on the points' own gaps (the two points' speeds and gravity's pull over the step; within `LINEAR_SLOP` always
  kept, so events are unchanged). Other bits, the same motion: a test of rods side by side past which a ball flies finds 4×
  fewer constraints and the same poses within 0.1 mm.

Median step in milliseconds after both, 200 steps (`awake`: `--no-sleep`):

| Scene, size | Box3D | GPU | Box3D ÷ GPU | Box3D, sleep on | GPU, sleep on | Box3D ÷ GPU |
|---|---|---|---|---|---|---|
| pile, 1 | 2.49 | 3.60 | 0.69 | 2.54 | 3.82 | 0.67 |
| pile, 4 | 10.63 | 6.75 | 1.57 | 10.55 | 7.96 | 1.32 |
| pile, 8 | 23.69 | 10.30 | 2.30 | 23.80 | 12.54 | 1.90 |
| wrecking, 1 | 1.62 | 3.42 | 0.47 | 1.63 | 3.11 | 0.52 |
| wrecking, 4 | 9.87 | 15.81 | 0.62 | 5.39 | 6.20 | 0.87 |
| wrecking, 8 | 11.60 | 9.26 | 1.25 | 11.23 | 9.63 | 1.17 |
| chains, 1 | 0.66 | 1.40 | 0.47 | 0.80 | 2.41 | 0.33 |
| chains, 4 | 1.31 | 2.28 | 0.58 | 1.36 | 2.61 | 0.52 |
| chains, 8 | 2.18 | 3.21 | 0.68 | 2.36 | 3.36 | 0.70 |
| pyramid, 1 | 3.45 | 4.14 | 0.83 | 2.13 | 0.11 | 19.4 |
| pyramid, 4 | 18.27 | 8.51 | 2.15 | 17.95 | 0.61 | 29.3 |
| pyramid, 8 | 40.61 | 12.91 | 3.15 | 39.94 | 0.88 | 45.5 |
| cloth, 1 | 7.06 | 1.87 | 3.78 | 7.46 | 1.93 | 3.87 |
| cloth, 4 | 15.61 | 2.82 | 5.53 | 12.92 | 2.88 | 4.48 |
| cloth, 8 | 21.45 | 4.59 | 4.67 | 18.38 | 4.57 | 4.02 |

At the comparison's default size (8) the GPU world now steps the pile, the wall, the pyramid and the cloth faster than
Box3D's (1.2× to 4.7×; before: the wall 0.47×, the chains 0.52×). Still slower:

- **The chains** (0.68× at size 8): a fast chain's links are all within reach of each other — the test uses the two
  points' speeds, not their approach (a body stopped by another contact meets what follows it), so `REACHABLE_CONTACTS` keeps
  them — and 7 400 contact constraints spread 7 700 joints over 8 colors and a Jacobi set of 2 300.
- **Worlds of about 2 000 bodies or fewer**: a GPU step costs at least about 1.4–3.5 ms (hundreds of dispatches each
  followed by a barrier, the submission, the wait, the readback), where Box3D steps them in 0.7–2.5 ms.
- **The wall at size 4** collapses by itself in the GPU world (as at size 8, its top falls to 13 m) and its falling bricks keep
  the step busy.

### 6.2 The chains

The chains stayed slower after §6.1: at size 8 a GPU step made 488 dispatches and 383 barriers, its 7 684 joints and 6 680
speculative contacts (8 touching) spread over 8.5 colors, the coloring ran its 24 rounds (the chains decide in 6), and the CPU
spent 0.47 ms before the GPU started (0.13–0.3 ms of it reading every body's record for the step's farthest motion) and 0.28 ms
reading the step back. Changes (2026-11-15): `RELATIVE_SPEED_COLORING` and `OVERFLOW_NEW_COLORS` (§4), the coloring's rounds
recorded by use (`RECORDED_PASSES_BY_USE`), each body's motion kept, the first part submitted once the broad phase is
recorded, the joints read back in parallel (§4). The comparison's program before (the previous commit, built beside it) and
after, alternated, 200 steps awake, median step in milliseconds (5 rounds for the chains, 3 for the others; runs where both
worlds slowed down together left out):

| Scene, size | Box3D | GPU before | GPU after | Box3D ÷ GPU before | after |
|---|---|---|---|---|---|
| chains, 1 | 0.64 | 1.38 | 1.20 | 0.47 | 0.53 |
| chains, 4 | 1.20 | 2.58 | 1.70 | 0.50 | 0.71 |
| chains, 8 | 2.09 | 3.15 | 2.24 | 0.67 | 0.88 |
| pile, 1 | 2.52 | 3.83 | 3.71 | 0.65 | 0.68 |
| pile, 8 | 23.95 | 10.33 | 9.71 | 2.31 | 2.43 |
| wrecking, 1 | 1.49 | 3.49 | 2.86 | 0.46 | 0.50 |
| wrecking, 8 | 11.14 | 9.54 | 8.54 | 1.17 | 1.30 |
| pyramid, 1 | 3.58 | 4.24 | 3.94 | 0.82 | 0.91 |
| pyramid, 8 | 40.65 | 12.93 | 12.18 | 3.16 | 3.37 |
| cloth, 1 | 8.07 | 1.87 | 1.64 | 4.25 | 4.89 |
| cloth, 8 | 20.53 | 4.52 | 4.21 | 4.68 | 4.88 |

Every scene is faster; the chains 13–34 %, but still slower than Box3D's (0.53–0.88). What a size-8 chains step is now: about
1.6 ms of GPU work (the solver 1.1, the contact cache's two sorts of 18 800 speculative manifolds 0.25, the broad phase 0.3)
and about 0.45 ms of CPU around it (the first submission, the wait's wake-up, the read-back). At size 1 the GPU's 0.9 ms are
about 180 dependent passes at 5–7 µs each: the comparison's paced frames leave the GPU at low clocks, where a pass costs more
(the headless benchmark's medians are bimodal by GPU clock for the same settings: 1.70 or 2.0 ms for `many_boxes` at a tenth
of its size).

**Found on the way: the Jacobi mode diverged on chains.** Since the fix after G10 (mass splitting weighted by shares), two
joints of equal masses on one link gave it shares of a half each, a sum of 1: the link was not split, both joints turned it by
their whole error, and chains of capsules swinging in the Jacobi mode reached 59 m/s (Intel) or 677 m/s and then NaN within
0.2 s (RTX 5060). Joints between rigid bodies now count a whole share each (equal splitting, Tonge et al. 2012); pins and
contacts keep their shares. The coloring mode solves joints in the overflow set the same way.

**The contact cache, rebuilt (2026-11-15).** The cache sorted every previous manifold slot twice a step (18 800 for the size-8
chains, 8 of them touching: 0.25 ms of GPU work), though only friction anchors that static friction held matter — and even
those little at rest: a box resting on 20–30° slopes creeps by the same micrometers in 3 s whether anchors are kept or not.
Now its keys take the instances' bits (the same bits, every world) and `PUSHED_CONTACT_CACHE` matches only the contacts that
pushed (§4). The previous commit built beside this one, alternated, median step in milliseconds:

| Scene, size | GPU before | GPU after | Box3D ÷ GPU before | after |
|---|---|---|---|---|
| chains, 1 | 1.11 | 1.10 | 0.73 | 0.74 |
| chains, 4 | 1.81 | 1.68 | 0.82 | 0.86 |
| chains, 8 | 2.32 | 2.19 | 1.31 | 1.43 |
| pile, 1 / 8 | 3.81 / 9.53 | 3.68 / 9.48 | 0.79 / 2.82 | 0.76 / 2.90 |
| wrecking, 1 / 8 | 2.93 / 8.25 | 2.85 / 7.70 | 0.60 / 1.59 | 0.58 / 1.66 |
| pyramid, 8 | 12.70 | 12.16 | 4.00 | 4.01 |
| cloth, 1 / 8 | 1.59 / 4.14 | 1.48 / 3.97 | 5.72 / 5.48 | 5.99 / 5.72 |

Box3D's own speed moved between two levels during the chains' runs (about 2.2 ms a step in some rounds, 3.1–3.2 in others),
so the ratio of each round, both worlds stepped one after the other, is the measure: at size 8 the GPU world now steps the
chains faster than Box3D's in every round (1.17–1.18 when Box3D ran at 2.2 ms, 1.43–1.48 at 3.2). The size-1 pyramid's GPU
step is bimodal whichever build runs (about 3.9 or 6.7 ms; medians 3.98 before, 3.88 after with the order swapped), not a
regression.

**Fewer detection passes (2026-11-15).** The graph's entries of no body were keyed 0xFFFFFFFF, so its sort took four radix
passes whatever the world; they are now keyed at the node count, and the sort takes the node count's bits (two passes up to
65 536 bodies, the same order). The islands ran 8 rounds of hooks and pointer jumps (16 passes) every step; each hook now
records the round that changed a label, and `RECORDED_PASSES_BY_USE` records those rounds and two more (labels still changing
in the last one replay the step with twice as many, as before: the same islands). The previous commit built beside this one,
the benchmark headless (the GPU's clocks not left to idle between frames), 3 rounds alternated, median of each run's median
step, GPU solver 4/3/1:

| Scene, scale | before | after |
|---|---|---|
| chain, 0.05 / 0.5 | 1.39 / 2.44 ms | 1.24 / 2.27 ms |
| many_boxes, 0.05 / 0.5 | 1.73 / 4.14 ms | 1.58 / 4.02 ms |
| pyramid, 0.05 / 0.5 | 3.86 / 11.50 ms | 3.83 / 11.44 ms |

### 6.3 A large scene alone at 60 frames a second

The comparison's `--single <backend>` shows one world alone in the whole window, stepped as an application steps it
(`--real-time`: 60 steps a simulated second, each frame advancing the world by its own real time), and its panel shows the
frames per second and, in real time, the world's steps per second and the share of real time it keeps. The VS Code
configuration "gfx: Large scale at 60 FPS (GPU XPBD; Box3D for contrast)" runs the pyramid with every body awake
(`--no-sleep`: asleep, the GPU world's pyramid costs under a millisecond a step and Box3D's still 40 ms, §6.1).

**Three faults of the real-time loop, found and fixed (2026-11-15).** A capture of the first version read "6 frames a
second":

1. *Presentation stalls.* The workstation was locked (LogonUI running): nothing composed the window, presentations blocked
   for 0.25–1 s now and then, or the display paced them at about 30 a second. Not the program's — but each stall left the
   world owing time.
2. *A catch-up spiral.* A frame ran up to two steps on the spot whenever time was owed, whatever a step cost. A pyramid of
   20 540 boxes stepping in 15.7 ms of its 16.7 fell behind once and then ran three steps every frame for good: 21 frames
   a second. Now a frame catches up only the steps that fit in its own time (one step's period at least) at the last
   step's cost (`catch_up_steps()` in scene.cpp); what does not fit is dropped and the world runs slower than real time.
3. *Frames waited for steps.* Each frame waited for the step begun the frame before, so a saturated world's frames followed
   its steps. Frames now wait for nothing: while a step runs they draw the last finished one (its interpolation held at
   the end), and the next step begins on the first frame after it finishes (`PhysicsWorld::is_step_finished()`, new: whether
   `wait_step()` would wait). The sandbox, whose picking needs the world between steps, still waits each frame.
4. *Frames drove the steps.* A step began only when a frame called `advance()`: frames held back by their presentation held
   the world back (6 % of real time with the screen locked, presentations blocked for half a second at a time), and a shot
   waited for the step in flight. In real time the comparison's worlds now step on threads of their own
   (`Scene::start_stepping()`: each step begun when due once the last has finished, the time it cannot step dropped; frames
   take the scene's lock only to draw and to shoot, interpolating by the time since the last step), and a shot is recorded for
   the next step without waiting. With the screen locked (frames at 1.9 a second), the GPU world kept 100 % of real time
   (9 196 steps in 153 s, 10.2 ms each) with a shot every 20 frames.

Medians of 900 frames each with the display unthrottled (240 Hz), the RTX 5060 (GPU solver 4/3/1), Box3D on 16 scheduler
threads, after the three fixes:

| Pyramid size | Boxes | GPU step | GPU world | Box3D step | Box3D world | Frames, either |
|---|---|---|---|---|---|---|
| 6 (the configuration's default) | 12 529 | 11.6 ms | 98 % of real time (59 steps a second) | 56 ms | 27 % (16 steps a second) | 221–233 a second |
| 8 | 16 206 | 14.0 ms | 88 % | 80 ms | 19 % | 209–227 a second |
| 10 | 20 540 | 17.4 ms | 75 % | — | — | 213 a second |
| 12 | 25 585 | 21.6 ms | 61 % | — | — | 197 a second |

The window drawing at 240 frames a second costs both worlds: their steps run beside it (the GPU's 10.2 → 11.6 ms at size 6;
Box3D's 29 → 56 ms, its 16 threads sharing the laptop's power with the GPU). With the display paced at about 30 frames a second
(locked), the size-6 panel read 95 % of real time on the GPU between the half-second stalls (57 steps a second, two steps
fitting some frames; 76 % over the whole run, stalls included, Box3D 33 %); with a catch-up budget of one step's period, not
the frame's time, it had kept 47 %.

A size-8 GPU step: about 9.2 ms of GPU work (the solver 5.2, the narrow phase 1.4, the contact reports 1.2, copying them back
1.0, the broad phase 0.4) and 2.7 ms of CPU after it sorting and converting its 69 000 touching contacts into the interface's
records, which nothing reads here.

**Contacts not reported (2026-11-15).** `PhysicsWorldDescription::report_contacts` (true by default): false, a step gathers no
contact and makes no contact event, `contacts_of()` answers UNSUPPORTED, and the simulation is the same (the conformance suite
compares both on every backend). The GPU backend keeps the flag pass its contact cache needs and skips the reports, their copy
and their conversion; Box3D's shapes make no contact events and the wrapper captures nothing; the reference backend keeps its
touching pairs (they wake what rested on a destroyed collider) and builds no list. Without touching pairs the GPU backend wakes
the dynamic bodies within reach of a destroyed collider's last box, which the framework now hands to the backends with the
destroy edit. The comparison's `--contacts off`; medians, RTX 5060 and 16 Box3D threads, the world on its own thread:

| Pyramid size | GPU, contacts on | GPU, off | Box3D, on | Box3D, off |
|---|---|---|---|---|
| 6 (12 529 boxes) | 9.80 ms (100 %) | 6.74 ms (99 %) | 28.2 ms (56 %) | 9.25 ms (99 %) |
| 8 (16 206 boxes) | 12.42 ms (99 %) | 8.53 ms (99 %) | about 44 ms (38 %) | — |
| 10 (20 540 boxes) | — | 10.7 ms (100 %) | — | 14.8 ms (97 %) |
| 12 (25 585 boxes) | — | 13.0 ms (99 %) | — | 20.5 ms (73 %) |
| 14 (29 370 boxes) | — | 15.8 ms (93 %) | — | 25.4 ms (57 %) |

Two thirds of Box3D's step at size 6 were the wrapper converting Box3D's 285 000 contact points (§7, follow-up 3): without
reports, the GPU world steps 1.4–1.6× faster than Box3D's from size 6, and Box3D falls behind real time from size 12. The
launch configuration asks for contacts on (what an application reading them pays) or off.

**A limit found on the way**: kept awake (`--no-sleep`), the GPU world's size-8 pyramid holds for about 45 s, then its top
sags (17.7 m high at 45 s, 15.7 m at 59 s, 12.7 m at two minutes) whatever the solver settings tried (4/3/1, 4/3/2, 4/3/8,
8/2/1: 12.7–14.5 m), where Box3D's stands; asleep, it never moves. Tall stacks are XPBD's known weakness at these iteration
counts (§7, follow-up 6).

## 7. Follow-ups

1. **Small and mid-size worlds on the GPU**: fewer passes per step. The velocity iterations' count is the first lever (§5;
   the comparison uses 4/3/1, §6.1 — the library's defaults could follow). Running a small world's substeps in one workgroup
   was measured and lost 1.6–6.5× (§5): the passes are bound by their work, spread over every multiprocessor, more than by
   their dispatches. What keeps the chains slower than Box3D at sizes 1 and 4 (§6.2) is the detection's passes and the CPU
   around the GPU's work.
2. **Contacts reported on demand**: `report_contacts` (§6.3) turns reporting off for a whole world; reporting only the
   colliders that ask for contact events, and the rest when `contacts_of()` is first called after a step, would serve worlds
   that read some contacts.
3. **The Box3D wrapper's contact capture** (`box3d_world.cpp`): the GPU backend's sorted, parallel conversion applied to
   Box3D's touching pairs.
4. **Debug builds with the validation layers**: reusing recorded substeps (the timestamps written by a replay need slots of
   their own).
5. **Queries on the GPU**: one submission and its wait per query (0.14 ms on the RTX 5060, 0.33–0.50 ms on the Intel GPU):
   a batch query API.
6. **Tall stacks kept awake on the GPU** sag after tens of seconds (§6.3): stacking stability (shock propagation, a rest
   stabilization of resting contacts, or more position iterations where stacks are deep).

## 8. Reproducing

```bash
python build.py --profile
build/profile/benchmarks/physics/gfx_physics_benchmark.exe --output build/profile/benchmark
build/profile/benchmarks/physics/gfx_physics_benchmark.exe --sleep --output build/profile/benchmark_sleep
build/profile/examples/physics_sandbox/gfx_physics_comparison.exe --scene pile --size 4 --no-sleep
build/profile/examples/physics_sandbox/gfx_physics_comparison.exe --single gpu_xpbd --scene pyramid --size 6 --no-sleep --real-time
build/profile/examples/physics_sandbox/gfx_physics_comparison.exe --single box3d --scene pyramid --size 12 --no-sleep --real-time --contacts off
```

The benchmark takes about 9 minutes per pass (7 scenes × 2 backends × 1 100 steps). `--record-baseline <directory>` and
`--check-baseline <directory>` keep a baseline per machine and fail on a median 5 % slower (measured again before failing);
`benchmarks/physics/pareto.py` runs the solver-settings grid. CTest runs a smoke run (`PhysicsBenchmark.Smoke`) and the whole
benchmark only with `GFX_RUN_BENCHMARKS=1` in the environment (`PhysicsBenchmark.Suite`).
