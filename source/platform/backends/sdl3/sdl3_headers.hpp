/// @file sdl3_headers.hpp
/// @brief The SDL headers the SDL3 backend uses, included in the global module fragment of each of its
///        module units (declarations of a global module fragment are not visible to importers). Only C
///        library headers come with them, which may share a module unit with `import std`.
#pragma once

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_touch.h>
#include <SDL3/SDL_video.h>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif
