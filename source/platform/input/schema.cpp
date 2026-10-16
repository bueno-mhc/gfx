// Run-time checks of input schemas: messages naming the actions and contexts concerned.
module gfx.platform.input;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;

namespace gfx::platform
{

namespace
{

/// What to do about @p problem.
[[nodiscard]] auto
suggestion_for(InputSchemaProblem problem) -> std::string_view
{
    switch (problem)
    {
        case InputSchemaProblem::MISSING_NAME:
            return "give every action and binding context a name";
        case InputSchemaProblem::DUPLICATE_ACTION:
        case InputSchemaProblem::DUPLICATE_CONTEXT:
            return "rename one of them";
        case InputSchemaProblem::UNKNOWN_ACTION:
            return "declare the action with action() in the schema, or fix the binding's action name";
        case InputSchemaProblem::UNKNOWN_CONTEXT:
            return "declare the binding context in the schema, or fix the binding's context name";
        case InputSchemaProblem::MALFORMED_BINDING:
            return "build bindings with the binding builders (single(), chord(), gamepad_chord(), composite_axis(), "
                   "composite_2d(), stick())";
        case InputSchemaProblem::INCOMPATIBLE_BINDING:
            return "declare the action as ActionValueType::AXIS_2D, or bind it to a one-dimensional input";
        case InputSchemaProblem::TRIGGER_ON_AXIS:
            return "declare the action as ActionValueType::BUTTON, or remove the trigger";
        case InputSchemaProblem::INVALID_TRIGGER_TIME:
            return "pass a positive time to held_for() or tapped_within()";
        case InputSchemaProblem::INVALID_DEADZONE:
            return "pass thresholds such as with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F, 1.0F)";
        case InputSchemaProblem::INVALID_RESPONSE_CURVE:
            return "pass a positive exponent to with_curve(ResponseCurve::POWER, exponent)";
        case InputSchemaProblem::NONE:
            break;
    }
    return {};
}

} // namespace

auto
describe_input_schema_problem(InputSchemaView schema,
                              const InputSchemaCheck& check) -> std::string
{
    // Name the elements the check points at.
    const auto action_name = [&schema](std::size_t index) { return std::format("'{}'", schema.actions[index].name); };
    const auto context_name = [&schema](std::size_t index) { return std::format("'{}'", schema.contexts[index].name); };
    switch (check.problem)
    {
        case InputSchemaProblem::NONE:
            return std::string{describe_input_schema_problem(check.problem)};
        case InputSchemaProblem::MISSING_NAME:
            return check.action ? std::format("action {} has an empty name", *check.action)
                                : std::format("binding context {} has an empty name", check.context.value_or(0));
        case InputSchemaProblem::DUPLICATE_ACTION:
        {
            const std::string_view first = schema.actions[check.duplicate_of.value_or(0)].name;
            const std::string_view second = schema.actions[check.action.value_or(0)].name;
            return first == second ? std::format("two actions are named '{}'", first)
                                   : std::format("actions '{}' and '{}' have the same identifier hash", first, second);
        }
        case InputSchemaProblem::DUPLICATE_CONTEXT:
        {
            const std::string_view first = schema.contexts[check.duplicate_of.value_or(0)].name;
            const std::string_view second = schema.contexts[check.context.value_or(0)].name;
            return first == second ? std::format("two binding contexts are named '{}'", first)
                                   : std::format("binding contexts '{}' and '{}' have the same identifier hash", first, second);
        }
        case InputSchemaProblem::UNKNOWN_CONTEXT:
        {
            const SchemaBinding& binding = schema.bindings[check.binding.value_or(0)];
            return std::format("binding {} names {}, which the schema does not declare", check.binding.value_or(0), binding.context);
        }
        case InputSchemaProblem::UNKNOWN_ACTION:
        {
            const SchemaBinding& binding = schema.bindings[check.binding.value_or(0)];
            return std::format("binding {} of binding context {} names {}, which the schema does not declare",
                               check.binding.value_or(0), context_name(check.context.value_or(0)), binding.action);
        }
        default:
            return std::format("binding {} of action {} in binding context {}: {}", check.binding.value_or(0),
                               action_name(check.action.value_or(0)), context_name(check.context.value_or(0)),
                               describe_input_schema_problem(check.problem));
    }
}

void
validate_input_schema(InputSchemaView schema,
                      std::source_location location)
{
    // Report the first problem with the names concerned.
    const InputSchemaCheck check = check_input_schema(schema);
    if (check.is_valid())
    {
        return;
    }
    throw InvalidUsageError(
        {
            .backend = std::nullopt,
            .operation = "validate_input_schema",
            .problem = std::format("{} ({})", describe_input_schema_problem(schema, check), check.problem),
            .suggestion = std::string{suggestion_for(check.problem)},
        },
        location);
}

} // namespace gfx::platform
