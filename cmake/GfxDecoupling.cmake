# ==============================================================================
# Decoupling checks: libraries that must not depend on others (physics and camera on graphics, platform, UI).
# ==============================================================================
include_guard(GLOBAL)

# gfx_require_decoupled(<target> FORBIDDEN <target>...)
#   Fails the configuration when <target> links, directly or through any library it links, one of the FORBIDDEN
#   targets (aliases resolved). Checked once every target exists (deferred to the end of the top-level directory).
function(gfx_require_decoupled target)
    cmake_parse_arguments(PARSE_ARGV 1 argument "" "" "FORBIDDEN")
    # A deferred call expands its arguments when it runs: EVAL CODE expands them now, while they are known.
    cmake_language(EVAL CODE
        "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _gfx_check_decoupled [[${target}]] [[${argument_FORBIDDEN}]])")
endfunction()

# The real targets named in the link item @p item (aliases resolved): every target-like token of it, so that
# generator expressions ($<$<CONFIG:Debug>:gfx::x>, $<LINK_ONLY:x>) and directory markers (::@(...)) are looked into.
function(_gfx_resolve_link_item item result)
    string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" tokens "${item}")
    set(targets "")
    foreach(token IN LISTS tokens)
        if(TARGET "${token}")
            get_target_property(aliased "${token}" ALIASED_TARGET)
            if(aliased)
                set(token "${aliased}")
            endif()
            list(APPEND targets "${token}")
        endif()
    endforeach()
    set(${result} "${targets}" PARENT_SCOPE)
endfunction()

function(_gfx_check_decoupled target forbidden)
    # Every target reachable through LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES, with the path that reached it.
    set(pending "${target}")
    set(visited "")
    set(path_${target} "${target}")
    while(pending)
        list(POP_FRONT pending current)
        if(current IN_LIST visited)
            continue()
        endif()
        list(APPEND visited "${current}")
        set(links "")
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(values "${current}" ${property})
            if(values)
                list(APPEND links ${values})
            endif()
        endforeach()
        set(reached "")
        foreach(link IN LISTS links)
            _gfx_resolve_link_item("${link}" resolved)
            list(APPEND reached ${resolved})
        endforeach()
        foreach(resolved IN LISTS reached)
            if(resolved IN_LIST visited)
                continue()
            endif()
            set(path_${resolved} "${path_${current}} -> ${resolved}")
            foreach(denied IN LISTS forbidden)
                if(resolved STREQUAL denied)
                    message(FATAL_ERROR "gfx: ${target} must not depend on ${denied}, but links it: ${path_${resolved}}.\n"
                                        "  The physics and camera libraries are decoupled from graphics, platform and UI "
                                        "(docs/physics-design.md A1); link gfx::core only.")
                endif()
            endforeach()
            list(APPEND pending "${resolved}")
        endforeach()
    endwhile()
    list(REMOVE_ITEM visited "${target}")
    list(LENGTH visited count)
    message(STATUS "gfx: ${target} depends on ${count} targets, none of the forbidden ones")
endfunction()
