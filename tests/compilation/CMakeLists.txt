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

gfx_add_compilation_failure_test(TypedPushConstantsRequireTheDeclaredSize
    SOURCES interface/typed_push_constants_require_the_declared_size.cpp
    EXPECTED_DIAGNOSTIC "no matching function for call to .*push_constants<Shader>"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ typed pipelines and descriptors
gfx_add_compilation_failure_test(TypedComputePipelineRejectsIncompatibleLayout
    SOURCES interface/typed_compute_pipeline_rejects_incompatible_layout.cpp
    EXPECTED_DIAGNOSTIC "LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH, 0, 1, gfx::DescriptorType::STORAGE_IMAGE, gfx::DescriptorType::SAMPLED_IMAGE"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(TypedGraphicsPipelineRejectsMissingVertexAttribute
    SOURCES interface/typed_graphics_pipeline_rejects_missing_vertex_attribute.cpp
    EXPECTED_DIAGNOSTIC "LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE, 0, 1"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(DescriptorWritesCheckTheResourceType
    SOURCES interface/descriptor_writes_check_the_resource_type.cpp
    EXPECTED_DIAGNOSTIC "the resource type does not match the binding's descriptor type"
    LIBRARIES gfx::gfx)

gfx_add_compilation_failure_test(DescriptorWritesRejectUnknownNames
    SOURCES interface/descriptor_writes_reject_unknown_names.cpp
    EXPECTED_DIAGNOSTIC "shader_declares_no_binding_with_this_name"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ backend contract
gfx_add_compilation_failure_test(BackendsMustOverrideEveryOperation
    SOURCES interface/backends_must_override_every_operation.cpp
    EXPECTED_DIAGNOSTIC "to be of abstract type 'IncompleteBuffer'"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ compile-time GLSL reflection
gfx_add_compilation_failure_test(GlslReflectionReportsUnknownMemberTypes
    SOURCES shader_layout/glsl_reflection_reports_unknown_member_types.cpp
    EXPECTED_DIAGNOSTIC "glsl_unknown_type_in_structure_or_block"
    LIBRARIES gfx::gfx)

# ------------------------------------------------------------------------------ naming
gfx_add_platform_macro_collision_test(NamesDoNotCollideWithPlatformMacros
    SOURCE ${PROJECT_SOURCE_DIR}/source)
