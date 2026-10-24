// Fixture of PhysicsDecoupling.ForbiddenImportsAreFound (never compiled): the import check must report the platform
// library and a module whose name merely starts like an allowed one, and accept the others.
export module gfx.physics.fixture;

import std;
import gfx.core.math;
export import gfx.physics.primitives;
import :partition_of_this_module;
import gfx.platform;
import gfx.camerawork;
