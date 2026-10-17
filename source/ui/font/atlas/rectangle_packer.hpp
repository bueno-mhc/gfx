/// @file rectangle_packer.hpp
/// @brief Bridge from the tight packer (a module unit) to msdf-atlas-gen's rectangle packer
///        (rectangle_packer.cpp, a plain translation unit). It includes no standard header on purpose:
///        msdf-atlas-gen's C++ headers include the standard library's, which cannot share a translation
///        unit with `import std`.
#pragma once

namespace gfx::ui::rectangle_packing
{

/// @brief A rectangle to place; x and y stay -1 when it does not fit.
struct Rectangle
{
    int x = -1;
    int y = -1;
    int width = 0;
    int height = 0;
};

/// @brief Places as many of @p rectangles as fit in @p width × @p height (msdf-atlas-gen's guillotine
///        packer, best fit first); returns how many did not fit.
int pack(Rectangle* rectangles,
         int count,
         int width,
         int height);

} // namespace gfx::ui::rectangle_packing
