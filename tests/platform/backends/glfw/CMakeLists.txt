# What is particular to the GLFW backend: its feature set and errors, and its input through the Win32
# messages GLFW's window procedure handles (GLFW cannot inject events).
set(GFX_GLFW_TEST_SOURCES glfw_platform_test.cpp)
if(WIN32)
    list(APPEND GFX_GLFW_TEST_SOURCES glfw_events_test.cpp)
endif()
gfx_add_test(gfx_platform_glfw_tests
    SOURCES ${GFX_GLFW_TEST_SOURCES}
    MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../backend_test_support.cppm
    MODULE_BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    LIBRARIES gfx::platform)
