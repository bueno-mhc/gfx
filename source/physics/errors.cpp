module gfx.physics.errors;

import std;
import gfx.core.enums;
import gfx.physics.enums;
import gfx.physics.handles;

namespace gfx::physics
{

namespace
{

/// The problem, the context's lines and the suggestion line, as Error prints details.
auto
details_of(const PhysicsErrorDescription& description) -> std::string
{
    const std::string suggestion = description.suggestion.empty() ? std::string{} : std::format("\n  Suggestion: {}", description.suggestion);
    return std::format("{}{}{}", description.problem, context_lines(description.context), suggestion);
}

} // namespace

PhysicsError::PhysicsError(PhysicsErrorDescription description,
                           std::source_location location)
    : Error(description.backend, description.operation, details_of(description), location)
    , m_problem{std::move(description.problem)}
    , m_suggestion{std::move(description.suggestion)}
    , m_context{description.context}
{
}

auto
PhysicsError::problem() const noexcept -> std::string_view
{
    return m_problem;
}

auto
PhysicsError::suggestion() const noexcept -> std::string_view
{
    return m_suggestion;
}

auto
PhysicsError::context() const noexcept -> const PhysicsErrorContext&
{
    return m_context;
}

auto
context_lines(const PhysicsErrorContext& context) -> std::string
{
    // One line per object the error concerns, in a fixed order.
    std::string lines;
    const auto add = [&lines](std::string_view label, const auto& handle)
    {
        if (handle)
        {
            lines += std::format("\n  {}: {}", label, *handle);
        }
    };
    add("World", context.world);
    add("Body", context.rigid_body);
    add("Soft body", context.soft_body);
    add("Collider", context.collider);
    add("Shape", context.shape);
    add("Joint", context.joint);
    add("Constraint", context.constraint);
    add("Trigger", context.trigger);
    return lines;
}

void
throw_unsupported(BackendIdentity backend,
                  std::string_view operation,
                  PhysicsCapability capability,
                  std::string_view reason,
                  std::string_view suggestion,
                  const PhysicsErrorContext& context,
                  std::source_location location)
{
    throw BackendCapabilityError({.backend = backend,
                                  .operation = std::string{operation},
                                  .capability = std::string{to_string(capability)},
                                  .reason = std::format("{}{}", reason, context_lines(context)),
                                  .suggestion = std::string{suggestion}},
                                 location);
}

auto
QueryFailure::message() const -> std::string
{
    // "<operation>: <kind>[ (<capability>)][: <detail>]", with the context's lines.
    const std::string origin = backend ? std::format("[{}] ", *backend) : std::string{"[gfx::physics] "};
    const std::string lacking = capability ? std::format(" ({})", to_string(*capability)) : std::string{};
    const std::string more = detail.empty() ? std::string{} : std::format(": {}", detail);
    return std::format("{}{}: {}{}{}{}", origin, operation, to_string(kind), lacking, more, context_lines(context));
}

void
QueryFailure::raise(std::source_location location) const
{
    if (kind == QueryFailureKind::UNSUPPORTED && backend && capability)
    {
        throw_unsupported(*backend, operation, *capability, detail.empty() ? std::string_view{"the backend cannot answer this query"} : detail,
                          "use a query the backend supports, or another backend", context, location);
    }
    throw PhysicsError({.backend = backend,
                        .operation = std::string{operation},
                        .problem = std::format("the query failed: {}{}", to_string(kind), detail.empty() ? std::string{} : std::format(" ({})", detail)),
                        .suggestion = {},
                        .context = context},
                       location);
}

} // namespace gfx::physics
