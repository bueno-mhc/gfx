// Defect detector: plain <windows.h> followed by `import std` conflicts on MinGW GCC 16.
#include <windows.h>

import std;

auto main() -> int {
    return static_cast<int>(std::string_view{"defect"}.size()) - 6 + ERROR;
}
