# Runs the atlas tool twice on the same font, with one thread and with eight, and fails unless both
# atlases are the same bytes (reproducible builds).
#   cmake -DTOOL=<gfx_ui_atlas_builder> -DFONT=<font> -DOUTPUT_DIRECTORY=<directory> -P GfxCompareAtlasBuilds.cmake
foreach(threads IN ITEMS 1 8)
    execute_process(
        COMMAND "${TOOL}" --font "${FONT}" --output "${OUTPUT_DIRECTORY}/reproducible_${threads}.uiatlas"
                --characters "BASIC_LATIN LATIN_1_SUPPLEMENT GREEK CYRILLIC" --threads ${threads} --quiet
        RESULT_VARIABLE result
        ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the atlas tool failed with ${threads} thread(s): ${errors}")
    endif()
endforeach()
file(SHA256 "${OUTPUT_DIRECTORY}/reproducible_1.uiatlas" single)
file(SHA256 "${OUTPUT_DIRECTORY}/reproducible_8.uiatlas" parallel)
if(NOT single STREQUAL parallel)
    message(FATAL_ERROR "the atlases differ: ${single} with one thread, ${parallel} with eight")
endif()
message(STATUS "same atlas with 1 and 8 threads: ${single}")
