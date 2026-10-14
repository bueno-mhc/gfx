# ==============================================================================
# Script mode (cmake -P): fails when a name declared by gfx is also a platform macro.
#
#   cmake -DCOMPILER=<c++ compiler> -DSOURCE=<file or directory> -DWORKING_DIRECTORY=<directory>
#         -P GfxCheckPlatformMacroCollisions.cmake
#
# SOURCE is a module file, or a directory whose *.cppm files (recursively) are all checked.
# Checked names: enumerators, constants, classes, structures, concepts, type aliases and functions.
#
# Macros leak into any translation unit that includes the platform headers (for example <windows.h>
# defines ERROR, OPAQUE, DELETE, MemoryBarrier, min and max). A gfx name spelled like one of them
# would not compile in such a translation unit.
# ==============================================================================
foreach(_required IN ITEMS COMPILER SOURCE WORKING_DIRECTORY)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "GfxCheckPlatformMacroCollisions: -D${_required}=... is required.")
    endif()
endforeach()

# ------------------------------------------------------------------------------
# 1. Every macro defined by the platform headers a consumer typically includes
# ------------------------------------------------------------------------------
set(_headers cstddef cstdint climits cerrno cmath cstdio cstdlib cstring ctime)
if(WIN32)
    list(APPEND _headers windows.h sys/param.h)
elseif(UNIX)
    list(APPEND _headers sys/param.h unistd.h)
endif()

file(MAKE_DIRECTORY "${WORKING_DIRECTORY}")
set(_probe "${WORKING_DIRECTORY}/platform_macros.cpp")
set(_probe_content "")
foreach(_header IN LISTS _headers)
    string(APPEND _probe_content "#include <${_header}>\n")
endforeach()
file(WRITE "${_probe}" "${_probe_content}")

execute_process(
    COMMAND "${COMPILER}" -std=c++23 -dM -E "${_probe}"
    OUTPUT_VARIABLE _definitions
    ERROR_VARIABLE _preprocessor_errors
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "GfxCheckPlatformMacroCollisions: preprocessing the platform headers failed:\n${_preprocessor_errors}")
endif()
string(REGEX MATCHALL "#define [A-Za-z_][A-Za-z0-9_]*" _macros "${_definitions}")
list(TRANSFORM _macros REPLACE "^#define " "")
list(REMOVE_DUPLICATES _macros)

# ------------------------------------------------------------------------------
# 2. Every name declared in the sources
# ------------------------------------------------------------------------------
if(IS_DIRECTORY "${SOURCE}")
    file(GLOB_RECURSE _files "${SOURCE}/*.cppm")
else()
    set(_files "${SOURCE}")
endif()

set(_names "")
foreach(_file IN LISTS _files)
    file(READ "${_file}" _content)
    string(REGEX REPLACE "//[^\n]*" "" _content "${_content}")

    # Enumerators: the first identifier of every item of every `enum class X { ... }` body.
    string(REGEX MATCHALL "enum class [A-Za-z0-9_]+[^{;]*{[^}]*}" _enumerations "${_content}")
    foreach(_enumeration IN LISTS _enumerations)
        string(REGEX REPLACE "^[^{]*{" "" _body "${_enumeration}")
        string(REPLACE "}" "" _body "${_body}")
        string(REPLACE "," ";" _items "${_body}")
        foreach(_item IN LISTS _items)
            if(_item MATCHES "^[ \t\r\n]*([A-Za-z_][A-Za-z0-9_]*)")
                list(APPEND _names "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()

    # Constants: `inline constexpr <type> NAME =`.
    string(REGEX MATCHALL "inline constexpr [^=;]+=" _constants "${_content}")
    foreach(_constant IN LISTS _constants)
        if(_constant MATCHES "([A-Za-z_][A-Za-z0-9_]*)[ \t]*=$")
            list(APPEND _names "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    # Types: classes, structures and concepts declared at the start of a line, and type aliases.
    string(REGEX MATCHALL "\n[ \t]*(class|struct|concept)[ \t]+[A-Za-z_][A-Za-z0-9_]*" _types "${_content}")
    foreach(_type IN LISTS _types)
        if(_type MATCHES "([A-Za-z_][A-Za-z0-9_]*)$")
            list(APPEND _names "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    string(REGEX MATCHALL "using[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*=" _aliases "${_content}")
    foreach(_alias IN LISTS _aliases)
        if(_alias MATCHES "using[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
            list(APPEND _names "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    # Functions: `auto|void|bool name(` declarations and Allman-style out-of-class definitions
    # starting a line with `Class::name(`.
    string(REGEX MATCHALL "(auto|void|bool)[ \t]+[a-z_][a-z0-9_]*[ \t]*\\(" _declarations "${_content}")
    string(REGEX MATCHALL "\n[A-Za-z_][A-Za-z0-9_]*::[a-z_][a-z0-9_]*\\(" _definitions_found "${_content}")
    foreach(_function IN LISTS _declarations _definitions_found)
        if(_function MATCHES "([a-z_][a-z0-9_]*)[ \t]*\\($")
            list(APPEND _names "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _names)

list(LENGTH _files _file_count)
list(LENGTH _names _name_count)
list(LENGTH _macros _macro_count)
if(_name_count EQUAL 0)
    message(FATAL_ERROR "GfxCheckPlatformMacroCollisions: no name found in ${SOURCE}; the parser is out of date.")
endif()

# ------------------------------------------------------------------------------
# 3. Collisions
# ------------------------------------------------------------------------------
set(_collisions "")
foreach(_name IN LISTS _names)
    if(_name IN_LIST _macros)
        list(APPEND _collisions "${_name}")
    endif()
endforeach()

if(_collisions)
    list(JOIN _collisions ", " _collision_text)
    list(JOIN _headers ", " _header_text)
    message(FATAL_ERROR
        "Names declared in ${SOURCE} collide with platform macros: ${_collision_text}\n"
        "  Reason: <${_header_text}> define these macros, so code including them cannot spell the names.\n"
        "  Suggestion: rename them (for example MINIMUM instead of MIN, CRITICAL instead of ERROR).")
endif()
message(STATUS "Checked ${_name_count} names from ${_file_count} file(s) against ${_macro_count} platform macros: no collision.")
