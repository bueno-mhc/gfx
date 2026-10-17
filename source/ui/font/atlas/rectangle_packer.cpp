/// @file rectangle_packer.cpp
/// @brief msdf-atlas-gen's rectangle packer behind rectangle_packer.hpp (a plain translation unit).
#include "rectangle_packer.hpp"

#include <msdf-atlas-gen/RectanglePacker.h>

#include <vector>

namespace gfx::ui::rectangle_packing
{

int
pack(Rectangle* rectangles,
     int count,
     int width,
     int height)
{
    // Unplaced rectangles keep x = y = -1.
    std::vector<msdf_atlas::Rectangle> packed(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        packed[static_cast<std::size_t>(index)] = {-1, -1, rectangles[index].width, rectangles[index].height};
    }
    const int remaining = msdf_atlas::RectanglePacker(width, height).pack(packed.data(), count);
    for (int index = 0; index < count; ++index)
    {
        rectangles[index].x = packed[static_cast<std::size_t>(index)].x;
        rectangles[index].y = packed[static_cast<std::size_t>(index)].y;
    }
    return remaining;
}

} // namespace gfx::ui::rectangle_packing
