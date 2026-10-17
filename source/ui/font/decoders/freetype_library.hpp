/// @file freetype_library.hpp
/// @brief Bridge from the FreeType decoder (a module unit) to FreeType (freetype_library.cpp, a plain
///        translation unit). It includes no standard header on purpose: FreeType's headers include
///        <stdio.h>, which cannot share a translation unit with `import std` (CLAUDE.md, defect 11).
#pragma once

namespace gfx::ui::freetype
{

/// @brief A FreeType library and one of its faces (defined in freetype_library.cpp).
struct Face;

/// @brief Text owned by FreeType or by a Face: first byte and size.
struct Text
{
    const char* data = nullptr;
    unsigned long long size = 0;
};

/// @brief Receives the drawing commands of an outline, in font units.
class OutlineSink
{
public:
    virtual ~OutlineSink() = default;

    virtual void move_to(double x,
                         double y) = 0;
    virtual void line_to(double x,
                         double y) = 0;
    virtual void quadratic_to(double control_x,
                              double control_y,
                              double x,
                              double y) = 0;
    virtual void cubic_to(double first_control_x,
                          double first_control_y,
                          double second_control_x,
                          double second_control_y,
                          double x,
                          double y) = 0;
};

/// @brief Metrics and properties of a face, in font units.
struct FaceInformation
{
    long glyph_count = 0;
    unsigned units_per_em = 0;
    int ascender = 0;
    int descender = 0;
    int line_gap = 0;
    int x_height = 0;
    int cap_height = 0;
    int underline_position = 0;  ///< Top of the underline.
    int underline_thickness = 0;
    Text family{};
    Text style{};
    bool is_sfnt = false;
    unsigned axis_count = 0;
    unsigned named_instance_count = 0;
    unsigned sfnt_face_index = 0;
};

/// @brief A variation axis.
struct Axis
{
    unsigned long tag = 0;
    double minimum = 0;
    double default_value = 0;
    double maximum = 0;
    Text name{};
};

/// @brief What happened to a call: success, or FreeType's error and the function that reported it.
struct Status
{
    bool success = true;
    int error = 0;
    Text function{};
    Text message{};
};

/// @brief Faces in the font of @p bytes.
Status face_count(const unsigned char* bytes,
                  unsigned long long size,
                  long& count);

/// @brief Opens face @p face_index of the font of @p bytes (which must outlive the face).
Status open(const unsigned char* bytes,
            unsigned long long size,
            long face_index,
            Face*& face);

/// @brief Closes @p face (null is ignored).
void close(Face* face);

/// @brief Metrics and properties of @p face (texts owned by the face).
FaceInformation information(const Face& face);

/// @brief Axis @p index of @p face (below FaceInformation::axis_count).
Axis axis(const Face& face,
          unsigned index);

/// @brief Name of named instance @p index (owned by the face) and its coordinates (axis_count values).
Text named_instance(Face& face,
                    unsigned index,
                    double* coordinates);

/// @brief Selects the instance at @p coordinates (one value per axis).
Status set_variation(Face& face,
                     const double* coordinates,
                     unsigned count);

/// @brief Characters the face maps to glyphs.
unsigned long long character_count(const Face& face);

/// @brief Fills @p code_points and @p glyphs with the character map, by code point (character_count() entries).
void character_map(const Face& face,
                   unsigned* code_points,
                   unsigned* glyphs);

/// @brief Advance of @p glyph in font units.
Status advance(const Face& face,
               unsigned glyph,
               double& advance);

/// @brief Sends the outline of @p glyph to @p sink; @p has_outline is false for glyphs without one
///        (bitmap-only glyphs).
Status outline(const Face& face,
               unsigned glyph,
               OutlineSink& sink,
               bool& has_outline);

/// @brief Whether the font draws @p glyph in color (COLR layers or paint).
bool has_color_layers(const Face& face,
                      unsigned glyph);

/// @brief The whole OpenType data of the face's file (decoded for WOFF and WOFF2): its size when
///        @p buffer is null, else copied into @p buffer (of @p size bytes).
Status sfnt(const Face& face,
            unsigned char* buffer,
            unsigned long long& size);

} // namespace gfx::ui::freetype
