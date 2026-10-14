# ==============================================================================
# gfx_project_options: compiler warnings and platform libraries shared by every gfx target.
# ==============================================================================
include_guard(GLOBAL)

add_library(gfx_project_options INTERFACE)
add_library(gfx::project_options ALIAS gfx_project_options)
target_compile_features(gfx_project_options INTERFACE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(gfx_project_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual
        $<$<BOOL:${GFX_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# MinGW's libstdc++ keeps the terminal back end of std::print in libstdc++exp.
if(MINGW)
    target_link_libraries(gfx_project_options INTERFACE stdc++exp)
endif()
