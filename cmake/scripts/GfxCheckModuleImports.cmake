# Checks that the C++ sources under a directory import only allowed gfx modules.
#   cmake -DDIRECTORY=<path> "-DALLOWED=<module prefix>;..." -P GfxCheckModuleImports.cmake
# A module is allowed when its name is an allowed prefix or starts with one followed by "." or ":" (partitions are
# always allowed). Every `import gfx...;` line of every .cppm, .cpp and .hpp file is checked; the script fails with the
# list of the offending lines.
if(NOT DIRECTORY OR NOT ALLOWED)
    message(FATAL_ERROR "GfxCheckModuleImports: give -DDIRECTORY=<path> and -DALLOWED=<prefixes>.")
endif()

file(GLOB_RECURSE sources LIST_DIRECTORIES false "${DIRECTORY}/*.cppm" "${DIRECTORY}/*.cpp" "${DIRECTORY}/*.hpp")
set(violations "")
set(checked 0)
foreach(source IN LISTS sources)
    file(STRINGS "${source}" lines REGEX "^[ \t]*(export[ \t]+)?import[ \t]+gfx[A-Za-z0-9_.]*[ \t]*;")
    foreach(line IN LISTS lines)
        math(EXPR checked "${checked} + 1")
        string(REGEX REPLACE "^[ \t]*(export[ \t]+)?import[ \t]+(gfx[A-Za-z0-9_.]*)[ \t]*;.*$" "\\2" module "${line}")
        set(allowed_module FALSE)
        foreach(prefix IN LISTS ALLOWED)
            string(LENGTH "${prefix}" prefix_length)
            string(SUBSTRING "${module}" 0 ${prefix_length} head)
            string(SUBSTRING "${module}." ${prefix_length} 1 next)
            if(head STREQUAL prefix AND (next STREQUAL "." OR next STREQUAL ":"))
                set(allowed_module TRUE)
            endif()
        endforeach()
        if(NOT allowed_module)
            file(RELATIVE_PATH relative "${DIRECTORY}" "${source}")
            list(APPEND violations "  ${relative} imports ${module}")
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n" listed)
    message(FATAL_ERROR "gfx: sources under ${DIRECTORY} import modules they must not depend on (allowed: ${ALLOWED}):\n${listed}")
endif()
message(STATUS "gfx: ${checked} imports of gfx modules checked under ${DIRECTORY}: all allowed (${ALLOWED})")
