# Consumers routinely include <windows.h> next to `import gfx;`; this suite proves the two coexist.
gfx_add_test(gfx_platform_tests
    SOURCES windows_headers_test.cpp
    LIBRARIES gfx::gfx)
