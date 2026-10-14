# ==============================================================================
# Copies the shared libraries a Windows executable needs next to it.
# ==============================================================================
include_guard(GLOBAL)

# Runtime libraries of the MinGW toolchain.
set(GFX_TOOLCHAIN_RUNTIME_LIBRARIES "")
if(MINGW)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _gfx_toolchain_binary_directory)
    foreach(_gfx_library IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${_gfx_toolchain_binary_directory}/${_gfx_library}")
            list(APPEND GFX_TOOLCHAIN_RUNTIME_LIBRARIES "${_gfx_toolchain_binary_directory}/${_gfx_library}")
        endif()
    endforeach()
endif()

# gfx_deploy_runtime_dependencies(<target>)
#   Copies the shared libraries a Windows executable depends on (imported DLL targets plus the
#   toolchain runtime) next to it, so it runs from any shell - including during GoogleTest
#   discovery, where CMake offers no way to extend PATH.
function(gfx_deploy_runtime_dependencies target)
    if(NOT WIN32)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}> ${GFX_TOOLCHAIN_RUNTIME_LIBRARIES} $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS
        COMMENT "Deploying runtime libraries next to ${target}")
endfunction()
