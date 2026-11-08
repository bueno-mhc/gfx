# Checks that the profiler's macros compile to nothing when GFX_PROFILER_ENABLED is 0 (CTest test
# ProfilerCompileOut.MacrosLeaveNoCodeAndNoData): with_macros.cpp (every PROFILE_* macro, arguments calling an undefined
# function) and without_macros.cpp (the same function without them) are compiled with -O2; the two objects must hold the
# same instructions, and the first no string of the macros' names and no reference to the undefined function.
#   cmake -DCOMPILER=<c++> -DOBJDUMP=<objdump> -DNM=<nm> -DINCLUDE_DIRECTORY=<source/profiler/include>
#         -DSOURCE_DIRECTORY=<tests/profiler/compile_out> -DWORK_DIRECTORY=<directory> -P GfxCheckProfilerCompileOut.cmake
foreach(variable IN ITEMS COMPILER OBJDUMP NM INCLUDE_DIRECTORY SOURCE_DIRECTORY WORK_DIRECTORY)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "GfxCheckProfilerCompileOut.cmake: -D${variable}=... is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK_DIRECTORY}")

# Both objects, optimized as a Release build is.
foreach(name IN ITEMS with_macros without_macros)
    execute_process(
        COMMAND "${COMPILER}" -std=c++23 -O2 -DGFX_PROFILER_ENABLED=0 "-I${INCLUDE_DIRECTORY}" -c "${SOURCE_DIRECTORY}/${name}.cpp"
                -o "${WORK_DIRECTORY}/${name}.o"
        RESULT_VARIABLE result
        ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "compiling ${name}.cpp failed:\n${errors}")
    endif()
endforeach()

# The instructions of each object, without addresses or the file's name.
function(_gfx_instructions object result)
    execute_process(COMMAND "${OBJDUMP}" -d --no-show-raw-insn "${object}" OUTPUT_VARIABLE listing RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "objdump failed on ${object}")
    endif()
    string(REGEX REPLACE "[^\n]*file format[^\n]*\n" "" listing "${listing}")
    string(REGEX REPLACE "\n *[0-9a-f]+:" "\n" listing "${listing}")
    set(${result} "${listing}" PARENT_SCOPE)
endfunction()
_gfx_instructions("${WORK_DIRECTORY}/with_macros.o" with_listing)
_gfx_instructions("${WORK_DIRECTORY}/without_macros.o" without_listing)
if(NOT with_listing STREQUAL without_listing)
    message(FATAL_ERROR "the profiler's macros left code with GFX_PROFILER_ENABLED=0.\nWith the macros:${with_listing}\nWithout:${without_listing}")
endif()

# No symbol of the profiler, of the sites or of the undefined function; no string of the macros' names.
execute_process(COMMAND "${NM}" "${WORK_DIRECTORY}/with_macros.o" OUTPUT_VARIABLE symbols)
if(symbols MATCHES "profiler_compile_out_argument|gfx_profile|profiler::")
    message(FATAL_ERROR "the profiler's macros left symbols with GFX_PROFILER_ENABLED=0:\n${symbols}")
endif()
file(STRINGS "${WORK_DIRECTORY}/with_macros.o" names REGEX "profiler_compile_out_(scope|category|named|counter|float|memory|marker|async|gpu|dispatch_scope)")
if(names)
    message(FATAL_ERROR "the profiler's macros left their names in the object with GFX_PROFILER_ENABLED=0: ${names}")
endif()
message(STATUS "the profiler's macros compile to nothing: same instructions as without them, no symbol, no string")
