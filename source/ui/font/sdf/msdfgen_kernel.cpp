/// @file msdfgen_kernel.cpp
/// @brief Distance fields generated with msdfgen, as msdf-atlas-gen does for a glyph (see
///        msdfgen_kernel.hpp: a plain translation unit, because msdfgen's headers include the standard
///        library's): the shape is normalized and oriented, its box wraps its bounds, half the distance
///        range, its miters and the padding, and the field's signs are corrected by a scanline pass.
#include "msdfgen_kernel.hpp"

#include <core/ShapeDistanceFinder.h>
#include <msdfgen.h>

#include <cmath>

namespace gfx::ui::msdfgen_kernel
{

namespace
{

/// The msdfgen shape of @p outline: normalized, its contours reversed when it is wound the wrong way.
/// False when a contour is not closed.
bool
make_shape(const Outline& outline,
           msdfgen::Shape& shape)
{
    // Edges of every contour.
    for (unsigned long long contour_index = 0; contour_index < outline.count; ++contour_index)
    {
        const Contour& source = outline.contours[contour_index];
        msdfgen::Contour& contour = shape.addContour();
        for (unsigned long long segment_index = 0; segment_index < source.count; ++segment_index)
        {
            const Segment& segment = source.segments[segment_index];
            const auto point = [&](int index) { return msdfgen::Point2(segment.points[index].x, segment.points[index].y); };
            switch (segment.kind)
            {
                case 0:  contour.addEdge(msdfgen::EdgeHolder(point(0), point(1))); break;
                case 1:  contour.addEdge(msdfgen::EdgeHolder(point(0), point(1), point(2))); break;
                default: contour.addEdge(msdfgen::EdgeHolder(point(0), point(1), point(2), point(3))); break;
            }
        }
    }
    if (!shape.validate())
    {
        return false;
    }
    shape.normalize();

    // A point far outside must be outside: reverse every contour otherwise (msdf-atlas-gen's GlyphGeometry).
    if (!shape.contours.empty())
    {
        const msdfgen::Shape::Bounds bounds = shape.getBounds();
        const msdfgen::Point2 outer(bounds.l - (bounds.r - bounds.l) - 1, bounds.b - (bounds.t - bounds.b) - 1);
        if (msdfgen::SimpleTrueShapeDistanceFinder::oneShotDistance(shape, outer) > 0)
        {
            for (msdfgen::Contour& contour : shape.contours)
            {
                contour.reverse();
            }
        }
    }
    return true;
}

/// Colors the edges of @p shape for a multi-channel field.
void
color_edges(msdfgen::Shape& shape,
            const Parameters& parameters)
{
    switch (parameters.coloring)
    {
        case 0:  msdfgen::edgeColoringSimple(shape, parameters.angle_threshold, parameters.seed); break;
        case 2:  msdfgen::edgeColoringByDistance(shape, parameters.angle_threshold, parameters.seed); break;
        default: msdfgen::edgeColoringInkTrap(shape, parameters.angle_threshold, parameters.seed); break;
    }
}

/// Copies @p bitmap (rows from the bottom) into @p output (rows from the top).
template<int CHANNELS>
void
copy_flipped(const msdfgen::Bitmap<float, CHANNELS>& bitmap,
             float* output)
{
    const int width = bitmap.width();
    const int height = bitmap.height();
    for (int y = 0; y < height; ++y)
    {
        const float* row = bitmap(0, height - 1 - y);
        float* destination = output + static_cast<long long>(y) * width * CHANNELS;
        for (int value = 0; value < width * CHANNELS; ++value)
        {
            destination[value] = row[value];
        }
    }
}

/// A multi-channel field (MSDF or MTSDF) with msdf-atlas-gen's passes.
template<int CHANNELS>
void
generate_multi_channel(const msdfgen::Shape& shape,
                       const Parameters& parameters,
                       const msdfgen::Projection& projection,
                       const msdfgen::Range& range,
                       msdfgen::Bitmap<float, CHANNELS>& bitmap)
{
    msdfgen::MSDFGeneratorConfig config(parameters.overlap_support, msdfgen::ErrorCorrectionConfig());
    const msdfgen::ErrorCorrectionConfig::Mode mode = config.errorCorrection.mode;
    // With a scanline pass, error correction runs after the signs are fixed.
    if (parameters.scanline_pass)
    {
        config.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::DISABLED;
    }
    if constexpr (CHANNELS == 3)
    {
        msdfgen::generateMSDF(bitmap, shape, projection, range, config);
    }
    else
    {
        msdfgen::generateMTSDF(bitmap, shape, projection, range, config);
    }
    if (parameters.scanline_pass)
    {
        msdfgen::distanceSignCorrection(bitmap, shape, projection, 0.5F, msdfgen::FILL_NONZERO);
        if (mode != msdfgen::ErrorCorrectionConfig::DISABLED)
        {
            config.errorCorrection.mode = mode;
            config.errorCorrection.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
            msdfgen::msdfErrorCorrection(bitmap, shape, projection, range, config);
        }
    }
}

} // namespace

bool
layout(const Outline& outline,
       const Parameters& parameters,
       Box& box)
{
    box = {};
    msdfgen::Shape shape;
    if (!make_shape(outline, shape))
    {
        return false;
    }
    if (shape.contours.empty())
    {
        return true;
    }
    // msdf-atlas-gen's GlyphGeometry::wrapBox, origin not aligned to texels.
    const double scale = parameters.scale;
    const msdfgen::Range range(parameters.range / scale);
    const double padding = parameters.padding / scale;
    const msdfgen::Shape::Bounds bounds = shape.getBounds();
    if (!(bounds.l < bounds.r && bounds.b < bounds.t))
    {
        return true;
    }
    double left = bounds.l + range.lower;
    double bottom = bounds.b + range.lower;
    double right = bounds.r - range.lower;
    double top = bounds.t - range.lower;
    if (parameters.miter_limit > 0)
    {
        shape.boundMiters(left, bottom, right, top, -range.lower, parameters.miter_limit, 1);
    }
    left -= padding;
    bottom -= padding;
    right += padding;
    top += padding;
    const double width = scale * (right - left);
    const double height = scale * (top - bottom);
    box.width = static_cast<int>(std::ceil(width)) + 1;
    box.height = static_cast<int>(std::ceil(height)) + 1;
    box.translate_x = -left + 0.5 * (box.width - width) / scale;
    box.translate_y = -bottom + 0.5 * (box.height - height) / scale;
    return true;
}

bool
generate(const Outline& outline,
         const Parameters& parameters,
         const Box& box,
         float* output)
{
    msdfgen::Shape shape;
    if (!make_shape(outline, shape))
    {
        return false;
    }
    const msdfgen::Projection projection(msdfgen::Vector2(parameters.scale), msdfgen::Vector2(box.translate_x, box.translate_y));
    const msdfgen::Range range(parameters.range / parameters.scale);
    const msdfgen::GeneratorConfig config(parameters.overlap_support);
    switch (parameters.kind)
    {
        case 0:
        case 1:
        {
            msdfgen::Bitmap<float, 1> bitmap(box.width, box.height);
            if (parameters.kind == 0)
            {
                msdfgen::generateSDF(bitmap, shape, projection, range, config);
            }
            else
            {
                msdfgen::generatePSDF(bitmap, shape, projection, range, config);
            }
            if (parameters.scanline_pass)
            {
                msdfgen::distanceSignCorrection(bitmap, shape, projection, 0.5F, msdfgen::FILL_NONZERO);
            }
            copy_flipped(bitmap, output);
            break;
        }
        case 2:
        {
            color_edges(shape, parameters);
            msdfgen::Bitmap<float, 3> bitmap(box.width, box.height);
            generate_multi_channel(shape, parameters, projection, range, bitmap);
            copy_flipped(bitmap, output);
            break;
        }
        default:
        {
            color_edges(shape, parameters);
            msdfgen::Bitmap<float, 4> bitmap(box.width, box.height);
            generate_multi_channel(shape, parameters, projection, range, bitmap);
            copy_flipped(bitmap, output);
            break;
        }
    }
    return true;
}

} // namespace gfx::ui::msdfgen_kernel
