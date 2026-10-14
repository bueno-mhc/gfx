# ==============================================================================
# gfx_project_options: compiler warnings and platform libraries shared by every gfx target.
# gfx_toolchain_workarounds: link options every executable using gfx needs on a defective toolchain.
# ==============================================================================
include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# Toolchain workarounds (propagated to consumers of the gfx library)
# ------------------------------------------------------------------------------
add_library(gfx_toolchain_workarounds INTERFACE)
add_library(gfx::toolchain_workarounds ALIAS gfx_toolchain_workarounds)

# WORKAROUND - GCC 16 on MinGW: with `import std`, GCC emits static local variables of standard
# library templates (for example std::__format::__write_escaped_unicode_part<...>::__replace_rep)
# as ordinary strong symbols instead of COMDAT sections. Two object files that format strings then
# fail to link with "multiple definition of". Those definitions are identical, so letting the linker
# keep the first one is safe for them - but the option also hides genuine duplicate definitions.
# The test CompilationFailure.ImportStdDuplicatesStaticLocals fails once GCC no longer needs this.
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(gfx_toolchain_workarounds INTERFACE -Wl,--allow-multiple-definition)
endif()

# MinGW's libstdc++ keeps the terminal back end of std::print in libstdc++exp.
if(MINGW)
    target_link_libraries(gfx_toolchain_workarounds INTERFACE stdc++exp)
endif()

# ------------------------------------------------------------------------------
# Project options (gfx's own targets only)
# ------------------------------------------------------------------------------
add_library(gfx_project_options INTERFACE)
add_library(gfx::project_options ALIAS gfx_project_options)
target_compile_features(gfx_project_options INTERFACE cxx_std_23)
target_link_libraries(gfx_project_options INTERFACE gfx::toolchain_workarounds)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(gfx_project_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual
        $<$<BOOL:${GFX_WARNINGS_AS_ERRORS}>:-Werror>)
endif()
