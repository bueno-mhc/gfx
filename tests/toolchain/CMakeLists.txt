# Toolchain conformance: C++23 features, `import std`, and GoogleTest headers mixed with modules.
gfx_add_test(gfx_toolchain_tests
    SOURCES toolchain_test.cpp
    MODULES toolchain_probe.cppm)

# A CMake defect detector (it PASSES while the defect exists; see cmake/scripts/GfxCheckTestDiscovery.cmake): POST_BUILD
# test discovery scripts name no target. Workaround: the per-directory link job pools of gfx_add_test().
add_test(NAME CMakeDefect.GoogleTestDiscoveryNamesNoTarget
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${CMAKE_CURRENT_BINARY_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckTestDiscovery.cmake)

# A CMake defect detector on Windows (it PASSES while the defect exists; see cmake/scripts/GfxCheckFileRemove.cmake):
# file(REMOVE) of a file another handle holds open fails silently. Workaround: gfx_add_test() empties the test lists
# before each discovery (cmake/scripts/GfxEmptyTestLists.cmake).
if(WIN32)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/file_remove_check)
    add_test(NAME CMakeDefect.FileRemoveIgnoresHeldFiles
             COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/file_remove_check
                     -P ${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckFileRemove.cmake)
endif()

add_subdirectory(defects)
