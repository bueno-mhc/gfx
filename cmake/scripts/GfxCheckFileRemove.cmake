# Detector of a CMake defect on Windows (seen with CMake 4.4.0-rc2): file(REMOVE) of a file another handle holds open
# (without sharing it for deletion) fails silently, and the file stays. GoogleTestAddTests.cmake deletes a test list that
# way before appending the tests it finds, so a test list read by another process during a build registered every test
# twice. This script PASSES while the defect exists. When it fails, CMake reports failed deletions (or deletes held files):
# remove cmake/scripts/GfxEmptyTestLists.cmake, its POST_BUILD command in gfx_add_test() (cmake/GfxTesting.cmake) and the
# test that runs this script.
#
#   cmake -DDIRECTORY=<a scratch directory> -P GfxCheckFileRemove.cmake
if(NOT DIRECTORY)
    message(FATAL_ERROR "give -DDIRECTORY=<a scratch directory>")
endif()
set(held "${DIRECTORY}/gfx_held_file.txt")
file(WRITE "${held}" "held\n")

# file(LOCK) keeps the file open (shared for reading and writing, not for deletion) until RELEASE.
file(LOCK "${held}" RESULT_VARIABLE locked)
if(NOT locked EQUAL 0)
    message(FATAL_ERROR "could not lock ${held}: ${locked}")
endif()
file(REMOVE "${held}")
set(still_there FALSE)
if(EXISTS "${held}")
    set(still_there TRUE)
endif()
file(LOCK "${held}" RELEASE)
file(REMOVE "${held}")

if(NOT still_there)
    message(FATAL_ERROR "file(REMOVE) deleted a held file: CMake was fixed (or the platform allows it). Remove "
                        "GfxEmptyTestLists.cmake, its POST_BUILD command in gfx_add_test() and this detector.")
endif()
message(STATUS "file(REMOVE) left a held file in place without an error: the defect is present, the test lists are "
               "emptied before each discovery.")
