module gfx.profiler.errors;

import std;
import gfx.core.enums;
import gfx.profiler.enums;

namespace gfx::profiler
{

namespace
{

/// The problem and the suggestion line, as Error prints details.
auto
details_of(const ProfilerErrorDescription& description) -> std::string
{
    const std::string suggestion = description.suggestion.empty() ? std::string{} : std::format("\n  Suggestion: {}", description.suggestion);
    return std::format("{}{}", description.problem, suggestion);
}

} // namespace

ProfilerError::ProfilerError(ProfilerErrorDescription description,
                             std::source_location location)
    : Error(BackendIdentity{description.subsystem}, description.operation, details_of(description), location)
    , m_subsystem{description.subsystem}
    , m_problem{std::move(description.problem)}
    , m_suggestion{std::move(description.suggestion)}
{
}

auto
ProfilerError::subsystem() const noexcept -> ProfilerSubsystem
{
    return m_subsystem;
}

auto
ProfilerError::problem() const noexcept -> std::string_view
{
    return m_problem;
}

auto
ProfilerError::suggestion() const noexcept -> std::string_view
{
    return m_suggestion;
}

} // namespace gfx::profiler
