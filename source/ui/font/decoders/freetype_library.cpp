/// @file freetype_library.cpp
/// @brief The calls of the FreeType decoder into FreeType (see freetype_library.hpp: a plain translation
///        unit, because FreeType's headers include <stdio.h>).
#include "freetype_library.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_COLOR_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace gfx::ui::freetype
{

struct Face
{
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    FT_MM_Var* variation = nullptr;
    std::vector<std::string> instance_names{};
};

namespace
{

/// The status of a FreeType call.
Status
status_of(FT_Error error,
          const char* function)
{
    if (error == 0)
    {
        return {};
    }
    const char* message = FT_Error_String(error);
    if (message == nullptr)
    {
        message = "unknown FreeType error";
    }
    return {.success = false,
            .error = error,
            .function = {function, std::strlen(function)},
            .message = {message, std::strlen(message)}};
}

/// Text of a C string (empty for null).
Text
text_of(const char* text)
{
    return text == nullptr ? Text{} : Text{text, std::strlen(text)};
}

/// UTF-8 of UTF-16BE bytes (unpaired surrogates become U+FFFD).
std::string
utf8_of_utf16_big_endian(const FT_Byte* bytes,
                         FT_UInt size)
{
    std::string text;
    for (FT_UInt index = 0; index + 1 < size; index += 2)
    {
        char32_t code_point = static_cast<char32_t>((bytes[index] << 8) | bytes[index + 1]);
        // A surrogate pair makes one code point.
        if (code_point >= 0xD800 && code_point < 0xDC00 && index + 3 < size)
        {
            const char32_t low = static_cast<char32_t>((bytes[index + 2] << 8) | bytes[index + 3]);
            if (low >= 0xDC00 && low < 0xE000)
            {
                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                index += 2;
            }
        }
        if (code_point >= 0xD800 && code_point < 0xE000)
        {
            code_point = 0xFFFD;
        }
        // Encode it.
        if (code_point < 0x80)
        {
            text += static_cast<char>(code_point);
        }
        else if (code_point < 0x800)
        {
            text += static_cast<char>(0xC0 | (code_point >> 6));
            text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
        else if (code_point < 0x10000)
        {
            text += static_cast<char>(0xE0 | (code_point >> 12));
            text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
        else
        {
            text += static_cast<char>(0xF0 | (code_point >> 18));
            text += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
            text += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code_point & 0x3F));
        }
    }
    return text;
}

/// The English name of name ID @p name_id (Windows Unicode, else Macintosh Roman read as ASCII).
std::string
sfnt_name(FT_Face face,
          FT_UInt name_id)
{
    std::string macintosh;
    const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
    for (FT_UInt index = 0; index < count; ++index)
    {
        FT_SfntName name{};
        if (FT_Get_Sfnt_Name(face, index, &name) != 0 || name.name_id != name_id)
        {
            continue;
        }
        if (name.platform_id == TT_PLATFORM_MICROSOFT && name.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES)
        {
            return utf8_of_utf16_big_endian(name.string, name.string_len);
        }
        if (name.platform_id == TT_PLATFORM_MACINTOSH && macintosh.empty())
        {
            macintosh.assign(reinterpret_cast<const char*>(name.string), name.string_len);
        }
    }
    return macintosh;
}

/// Forwards FreeType's outline decomposition to an OutlineSink (coordinates in font units).
struct Decomposition
{
    OutlineSink* sink = nullptr;

    static int move_to(const FT_Vector* to, void* user)
    {
        static_cast<Decomposition*>(user)->sink->move_to(static_cast<double>(to->x), static_cast<double>(to->y));
        return 0;
    }

    static int line_to(const FT_Vector* to, void* user)
    {
        static_cast<Decomposition*>(user)->sink->line_to(static_cast<double>(to->x), static_cast<double>(to->y));
        return 0;
    }

    static int conic_to(const FT_Vector* control, const FT_Vector* to, void* user)
    {
        static_cast<Decomposition*>(user)->sink->quadratic_to(static_cast<double>(control->x), static_cast<double>(control->y),
                                                               static_cast<double>(to->x), static_cast<double>(to->y));
        return 0;
    }

    static int cubic_to(const FT_Vector* first, const FT_Vector* second, const FT_Vector* to, void* user)
    {
        static_cast<Decomposition*>(user)->sink->cubic_to(static_cast<double>(first->x), static_cast<double>(first->y),
                                                           static_cast<double>(second->x), static_cast<double>(second->y),
                                                           static_cast<double>(to->x), static_cast<double>(to->y));
        return 0;
    }
};

/// Flags of every glyph load: font units, no hinting, outlines only.
constexpr FT_Int32 LOAD_FLAGS = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;

} // namespace

Status
face_count(const unsigned char* bytes,
           unsigned long long size,
           long& count)
{
    // Face -1 only reads how many faces the font has.
    Face* face = nullptr;
    const Status status = open(bytes, size, -1, face);
    if (status.success)
    {
        count = face->face->num_faces;
        close(face);
    }
    return status;
}

Status
open(const unsigned char* bytes,
     unsigned long long size,
     long face_index,
     Face*& face)
{
    // One library per face: faces of different libraries can be used on different threads.
    face = nullptr;
    Face opened;
    FT_Error error = FT_Init_FreeType(&opened.library);
    if (error != 0)
    {
        return status_of(error, "FT_Init_FreeType");
    }
    error = FT_New_Memory_Face(opened.library, bytes, static_cast<FT_Long>(size), face_index, &opened.face);
    if (error != 0)
    {
        FT_Done_FreeType(opened.library);
        return status_of(error, "FT_New_Memory_Face");
    }
    // Unicode character map, and the variation description of a variable font.
    FT_Select_Charmap(opened.face, FT_ENCODING_UNICODE);
    if (face_index >= 0 && FT_HAS_MULTIPLE_MASTERS(opened.face))
    {
        error = FT_Get_MM_Var(opened.face, &opened.variation);
        if (error != 0)
        {
            FT_Done_Face(opened.face);
            FT_Done_FreeType(opened.library);
            return status_of(error, "FT_Get_MM_Var");
        }
        opened.instance_names.resize(opened.variation->num_namedstyles);
    }
    face = new Face{std::move(opened)};
    return {};
}

void
close(Face* face)
{
    if (face == nullptr)
    {
        return;
    }
    if (face->variation != nullptr)
    {
        FT_Done_MM_Var(face->library, face->variation);
    }
    FT_Done_Face(face->face);
    FT_Done_FreeType(face->library);
    delete face;
}

FaceInformation
information(const Face& face)
{
    const FT_Face ft = face.face;
    FaceInformation result{.glyph_count = ft->num_glyphs,
                           .units_per_em = ft->units_per_EM,
                           .ascender = ft->ascender,
                           .descender = ft->descender,
                           .line_gap = ft->height - ft->ascender + ft->descender,
                           .underline_position = ft->underline_position + ft->underline_thickness / 2,
                           .underline_thickness = ft->underline_thickness,
                           .family = text_of(ft->family_name),
                           .style = text_of(ft->style_name),
                           .is_sfnt = FT_IS_SFNT(ft),
                           .axis_count = face.variation != nullptr ? face.variation->num_axis : 0,
                           .named_instance_count = face.variation != nullptr ? face.variation->num_namedstyles : 0,
                           .sfnt_face_index = static_cast<unsigned>(ft->face_index & 0xFFFF)};
    // OS/2: x-height, cap height, and the typographic metrics when the font asks for them (USE_TYPO_METRICS).
    if (const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(ft, FT_SFNT_OS2)); os2 != nullptr && os2->version != 0xFFFF)
    {
        if (os2->version >= 2)
        {
            result.x_height = os2->sxHeight;
            result.cap_height = os2->sCapHeight;
        }
        if ((os2->fsSelection & (1U << 7)) != 0)
        {
            result.ascender = os2->sTypoAscender;
            result.descender = os2->sTypoDescender;
            result.line_gap = os2->sTypoLineGap;
        }
    }
    return result;
}

Axis
axis(const Face& face,
     unsigned index)
{
    const FT_Var_Axis& axis = face.variation->axis[index];
    return {.tag = axis.tag,
            .minimum = static_cast<double>(axis.minimum) / 65536.0,
            .default_value = static_cast<double>(axis.def) / 65536.0,
            .maximum = static_cast<double>(axis.maximum) / 65536.0,
            .name = text_of(axis.name)};
}

Text
named_instance(Face& face,
               unsigned index,
               double* coordinates)
{
    const FT_Var_Named_Style& style = face.variation->namedstyle[index];
    for (FT_UInt axis = 0; axis < face.variation->num_axis; ++axis)
    {
        coordinates[axis] = static_cast<double>(style.coords[axis]) / 65536.0;
    }
    std::string& name = face.instance_names[index];
    if (name.empty())
    {
        name = sfnt_name(face.face, style.strid);
    }
    return {name.data(), name.size()};
}

Status
set_variation(Face& face,
              const double* coordinates,
              unsigned count)
{
    std::vector<FT_Fixed> fixed(count);
    for (unsigned axis = 0; axis < count; ++axis)
    {
        fixed[axis] = static_cast<FT_Fixed>(std::lround(coordinates[axis] * 65536.0));
    }
    return status_of(FT_Set_Var_Design_Coordinates(face.face, count, fixed.data()), "FT_Set_Var_Design_Coordinates");
}

unsigned long long
character_count(const Face& face)
{
    unsigned long long count = 0;
    FT_UInt glyph = 0;
    for (FT_ULong code_point = FT_Get_First_Char(face.face, &glyph); glyph != 0; code_point = FT_Get_Next_Char(face.face, code_point, &glyph))
    {
        ++count;
    }
    return count;
}

void
character_map(const Face& face,
              unsigned* code_points,
              unsigned* glyphs)
{
    // FreeType walks the map by ascending code point.
    FT_UInt glyph = 0;
    unsigned long long index = 0;
    for (FT_ULong code_point = FT_Get_First_Char(face.face, &glyph); glyph != 0; code_point = FT_Get_Next_Char(face.face, code_point, &glyph))
    {
        code_points[index] = static_cast<unsigned>(code_point);
        glyphs[index] = glyph;
        ++index;
    }
}

Status
advance(const Face& face,
        unsigned glyph,
        double& advance)
{
    FT_Fixed value = 0;
    const FT_Error error = FT_Get_Advance(face.face, glyph, LOAD_FLAGS, &value);
    advance = static_cast<double>(value);
    return status_of(error, "FT_Get_Advance");
}

Status
outline(const Face& face,
        unsigned glyph,
        OutlineSink& sink,
        bool& has_outline)
{
    has_outline = false;
    const FT_Error error = FT_Load_Glyph(face.face, glyph, LOAD_FLAGS);
    if (error != 0)
    {
        // A bitmap-only glyph has nothing to load without bitmaps.
        if (FT_HAS_FIXED_SIZES(face.face))
        {
            return {};
        }
        return status_of(error, "FT_Load_Glyph");
    }
    if (face.face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    {
        return {};
    }
    has_outline = true;
    Decomposition decomposition{.sink = &sink};
    const FT_Outline_Funcs functions{.move_to = &Decomposition::move_to,
                                     .line_to = &Decomposition::line_to,
                                     .conic_to = &Decomposition::conic_to,
                                     .cubic_to = &Decomposition::cubic_to,
                                     .shift = 0,
                                     .delta = 0};
    return status_of(FT_Outline_Decompose(&face.face->glyph->outline, &functions, &decomposition), "FT_Outline_Decompose");
}

bool
has_color_layers(const Face& face,
                 unsigned glyph)
{
    if (!FT_HAS_COLOR(face.face))
    {
        return false;
    }
    // COLR version 0 layers, or a version 1 paint graph.
    FT_UInt layer_glyph = 0;
    FT_UInt color_index = 0;
    FT_LayerIterator iterator{};
    if (FT_Get_Color_Glyph_Layer(face.face, glyph, &layer_glyph, &color_index, &iterator) != 0)
    {
        return true;
    }
    FT_OpaquePaint paint{};
    return FT_Get_Color_Glyph_Paint(face.face, glyph, FT_COLOR_INCLUDE_ROOT_TRANSFORM, &paint) != 0;
}

Status
sfnt(const Face& face,
     unsigned char* buffer,
     unsigned long long& size)
{
    // Tag 0 loads the whole file FreeType reads from: the font itself, or the tables it decoded from WOFF.
    FT_ULong length = buffer == nullptr ? 0 : static_cast<FT_ULong>(size);
    const FT_Error error = FT_Load_Sfnt_Table(face.face, 0, 0, buffer, &length);
    size = length;
    return status_of(error, "FT_Load_Sfnt_Table");
}

} // namespace gfx::ui::freetype
