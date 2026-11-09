// gfx_physics_benchmark: the physics benchmark (docs/profiler-performance-design.md, PF3). Each scene of
// physics_benchmark.scenes runs on each backend asked for — Box3D (on a ThreadTaskScheduler), the GPU backend (on a device
// the benchmark makes, shared by the worlds of a scene), the reference backend — through PhysicsWorld::step(): a warm-up,
// then measured steps recorded by the profiler. Per scene and backend: step() wall clock (mean, p50, p95, p99), the GPU's
// busy time, the CPU's work on every thread, the CPU/GPU overlap and the GPU's idle share, Box3D's own step time, the
// scene's quality check; a Chrome trace of the last steps; benchmark_report.md (generated, never written by hand) with the
// speed-up against Box3D and the acceptance target (at least as fast, 1.25 times faster from 10 000 bodies or 100 000
// constraints); against a baseline recorded on the same machine with the same settings, a regression of the median above
// 5 % fails the run, naming the scene, the backend and the change — a regression that persists: a median above it is
// measured again (--confirmations times, 2 by default) and the best run judged, as this laptop's medians move a few percent
// run to run (up to 11 % once for two worlds on one GPU). Only gfx's own backends are judged (the GPU backend and
// the reference backend): Box3D's failures and changes are reported, not failed on — its medians move by up to 10 % run to
// run on a laptop (its scheduler's threads), and its results are not this library's to fix.
//
// gfx_physics_benchmark [--scenes many_boxes,pyramid,...] [--backends box3d,gpu_xpbd,reference] [--warm-up 100]
//                       [--steps 1000] [--threads 16] [--scale 1] [--gpu-device <index>] [--trace-frames 100]
//                       [--output <directory>] [--check-baseline <directory>] [--record-baseline <directory>]
//                       [--confirmations 2] [--sleep] [--note <text>] [--only-when-asked]
//
// Exit codes: 0 every check passed; 1 a quality check failed, a run failed or a scene regressed; 2 bad arguments; 77 not
// asked (--only-when-asked without GFX_RUN_BENCHMARKS=1 in the environment: CTest counts it as skipped).
#include "environment.hpp"

import std;
#if GFX_BENCHMARK_GPU_XPBD
import gfx;
import gfx.physics.gpu_xpbd;
#endif
import gfx.profiler;
import gfx.physics;
import physics_benchmark.scenes;

namespace
{

namespace physics = gfx::physics;
namespace profiler = gfx::profiler;
using gfx::benchmark::BenchmarkScene;
using gfx::benchmark::SceneObjects;

/// The step every scene takes.
constexpr float STEP_SECONDS = 1.0F / 60.0F;

/// A median that much above the baseline's is a regression.
constexpr double REGRESSION_TOLERANCE = 0.05;

/// The speed-up asked of the GPU backend on large scenes (from 10 000 bodies or 100 000 constraints).
constexpr double SPEED_UP_TARGET = 1.25;

/// What a scene counts as large.
constexpr std::uint32_t LARGE_BODIES = 10'000;
constexpr std::uint32_t LARGE_CONSTRAINTS = 100'000;

/// Exit codes.
constexpr int PASSED = 0;
constexpr int FAILED = 1;
constexpr int BAD_ARGUMENTS = 2;
constexpr int SKIPPED = 77;

// ===================================================================================================== options

/// The backends the benchmark runs.
enum class Backend : std::uint8_t
{
    BOX3D,
    GPU_XPBD,
    REFERENCE,
};

/// Every backend.
constexpr std::array ALL_BACKENDS{Backend::BOX3D, Backend::GPU_XPBD, Backend::REFERENCE};

[[nodiscard]] constexpr auto
backend_name(Backend backend) noexcept -> std::string_view
{
    switch (backend)
    {
        case Backend::BOX3D:
            return "box3d";
        case Backend::GPU_XPBD:
            return "gpu_xpbd";
        case Backend::REFERENCE:
            return "reference";
    }
    return "?";
}

/// Whether @p backend's failures and regressions fail the run (gfx's own backends; Box3D is the reference point).
[[nodiscard]] constexpr auto
judged(Backend backend) noexcept -> bool
{
    return backend != Backend::BOX3D;
}

struct Options
{
    std::vector<BenchmarkScene> scenes{};
    std::vector<Backend> backends{};
    std::uint32_t warm_up = 100;
    std::uint32_t steps = 1000;
    std::uint32_t threads = 16;
    double scale = 1.0;
    std::optional<std::size_t> gpu_device{};
    std::uint32_t trace_frames = 100;
    std::filesystem::path output{"benchmark"};
    std::optional<std::filesystem::path> check_baseline{};
    std::optional<std::filesystem::path> record_baseline{};
    std::uint32_t confirmations = 2;
    bool sleep = false;
    std::string note{};
    bool only_when_asked = false;
};

/// The comma-separated items of @p text.
[[nodiscard]] auto
split(std::string_view text) -> std::vector<std::string_view>
{
    std::vector<std::string_view> items;
    for (const auto part : std::views::split(text, ','))
    {
        items.emplace_back(part.begin(), part.end());
    }
    return items;
}

/// The options, or the problem with the arguments.
[[nodiscard]] auto
parse_options(std::span<char*> arguments) -> std::expected<Options, std::string>
{
    Options options{};
    std::vector<std::string_view> scene_names;
    std::vector<std::string_view> backend_names{"box3d", "gpu_xpbd"};
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        const auto value = [&]() -> std::optional<std::string_view> {
            return index + 1 < arguments.size() ? std::optional<std::string_view>{arguments[++index]} : std::nullopt;
        };
        const auto number = [&](auto& target) -> bool {
            const std::optional<std::string_view> text = value();
            if (!text)
            {
                return false;
            }
            const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), target);
            return error == std::errc{} && end == text->data() + text->size();
        };
        bool valid = true;
        if (argument == "--scenes")
        {
            const std::optional<std::string_view> text = value();
            valid = text.has_value();
            if (valid)
            {
                scene_names = split(*text);
            }
        }
        else if (argument == "--backends")
        {
            const std::optional<std::string_view> text = value();
            valid = text.has_value();
            if (valid)
            {
                backend_names = split(*text);
            }
        }
        else if (argument == "--warm-up")
        {
            valid = number(options.warm_up);
        }
        else if (argument == "--steps")
        {
            valid = number(options.steps) && options.steps > 0;
        }
        else if (argument == "--threads")
        {
            valid = number(options.threads) && options.threads > 0;
        }
        else if (argument == "--scale")
        {
            valid = number(options.scale) && options.scale > 0.0;
        }
        else if (argument == "--gpu-device")
        {
            std::size_t device = 0;
            valid = number(device);
            options.gpu_device = device;
        }
        else if (argument == "--confirmations")
        {
            valid = number(options.confirmations);
        }
        else if (argument == "--trace-frames")
        {
            valid = number(options.trace_frames);
        }
        else if (argument == "--output" || argument == "--check-baseline" || argument == "--record-baseline" || argument == "--note")
        {
            const std::optional<std::string_view> text = value();
            valid = text.has_value();
            if (valid)
            {
                if (argument == "--output")
                {
                    options.output = std::filesystem::path{*text};
                }
                else if (argument == "--check-baseline")
                {
                    options.check_baseline = std::filesystem::path{*text};
                }
                else if (argument == "--record-baseline")
                {
                    options.record_baseline = std::filesystem::path{*text};
                }
                else
                {
                    options.note = std::string{*text};
                }
            }
        }
        else if (argument == "--sleep")
        {
            options.sleep = true;
        }
        else if (argument == "--only-when-asked")
        {
            options.only_when_asked = true;
        }
        else
        {
            return std::unexpected(std::format("unknown argument '{}'", argument));
        }
        if (!valid)
        {
            return std::unexpected(std::format("'{}' needs a valid value", argument));
        }
    }

    // The scenes (every one by default) and the backends, by name.
    if (scene_names.empty())
    {
        for (const gfx::benchmark::SceneInformation& scene : gfx::benchmark::SCENES)
        {
            options.scenes.push_back(scene.scene);
        }
    }
    for (const std::string_view name : scene_names)
    {
        const std::optional<BenchmarkScene> scene = gfx::benchmark::find_scene(name);
        if (!scene)
        {
            return std::unexpected(std::format("unknown scene '{}'", name));
        }
        options.scenes.push_back(*scene);
    }
    for (const std::string_view name : backend_names)
    {
        const auto found = std::ranges::find_if(ALL_BACKENDS, [name](Backend backend) { return backend_name(backend) == name; });
        if (found == ALL_BACKENDS.end())
        {
            return std::unexpected(std::format("unknown backend '{}' (box3d, gpu_xpbd, reference)", name));
        }
        options.backends.push_back(*found);
    }
    return options;
}

// ===================================================================================================== environment

/// What the numbers were measured on.
struct Environment
{
    std::string processor{};
    std::string operating_system{};
    std::string power_scheme{};
    std::string gpu{"no GPU backend"};
    std::string gpu_driver{};
    std::string date{};
    std::string build{};

    /// The machine, as baselines name it.
    [[nodiscard]] auto fingerprint() const -> std::string
    {
        return std::format("{} | {} {} | {}", processor, gpu, gpu_driver, operating_system);
    }
};

[[nodiscard]] auto
read_text(unsigned long (*reader)(char*, unsigned long) noexcept) -> std::string
{
    std::array<char, 256> text{};
    const unsigned long length = reader(text.data(), static_cast<unsigned long>(text.size()));
    return std::string{text.data(), length};
}

/// A GPU device for the GPU backend's worlds: an instance without validation layers, a device on the chosen physical
/// device (or the one the graphics library selects) with a compute queue and 64-bit atomics when it has them.
struct GpuDevice
{
#if GFX_BENCHMARK_GPU_XPBD
    std::unique_ptr<gfx::Instance> instance{};
    gfx::PhysicalDevice* physical_device = nullptr;
    std::unique_ptr<gfx::Device> device{};
#endif
};

#if GFX_BENCHMARK_GPU_XPBD
[[nodiscard]] auto
choose_gpu(GpuDevice& gpu,
           std::optional<std::size_t> index) -> std::string
{
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return "Vulkan is unavailable";
    }
    gpu.instance = gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx physics benchmark"});
    if (index)
    {
        if (*index >= gpu.instance->physical_device_count())
        {
            return std::format("there is no physical device {}", *index);
        }
        gpu.physical_device = &gpu.instance->physical_device(*index);
    }
    else
    {
        gpu.physical_device = &gpu.instance->select_physical_device();
    }
    return {};
}

void
make_gpu_device(GpuDevice& gpu)
{
    if (gpu.device != nullptr)
    {
        return;
    }
    static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::COMPUTE, .count = 1, .priority = 1.0F}};
    std::vector<gfx::DeviceFeature> features;
    if (gpu.physical_device->supports(gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS))
    {
        features.push_back(gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS);
    }
    gpu.device = gpu.physical_device->create_device({.queues = QUEUES, .required_features = features, .debug_name = "gfx physics benchmark"});
}
#endif

// ===================================================================================================== measurement

/// What one scene on one backend measured (milliseconds).
struct Measurement
{
    BenchmarkScene scene = BenchmarkScene::MANY_BOXES;
    Backend backend = Backend::BOX3D;
    std::uint32_t bodies = 0;
    std::uint32_t joints = 0;
    std::uint32_t particles = 0;
    double build = 0.0;
    double first_step = 0.0;
    profiler::DurationStatistics step{};
    double gpu_busy = 0.0;
    double cpu_work = 0.0;
    double overlap = 0.0;
    double gpu_idle = 0.0;
    std::optional<double> box3d_native{};
    std::string problem{};
    std::string measure{};
    std::string error{};

    [[nodiscard]] auto large() const noexcept -> bool
    {
        return bodies + particles >= LARGE_BODIES || joints >= LARGE_CONSTRAINTS;
    }
};

[[nodiscard]] auto
milliseconds_since(std::chrono::steady_clock::time_point start) -> double
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

/// The world of @p backend.
[[nodiscard]] auto
make_world(Backend backend,
           const physics::PhysicsWorldDescription& description,
           [[maybe_unused]] GpuDevice& gpu) -> std::unique_ptr<physics::PhysicsWorld>
{
    switch (backend)
    {
        case Backend::BOX3D:
            return physics::create_physics_world(description, physics::PhysicsBackendKind::BOX3D);
        case Backend::REFERENCE:
            return physics::create_physics_world(description, physics::PhysicsBackendKind::REFERENCE);
        case Backend::GPU_XPBD:
#if GFX_BENCHMARK_GPU_XPBD
            make_gpu_device(gpu);
            return physics::gpu_xpbd::create_physics_world(description, *gpu.device);
#else
            break;
#endif
    }
    throw std::runtime_error{"the GPU backend is not built (GFX_PHYSICS_BACKEND_GPU_XPBD)"};
}

/// Runs @p scene on @p backend.
[[nodiscard]] auto
measure(BenchmarkScene scene,
        Backend backend,
        const Options& options,
        physics::TaskScheduler& scheduler,
        GpuDevice& gpu) -> Measurement
{
    Measurement measurement{.scene = scene, .backend = backend};
    const std::string name = std::format("{} on {}", gfx::benchmark::information(scene).name, backend_name(backend));
    try
    {
        // The worlds (two for TWO_WORLDS) and their scene; the first step (it applies the creations) timed apart.
        const auto building = std::chrono::steady_clock::now();
        const std::size_t world_count = scene == BenchmarkScene::TWO_WORLDS ? 2 : 1;
        std::vector<std::unique_ptr<physics::PhysicsWorld>> worlds;
        std::vector<SceneObjects> objects;
        for (std::size_t world = 0; world < world_count; ++world)
        {
            worlds.push_back(make_world(backend, {.task_scheduler = &scheduler, .sleep = {.enabled = options.sleep}, .debug_name = name}, gpu));
            objects.push_back(gfx::benchmark::build_scene(scene, *worlds.back(), options.scale));
            measurement.bodies += objects.back().dynamic_bodies;
            measurement.joints += static_cast<std::uint32_t>(objects.back().joints.size());
            measurement.particles += objects.back().particles;
        }
        measurement.build = milliseconds_since(building);
        const auto step_all = [&worlds] {
            for (const std::unique_ptr<physics::PhysicsWorld>& world : worlds)
            {
                world->step(STEP_SECONDS);
            }
        };
        const auto first = std::chrono::steady_clock::now();
        step_all();
        measurement.first_step = milliseconds_since(first);
        for (std::uint32_t step = 1; step < options.warm_up; ++step)
        {
            step_all();
        }

        // The measured steps, a profiler frame each.
        const std::unique_ptr<profiler::Profiler> recorder = profiler::create_profiler({.ring_capacity = 1U << 20U});
        profiler::set_current_profiler(recorder.get());
        profiler::set_thread_name("benchmark");
        const profiler::SessionHandle session = recorder->create_session({.name = name});
        std::vector<double> step_times;
        step_times.reserve(options.steps);
        for (std::uint32_t step = 0; step < options.steps; ++step)
        {
            recorder->begin_frame(session);
            const auto begin = std::chrono::steady_clock::now();
            step_all();
            step_times.push_back(milliseconds_since(begin));
            (void)recorder->end_frame(session);
        }
        const profiler::Trace trace = recorder->finalize_session(session);
        profiler::set_current_profiler(nullptr);

        // Wall clock; GPU busy, overlap and idle; the CPU's work on every thread (outermost scopes summed).
        measurement.step = profiler::statistics_of(step_times);
        const profiler::Report report = profiler::make_report(trace);
        measurement.gpu_busy = report.gpu_time.mean * 1.0e-6;
        measurement.overlap = report.overlap_ratio;
        measurement.gpu_idle = report.idle_ratio;
        double work = 0.0;
        for (const profiler::TraceFrame& frame : trace.frames())
        {
            for (const profiler::TraceScope& scope : frame.scopes)
            {
                work += scope.depth == 0 ? static_cast<double>(scope.end - scope.begin) : 0.0;
            }
        }
        measurement.cpu_work = trace.frames().empty() ? 0.0 : work * 1.0e-6 / static_cast<double>(trace.frames().size());
        const auto native = std::ranges::find(report.counters, std::string_view{"Box3D: step (ms)"}, &profiler::CounterReport::name);
        if (native != report.counters.end())
        {
            measurement.box3d_native = native->mean * static_cast<double>(world_count);
        }

        // The last frames as a Chrome trace.
        if (options.trace_frames > 0 && !trace.frames().empty())
        {
            const std::span<const profiler::TraceFrame> frames = trace.frames();
            const std::size_t kept = std::min<std::size_t>(options.trace_frames, frames.size());
            const profiler::Trace last{std::string{trace.name()},
                                       {trace.sites().begin(), trace.sites().end()},
                                       {trace.threads().begin(), trace.threads().end()},
                                       {frames.end() - static_cast<std::ptrdiff_t>(kept), frames.end()},
                                       {trace.gpu_tracks().begin(), trace.gpu_tracks().end()}};
            profiler::export_trace(last, profiler::TraceFormat::CHROME_JSON,
                                   options.output / std::format("{}_{}.json", gfx::benchmark::information(scene).name, backend_name(backend)));
        }

        // The quality checks.
        for (const SceneObjects& scene_objects : objects)
        {
            const gfx::benchmark::SceneCheck check = gfx::benchmark::check_scene(scene_objects);
            if (measurement.problem.empty())
            {
                measurement.problem = check.problem;
            }
            if (measurement.measure.empty())
            {
                measurement.measure = check.measure;
            }
        }
    }
    catch (const std::exception& error)
    {
        profiler::set_current_profiler(nullptr);
        measurement.error = error.what();
    }
    return measurement;
}

// ===================================================================================================== baselines

/// A baseline's row.
struct BaselineRow
{
    std::string scene{};
    std::string backend{};
    double p50 = 0.0;
    double mean = 0.0;
};

struct Baseline
{
    std::string machine{};
    std::string settings{};
    std::string recorded{};
    std::vector<BaselineRow> rows{};
};

/// FNV-1a 64 of @p text, in hexadecimal (a baseline's file name).
[[nodiscard]] auto
fingerprint_hash(std::string_view text) -> std::string
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char character : text)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 0x100000001b3ULL;
    }
    return std::format("{:016x}", hash);
}

[[nodiscard]] auto
baseline_path(const std::filesystem::path& directory,
              const Environment& environment) -> std::filesystem::path
{
    return directory / std::format("baseline-{}.tsv", fingerprint_hash(environment.fingerprint()));
}

/// The settings a baseline holds for (a baseline compares only with runs made with the same).
[[nodiscard]] auto
settings_text(const Options& options,
              const Environment& environment) -> std::string
{
    return std::format("{}; warm-up {}, steps {}, threads {}, scale {}, sleep {}", environment.build, options.warm_up, options.steps, options.threads,
                       options.scale, options.sleep ? "on" : "off");
}

[[nodiscard]] auto
read_baseline(const std::filesystem::path& path) -> std::optional<Baseline>
{
    std::ifstream stream{path};
    if (!stream)
    {
        return std::nullopt;
    }
    Baseline baseline{};
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::vector<std::string> fields;
        for (const auto part : std::views::split(line, '\t'))
        {
            fields.emplace_back(part.begin(), part.end());
        }
        if (line.starts_with('#') || fields.size() < 2)
        {
            continue;
        }
        if (fields[0] == "machine")
        {
            baseline.machine = fields[1];
        }
        else if (fields[0] == "settings")
        {
            baseline.settings = fields[1];
        }
        else if (fields[0] == "recorded")
        {
            baseline.recorded = fields[1];
        }
        else if (fields.size() >= 4 && fields[0] != "scene")
        {
            BaselineRow row{.scene = fields[0], .backend = fields[1]};
            const auto parse = [](const std::string& text, double& target) { (void)std::from_chars(text.data(), text.data() + text.size(), target); };
            parse(fields[2], row.p50);
            parse(fields[3], row.mean);
            baseline.rows.push_back(std::move(row));
        }
    }
    return baseline;
}

void
write_baseline(const std::filesystem::path& path,
               const Environment& environment,
               const Options& options,
               std::span<const Measurement> measurements)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary};
    stream << "# gfx physics benchmark baseline (gfx_physics_benchmark --record-baseline): step() medians and means in milliseconds\n";
    stream << std::format("machine\t{}\nsettings\t{}\nrecorded\t{}\nscene\tbackend\tp50\tmean\tp95\tp99\n", environment.fingerprint(),
                          settings_text(options, environment), environment.date);
    for (const Measurement& measurement : measurements)
    {
        if (measurement.error.empty())
        {
            stream << std::format("{}\t{}\t{:.4f}\t{:.4f}\t{:.4f}\t{:.4f}\n", gfx::benchmark::information(measurement.scene).name, backend_name(measurement.backend),
                                  measurement.step.p50, measurement.step.mean, measurement.step.p95, measurement.step.p99);
        }
    }
}

/// A measurement against the baseline's row.
struct Comparison
{
    const Measurement* measurement = nullptr;
    double baseline = 0.0;
    double median = 0.0;        ///< The best median of the runs (the first, and those confirming a suspected regression).
    std::uint32_t runs = 1;
    double change = 0.0;
    bool regressed = false;
};

// ===================================================================================================== report

[[nodiscard]] auto
quality_text(const Measurement& measurement) -> std::string
{
    if (!measurement.error.empty())
    {
        return std::format("failed: {}", measurement.error);
    }
    if (measurement.problem.empty())
    {
        return std::format("passed ({})", measurement.measure);
    }
    return std::format("**failed**{}: {}", judged(measurement.backend) ? "" : " (not judged)", measurement.problem);
}

void
write_report(const std::filesystem::path& path,
             const Environment& environment,
             const Options& options,
             std::span<const Measurement> measurements,
             std::string_view baseline_note,
             std::span<const Comparison> comparisons)
{
    std::ofstream stream{path, std::ios::binary};
    stream << "# gfx physics benchmark\n\n";
    stream << "Generated by `gfx_physics_benchmark` (benchmarks/physics; docs/profiler-performance-design.md, PF3) — not written by "
              "hand. Every scene is stepped through `PhysicsWorld::step()` by each backend: a warm-up, then measured steps recorded "
              "by the profiler.\n\n";
    stream << "| Environment | |\n|---|---|\n";
    stream << std::format("| Date | {} |\n| Processor | {} |\n| GPU | {} (driver {}) |\n| Operating system | {} |\n| Power scheme | {} |\n", environment.date,
                          environment.processor, environment.gpu, environment.gpu_driver, environment.operating_system, environment.power_scheme);
    stream << std::format("| Build | {} |\n| Settings | {} steps after {} warm-up steps of 1/60 s; Box3D and the reference backend on {} scheduler threads; "
                          "scenes at {} times their size; sleep {} |\n",
                          environment.build, options.steps, options.warm_up, options.threads, options.scale, options.sleep ? "on" : "off");
    if (!options.note.empty())
    {
        stream << std::format("| Note | {} |\n", options.note);
    }

    // Every measurement.
    stream << "\n## Measurements\n\nMilliseconds per step. *CPU work*: every thread's outermost profiler scopes, summed (Box3D's tasks on its "
              "scheduler's threads included); *GPU busy*: the union of the GPU's timed scopes; *overlap*: the share of a step both were busy; "
              "*GPU idle*: the share the GPU was not; *Box3D native*: Box3D's own step time (`b3World_GetProfile`).\n\n";
    stream << "| Scene | Backend | Bodies | Joints | Particles | Mean | p50 | p95 | p99 | Max | CPU work | GPU busy | Overlap | GPU idle | Box3D native | "
              "First step | Quality |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const Measurement& measurement : measurements)
    {
        const bool gpu = measurement.backend == Backend::GPU_XPBD;
        stream << std::format("| {} | {} | {} | {} | {} | {:.3f} | {:.3f} | {:.3f} | {:.3f} | {:.3f} | {:.3f} | {} | {} | {} | {} | {:.1f} | {} |\n",
                              gfx::benchmark::information(measurement.scene).name, backend_name(measurement.backend), measurement.bodies,
                              measurement.joints, measurement.particles, measurement.step.mean, measurement.step.p50, measurement.step.p95,
                              measurement.step.p99, measurement.step.maximum, measurement.cpu_work,
                              gpu ? std::format("{:.3f}", measurement.gpu_busy) : std::string{"—"},
                              gpu ? std::format("{:.2f}", measurement.overlap) : std::string{"—"},
                              gpu ? std::format("{:.2f}", measurement.gpu_idle) : std::string{"—"},
                              measurement.box3d_native ? std::format("{:.3f}", *measurement.box3d_native) : std::string{"—"}, measurement.first_step,
                              quality_text(measurement));
    }

    // The GPU backend against Box3D.
    stream << "\n## The GPU backend against Box3D\n\nMean `step()` wall clock. Target: no slower than Box3D on every scene, and "
           << std::format("{:.2f} times faster from 10 000 bodies (or particles) or 100 000 constraints", SPEED_UP_TARGET)
           << ".\n\n| Scene | Box3D | GPU | Speed-up | Target | Met |\n|---|---|---|---|---|---|\n";
    for (const BenchmarkScene scene : options.scenes)
    {
        const auto find = [&](Backend backend) -> const Measurement* {
            const auto found = std::ranges::find_if(measurements, [&](const Measurement& measurement) {
                return measurement.scene == scene && measurement.backend == backend && measurement.error.empty();
            });
            return found != measurements.end() ? &*found : nullptr;
        };
        const Measurement* box3d = find(Backend::BOX3D);
        const Measurement* gpu = find(Backend::GPU_XPBD);
        if (box3d == nullptr || gpu == nullptr)
        {
            continue;
        }
        const double speed_up = box3d->step.mean / gpu->step.mean;
        const double target = gpu->large() ? SPEED_UP_TARGET : 1.0;
        stream << std::format("| {} | {:.3f} | {:.3f} | {:.2f}x | {:.2f}x | {} |\n", gfx::benchmark::information(scene).name, box3d->step.mean, gpu->step.mean,
                              speed_up, target, speed_up >= target ? "yes" : "**no**");
    }

    // Against the baseline.
    stream << "\n## Against the baseline\n\n" << baseline_note << "\n";
    if (!comparisons.empty())
    {
        stream << std::format("\nA median more than {:.0f} % above the baseline's is a regression of gfx's backends (Box3D's changes are shown, "
                              "not judged: its medians move by up to 10 % run to run here).\n\n",
                              100.0 * REGRESSION_TOLERANCE);
        stream << "A median above it is measured again (up to " << options.confirmations
               << " more runs) and the best run judged: a regression must persist.\n\n";
        stream << "| Scene | Backend | Baseline p50 | p50 (best run) | Runs | Change | Verdict |\n|---|---|---|---|---|---|---|\n";
        for (const Comparison& comparison : comparisons)
        {
            stream << std::format("| {} | {} | {:.3f} | {:.3f} | {} | {:+.1f} % | {} |\n", gfx::benchmark::information(comparison.measurement->scene).name,
                                  backend_name(comparison.measurement->backend), comparison.baseline, comparison.median, comparison.runs,
                                  100.0 * comparison.change, comparison.regressed ? "**regression**" : "ok");
        }
    }
}

} // namespace

auto
main(int argument_count,
     char** arguments) -> int
{
    const std::expected<Options, std::string> parsed = parse_options(std::span{arguments, static_cast<std::size_t>(argument_count)});
    if (!parsed)
    {
        std::println(std::cerr, "gfx_physics_benchmark: {}", parsed.error());
        return BAD_ARGUMENTS;
    }
    const Options& options = *parsed;
    if (options.only_when_asked)
    {
        const char* asked = std::getenv("GFX_RUN_BENCHMARKS");
        if (asked == nullptr || std::string_view{asked} != "1")
        {
            std::println("gfx_physics_benchmark: skipped (set GFX_RUN_BENCHMARKS=1 to run the benchmark; it takes minutes)");
            return SKIPPED;
        }
    }

    // The machine; the GPU the GPU backend runs on.
    Environment environment{.processor = read_text(&gfx::benchmark::environment::processor_name),
                            .operating_system = read_text(&gfx::benchmark::environment::operating_system),
                            .power_scheme = read_text(&gfx::benchmark::environment::power_scheme)};
    environment.date = std::format("{:%Y-%m-%d %H:%M} UTC", std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now()));
    environment.build = std::format("{} build, profiler {}", gfx::DEBUG_BUILD ? "Debug" : "optimized", profiler::PROFILER_ENABLED ? "on" : "off");
    GpuDevice gpu{};
    std::vector<Backend> backends;
    for (const Backend backend : options.backends)
    {
        if (backend == Backend::BOX3D && !physics::physics_backend_availability(physics::PhysicsBackendKind::BOX3D).available)
        {
            std::println("gfx_physics_benchmark: Box3D is not built: its runs are left out");
            continue;
        }
        if (backend == Backend::GPU_XPBD)
        {
#if GFX_BENCHMARK_GPU_XPBD
            const std::string problem = choose_gpu(gpu, options.gpu_device);
            if (!problem.empty())
            {
                std::println("gfx_physics_benchmark: {}: the GPU backend's runs are left out", problem);
                continue;
            }
            const gfx::PhysicalDeviceProperties& properties = gpu.physical_device->properties();
            environment.gpu = properties.name;
            environment.gpu_driver = std::format("{}.{}.{}", properties.driver_version.major, properties.driver_version.minor, properties.driver_version.patch);
#else
            std::println("gfx_physics_benchmark: the GPU backend is not built (GFX_PHYSICS_BACKEND_GPU_XPBD): its runs are left out");
            continue;
#endif
        }
        backends.push_back(backend);
    }
    if (gfx::DEBUG_BUILD || !profiler::PROFILER_ENABLED)
    {
        std::println("gfx_physics_benchmark: note: {} — measure with the profile build (python build.py --profile)", environment.build);
    }
    std::filesystem::create_directories(options.output);

    // Every scene on every backend.
    physics::ThreadTaskScheduler scheduler{options.threads};
    std::vector<Measurement> measurements;
    bool failed = false;
    for (const BenchmarkScene scene : options.scenes)
    {
        for (const Backend backend : backends)
        {
            Measurement measurement = measure(scene, backend, options, scheduler, gpu);
            std::println("{:<18} {:<9} mean {:9.3f} ms  p50 {:9.3f}  p95 {:9.3f}  GPU busy {:7.3f}  build {:8.0f} ms  {}",
                         gfx::benchmark::information(scene).name, backend_name(backend), measurement.step.mean, measurement.step.p50,
                         measurement.step.p95, measurement.gpu_busy, measurement.build, quality_text(measurement));
            failed = failed || (judged(backend) && (!measurement.error.empty() || !measurement.problem.empty()));
            measurements.push_back(std::move(measurement));
        }
    }

    // Against this machine's baseline, when it was recorded with the same settings.
    std::string baseline_note = "No baseline was given (--check-baseline).";
    std::vector<Comparison> comparisons;
    if (options.check_baseline)
    {
        const std::filesystem::path path = baseline_path(*options.check_baseline, environment);
        const std::optional<Baseline> baseline = read_baseline(path);
        if (!baseline)
        {
            baseline_note = std::format("No baseline of this machine in {} (looked for {}): nothing compared.", options.check_baseline->generic_string(),
                                        path.filename().generic_string());
        }
        else if (baseline->machine != environment.fingerprint() || baseline->settings != settings_text(options, environment))
        {
            baseline_note = std::format("The baseline {} was recorded with other settings ({}; {}): nothing compared.", path.filename().generic_string(),
                                        baseline->machine, baseline->settings);
        }
        else
        {
            baseline_note = std::format("Baseline {} (recorded {}).", path.filename().generic_string(), baseline->recorded);
            for (const Measurement& measurement : measurements)
            {
                const auto row = std::ranges::find_if(baseline->rows, [&](const BaselineRow& candidate) {
                    return candidate.scene == gfx::benchmark::information(measurement.scene).name && candidate.backend == backend_name(measurement.backend);
                });
                if (row == baseline->rows.end() || !measurement.error.empty() || !(row->p50 > 0.0))
                {
                    continue;
                }
                Comparison comparison{.measurement = &measurement,
                                      .baseline = row->p50,
                                      .median = measurement.step.p50,
                                      .runs = 1,
                                      .change = measurement.step.p50 / row->p50 - 1.0};

                // A suspected regression of a judged backend measured again; the best run judged.
                while (judged(measurement.backend) && comparison.change > REGRESSION_TOLERANCE && comparison.runs <= options.confirmations)
                {
                    std::println("{} on {}: median {:+.1f} % above the baseline, measured again", gfx::benchmark::information(measurement.scene).name,
                                 backend_name(measurement.backend), 100.0 * comparison.change);
                    const Measurement again = measure(measurement.scene, measurement.backend, options, scheduler, gpu);
                    ++comparison.runs;
                    if (again.error.empty() && again.step.p50 < comparison.median)
                    {
                        comparison.median = again.step.p50;
                        comparison.change = comparison.median / row->p50 - 1.0;
                    }
                }
                comparison.regressed = judged(measurement.backend) && comparison.change > REGRESSION_TOLERANCE;
                if (comparison.regressed)
                {
                    failed = true;
                    std::println("REGRESSION {} on {}: median {:.3f} ms (best of {} runs), baseline {:.3f} ms ({:+.1f} %)",
                                 gfx::benchmark::information(measurement.scene).name, backend_name(measurement.backend), comparison.median, comparison.runs,
                                 row->p50, 100.0 * comparison.change);
                }
                comparisons.push_back(comparison);
            }
        }
        std::println("gfx_physics_benchmark: {}", baseline_note);
    }
    if (options.record_baseline)
    {
        const std::filesystem::path path = baseline_path(*options.record_baseline, environment);
        write_baseline(path, environment, options, measurements);
        std::println("gfx_physics_benchmark: baseline recorded in {}", path.generic_string());
    }
    const std::filesystem::path report = options.output / "benchmark_report.md";
    write_report(report, environment, options, measurements, baseline_note, comparisons);
    std::println("gfx_physics_benchmark: {} written ({})", report.generic_string(), failed ? "failed" : "passed");
    return failed ? FAILED : PASSED;
}
