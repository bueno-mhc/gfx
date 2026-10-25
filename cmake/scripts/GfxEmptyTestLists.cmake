# Empties the CTest test lists gtest_discover_tests() wrote for one test executable, just before its POST_BUILD discovery
# writes them again. The discovery deletes the list, then appends the tests it finds; on Windows the deletion fails
# silently while another process holds the file open (VS Code's CMake Tools reading the tests, a `ctest -N`), and the
# tests are appended to the old list: every test registered twice (seen with 32 duplicated tests in PH8). Emptying the
# file works where deleting it does not (readers share it for writing). When this script fails, a reader shares the file
# not even for writing: build again. Detector: CMakeDefect.FileRemoveIgnoresHeldFiles (GfxCheckFileRemove.cmake); when it
# fails, CMake reports the failed deletion: remove this script and its POST_BUILD command in gfx_add_test().
#
#   cmake -DDIRECTORY=<the executable's binary directory> -DNAME=<the test executable's target> -P GfxEmptyTestLists.cmake
if(NOT DIRECTORY OR NOT NAME)
    message(FATAL_ERROR "give -DDIRECTORY=<binary directory> and -DNAME=<test executable target>")
endif()
# GoogleTest.cmake names the list <target>_<hash of its arguments>_tests.cmake.
file(GLOB lists "${DIRECTORY}/${NAME}_*_tests.cmake")
foreach(list IN LISTS lists)
    get_filename_component(list_name "${list}" NAME)
    if(list_name MATCHES "^${NAME}_[0-9a-f]+_tests\\.cmake$")
        file(WRITE "${list}" "")
    endif()
endforeach()
