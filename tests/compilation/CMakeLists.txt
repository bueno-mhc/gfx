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

# ------------------------------------------------------------------------------ naming
gfx_add_platform_macro_collision_test(EnumeratorsDoNotCollideWithPlatformMacros
    SOURCE ${PROJECT_SOURCE_DIR}/source/enums.cppm)
