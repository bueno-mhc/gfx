# Each test compiles one snippet that the type system must reject, and checks the diagnostic.
# Run only these with: ctest --test-dir build/debug -L compilation

# ------------------------------------------------------------------------------ handles
gfx_add_compilation_failure_test(HandleKeyIsNotConstructibleByUsers
    SOURCES handles/handle_key_is_not_constructible.cpp
    EXPECTED_DIAGNOSTIC "HandleKey.*is private within this context"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(HandleValueIsPrivate
    SOURCES handles/handle_value_is_private.cpp
    EXPECTED_DIAGNOSTIC "m_value.*is private within this context"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(HandleIndexRequiresAKey
    SOURCES handles/handle_index_requires_a_key.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*index\\(\\)"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(HandlesRejectArithmetic
    SOURCES handles/handles_reject_arithmetic.cpp
    EXPECTED_DIAGNOSTIC "no match for 'operator\\+'"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(HandlesAreNotConstructibleFromIntegers
    SOURCES handles/handles_are_not_constructible_from_integers.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*Handle"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(HandlesOfDifferentKindsDoNotCompare
    SOURCES handles/handles_of_different_kinds_do_not_compare.cpp
    EXPECTED_DIAGNOSTIC "no match for 'operator=='"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(GraphicsHandleConceptRejectsComputePipelines
    SOURCES handles/graphics_handle_rejects_compute_pipelines.cpp
    EXPECTED_DIAGNOSTIC "GraphicsHandle.*(not satisfied|evaluated to 'false')"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ enumerations
gfx_add_compilation_failure_test(SequentialEnumerationsRejectBitwiseOperators
    SOURCES enums/sequential_enumerations_reject_bitwise_operators.cpp
    EXPECTED_DIAGNOSTIC "no match for 'operator\\|'"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ typed recording
gfx_add_compilation_failure_test(ComputeRecorderCannotBeginRendering
    SOURCES interface/compute_recorder_cannot_begin_rendering.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*QueueType::COMPUTE>::begin_rendering"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(TransferRecorderCannotDispatch
    SOURCES interface/transfer_recorder_cannot_dispatch.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*QueueType::TRANSFER>::dispatch"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(DrawsRequireARenderingScope
    SOURCES interface/draws_require_a_rendering_scope.cpp
    EXPECTED_DIAGNOSTIC "has no member named 'draw'"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(ParallelRenderingScopeCannotDraw
    SOURCES interface/parallel_rendering_scope_cannot_draw.cpp
    EXPECTED_DIAGNOSTIC "class gfx::ParallelRenderingScope[^']*' has no member named 'draw'"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(RenderingPartsCannotRecordBarriers
    SOURCES interface/rendering_parts_cannot_record_barriers.cpp
    EXPECTED_DIAGNOSTIC "class gfx::RenderingPartRecorder[^']*' has no member named 'barrier'"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(TransferRecorderCannotTraceRays
    SOURCES interface/transfer_recorder_cannot_trace_rays.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*QueueType::TRANSFER>::trace_rays"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(BindlessIndicesOfDifferentKindsDoNotMix
    SOURCES interface/bindless_indices_of_different_kinds_do_not_mix.cpp
    EXPECTED_DIAGNOSTIC "conversion from 'BindlessIndex<gfx::BindlessResourceKind::SAMPLED_IMAGE>' to non-scalar type 'BindlessIndex<gfx::BindlessResourceKind::STORAGE_IMAGE>'"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(PushConstantsRejectPointers
    SOURCES interface/push_constants_reject_pointers.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*push_constants"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ backend contract
gfx_add_compilation_failure_test(BackendsMustOverrideEveryOperation
    SOURCES interface/backends_must_override_every_operation.cpp
    EXPECTED_DIAGNOSTIC "to be of abstract type 'IncompleteBuffer'"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ naming
gfx_add_platform_macro_collision_test(NamesDoNotCollideWithPlatformMacros
    SOURCE ${PROJECT_SOURCE_DIR}/source)

# ------------------------------------------------------------------------------ platform
if(GFX_BUILD_PLATFORM)
    gfx_add_compilation_failure_test(EventPayloadMustMatchItsType
        SOURCES platform/event_payload_must_match_its_type.cpp
        EXPECTED_DIAGNOSTIC "could not convert|no matching function for call to .*make"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(PlatformHandlesOfDifferentKindsDoNotCompare
        SOURCES platform/platform_handles_of_different_kinds_do_not_compare.cpp
        EXPECTED_DIAGNOSTIC "no match for 'operator=='"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(IdentifiersOfDifferentKindsDoNotMix
        SOURCES platform/identifiers_of_different_kinds_do_not_mix.cpp
        EXPECTED_DIAGNOSTIC "conversion from 'gfx::Identifier<gfx::platform::BindingContextTag>' to non-scalar type 'const gfx::platform::ActionIdentifier'|conversion from .*BindingContextTag.* to non-scalar type"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(IdentifiersNeedAName
        SOURCES platform/identifiers_need_a_name.cpp
        EXPECTED_DIAGNOSTIC "an identifier needs a non-empty name|not a constant expression"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(InputSchemaRejectsDuplicateActions
        SOURCES platform/input_schema_rejects_duplicate_actions.cpp
        EXPECTED_DIAGNOSTIC "input_schema_error_two_actions_have_the_same_name"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(InputSchemaRejectsUndeclaredActions
        SOURCES platform/input_schema_rejects_undeclared_actions.cpp
        EXPECTED_DIAGNOSTIC "input_schema_error_a_binding_names_an_undeclared_action"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(InputSchemaRejectsTwoDimensionalBindingsOnButtons
        SOURCES platform/input_schema_rejects_two_dimensional_bindings_on_buttons.cpp
        EXPECTED_DIAGNOSTIC "input_schema_error_a_two_dimensional_binding_needs_an_axis_2d_action"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(InputSchemaRejectsTriggersOnAxes
        SOURCES platform/input_schema_rejects_triggers_on_axes.cpp
        EXPECTED_DIAGNOSTIC "input_schema_error_hold_and_tap_triggers_need_a_button_action"
        LIBRARIES gfx::platform)

    gfx_add_compilation_failure_test(CompositesNeedDigitalInputs
        SOURCES platform/composites_need_digital_inputs.cpp
        EXPECTED_DIAGNOSTIC "no matching function for call to 'composite_axis|DigitalInputSourceType"
        LIBRARIES gfx::platform)
endif()

# ------------------------------------------------------------------------------ ui
if(GFX_BUILD_UI)
    gfx_add_compilation_failure_test(UiHandlesOfDifferentKindsDoNotCompare
        SOURCES ui/ui_handles_of_different_kinds_do_not_compare.cpp
        EXPECTED_DIAGNOSTIC "no match for 'operator=='"
        LIBRARIES gfx::ui)

    gfx_add_compilation_failure_test(UiIdentifiersOfDifferentKindsDoNotMix
        SOURCES ui/ui_identifiers_of_different_kinds_do_not_mix.cpp
        EXPECTED_DIAGNOSTIC "conversion from .*PopupTag.* to non-scalar type"
        LIBRARIES gfx::ui)

    gfx_add_compilation_failure_test(UiSlidersRejectBooleans
        SOURCES ui/ui_sliders_reject_booleans.cpp
        EXPECTED_DIAGNOSTIC "ArithmeticValue"
        LIBRARIES gfx::ui)

    gfx_add_compilation_failure_test(UiCombosNeedTextItems
        SOURCES ui/ui_combos_need_text_items.cpp
        EXPECTED_DIAGNOSTIC "TextLike"
        LIBRARIES gfx::ui)

    gfx_add_compilation_failure_test(UiComponentsNeedWritableNumbers
        SOURCES ui/ui_components_need_writable_numbers.cpp
        EXPECTED_DIAGNOSTIC "EditableNumbers"
        LIBRARIES gfx::ui)

    gfx_add_compilation_failure_test(UiPayloadsAreTriviallyCopyable
        SOURCES ui/ui_payloads_are_trivially_copyable.cpp
        EXPECTED_DIAGNOSTIC "is_trivially_copyable"
        LIBRARIES gfx::ui)
endif()
