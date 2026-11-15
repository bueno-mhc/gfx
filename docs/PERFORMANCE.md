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
| `RECORDED_PASSES_BY_USE` | PF5 | solver passes recorded for the colors each kind of constraint used in the previous step; overflow passes only after a step that had an overflow set; a replay otherwise | 308–703 fewer dispatches and barriers a step | the same |
| `REACH_BOXES` | PF5 | the broad phase's boxes grown by each collider's own reach, not the fastest body's | `chain`'s 832 000 tree pairs a step gone; its broad phase 1.65 → 0.58 ms | the same pairs, another order |
| `COMPACT_READBACK` | PF5b | the GPU lists and gathers the bodies that may have changed; only in worlds of 4 096 bodies or more of which at most half changed | the readback of resting worlds (with sleep) | the same |
| `RESTING_STEPS` | PF5b | a world at rest (no awake body, no edit, no particle or trigger) steps without the GPU | with sleep: 50 000 boxes 12.4 → 0.9 ms, the pyramid 14.0 → 0.9 ms | the same for the same steps |
| `BVH_REFIT` | PF5b | the moving tree refit for 30 steps while the moving colliders stay the same, then built again | the tree's build in the scenes with motion | the same pairs, another order |
| `KERNEL_FUSION` | PF5b | bodies' and particles' velocity updates in one dispatch | a dispatch and a barrier per substep with particles | the same |
| `PARTIAL_SUBMISSIONS` | PF5b | the step submitted in parts of at least 64 dispatches as it is recorded | recording hidden behind the GPU's work; small worlds 5–24 % faster | the same |
| `REACHABLE_CONTACTS` | after PF6 | a contact none of whose points can meet within the step is no constraint (the pair filter's reach test on the points' gaps) | rods beside a fast ball: 4× fewer constraints; the comparison's wall at size 8 with 4/3/1: 11.5 → 9.3 ms | other bits, the same motion |

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
  found by comparing with the previous commit, and fixed).
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

## 7. Follow-ups

1. **Small and mid-size worlds on the GPU**: fewer passes per step. The velocity iterations' count is the first lever (§5;
   the comparison uses 4/3/1, §6.1 — the library's defaults could follow). A single-dispatch solve of small colors or islands
   within one workgroup (no inter-workgroup wait, so no deadlock) is the second. Chains of fast links need fewer speculative
   contacts than the speeds-sum test keeps, without losing a link stopped by another.
2. **The Box3D wrapper's contact capture** (`box3d_world.cpp`): the GPU backend's sorted, parallel conversion applied to
   Box3D's touching pairs.
3. **Debug builds with the validation layers**: reusing recorded substeps (the timestamps written by a replay need slots of
   their own).
4. **Queries on the GPU**: one submission and its wait per query (0.14 ms on the RTX 5060, 0.33–0.50 ms on the Intel GPU):
   a batch query API.

## 8. Reproducing

```bash
python build.py --profile
build/profile/benchmarks/physics/gfx_physics_benchmark.exe --output build/profile/benchmark
build/profile/benchmarks/physics/gfx_physics_benchmark.exe --sleep --output build/profile/benchmark_sleep
build/profile/examples/physics_sandbox/gfx_physics_comparison.exe --scene pile --size 4 --no-sleep
```

The benchmark takes about 9 minutes per pass (7 scenes × 2 backends × 1 100 steps). `--record-baseline <directory>` and
`--check-baseline <directory>` keep a baseline per machine and fail on a median 5 % slower (measured again before failing);
`benchmarks/physics/pareto.py` runs the solver-settings grid. CTest runs a smoke run (`PhysicsBenchmark.Smoke`) and the whole
benchmark only with `GFX_RUN_BENCHMARKS=1` in the environment (`PhysicsBenchmark.Suite`).
