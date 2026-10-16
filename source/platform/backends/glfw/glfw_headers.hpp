/// @file glfw_headers.hpp
/// @brief The GLFW header the GLFW backend uses, included in the global module fragment of each of its
///        module units. GLFW_INCLUDE_NONE keeps OpenGL headers out (the backend creates windows without
///        a graphics context); only <stddef.h> and <stdint.h> come with it.
#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
