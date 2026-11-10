"""The substep/iteration Pareto front of the physics backends (docs/profiler-performance-design.md, A10 item 14).

Runs gfx_physics_benchmark over a grid of solver settings (substeps × position iterations × velocity iterations) on the
scenes whose quality an iterative solver trades for time — a stack's sinking (pyramid), a chain's stretch (chain), a
cloth's stretch (cloth) — checking each run every few steps and keeping its worst error, then writes pareto.md: per scene
and backend, every setting's mean step time and worst error, the settings on the Pareto front (no other setting both
faster and more accurate) marked, and the defaults' place among them.

    python benchmarks/physics/pareto.py --benchmark build/profile/benchmarks/physics/gfx_physics_benchmark.exe
        [--output pareto] [--scenes pyramid,chain,cloth] [--backends gpu_xpbd] [--sub-steps 2,4,8]
        [--position-iterations 1,2,3] [--velocity-iterations 1,2,4,8] [--warm-up 100] [--steps 200] [--scale 1]
        [--check-every 10]

Only the Python standard library; each run is a process of its own (its report and output kept under --output).
"""
import argparse
import dataclasses
import itertools
import pathlib
import re
import subprocess
import sys

DEFAULTS = (4, 3, 8)  # SolverDescription's sub_steps, position_iterations, velocity_iterations

# A report line: "<scene> <backend> mean <ms> ms ... passed (<measure>)" or "... **failed** ...: <problem>".
LINE = re.compile(r"^(?P<scene>\S+)\s+(?P<backend>\S+)\s+mean\s+(?P<mean>[0-9.]+) ms.*?(?P<status>passed|\*\*failed\*\*)(?P<rest>.*)$")


@dataclasses.dataclass
class Run:
    scene: str
    backend: str
    setting: tuple
    mean: float
    error: float | None
    measure: str
    passed: bool


def error_of(measure: str) -> float | None:
    """The measure's number as an error: a cloth's stretch, a rod's error (fractions), a stack's sinking (m)."""
    for pattern, scale in ((r"stretch ([0-9.]+) %", 0.01), (r"max rod error ([0-9.]+) %", 0.01), (r"top box ([+-]?[0-9.]+) m", 1.0),
                           (r"within ([0-9.]+) m", 1.0)):
        match = re.search(pattern, measure)
        if match:
            return abs(float(match.group(1))) * scale
    return None


def unit_of(scene: str) -> str:
    return "m" if scene in ("pyramid", "many_boxes", "two_worlds") else "%"


def shown(error: float | None, scene: str) -> str:
    if error is None:
        return "—"
    return f"{error:.3f} m" if unit_of(scene) == "m" else f"{100.0 * error:.2f} %"


def run_grid(arguments) -> list[Run]:
    runs = []
    grid = list(itertools.product(arguments.sub_steps, arguments.position_iterations, arguments.velocity_iterations))
    for number, (sub_steps, position, velocity) in enumerate(grid, 1):
        output = arguments.output / f"s{sub_steps}_p{position}_v{velocity}"
        command = [str(arguments.benchmark), "--scenes", ",".join(arguments.scenes), "--backends", ",".join(arguments.backends),
                   "--warm-up", str(arguments.warm_up), "--steps", str(arguments.steps), "--scale", str(arguments.scale),
                   "--trace-frames", "0", "--check-every", str(arguments.check_every), "--sub-steps", str(sub_steps),
                   "--position-iterations", str(position), "--velocity-iterations", str(velocity), "--output", str(output)]
        print(f"[{number}/{len(grid)}] substeps {sub_steps}, position iterations {position}, velocity iterations {velocity}", flush=True)
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
        found = 0
        for line in completed.stdout.splitlines():
            match = LINE.match(line.strip())
            if not match:
                continue
            rest = match.group("rest")
            measure = rest[rest.find("(") + 1:rest.rfind(")")] if "(" in rest and match.group("status") == "passed" else rest.strip(" :")
            passed = match.group("status") == "passed"
            runs.append(Run(match.group("scene"), match.group("backend"), (sub_steps, position, velocity), float(match.group("mean")),
                            error_of(measure) if passed else None, measure, passed))
            found += 1
        if found == 0:
            print(completed.stdout[-2000:], completed.stderr[-2000:], sep="\n", file=sys.stderr)
            raise SystemExit(f"the benchmark reported nothing for substeps {sub_steps}, position iterations {position}, velocity iterations {velocity}")
    return runs


def front(runs: list[Run]) -> set[tuple]:
    """The settings no other passing setting beats on both time and error."""
    passing = [run for run in runs if run.passed and run.error is not None]
    kept = set()
    for run in passing:
        if not any(other is not run and other.mean <= run.mean and other.error <= run.error and (other.mean < run.mean or other.error < run.error)
                   for other in passing):
            kept.add(run.setting)
    return kept


def write_report(arguments, runs: list[Run]) -> pathlib.Path:
    lines = ["# Substep/iteration Pareto front (generated by benchmarks/physics/pareto.py)", "",
             f"Scenes {', '.join(arguments.scenes)} on {', '.join(arguments.backends)}; {arguments.steps} measured steps of 1/60 s after "
             f"{arguments.warm_up} warm-up steps at scale {arguments.scale}; each run checked every {arguments.check_every} steps, its worst "
             f"error kept (a pyramid's top box: how far it moved; a chain's rods, a cloth's stretch constraints: the largest relative "
             f"stretch). Settings are substeps / position iterations / velocity iterations; the defaults are "
             f"{'/'.join(map(str, DEFAULTS))}. **Front**: no other setting is both faster and more accurate.", ""]
    for scene, backend in sorted({(run.scene, run.backend) for run in runs}):
        group = sorted((run for run in runs if run.scene == scene and run.backend == backend), key=lambda run: run.mean)
        kept = front(group)
        default = next((run for run in group if run.setting == DEFAULTS), None)
        lines += [f"## {scene} on {backend}", ""]
        if default is not None:
            lines += [f"Defaults ({'/'.join(map(str, DEFAULTS))}): {default.mean:.2f} ms, error {shown(default.error, scene)}"
                      f"{' (on the front)' if DEFAULTS in kept else ''}.", ""]
            better = [run for run in group if run.passed and run.error is not None and default.error is not None and run.error <= default.error
                      and run.mean < default.mean]
            if better:
                fastest = min(better, key=lambda run: run.mean)
                lines += [f"Fastest setting at least as accurate as the defaults: {'/'.join(map(str, fastest.setting))} — "
                          f"{fastest.mean:.2f} ms ({default.mean / fastest.mean:.2f}× faster), error {shown(fastest.error, scene)}.", ""]
        lines += ["| Setting | Mean step (ms) | Worst error | Front | Check |", "|---|---:|---:|:---:|---|"]
        for run in group:
            mark = "●" if run.setting in kept else ""
            check = "passed" if run.passed else f"failed: {run.measure}"
            lines.append(f"| {'/'.join(map(str, run.setting))}{' (defaults)' if run.setting == DEFAULTS else ''} | {run.mean:.2f} | "
                         f"{shown(run.error, scene)} | {mark} | {check} |")
        lines.append("")
    path = arguments.output / "pareto.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def numbers(text: str) -> list[int]:
    return [int(item) for item in text.split(",") if item]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--benchmark", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("pareto"))
    parser.add_argument("--scenes", type=lambda text: text.split(","), default=["pyramid", "chain", "cloth"])
    parser.add_argument("--backends", type=lambda text: text.split(","), default=["gpu_xpbd"])
    parser.add_argument("--sub-steps", type=numbers, default=[2, 4, 8])
    parser.add_argument("--position-iterations", type=numbers, default=[1, 2, 3])
    parser.add_argument("--velocity-iterations", type=numbers, default=[1, 2, 4, 8])
    parser.add_argument("--warm-up", type=int, default=100)
    parser.add_argument("--steps", type=int, default=200)
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--check-every", type=int, default=10)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    runs = run_grid(arguments)
    print(f"{write_report(arguments, runs)} written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
