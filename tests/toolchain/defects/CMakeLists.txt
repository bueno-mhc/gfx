# Toolchain defect detectors. Each test PASSES while the defect it describes still exists, which
# justifies the matching workaround. When one starts FAILING, the toolchain was fixed: remove the
# workaround named in the comment and this test.
if(NOT (MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    return()
endif()

# Workaround: -Wl,--allow-multiple-definition in cmake/GfxProjectOptions.cmake.
gfx_add_compilation_failure_test(ImportStdDuplicatesStaticLocals
    SOURCES import_std_first_formatter.cpp import_std_second_formatter.cpp
    EXPECTED_DIAGNOSTIC "multiple definition of"
    WITHOUT_PROJECT_OPTIONS)
# Optimized builds fold this particular static local away; -O0 reproduces the defect in every configuration.
target_compile_options(gfx_compilation_failure_ImportStdDuplicatesStaticLocals PRIVATE -O0)

# Workaround: source/include/gfx/windows_headers.hpp.
gfx_add_compilation_failure_test(WindowsHeadersConflictWithModules
    SOURCES windows_headers_with_modules.cpp
    EXPECTED_DIAGNOSTIC "conflicting language linkage for imported declaration"
    WITHOUT_PROJECT_OPTIONS)

# A class declared in one module partition and defined in another cannot have a capturing lambda in an
# in-class constructor: the defining partition's module file cannot be read back.
# Workaround: a private static [[noreturn]] function instead of the lambda
# (RenderingPartRecorder::reject in source/interface/recorder.cppm; :command_buffer declares the class).
gfx_add_compilation_failure_test(LambdaInConstructorCorruptsModuleFile
    SOURCES lambda_in_constructor_importer.cpp
    MODULES lambda_in_constructor_primary.cppm lambda_in_constructor_declaration.cppm lambda_in_constructor_module.cppm
    EXPECTED_DIAGNOSTIC "failed to read compiled module"
    WITHOUT_PROJECT_OPTIONS)

# A constructor defined in a module implementation unit, of a class declared in an internal partition,
# crashes GCC 16 (segmentation fault at the end of its mem-initializer list) when it default-initializes
# a member of a class template specialization from another module (a member no initializer names).
# Workaround: such classes are declared in the implementation unit that defines them
# (source/backends/vulkan/vulkan_synchronization.cpp); value-initializing the member in the class
# (`FenceHandle m_handle{};`) avoids it too.
gfx_add_compilation_failure_test(MemberInitializerOfPartitionClassCrashesCompiler
    SOURCES member_initializer_definition.cpp member_initializer_importer.cpp
    MODULES member_initializer_primary.cppm member_initializer_base.cppm
            member_initializer_declaration.cppm
    EXPECTED_DIAGNOSTIC "internal compiler error"
    WITHOUT_PROJECT_OPTIONS)

# HandleTable::find() or remove() instantiated in a module interface, for a tag of another module and a
# record pointing to a class of the instantiating module, makes the module file unreadable for importers
# ("failed to read compiled module cluster N: Bad file data").
# Workaround: such uses live in module implementation units (the Vulkan backend's .cpp files,
# tests/platform/fake_platform.cpp).
gfx_add_compilation_failure_test(TableLookupInModuleInterfaceCorruptsModuleFile
    SOURCES table_lookup_importer.cpp
    MODULES table_lookup_holder.cppm
    EXPECTED_DIAGNOSTIC "failed to read compiled module"
    LIBRARIES gfx::platform
    WITHOUT_PROJECT_OPTIONS)

# A block-scope using-declaration of another module's enumeration (`using platform::Button;`) in a consteval
# function of a module interface crashes GCC 16 while it writes the module file (internal compiler error in
# import_entity_module). A using-declaration of a function, or the same function inline (not consteval), is fine.
# Workaround: the enumeration named in full (navigation_bindings::gamepad() in source/ui/platform/ui_platform.cppm).
gfx_add_compilation_failure_test(UsingDeclarationOfImportedEnumerationCrashesCompiler
    SOURCES using_declaration_importer.cpp
    MODULES using_declaration_enumerations.cppm using_declaration_function.cppm
    EXPECTED_DIAGNOSTIC "internal compiler error"
    WITHOUT_PROJECT_OPTIONS)
