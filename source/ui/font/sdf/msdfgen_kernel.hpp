/// @file msdfgen_kernel.hpp
/// @brief Bridge from the msdfgen generator (a module unit) to msdfgen (msdfgen_kernel.cpp, a plain
///        translation unit). It includes no standard header on purpose: msdfgen's C++ headers include the
///        standard library's, which cannot share a translation unit with `import std`.
#pragma once

namespace gfx::ui::msdfgen_kernel
{

/// @brief A point in font units (y up).
struct Point
{
    double x = 0;
    double y = 0;
};

/// @brief A segment: kind 0 = line (points 0 and 1), 1 = quadratic (0 to 2), 2 = cubic (0 to 3).
struct Segment
{
    unsigned char kind = 0;
    Point points[4]{};
};

/// @brief A closed contour.
struct Contour
{
    const Segment* segments = nullptr;
    unsigned long long count = 0;
};

/// @brief A glyph outline.
struct Outline
{
    const Contour* contours = nullptr;
    unsigned long long count = 0;
};

/// @brief Parameters of the field (see gfx::ui::SdfGenerationDescription).
struct Parameters
{
    int kind = 3;                ///< 0 SDF, 1 PSDF, 2 MSDF, 3 MTSDF.
    double scale = 1;            ///< Texels per font unit.
    double range = 4;            ///< Distance range, texels.
    double padding = 0;          ///< Texels around the glyph.
    double angle_threshold = 3;  ///< Radians.
    double miter_limit = 1;      ///< 0: no miters.
    int coloring = 1;            ///< 0 simple, 1 ink trap, 2 by distance.
    unsigned long long seed = 0; ///< Edge coloring seed.
    bool overlap_support = true;
    bool scanline_pass = true;
};

/// @brief A glyph box: texels and the translation (font units) applied before scaling.
struct Box
{
    int width = 0;
    int height = 0;
    double translate_x = 0;
    double translate_y = 0;
};

/// @brief The box of @p outline (empty for an empty outline); false when the outline is not closed.
bool layout(const Outline& outline,
            const Parameters& parameters,
            Box& box);

/// @brief Generates the field of @p outline in @p box into @p output: width × height texels of 1, 3 or
///        4 floats, rows from the top. False when the outline is not closed.
bool generate(const Outline& outline,
              const Parameters& parameters,
              const Box& box,
              float* output);

} // namespace gfx::ui::msdfgen_kernel
