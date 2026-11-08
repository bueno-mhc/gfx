/// @file errors.cppm
/// @brief The profiler's error, ProfilerError: a configuration the profiler cannot honor (an invalid session
///        description, a second session while one records), a trace file that cannot be written, or one that cannot
///        be read (unknown version, truncated). It joins the gfx hierarchy (gfx::Error, a std::runtime_error).
///        Recording never throws: what cannot be recorded is counted and marked in the trace.
/// @code
/// [gfx::profiler::EXPORT] export_trace: cannot open 'C:/traces/step.json' for writing.
///   Suggestion: check that the directory exists and is writable.
///   Location: source/profiler/exporters.cpp:212 (export_trace)
/// @endcode
export module gfx.profiler.errors;

import std;
export import gfx.core.errors;
import gfx.profiler.enums;

export namespace gfx::profiler
{

/// @brief Describes an operation of the profiler that cannot be honored.
struct ProfilerErrorDescription
{
    ProfilerSubsystem subsystem = ProfilerSubsystem::SESSIONS; ///< The part of the profiler.
    std::string operation{};                                   ///< The operation, e.g. "Profiler::create_session".
    std::string problem{};                                     ///< What is wrong.
    std::string suggestion{};                                  ///< What to do instead; may be empty.
};

/// @brief Thrown when an operation of the profiler cannot be honored (never while recording).
class ProfilerError : public Error
{
public:
    /// @param description What failed, why, and what to do instead.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit ProfilerError(ProfilerErrorDescription description,
                           std::source_location location = std::source_location::current());

    /// @brief The part of the profiler that failed.
    [[nodiscard]] auto subsystem() const noexcept -> ProfilerSubsystem;

    /// @brief What is wrong.
    [[nodiscard]] auto problem() const noexcept -> std::string_view;

    /// @brief What to do instead; may be empty.
    [[nodiscard]] auto suggestion() const noexcept -> std::string_view;

private:
    ProfilerSubsystem m_subsystem;
    std::string m_problem;
    std::string m_suggestion;
};

} // namespace gfx::profiler
