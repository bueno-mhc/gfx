# Detector of a CMake defect (seen with CMake 4.4.0-rc2): the POST_BUILD discovery scripts gtest_discover_tests() writes
# name no target (no TEST_TARGET argument), so GoogleTestAddTests.cmake hashes an empty name and every test executable of
# a directory lists its tests into the same JSON file (cmake_test_discovery_e3b0c44298.json, after SHA-256("")):
# executables linked at once overwrite each other's listing ("string sub-command JSON failed parsing").
# This script PASSES while the defect exists. When it fails, CMake was fixed: remove the per-directory job pools of
# gfx_add_test() (cmake/GfxTesting.cmake) and the test that runs this script.
#
#   cmake -DDIRECTORY=<a binary directory holding discovery scripts> -P GfxCheckTestDiscovery.cmake
if(NOT DIRECTORY)
    message(FATAL_ERROR "give -DDIRECTORY=<a binary directory with *_discovery.cmake scripts>")
endif()
file(GLOB scripts "${DIRECTORY}/*_discovery.cmake")
if(NOT scripts)
    message(FATAL_ERROR "no discovery script in ${DIRECTORY}")
endif()
foreach(script IN LISTS scripts)
    file(READ "${script}" content)
    if(content MATCHES "TEST_TARGET")
        message(FATAL_ERROR "${script} names its target: CMake was fixed. Remove the job pools of gfx_add_test() "
                            "(cmake/GfxTesting.cmake) and this detector.")
    endif()
endforeach()
message(STATUS "The discovery scripts name no target: the defect is present, the job pools are needed.")
