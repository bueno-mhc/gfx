/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx UI library (widgets, fonts, atlases). No other file
///        of the UI library defines either; the enumeration machinery (traits, verification, bitmask
///        operators, spelling and parsing) comes from gfx.core.enums, which this module re-exports.
///
/// Conventions: enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform
/// headers define as macros are avoided, which a CTest test checks. Enumerations stored in `.uiatlas`
/// files (AtlasSectionKind, SdfKind, AtlasTexelFormat, AtlasCompression, AtlasPackerKind, FontDecoderKind,
/// SdfGeneratorKind, EdgeColoring, AtlasGlyphFlag) only ever gain enumerators at their end: their values
/// are part of the format (docs/ui-atlas-format.md).
export module gfx.ui.enums;

import std;
export import gfx.core.enums;

export namespace gfx::ui
{

// The bitmask operators and helpers of gfx, found by argument-dependent lookup for the bitmask
// enumerations of this namespace too.
using gfx::operator|;
using gfx::operator&;
using gfx::operator^;
using gfx::operator~;
using gfx::operator|=;
using gfx::operator&=;
using gfx::operator^=;
using gfx::has_all;
using gfx::has_any;
using gfx::is_empty;
using gfx::to_string;

// =================================================================================================
// Enumerations
// =================================================================================================

/// @brief The part of the UI library an error comes from: errors name it as their backend
///        ("[gfx::ui::FONT] FontAtlas::load: ...").
enum class UiSubsystem : std::uint8_t
{
    GRAPHICS,      ///< Rendering through the graphics abstraction (pipelines, images, buffers).
    FONT,          ///< Font sources, decoders, distance fields, atlases, shaping and layout.
    INPUT,         ///< Input from the platform library: keys, mouse, text input, navigation.
    PLATFORM,      ///< Platform services: clipboard, cursors, input method placement.
    ACCESSIBILITY, ///< Accessibility trees and screen-reader providers.
    WIDGETS,       ///< Contexts, windows, widgets, layouts and docking.
};

/// @brief Container format of a font file.
enum class FontFormat : std::uint8_t
{
    TRUETYPE,      ///< OpenType with TrueType outlines (.ttf).
    OPENTYPE_CFF,  ///< OpenType with CFF outlines (.otf).
    OPENTYPE_CFF2, ///< OpenType with CFF2 outlines (variable .otf).
    COLLECTION,    ///< Several faces in one file (.ttc, .otc).
    WOFF,          ///< Web Open Font Format 1 (zlib-compressed tables).
    WOFF2,         ///< Web Open Font Format 2 (Brotli-compressed, transformed tables).
    CUSTOM,        ///< A format a custom FontDecoder reads.
};

/// @brief Library that reads font outlines at atlas build time.
enum class FontDecoderKind : std::uint8_t
{
    FREETYPE,     ///< FreeType: every format of FontFormat but CUSTOM, variable fonts.
    STB_TRUETYPE, ///< stb_truetype: TrueType, OpenType CFF and collections.
    CUSTOM,       ///< A FontDecoder of the application.
};

/// @brief What the texels of an atlas page store (the distance field kind).
enum class SdfKind : std::uint8_t
{
    SDF,   ///< One channel: true signed distance (soft effects, rounded corners).
    PSDF,  ///< One channel: pseudo signed distance (hard effects).
    MSDF,  ///< Three channels: multi-channel distance (sharp corners).
    MTSDF, ///< Four channels: MSDF in RGB and the true distance in alpha (sharp corners and soft effects).
};

/// @brief Texel format of an atlas page.
enum class AtlasTexelFormat : std::uint8_t
{
    R8_UNORM,            ///< One 8-bit channel (SDF, PSDF).
    R8G8B8A8_UNORM,      ///< Four 8-bit channels (MSDF with alpha unused, MTSDF).
    R16_SFLOAT,          ///< One 16-bit floating-point channel (SDF, PSDF; wide distance ranges).
    R16G16B16A16_SFLOAT, ///< Four 16-bit floating-point channels (MSDF, MTSDF; wide distance ranges).
};

/// @brief Compression of a section of an atlas.
enum class AtlasCompression : std::uint8_t
{
    NONE, ///< Stored as is.
    ZSTD, ///< zstd frame.
};

/// @brief Section of a `.uiatlas` blob (docs/ui-design.md A7).
enum class AtlasSectionKind : std::uint8_t
{
    FACE_METRICS,  ///< Units per em, ascender, descender, line gap, x-height, cap height, underline, names, axes.
    GLYPHS,        ///< Per glyph: advance, plane bounds, atlas bounds, page, flags.
    CHARACTER_MAP, ///< Sorted code point to glyph pairs.
    KERNING,       ///< Sorted glyph pair adjustments.
    LIGATURES,     ///< Standard ligatures, for the basic shaper.
    SHAPING_FONT,  ///< The subset font whose OpenType tables HarfBuzz shapes with.
    PIXELS,        ///< Atlas pages: dimensions, texel format, distance range, texels.
    DESCRIPTOR,    ///< The parameters the atlas was built with.
};

/// @brief How glyphs are placed in an atlas.
enum class AtlasPackerKind : std::uint8_t
{
    TIGHT, ///< Rectangles packed tightly (smallest atlas).
    GRID,  ///< Uniform cells (simple addressing, larger atlas).
    CUSTOM, ///< An AtlasPacker of the application.
};

/// @brief Direction in which text runs.
enum class TextDirection : std::uint8_t
{
    LEFT_TO_RIGHT, ///< Latin, Cyrillic, Greek, CJK, ...
    RIGHT_TO_LEFT, ///< Arabic, Hebrew, ...
};

/// @brief A named set of characters an atlas can include: a Unicode block (CHARACTER_SET_RANGES gives
///        its code points). Atlas descriptions name them ("BASIC_LATIN LATIN_1_SUPPLEMENT ARROWS").
enum class CharacterSet : std::uint8_t
{
    BASIC_LATIN,            ///< U+0020 to U+007E: printable ASCII.
    LATIN_1_SUPPLEMENT,     ///< U+00A0 to U+00FF: Western European letters and symbols.
    LATIN_EXTENDED_A,       ///< U+0100 to U+017F: Central and Eastern European letters.
    LATIN_EXTENDED_B,       ///< U+0180 to U+024F.
    GREEK,                  ///< U+0370 to U+03FF: Greek and Coptic.
    CYRILLIC,               ///< U+0400 to U+04FF.
    HEBREW,                 ///< U+0590 to U+05FF.
    ARABIC,                 ///< U+0600 to U+06FF.
    GENERAL_PUNCTUATION,    ///< U+2000 to U+206F: dashes, quotation marks, ellipsis, spaces.
    CURRENCY_SYMBOLS,       ///< U+20A0 to U+20CF.
    LETTERLIKE_SYMBOLS,     ///< U+2100 to U+214F.
    ARROWS,                 ///< U+2190 to U+21FF.
    MATHEMATICAL_OPERATORS, ///< U+2200 to U+22FF.
    BOX_DRAWING,            ///< U+2500 to U+257F.
    GEOMETRIC_SHAPES,       ///< U+25A0 to U+25FF.
};

/// @brief Kind of a segment of a glyph outline.
enum class OutlineSegmentKind : std::uint8_t
{
    LINE,      ///< A straight line: two points.
    QUADRATIC, ///< A quadratic Bezier curve (TrueType): start, control and end points.
    CUBIC,     ///< A cubic Bezier curve (CFF, CFF2): start, two control points and end point.
};

/// @brief Algorithm that generates the distance fields of an atlas at build time.
enum class SdfGeneratorKind : std::uint8_t
{
    MSDFGEN,  ///< msdfgen: SDF, PSDF, MSDF and MTSDF.
    BUILT_IN, ///< gfx's own exact-distance kernel: SDF only, builds without msdfgen.
    CUSTOM,   ///< An SdfGenerator of the application.
};

/// @brief How msdfgen assigns channels to the edges of a glyph before generating an MSDF or MTSDF.
enum class EdgeColoring : std::uint8_t
{
    SIMPLE,   ///< msdfgen's simple coloring: fastest.
    INK_TRAP, ///< Keeps the corners of ink traps sharp (msdf-atlas-gen's default).
    DISTANCE, ///< Colors by distance between edges: best quality, slowest.
};

/// @brief Properties of a glyph of an atlas (stored in `.uiatlas` files).
enum class AtlasGlyphFlag : std::uint16_t
{
    NONE         = 0,       ///< No bit.
    WHITESPACE   = 1U << 0, ///< No outline (a space): advances only, has no box in the atlas.
    COLOR_LAYERS = 1U << 1, ///< The font draws it in color (COLR); the atlas holds its monochrome outline.
    NO_OUTLINE   = 1U << 2, ///< The font has no outline for it (bitmap-only glyph): drawn as nothing.
};

/// @brief How much of a `.uiatlas` blob AtlasView::parse() verifies.
enum class AtlasVerification : std::uint8_t
{
    STRUCTURE, ///< Header, section table, bounds, alignment and the tables' ordering.
    CHECKSUMS, ///< STRUCTURE and every xxHash64 checksum (the whole blob is read).
};

/// @brief Problem AtlasView::parse() found in a `.uiatlas` blob.
enum class AtlasProblem : std::uint8_t
{
    TRUNCATED,                 ///< The blob is shorter than its header or than the size its header gives.
    BAD_MAGIC,                 ///< The blob does not start with UI_ATLAS_MAGIC: not an atlas.
    UNSUPPORTED_VERSION,       ///< The major version is not UI_ATLAS_VERSION_MAJOR.
    WRONG_ENDIANNESS,          ///< The endianness tag reads differently: written big-endian.
    SIZE_MISMATCH,             ///< The size in the header is not the size of the blob.
    CHECKSUM_MISMATCH,         ///< The checksum of the header does not match the blob.
    SECTION_OUT_OF_BOUNDS,     ///< A section (or the section table) extends past the end of the blob.
    MISALIGNED_SECTION,        ///< A section does not start at a multiple of UI_ATLAS_ALIGNMENT.
    SECTION_CHECKSUM_MISMATCH, ///< The checksum of a section does not match its bytes.
    UNKNOWN_COMPRESSION,       ///< A section names a compression this reader does not know.
    MISSING_SECTION,           ///< A required section is absent.
    DUPLICATE_SECTION,         ///< A section that may appear once appears several times.
    MALFORMED_SECTION,         ///< A section's content is inconsistent (sizes, counts, ordering, compression).
};

/// @brief Base direction of a paragraph of text.
enum class ParagraphDirection : std::uint8_t
{
    AUTOMATIC,     ///< From the first strong character (UAX #9 rules P2 and P3); left to right when there is none.
    LEFT_TO_RIGHT, ///< Left to right, whatever the text.
    RIGHT_TO_LEFT, ///< Right to left, whatever the text.
};

/// @brief Horizontal alignment of the lines of a text in its width.
enum class TextAlignment : std::uint8_t
{
    START,  ///< Where the paragraph's direction starts: left for left-to-right paragraphs, right otherwise.
    END,    ///< Where it ends.
    LEFT,   ///< Left, whatever the direction.
    CENTER, ///< Centered.
    RIGHT,  ///< Right, whatever the direction.
};

/// @brief Which shaper turns runs of characters into glyphs.
enum class TextShaping : std::uint8_t
{
    AUTOMATIC, ///< The basic shaper for left-to-right Latin, Greek, Cyrillic and common characters; HarfBuzz otherwise.
    BASIC,     ///< The basic shaper (character map, standard ligatures, pair kerning) for every run.
    HARFBUZZ,  ///< HarfBuzz for every run (needs the atlas's shaping font).
};

/// @brief What went wrong while laying a text out (the layout is still made: these are reported).
enum class TextLayoutProblem : std::uint8_t
{
    NONE            = 0,       ///< No bit.
    UNKNOWN_FONT    = 1U << 0, ///< The font handle names no font: the layout is empty.
    INVALID_UTF8    = 1U << 1, ///< Bytes that are not UTF-8 were laid out as U+FFFD.
    MISSING_GLYPHS  = 1U << 2, ///< Characters no atlas of the font's chain has were drawn as `.notdef`.
    NO_SHAPING_FONT = 1U << 3, ///< A run needed HarfBuzz but its atlas has no shaping font: the basic shaper shaped it.
    CACHE_FULL      = 1U << 4, ///< Every cache entry was in use this frame: the layout lives until the next frame only.
};

/// @brief What a draw command samples.
enum class DrawTextureKind : std::uint8_t
{
    NONE,      ///< Nothing (shapes, solid triangles): compatible with any texture.
    FONT_PAGE, ///< A page of a font atlas, uploaded by the renderer.
    IMAGE,     ///< An image of the application, named by its bindless sampled-image index.
};

/// @brief How the UI shader draws a vertex's primitive (bits 0 to 7 of UiVertex::mode; the values are the
///        shader's).
enum class UiShaderMode : std::uint8_t
{
    SOLID,        ///< The vertex color (triangles, with alpha for anti-aliased fringes).
    SHAPE,        ///< A rounded box evaluated as a signed distance: rectangles, borders, circles, lines.
    GLYPH,        ///< A glyph of a distance-field atlas, with its outline.
    GLYPH_SHADOW, ///< A glyph's shadow or glow: its true distance, softened.
    IMAGE,        ///< A texture, tinted by the vertex color.
};

// ----------------------------------------------------------------- immediate mode (U4)

/// @brief Colors of a theme (Theme::colors): one per element the UI draws.
enum class StyleColor : std::uint8_t
{
    TEXT_NORMAL,                         ///< Text (TEXT is a <windows.h> macro).
    TEXT_DISABLED,                       ///< Text of disabled items.
    WINDOW_BACKGROUND,                   ///< Background of windows.
    CHILD_BACKGROUND,                    ///< Background of child windows.
    POPUP_BACKGROUND,                    ///< Background of popups, menus and tooltips.
    BORDER,                              ///< Borders of windows and frames.
    BORDER_SHADOW,                       ///< Shadow drawn under borders.
    FRAME_BACKGROUND,                    ///< Background of checkboxes, radio buttons, sliders and text fields.
    FRAME_BACKGROUND_HOVERED,            ///< Frame background under the mouse.
    FRAME_BACKGROUND_ACTIVE,             ///< Frame background while it is held.
    TITLE_BACKGROUND,                    ///< Title bar of an unfocused window.
    TITLE_BACKGROUND_ACTIVE,             ///< Title bar of the focused window.
    TITLE_BACKGROUND_COLLAPSED,          ///< Title bar of a collapsed window.
    MENU_BAR_BACKGROUND,                 ///< Menu bars.
    SCROLLBAR_BACKGROUND,                ///< Scrollbar tracks.
    SCROLLBAR_GRAB,                      ///< Scrollbar grabs.
    SCROLLBAR_GRAB_HOVERED,              ///< Scrollbar grab under the mouse.
    SCROLLBAR_GRAB_ACTIVE,               ///< Scrollbar grab while it is dragged.
    CHECK_MARK,                          ///< Check marks, radio dots and switch knobs.
    SLIDER_GRAB,                         ///< Slider grabs.
    SLIDER_GRAB_ACTIVE,                  ///< Slider grab while it is dragged.
    BUTTON,                              ///< Buttons.
    BUTTON_HOVERED,                      ///< Button under the mouse.
    BUTTON_ACTIVE,                       ///< Button while it is held.
    HEADER,                              ///< Selected selectables, tree nodes and collapsing headers.
    HEADER_HOVERED,                      ///< Header under the mouse.
    HEADER_ACTIVE,                       ///< Header while it is held.
    SEPARATOR,                           ///< Separators.
    SEPARATOR_HOVERED,                   ///< Separator (splitter) under the mouse.
    SEPARATOR_ACTIVE,                    ///< Separator (splitter) while it is dragged.
    RESIZE_GRIP,                         ///< Resize grip of windows.
    RESIZE_GRIP_HOVERED,                 ///< Resize grip under the mouse.
    RESIZE_GRIP_ACTIVE,                  ///< Resize grip while it is dragged.
    INPUT_TEXT_CURSOR,                   ///< Text cursor of text fields.
    TAB,                                 ///< Tabs.
    TAB_HOVERED,                         ///< Tab under the mouse.
    TAB_SELECTED,                        ///< Selected tab.
    TAB_SELECTED_OVERLINE,               ///< Line over the selected tab.
    TAB_DIMMED,                          ///< Tab of an unfocused tab bar.
    TAB_DIMMED_SELECTED,                 ///< Selected tab of an unfocused tab bar.
    DOCKING_PREVIEW,                     ///< Where a dragged window would dock.
    DOCKING_EMPTY_BACKGROUND,            ///< Empty dock nodes.
    PLOT_LINES,                          ///< Line plots.
    PLOT_LINES_HOVERED,                  ///< Line plot point under the mouse.
    PLOT_HISTOGRAM,                      ///< Histograms and progress bars.
    PLOT_HISTOGRAM_HOVERED,              ///< Histogram bar under the mouse.
    TABLE_HEADER_BACKGROUND,             ///< Table header rows.
    TABLE_BORDER_STRONG,                 ///< Outer table borders and header borders.
    TABLE_BORDER_LIGHT,                  ///< Inner table borders.
    TABLE_ROW_BACKGROUND,                ///< Even table rows.
    TABLE_ROW_BACKGROUND_ALTERNATE,      ///< Odd table rows.
    TEXT_LINK,                           ///< Links.
    TEXT_SELECTED_BACKGROUND,            ///< Selected text.
    DRAG_DROP_TARGET,                    ///< Highlight of a drop target.
    NAVIGATION_HIGHLIGHT,                ///< Rectangle around the item that has the keyboard focus.
    NAVIGATION_WINDOWING_HIGHLIGHT,      ///< Window being selected by keyboard window switching.
    NAVIGATION_WINDOWING_DIM_BACKGROUND, ///< Behind windows while switching windows by keyboard.
    MODAL_WINDOW_DIM_BACKGROUND,         ///< Behind a modal window.
};

/// @brief Style variables push_style_variable() can change (a float or a Vector2 member of Style each).
enum class StyleVariable : std::uint8_t
{
    ALPHA,                      ///< float: opacity of everything.
    DISABLED_ALPHA,             ///< float: opacity of disabled items, times ALPHA.
    WINDOW_PADDING,             ///< Vector2: space between a window's edge and its content.
    WINDOW_ROUNDING,            ///< float: radius of window corners.
    WINDOW_BORDER_SIZE,         ///< float: width of window borders.
    WINDOW_MINIMUM_SIZE,        ///< Vector2: smallest window size.
    WINDOW_TITLE_ALIGN,         ///< Vector2: alignment of the title in its bar (0 to 1).
    CHILD_ROUNDING,             ///< float: radius of child window corners.
    CHILD_BORDER_SIZE,          ///< float: width of child window borders.
    POPUP_ROUNDING,             ///< float: radius of popup corners.
    POPUP_BORDER_SIZE,          ///< float: width of popup borders.
    FRAME_PADDING,              ///< Vector2: space between a frame's edge and its content.
    FRAME_ROUNDING,             ///< float: radius of frame corners.
    FRAME_BORDER_SIZE,          ///< float: width of frame borders.
    ITEM_SPACING,               ///< Vector2: space between items.
    ITEM_INNER_SPACING,         ///< Vector2: space between the parts of an item (a checkbox and its label).
    INDENT_SPACING,             ///< float: indentation of indent() and tree nodes.
    CELL_PADDING,               ///< Vector2: space inside table cells.
    SCROLLBAR_SIZE,             ///< float: width of vertical scrollbars, height of horizontal ones.
    SCROLLBAR_ROUNDING,         ///< float: radius of scrollbar grabs.
    GRAB_MINIMUM_SIZE,          ///< float: smallest size of slider and scrollbar grabs.
    GRAB_ROUNDING,              ///< float: radius of grabs.
    TAB_ROUNDING,               ///< float: radius of tab corners.
    TAB_BORDER_SIZE,            ///< float: width of tab borders.
    BUTTON_TEXT_ALIGN,          ///< Vector2: alignment of a button's label (0 to 1).
    SELECTABLE_TEXT_ALIGN,      ///< Vector2: alignment of a selectable's label (0 to 1).
    SEPARATOR_TEXT_BORDER_SIZE, ///< float: thickness of the lines of separator_text().
    SEPARATOR_TEXT_ALIGN,       ///< Vector2: alignment of the text of separator_text() (0 to 1).
    SEPARATOR_TEXT_PADDING,     ///< Vector2: space around the text of separator_text().
    DOCKING_SEPARATOR_SIZE,     ///< float: width of the separators between docked windows.
};

/// @brief The themes builtin_theme() makes.
enum class ThemeKind : std::uint8_t
{
    DARK,          ///< Light text on dark backgrounds, blue highlights (the default).
    LIGHT,         ///< Dark text on light backgrounds.
    CLASSIC,       ///< Light text on translucent dark backgrounds, purple highlights.
    HIGH_CONTRAST, ///< White on black with yellow focus and thick borders, for low vision.
};

/// @brief How a UI window behaves (UiFrame::begin_window()). Named UiWindowFlag: the platform library's
///        WindowFlag is a different enumeration. Bitmask.
enum class UiWindowFlag : std::uint32_t
{
    NONE                        = 0,        ///< No bit.
    NO_TITLE_BAR                = 1U << 0,  ///< No title bar.
    NO_RESIZE                   = 1U << 1,  ///< The user cannot resize it.
    NO_MOVE                     = 1U << 2,  ///< The user cannot move it.
    NO_SCROLLBAR                = 1U << 3,  ///< No scrollbars (it can still scroll with the mouse wheel or by code).
    NO_SCROLL_WITH_MOUSE        = 1U << 4,  ///< The mouse wheel does not scroll it (it scrolls its parent instead, for a child).
    NO_COLLAPSE                 = 1U << 5,  ///< The user cannot collapse it (no arrow, double clicks on the title do nothing).
    ALWAYS_AUTO_RESIZE          = 1U << 6,  ///< Resized to fit its content every frame.
    NO_BACKGROUND               = 1U << 7,  ///< No background or border.
    NO_SAVED_SETTINGS           = 1U << 8,  ///< Its position and size are not saved with the layout.
    NO_MOUSE_INPUTS             = 1U << 9,  ///< Mouse events pass through it.
    MENU_BAR                    = 1U << 10, ///< It has a menu bar (begin_menu_bar()).
    HORIZONTAL_SCROLLBAR        = 1U << 11, ///< It may scroll horizontally (a horizontal scrollbar when its content is wider).
    NO_FOCUS_ON_APPEARING       = 1U << 12, ///< It does not take the focus when it appears.
    NO_BRING_TO_FRONT_ON_FOCUS  = 1U << 13, ///< Focusing it does not bring it in front of the others.
    ALWAYS_VERTICAL_SCROLLBAR   = 1U << 14, ///< A vertical scrollbar even when the content fits.
    ALWAYS_HORIZONTAL_SCROLLBAR = 1U << 15, ///< A horizontal scrollbar even when the content fits.
    NO_NAVIGATION_INPUTS        = 1U << 16, ///< Keyboard and gamepad navigation do not move inside it.
    NO_NAVIGATION_FOCUS         = 1U << 17, ///< Keyboard and gamepad navigation do not focus it.
    UNSAVED_DOCUMENT            = 1U << 18, ///< A dot in its title (and its tab): it has unsaved changes.
    NO_DOCKING                  = 1U << 19, ///< It cannot be docked or receive docked windows.
    NO_NAVIGATION               = NO_NAVIGATION_INPUTS | NO_NAVIGATION_FOCUS, ///< No keyboard or gamepad navigation.
    NO_DECORATION               = NO_TITLE_BAR | NO_RESIZE | NO_SCROLLBAR | NO_COLLAPSE, ///< No title bar, resize grip, scrollbars or collapse arrow.
    NO_INPUTS                   = NO_MOUSE_INPUTS | NO_NAVIGATION_INPUTS | NO_NAVIGATION_FOCUS, ///< No mouse, keyboard or gamepad input.
};

/// @brief How a child window behaves (UiFrame::begin_child()). Bitmask.
enum class ChildFlag : std::uint8_t
{
    NONE                      = 0,       ///< No bit.
    BORDERS                   = 1U << 0, ///< A border around it.
    ALWAYS_USE_WINDOW_PADDING = 1U << 1, ///< The window padding inside it even without a border.
    RESIZE_X                  = 1U << 2, ///< The user can resize its width (a grip on its right edge).
    RESIZE_Y                  = 1U << 3, ///< The user can resize its height (a grip on its bottom edge).
    AUTO_RESIZE_X             = 1U << 4, ///< Its width follows its content (of the previous frame).
    AUTO_RESIZE_Y             = 1U << 5, ///< Its height follows its content (of the previous frame).
    FRAME_STYLE               = 1U << 6, ///< Drawn as a frame (FRAME_BACKGROUND, frame rounding, border and padding).
};

/// @brief How invisible_button() reacts (no button flag: the left button). Bitmask.
enum class ButtonFlag : std::uint8_t
{
    NONE                = 0,       ///< No bit.
    MOUSE_BUTTON_LEFT   = 1U << 0, ///< Reacts to the left button.
    MOUSE_BUTTON_RIGHT  = 1U << 1, ///< Reacts to the right button.
    MOUSE_BUTTON_MIDDLE = 1U << 2, ///< Reacts to the middle button.
    REPEAT              = 1U << 3, ///< Pressed repeatedly while held (key repeat timing).
    PRESS_ON_CLICK      = 1U << 4, ///< Pressed when the button goes down instead of when it is released over the item.
    ALLOW_OVERLAP       = 1U << 5, ///< Items submitted later over it may take the mouse.
};

/// @brief How a selectable behaves. Bitmask.
enum class SelectableFlag : std::uint8_t
{
    NONE                 = 0,       ///< No bit.
    NO_AUTO_CLOSE_POPUPS = 1U << 0, ///< Clicking it does not close the popup it is in.
    SPAN_ALL_COLUMNS     = 1U << 1, ///< Its highlight spans every column of its table.
    ALLOW_DOUBLE_CLICK   = 1U << 2, ///< It is also pressed on the second click of a double click.
    DISABLED             = 1U << 3, ///< It cannot be selected; its text is drawn disabled.
    ALLOW_OVERLAP        = 1U << 4, ///< Items submitted later over it may take the mouse.
    HIGHLIGHT            = 1U << 5, ///< Drawn as hovered.
};

/// @brief When a set_next_window_*() setting applies.
enum class UiCondition : std::uint8_t
{
    ALWAYS,         ///< Every frame it is set.
    ONCE,           ///< The first time it is set in the context's life.
    FIRST_USE_EVER, ///< When the window has no saved state yet.
    APPEARING,      ///< When the window appears (first submitted, or submitted again after a frame without).
};

/// @brief A direction: arrow buttons, dock splits.
enum class ArrowDirection : std::uint8_t
{
    LEFT,  ///< Left.
    RIGHT, ///< Right.
    UP,    ///< Up.
    DOWN,  ///< Down.
};

/// @brief What is_window_hovered() and is_item_hovered() consider. Bitmask.
enum class HoveredFlag : std::uint8_t
{
    NONE                              = 0,       ///< No bit.
    CHILD_WINDOWS                     = 1U << 0, ///< The window, or one of its child windows.
    ROOT_WINDOW                       = 1U << 1, ///< The root of the current window's hierarchy.
    ANY_WINDOW                        = 1U << 2, ///< Any window.
    ALLOW_WHEN_BLOCKED_BY_ACTIVE_ITEM = 1U << 3, ///< Even while another item is held.
    ALLOW_WHEN_OVERLAPPED             = 1U << 4, ///< Even when a window or item in front covers it.
    ALLOW_WHEN_DISABLED               = 1U << 5, ///< Even when the item is disabled.
    ROOT_AND_CHILD_WINDOWS            = ROOT_WINDOW | CHILD_WINDOWS, ///< The root window or any of its child windows.
};

/// @brief What is_window_focused() considers. Bitmask.
enum class FocusedFlag : std::uint8_t
{
    NONE                   = 0,       ///< No bit.
    CHILD_WINDOWS          = 1U << 0, ///< The window, or one of its child windows.
    ROOT_WINDOW            = 1U << 1, ///< The root of the current window's hierarchy.
    ANY_WINDOW             = 1U << 2, ///< Any window.
    ROOT_AND_CHILD_WINDOWS = ROOT_WINDOW | CHILD_WINDOWS, ///< The root window or any of its child windows.
};

/// @brief What went wrong in a frame (UiFrameReport): per-frame functions never throw, they report. Bitmask.
enum class UiFrameProblem : std::uint16_t
{
    NONE                  = 0,        ///< No bit.
    FRAME_NOT_ACTIVE      = 1U << 0,  ///< A frame function was called outside begin_frame() / end_frame(): ignored.
    WIDGET_OUTSIDE_WINDOW = 1U << 1,  ///< A widget was submitted outside any window: ignored.
    UNBALANCED_STACK      = 1U << 2,  ///< A begin or push had no end or pop (closed by end_frame()), or a pop had no push.
    END_WITHOUT_BEGIN     = 1U << 3,  ///< An end_*() had no matching begin_*() (ignored).
    IDENTIFIER_CONFLICT   = 1U << 4,  ///< Two interactive items of a frame had the same identifier: use push_id() or "label##suffix".
    CAPACITY_EXCEEDED     = 1U << 5,  ///< A reserved capacity (UiContextLimits) was exceeded: memory was allocated, or a check was skipped.
    TEXT_LAYOUT           = 1U << 6,  ///< Text could not be laid out as asked (missing glyphs, invalid UTF-8, a full cache).
    NO_FONT               = 1U << 7,  ///< There is no font to draw text with (UiContext::load_font()).
    NOT_IMPLEMENTED       = 1U << 8,  ///< A function of a later phase was called: it did nothing.
    INVALID_ARGUMENT      = 1U << 9,  ///< An argument was out of range (a negative count, a column that does not exist): clamped or ignored.
    PLATFORM_SERVICE      = 1U << 10, ///< A platform service (clipboard, cursor, text input) failed.
    ISSUES_DROPPED        = 1U << 11, ///< More issues than the report holds (MAXIMUM_FRAME_ISSUES) occurred.
};

/// @brief How a text field behaves (UiFrame::input_text()). Bitmask.
enum class InputTextFlag : std::uint32_t
{
    NONE                       = 0,        ///< No bit.
    CHARACTERS_DECIMAL         = 1U << 0,  ///< Accepts 0123456789.+-*/ only.
    CHARACTERS_HEXADECIMAL     = 1U << 1,  ///< Accepts 0123456789ABCDEFabcdef only.
    CHARACTERS_SCIENTIFIC      = 1U << 2,  ///< Accepts 0123456789.+-*/eE only.
    CHARACTERS_UPPERCASE       = 1U << 3,  ///< Turns a to z into A to Z.
    CHARACTERS_NO_BLANK        = 1U << 4,  ///< Refuses spaces and tabs.
    ALLOW_TAB_INPUT            = 1U << 5,  ///< Tab types a tab character instead of moving the focus.
    ENTER_RETURNS_TRUE         = 1U << 6,  ///< input_text() returns true when Return is pressed, instead of on every change.
    ESCAPE_CLEARS_ALL          = 1U << 7,  ///< Escape clears the text instead of reverting it.
    CONTROL_ENTER_FOR_NEW_LINE = 1U << 8,  ///< Multi-line: Control+Return types a new line, Return confirms.
    READ_ONLY                  = 1U << 9,  ///< The text can be selected and copied, not changed.
    PASSWORD                   = 1U << 10, ///< Shows '*' for each character and refuses copying.
    ALWAYS_OVERWRITE           = 1U << 11, ///< Typing replaces the character after the cursor.
    AUTO_SELECT_ALL            = 1U << 12, ///< Everything is selected when the field takes the focus.
    NO_HORIZONTAL_SCROLL       = 1U << 13, ///< The text does not scroll horizontally to follow the cursor.
    NO_UNDO_REDO               = 1U << 14, ///< No undo and redo history.
    ELIDE_LEFT                 = 1U << 15, ///< When the text does not fit, its beginning is hidden instead of its end.
    WORD_WRAP                  = 1U << 16, ///< Multi-line: lines wrap at word boundaries.
};

/// @brief How sliders and drags behave. Bitmask.
enum class SliderFlag : std::uint8_t
{
    NONE               = 0,       ///< No bit.
    LOGARITHMIC        = 1U << 0, ///< A logarithmic scale.
    NO_ROUND_TO_FORMAT = 1U << 1, ///< The value is not rounded to the precision of its format.
    NO_INPUT           = 1U << 2, ///< Control+click or a double click does not turn it into a text field.
    WRAP_AROUND        = 1U << 3, ///< Drags past an end continue from the other end.
    CLAMP_ON_INPUT     = 1U << 4, ///< Values typed are clamped to the range.
    CLAMP_ZERO_RANGE   = 1U << 5, ///< Clamps even when the minimum equals the maximum.
    NO_SPEED_TWEAKS    = 1U << 6, ///< Shift and Alt do not change the drag speed.
    ALWAYS_CLAMP       = CLAMP_ON_INPUT | CLAMP_ZERO_RANGE, ///< Always clamped to the range.
};

/// @brief How color_edit(), color_picker() and color_button() behave. Bitmask.
enum class ColorEditFlag : std::uint32_t
{
    NONE                = 0,        ///< No bit.
    NO_ALPHA            = 1U << 0,  ///< The alpha channel is ignored.
    NO_PICKER           = 1U << 1,  ///< Clicking the preview does not open a picker.
    NO_OPTIONS          = 1U << 2,  ///< No context menu of display options.
    NO_SMALL_PREVIEW    = 1U << 3,  ///< No color square next to the inputs.
    NO_INPUTS           = 1U << 4,  ///< No numeric inputs (the preview only).
    NO_TOOLTIP          = 1U << 5,  ///< No tooltip on the preview.
    NO_LABEL            = 1U << 6,  ///< No label after the inputs.
    NO_SIDE_PREVIEW     = 1U << 7,  ///< Picker: no larger preview next to it.
    NO_DRAG_DROP        = 1U << 8,  ///< Colors cannot be dragged from or dropped onto it.
    NO_BORDER           = 1U << 9,  ///< No border around the preview.
    ALPHA_BAR           = 1U << 10, ///< Picker: an alpha bar.
    ALPHA_PREVIEW_HALF  = 1U << 11, ///< The preview shows half opaque, half with alpha over a checkerboard.
    HDR                 = 1U << 12, ///< Channels may exceed 1 (high dynamic range).
    DISPLAY_RGB         = 1U << 13, ///< Inputs show red, green and blue.
    DISPLAY_HSV         = 1U << 14, ///< Inputs show hue, saturation and value.
    DISPLAY_HEXADECIMAL = 1U << 15, ///< Inputs show #RRGGBBAA.
    INTEGER_CHANNELS    = 1U << 16, ///< Channels as integers from 0 to 255.
    FLOAT_CHANNELS      = 1U << 17, ///< Channels as numbers from 0 to 1.
    PICKER_HUE_BAR      = 1U << 18, ///< Picker: a saturation/value square and a hue bar.
    PICKER_HUE_WHEEL    = 1U << 19, ///< Picker: a hue wheel and a triangle.
    INPUT_RGB           = 1U << 20, ///< The edited values are red, green and blue.
    INPUT_HSV           = 1U << 21, ///< The edited values are hue, saturation and value.
};

/// @brief How a combo box behaves. Bitmask.
enum class ComboFlag : std::uint8_t
{
    NONE              = 0,       ///< No bit.
    POPUP_ALIGN_LEFT  = 1U << 0, ///< The popup is aligned on the left of the item.
    HEIGHT_SMALL      = 1U << 1, ///< The popup shows about 4 items.
    HEIGHT_REGULAR    = 1U << 2, ///< The popup shows about 8 items (the default).
    HEIGHT_LARGE      = 1U << 3, ///< The popup shows about 20 items.
    HEIGHT_LARGEST    = 1U << 4, ///< The popup is as tall as it can be.
    NO_ARROW_BUTTON   = 1U << 5, ///< No arrow button next to the preview.
    NO_PREVIEW        = 1U << 6, ///< The arrow button only.
    WIDTH_FIT_PREVIEW = 1U << 7, ///< As wide as its preview.
};

/// @brief How tree nodes and collapsing headers behave. Bitmask.
enum class TreeNodeFlag : std::uint16_t
{
    NONE                 = 0,        ///< No bit.
    SELECTED             = 1U << 0,  ///< Drawn as selected.
    FRAMED               = 1U << 1,  ///< A full frame with a background (collapsing headers).
    ALLOW_OVERLAP        = 1U << 2,  ///< Items submitted later over it may take the mouse.
    NO_TREE_PUSH_ON_OPEN = 1U << 3,  ///< Opening it neither indents nor pushes an identifier (no tree_pop()).
    DEFAULT_OPEN         = 1U << 4,  ///< Open the first time it is shown.
    OPEN_ON_DOUBLE_CLICK = 1U << 5,  ///< Opens on a double click instead of a click.
    OPEN_ON_ARROW        = 1U << 6,  ///< Opens when its arrow is clicked only.
    LEAF                 = 1U << 7,  ///< No arrow: it never opens (a leaf of the tree).
    BULLET               = 1U << 8,  ///< A bullet instead of an arrow.
    FRAME_PADDING        = 1U << 9,  ///< Its height is a frame's (aligned with framed widgets).
    SPAN_AVAILABLE_WIDTH = 1U << 10, ///< Its hit box extends to the right edge of the region.
    SPAN_FULL_WIDTH      = 1U << 11, ///< Its hit box spans the whole width, the indentation included.
    SPAN_TEXT_WIDTH      = 1U << 12, ///< Its hit box covers its label only.
    SPAN_ALL_COLUMNS     = 1U << 13, ///< Its hit box spans every column of its table.
    COLLAPSING_HEADER    = FRAMED | NO_TREE_PUSH_ON_OPEN, ///< A collapsing header.
};

/// @brief How a tab bar behaves. Bitmask.
enum class TabBarFlag : std::uint16_t
{
    NONE                              = 0,       ///< No bit.
    REORDERABLE                       = 1U << 0, ///< Tabs can be reordered by dragging.
    AUTO_SELECT_NEW_TABS              = 1U << 1, ///< A tab that appears is selected.
    TAB_LIST_POPUP_BUTTON             = 1U << 2, ///< A button opening a list of the tabs.
    NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON = 1U << 3, ///< The middle button does not close tabs.
    NO_TAB_LIST_SCROLLING_BUTTONS     = 1U << 4, ///< No scrolling buttons when the tabs do not fit.
    NO_TOOLTIP                        = 1U << 5, ///< No tooltip when a tab's label is cut.
    DRAW_SELECTED_OVERLINE            = 1U << 6, ///< A line over the selected tab.
    FITTING_POLICY_SHRINK             = 1U << 7, ///< Tabs shrink when they do not fit.
    FITTING_POLICY_SCROLL             = 1U << 8, ///< Tabs scroll when they do not fit.
};

/// @brief How a tab behaves. Bitmask.
enum class TabItemFlag : std::uint16_t
{
    NONE                              = 0,       ///< No bit.
    UNSAVED_DOCUMENT                  = 1U << 0, ///< A dot in its label: unsaved changes.
    SET_SELECTED                      = 1U << 1, ///< Selected by code this frame.
    NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON = 1U << 2, ///< The middle button does not close it.
    NO_PUSH_IDENTIFIER                = 1U << 3, ///< Its content does not push its identifier.
    NO_TOOLTIP                        = 1U << 4, ///< No tooltip when its label is cut.
    NO_REORDER                        = 1U << 5, ///< It cannot be moved by dragging.
    LEADING                           = 1U << 6, ///< Placed before the other tabs.
    TRAILING                          = 1U << 7, ///< Placed after the other tabs.
};

/// @brief How popups open (no button flag: the right button for context popups). Bitmask.
enum class PopupFlag : std::uint8_t
{
    NONE                        = 0,       ///< No bit.
    MOUSE_BUTTON_LEFT           = 1U << 0, ///< Opened by the left button.
    MOUSE_BUTTON_RIGHT          = 1U << 1, ///< Opened by the right button.
    MOUSE_BUTTON_MIDDLE         = 1U << 2, ///< Opened by the middle button.
    NO_REOPEN                   = 1U << 3, ///< Opening a popup already open does nothing.
    NO_OPEN_OVER_EXISTING_POPUP = 1U << 4, ///< Does not open over another popup of the same level.
    NO_OPEN_OVER_ITEMS          = 1U << 5, ///< A window context popup opens over empty space only.
    ANY_POPUP_IDENTIFIER        = 1U << 6, ///< is_popup_open(): any popup.
    ANY_POPUP_LEVEL             = 1U << 7, ///< is_popup_open(): any popup level.
};

/// @brief How a table behaves (one SIZING_* flag at most). Bitmask.
enum class TableFlag : std::uint32_t
{
    NONE                            = 0,        ///< No bit.
    RESIZABLE                       = 1U << 0,  ///< Columns can be resized.
    REORDERABLE                     = 1U << 1,  ///< Columns can be reordered by dragging their headers.
    HIDEABLE                        = 1U << 2,  ///< Columns can be hidden from the header's context menu.
    SORTABLE                        = 1U << 3,  ///< Clicking a header sorts by its column (table_sort_specifications()).
    NO_SAVED_SETTINGS               = 1U << 4,  ///< Widths, order, visibility and sorting are not saved with the layout.
    CONTEXT_MENU_IN_BODY            = 1U << 5,  ///< Right-clicking the body opens the header's context menu.
    ROW_BACKGROUND                  = 1U << 6,  ///< Rows alternate TABLE_ROW_BACKGROUND and TABLE_ROW_BACKGROUND_ALTERNATE.
    BORDERS_INNER_HORIZONTAL        = 1U << 7,  ///< Borders between rows.
    BORDERS_OUTER_HORIZONTAL        = 1U << 8,  ///< Borders above and below the table.
    BORDERS_INNER_VERTICAL          = 1U << 9,  ///< Borders between columns.
    BORDERS_OUTER_VERTICAL          = 1U << 10, ///< Borders left and right of the table.
    NO_BORDERS_IN_BODY              = 1U << 11, ///< Vertical borders in the header only.
    NO_BORDERS_IN_BODY_UNTIL_RESIZE = 1U << 12, ///< Vertical borders in the body while a column is resized only.
    SIZING_FIXED_FIT                = 1U << 13, ///< Columns are as wide as their content.
    SIZING_FIXED_SAME               = 1U << 14, ///< Columns are as wide as the widest content of any column.
    SIZING_STRETCH_PROPORTIONAL     = 1U << 15, ///< Columns share the width in proportion to their content.
    SIZING_STRETCH_SAME             = 1U << 16, ///< Columns share the width equally.
    NO_HOST_EXTEND_X                = 1U << 17, ///< The table does not grow to its outer width.
    NO_HOST_EXTEND_Y                = 1U << 18, ///< The table does not grow to its outer height.
    NO_KEEP_COLUMNS_VISIBLE         = 1U << 19, ///< Columns may shrink out of view.
    PRECISE_WIDTHS                  = 1U << 20, ///< Stretched columns get fractional widths.
    NO_CLIP                         = 1U << 21, ///< Cells are not clipped.
    PAD_OUTER_X                     = 1U << 22, ///< Padding at the outer edges.
    NO_PAD_OUTER_X                  = 1U << 23, ///< No padding at the outer edges.
    NO_PAD_INNER_X                  = 1U << 24, ///< No padding between columns.
    SCROLL_X                        = 1U << 25, ///< The table scrolls horizontally (it needs an outer size).
    SCROLL_Y                        = 1U << 26, ///< The table scrolls vertically (it needs an outer size).
    SORT_MULTIPLE                   = 1U << 27, ///< Shift+click sorts by several columns.
    SORT_TRISTATE                   = 1U << 28, ///< A third click removes a column from the sort.
    HIGHLIGHT_HOVERED_COLUMN        = 1U << 29, ///< The header of the hovered column is highlighted.
    BORDERS_HORIZONTAL              = BORDERS_INNER_HORIZONTAL | BORDERS_OUTER_HORIZONTAL, ///< Horizontal borders.
    BORDERS_VERTICAL                = BORDERS_INNER_VERTICAL | BORDERS_OUTER_VERTICAL, ///< Vertical borders.
    BORDERS_INNER                   = BORDERS_INNER_HORIZONTAL | BORDERS_INNER_VERTICAL, ///< Inner borders.
    BORDERS_OUTER                   = BORDERS_OUTER_HORIZONTAL | BORDERS_OUTER_VERTICAL, ///< Outer borders.
    BORDERS                         = BORDERS_INNER | BORDERS_OUTER, ///< Every border.
};

/// @brief How a table column behaves (table_setup_column()). Bitmask.
enum class TableColumnFlag : std::uint32_t
{
    NONE                   = 0,        ///< No bit.
    DISABLED               = 1U << 0,  ///< Hidden and not listed in the context menu.
    DEFAULT_HIDE           = 1U << 1,  ///< Hidden the first time.
    DEFAULT_SORT           = 1U << 2,  ///< Sorted by the first time.
    WIDTH_STRETCH          = 1U << 3,  ///< Shares the remaining width.
    WIDTH_FIXED            = 1U << 4,  ///< Has a fixed width.
    NO_RESIZE              = 1U << 5,  ///< Cannot be resized.
    NO_REORDER             = 1U << 6,  ///< Cannot be reordered.
    NO_HIDE                = 1U << 7,  ///< Cannot be hidden.
    NO_CLIP                = 1U << 8,  ///< Its cells are not clipped.
    NO_SORT                = 1U << 9,  ///< Cannot be sorted by.
    NO_SORT_ASCENDING      = 1U << 10, ///< Cannot be sorted in ascending order.
    NO_SORT_DESCENDING     = 1U << 11, ///< Cannot be sorted in descending order.
    NO_HEADER_LABEL        = 1U << 12, ///< Its header shows no label.
    NO_HEADER_WIDTH        = 1U << 13, ///< Its label does not count in its width.
    PREFER_SORT_ASCENDING  = 1U << 14, ///< Sorted ascending first.
    PREFER_SORT_DESCENDING = 1U << 15, ///< Sorted descending first.
    INDENT_ENABLE          = 1U << 16, ///< Indentation applies in its cells.
    INDENT_DISABLE         = 1U << 17, ///< Indentation does not apply in its cells.
    ANGLED_HEADER          = 1U << 18, ///< Its header label is drawn at an angle.
};

/// @brief How a table row behaves (table_next_row()). Bitmask.
enum class TableRowFlag : std::uint8_t
{
    NONE    = 0,       ///< No bit.
    HEADERS = 1U << 0, ///< A header row (header background, not counted by table_row_index()).
};

/// @brief How a column is sorted.
enum class SortDirection : std::uint8_t
{
    NONE,       ///< Not sorted.
    ASCENDING,  ///< Smallest first.
    DESCENDING, ///< Largest first.
};

/// @brief How drag and drop sources and targets behave. Bitmask.
enum class DragDropFlag : std::uint16_t
{
    NONE                             = 0,       ///< No bit.
    SOURCE_NO_PREVIEW_TOOLTIP        = 1U << 0, ///< Source: no tooltip showing the payload.
    SOURCE_NO_DISABLE_HOVER          = 1U << 1, ///< Source: the source item still reports hovering while dragged.
    SOURCE_NO_HOLD_TO_OPEN_OTHERS    = 1U << 2, ///< Source: hovering tree nodes and tabs does not open them.
    SOURCE_ALLOW_NULL_IDENTIFIER     = 1U << 3, ///< Source: items without identifier (text, images) can be sources.
    SOURCE_EXTERNAL                  = 1U << 4, ///< Source: the payload comes from outside the UI (files dropped on the window).
    PAYLOAD_AUTO_EXPIRE              = 1U << 5, ///< Source: the payload expires when the source stops being submitted.
    ACCEPT_BEFORE_DELIVERY           = 1U << 6, ///< Target: accept_drag_drop_payload() returns the payload before the button is released.
    ACCEPT_NO_DRAW_DEFAULT_RECTANGLE = 1U << 7, ///< Target: no highlight rectangle.
    ACCEPT_NO_PREVIEW_TOOLTIP        = 1U << 8, ///< Target: hides the source's preview tooltip.
    ACCEPT_PEEK_ONLY                 = ACCEPT_BEFORE_DELIVERY | ACCEPT_NO_DRAW_DEFAULT_RECTANGLE, ///< Target: look at the payload before delivery, without highlight.
};

/// @brief How a dock space and its nodes behave. Bitmask.
enum class DockNodeFlag : std::uint8_t
{
    NONE                         = 0,       ///< No bit.
    KEEP_ALIVE_ONLY              = 1U << 0, ///< The dock space keeps its docked windows without being drawn.
    NO_DOCKING_OVER_CENTRAL_NODE = 1U << 1, ///< Nothing docks over the central node.
    PASS_THROUGH_CENTRAL_NODE    = 1U << 2, ///< The empty central node shows what is behind it and lets inputs through.
    NO_DOCKING_SPLIT             = 1U << 3, ///< Nodes cannot be split.
    NO_RESIZE                    = 1U << 4, ///< Splits cannot be resized.
    AUTO_HIDE_TAB_BAR            = 1U << 5, ///< A node with one window hides its tab bar.
    NO_UNDOCKING                 = 1U << 6, ///< Windows cannot be undocked.
};

/// @brief How shortcut() routes a key chord. Bitmask.
enum class ShortcutFlag : std::uint8_t
{
    NONE          = 0,       ///< No bit.
    REPEAT        = 1U << 0, ///< Also true on key repeat.
    ROUTE_ACTIVE  = 1U << 1, ///< Only while the current item is active.
    ROUTE_FOCUSED = 1U << 2, ///< Only while the current window (or a child) has the focus (the default).
    ROUTE_GLOBAL  = 1U << 3, ///< Whatever has the focus, unless a focused route takes it.
    ROUTE_ALWAYS  = 1U << 4, ///< Always, without taking it from other routes.
};

/// @brief What an item is for assistive technologies (screen readers).
enum class AccessibilityRole : std::uint8_t
{
    GENERIC,       ///< None of the others.
    WINDOW,        ///< A window.
    DIALOG,        ///< A modal window.
    GROUP,         ///< A group of items.
    STATIC_TEXT,   ///< Text that cannot be edited.
    LINK,          ///< A link.
    BUTTON,        ///< A button.
    CHECK_BOX,     ///< A check box.
    RADIO_BUTTON,  ///< A radio button.
    SWITCH,        ///< An on/off switch.
    SLIDER,        ///< A slider or a drag.
    SPIN_BUTTON,   ///< A numeric field with step buttons.
    TEXT_FIELD,    ///< A text field.
    COMBO_BOX,     ///< A combo box.
    LIST,          ///< A list box.
    LIST_ITEM,     ///< An item of a list.
    MENU_BAR,      ///< A menu bar.
    MENU,          ///< A menu.
    MENU_ITEM,     ///< An item of a menu.
    TAB_LIST,      ///< A tab bar.
    TAB,           ///< A tab.
    TAB_PANEL,     ///< The content of a tab.
    TREE,          ///< A tree.
    TREE_ITEM,     ///< A node of a tree.
    TABLE,         ///< A table.
    ROW,           ///< A row of a table.
    CELL,          ///< A cell of a table.
    COLUMN_HEADER, ///< A header of a table column.
    TOOLTIP,       ///< A tooltip.
    PROGRESS_BAR,  ///< A progress indicator.
    SCROLL_BAR,    ///< A scrollbar.
    SEPARATOR,     ///< A separator or splitter.
    IMAGE,         ///< An image.
};

/// @brief How assistive technologies announce changes of an item.
enum class AccessibilityLiveRegion : std::uint8_t
{
    OFF,       ///< Not announced.
    POLITE,    ///< Announced when the user is idle.
    ASSERTIVE, ///< Announced at once, interrupting.
};

// ----------------------------------------------------------------- immediate mode (U4)

/// @brief Where a TextEditor moves its cursor (or erases to).
enum class TextMotion : std::uint8_t
{
    PREVIOUS_GRAPHEME, ///< To the previous grapheme cluster boundary (a user-perceived character).
    NEXT_GRAPHEME,     ///< To the next grapheme cluster boundary.
    PREVIOUS_WORD,     ///< To the start of the word before the cursor (Control+Left).
    NEXT_WORD,         ///< To the start of the word after the cursor (Control+Right).
    LINE_START,        ///< To the start of the cursor's line (Home).
    LINE_END,          ///< To the end of the cursor's line (End).
    TEXT_START,        ///< To the start of the text (Control+Home).
    TEXT_END,          ///< To the end of the text (Control+End).
};

/// @brief How the atlas tool embeds a `.uiatlas` blob into a generated C++ module.
enum class AtlasEmbedding : std::uint8_t
{
    EMBED_DIRECTIVE, ///< `#embed` (GCC 15, Clang 19): fast to compile.
    BYTE_LIST,       ///< A list of byte literals: for compilers without `#embed` (MSVC).
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief First bytes of every `.uiatlas` blob.
inline constexpr std::array<char, 8> UI_ATLAS_MAGIC{'G', 'F', 'X', 'U', 'I', 'A', 'T', 'L'};

/// @brief Major version of the `.uiatlas` format: readers refuse other major versions.
inline constexpr std::uint16_t UI_ATLAS_VERSION_MAJOR = 1;

/// @brief Minor version of the `.uiatlas` format: readers accept every minor version of their major.
inline constexpr std::uint16_t UI_ATLAS_VERSION_MINOR = 0;

/// @brief Written as a little-endian 32-bit value: a reader that sees other bytes has the wrong endianness.
inline constexpr std::uint32_t UI_ATLAS_ENDIANNESS_TAG = 0x01020304U;

/// @brief Alignment, in bytes, of every structure and section of a `.uiatlas` blob.
inline constexpr std::size_t UI_ATLAS_ALIGNMENT = 8;

/// @brief Size, in bytes, of the header of a `.uiatlas` blob.
inline constexpr std::size_t UI_ATLAS_HEADER_SIZE = 64;

/// @brief Version of the atlas generator, stored in every atlas: raised whenever the same inputs would
///        produce different bytes (the pixels, the packing or the tables).
inline constexpr std::uint32_t UI_ATLAS_GENERATOR_VERSION = 1;

/// @brief U+FFFD, which stands for bytes that are not UTF-8.
inline constexpr char32_t REPLACEMENT_CHARACTER = U'\uFFFD';

/// @brief Largest number of glyphs a ligature of the LIGATURES section replaces.
inline constexpr std::size_t MAXIMUM_LIGATURE_COMPONENTS = 4;

/// @brief Edits a TextEditor can undo by default.
inline constexpr std::uint32_t DEFAULT_UNDO_LEVELS = 100;

/// @brief Period of a text field's blinking cursor, in seconds.
inline constexpr double TEXT_CURSOR_BLINK_SECONDS = 1.2;

/// @brief Part of the blink period the cursor is shown, in seconds (and how long it stays after a change).
inline constexpr double TEXT_CURSOR_VISIBLE_SECONDS = 0.8;

/// @brief How numeric widgets show integers when no format is given.
inline constexpr std::string_view DEFAULT_INTEGER_FORMAT = "{}";

/// @brief How numeric widgets show floats when no format is given.
inline constexpr std::string_view DEFAULT_FLOAT_FORMAT = "{:.3f}";

/// @brief How numeric widgets show doubles when no format is given.
inline constexpr std::string_view DEFAULT_DOUBLE_FORMAT = "{:.6f}";

/// @brief Factor of a drag's speed while Shift is held (SliderFlag::NO_SPEED_TWEAKS: none).
inline constexpr float DRAG_FAST_FACTOR = 10.0F;

/// @brief Factor of a drag's speed while Alt is held.
inline constexpr float DRAG_SLOW_FACTOR = 0.1F;

/// @brief Items a combo's popup shows with ComboFlag::HEIGHT_SMALL.
inline constexpr std::uint32_t COMBO_SMALL_ITEMS = 4;

/// @brief Items a combo's popup shows by default (ComboFlag::HEIGHT_REGULAR).
inline constexpr std::uint32_t COMBO_REGULAR_ITEMS = 8;

/// @brief Items a combo's popup shows with ComboFlag::HEIGHT_LARGE.
inline constexpr std::uint32_t COMBO_LARGE_ITEMS = 20;

/// @brief Issues a UiFrameReport holds; more set UiFrameProblem::ISSUES_DROPPED.
inline constexpr std::size_t MAXIMUM_FRAME_ISSUES = 64;

/// @brief Bytes of a name an issue keeps (longer names are cut).
inline constexpr std::size_t MAXIMUM_ISSUE_NAME_SIZE = 48;

/// @brief Bytes formatted text (UiFrame::text("{}", ...)) is formatted into before it moves to the frame arena.
inline constexpr std::size_t UI_FORMAT_BUFFER_CAPACITY = 1024;

/// @brief Default longest time between the clicks of a double click, in seconds.
inline constexpr double DEFAULT_DOUBLE_CLICK_SECONDS = 0.30;

/// @brief Default largest distance between the clicks of a double click, in UI units.
inline constexpr float DEFAULT_DOUBLE_CLICK_DISTANCE = 6.0F;

/// @brief Default distance the mouse moves with a button held before it drags, in UI units.
inline constexpr float DEFAULT_DRAG_THRESHOLD = 6.0F;

/// @brief Default time a key or button is held before it repeats, in seconds.
inline constexpr double DEFAULT_REPEAT_DELAY_SECONDS = 0.275;

/// @brief Default time between repeats of a held key or button, in seconds.
inline constexpr double DEFAULT_REPEAT_INTERVAL_SECONDS = 0.050;

/// @brief Fraction of a window's width an item takes by default (sliders, combos, text fields).
inline constexpr float DEFAULT_ITEM_WIDTH_FRACTION = 0.65F;

/// @brief Columns columns() can make.
inline constexpr std::uint32_t MAXIMUM_COLUMNS = 64;

/// @brief The code points of a CharacterSet, first and last included.
struct CharacterRange
{
    char32_t first = 0; ///< First code point.
    char32_t last = 0;  ///< Last code point (included).
};

/// @brief The code points of every CharacterSet, indexed by its value.
inline constexpr std::array<CharacterRange, 15> CHARACTER_SET_RANGES{{
    {U'\u0020', U'\u007E'}, // BASIC_LATIN
    {U'\u00A0', U'\u00FF'}, // LATIN_1_SUPPLEMENT
    {U'\u0100', U'\u017F'}, // LATIN_EXTENDED_A
    {U'\u0180', U'\u024F'}, // LATIN_EXTENDED_B
    {U'\u0370', U'\u03FF'}, // GREEK
    {U'\u0400', U'\u04FF'}, // CYRILLIC
    {U'\u0590', U'\u05FF'}, // HEBREW
    {U'\u0600', U'\u06FF'}, // ARABIC
    {U'\u2000', U'\u206F'}, // GENERAL_PUNCTUATION
    {U'\u20A0', U'\u20CF'}, // CURRENCY_SYMBOLS
    {U'\u2100', U'\u214F'}, // LETTERLIKE_SYMBOLS
    {U'\u2190', U'\u21FF'}, // ARROWS
    {U'\u2200', U'\u22FF'}, // MATHEMATICAL_OPERATORS
    {U'\u2500', U'\u257F'}, // BOX_DRAWING
    {U'\u25A0', U'\u25FF'}, // GEOMETRIC_SHAPES
}};

} // namespace gfx::ui

// =================================================================================================
// Enumeration descriptions
// =================================================================================================
export namespace gfx
{

template<>
struct EnumerationTraits<ui::UiSubsystem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::ui"; ///< Errors name these subsystems "gfx::ui::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiSubsystem::GRAPHICS, "GRAPHICS"},
        EnumeratorName{ui::UiSubsystem::FONT, "FONT"},
        EnumeratorName{ui::UiSubsystem::INPUT, "INPUT"},
        EnumeratorName{ui::UiSubsystem::PLATFORM, "PLATFORM"},
        EnumeratorName{ui::UiSubsystem::ACCESSIBILITY, "ACCESSIBILITY"},
        EnumeratorName{ui::UiSubsystem::WIDGETS, "WIDGETS"},
    };
};

template<>
struct EnumerationTraits<ui::FontFormat>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::FontFormat::TRUETYPE, "TRUETYPE"},
        EnumeratorName{ui::FontFormat::OPENTYPE_CFF, "OPENTYPE_CFF"},
        EnumeratorName{ui::FontFormat::OPENTYPE_CFF2, "OPENTYPE_CFF2"},
        EnumeratorName{ui::FontFormat::COLLECTION, "COLLECTION"},
        EnumeratorName{ui::FontFormat::WOFF, "WOFF"},
        EnumeratorName{ui::FontFormat::WOFF2, "WOFF2"},
        EnumeratorName{ui::FontFormat::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::FontDecoderKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::FontDecoderKind::FREETYPE, "FREETYPE"},
        EnumeratorName{ui::FontDecoderKind::STB_TRUETYPE, "STB_TRUETYPE"},
        EnumeratorName{ui::FontDecoderKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::SdfKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SdfKind::SDF, "SDF"},
        EnumeratorName{ui::SdfKind::PSDF, "PSDF"},
        EnumeratorName{ui::SdfKind::MSDF, "MSDF"},
        EnumeratorName{ui::SdfKind::MTSDF, "MTSDF"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasTexelFormat>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasTexelFormat::R8_UNORM, "R8_UNORM"},
        EnumeratorName{ui::AtlasTexelFormat::R8G8B8A8_UNORM, "R8G8B8A8_UNORM"},
        EnumeratorName{ui::AtlasTexelFormat::R16_SFLOAT, "R16_SFLOAT"},
        EnumeratorName{ui::AtlasTexelFormat::R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasCompression>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasCompression::NONE, "NONE"},
        EnumeratorName{ui::AtlasCompression::ZSTD, "ZSTD"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasSectionKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasSectionKind::FACE_METRICS, "FACE_METRICS"},
        EnumeratorName{ui::AtlasSectionKind::GLYPHS, "GLYPHS"},
        EnumeratorName{ui::AtlasSectionKind::CHARACTER_MAP, "CHARACTER_MAP"},
        EnumeratorName{ui::AtlasSectionKind::KERNING, "KERNING"},
        EnumeratorName{ui::AtlasSectionKind::LIGATURES, "LIGATURES"},
        EnumeratorName{ui::AtlasSectionKind::SHAPING_FONT, "SHAPING_FONT"},
        EnumeratorName{ui::AtlasSectionKind::PIXELS, "PIXELS"},
        EnumeratorName{ui::AtlasSectionKind::DESCRIPTOR, "DESCRIPTOR"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasPackerKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasPackerKind::TIGHT, "TIGHT"},
        EnumeratorName{ui::AtlasPackerKind::GRID, "GRID"},
        EnumeratorName{ui::AtlasPackerKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::TextDirection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextDirection::LEFT_TO_RIGHT, "LEFT_TO_RIGHT"},
        EnumeratorName{ui::TextDirection::RIGHT_TO_LEFT, "RIGHT_TO_LEFT"},
    };
};

template<>
struct EnumerationTraits<ui::CharacterSet>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::CharacterSet::BASIC_LATIN, "BASIC_LATIN"},
        EnumeratorName{ui::CharacterSet::LATIN_1_SUPPLEMENT, "LATIN_1_SUPPLEMENT"},
        EnumeratorName{ui::CharacterSet::LATIN_EXTENDED_A, "LATIN_EXTENDED_A"},
        EnumeratorName{ui::CharacterSet::LATIN_EXTENDED_B, "LATIN_EXTENDED_B"},
        EnumeratorName{ui::CharacterSet::GREEK, "GREEK"},
        EnumeratorName{ui::CharacterSet::CYRILLIC, "CYRILLIC"},
        EnumeratorName{ui::CharacterSet::HEBREW, "HEBREW"},
        EnumeratorName{ui::CharacterSet::ARABIC, "ARABIC"},
        EnumeratorName{ui::CharacterSet::GENERAL_PUNCTUATION, "GENERAL_PUNCTUATION"},
        EnumeratorName{ui::CharacterSet::CURRENCY_SYMBOLS, "CURRENCY_SYMBOLS"},
        EnumeratorName{ui::CharacterSet::LETTERLIKE_SYMBOLS, "LETTERLIKE_SYMBOLS"},
        EnumeratorName{ui::CharacterSet::ARROWS, "ARROWS"},
        EnumeratorName{ui::CharacterSet::MATHEMATICAL_OPERATORS, "MATHEMATICAL_OPERATORS"},
        EnumeratorName{ui::CharacterSet::BOX_DRAWING, "BOX_DRAWING"},
        EnumeratorName{ui::CharacterSet::GEOMETRIC_SHAPES, "GEOMETRIC_SHAPES"},
    };
};

template<>
struct EnumerationTraits<ui::OutlineSegmentKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::OutlineSegmentKind::LINE, "LINE"},
        EnumeratorName{ui::OutlineSegmentKind::QUADRATIC, "QUADRATIC"},
        EnumeratorName{ui::OutlineSegmentKind::CUBIC, "CUBIC"},
    };
};

template<>
struct EnumerationTraits<ui::SdfGeneratorKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SdfGeneratorKind::MSDFGEN, "MSDFGEN"},
        EnumeratorName{ui::SdfGeneratorKind::BUILT_IN, "BUILT_IN"},
        EnumeratorName{ui::SdfGeneratorKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::EdgeColoring>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::EdgeColoring::SIMPLE, "SIMPLE"},
        EnumeratorName{ui::EdgeColoring::INK_TRAP, "INK_TRAP"},
        EnumeratorName{ui::EdgeColoring::DISTANCE, "DISTANCE"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasGlyphFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasGlyphFlag::WHITESPACE, "WHITESPACE"},
        EnumeratorName{ui::AtlasGlyphFlag::COLOR_LAYERS, "COLOR_LAYERS"},
        EnumeratorName{ui::AtlasGlyphFlag::NO_OUTLINE, "NO_OUTLINE"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasVerification>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasVerification::STRUCTURE, "STRUCTURE"},
        EnumeratorName{ui::AtlasVerification::CHECKSUMS, "CHECKSUMS"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasProblem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasProblem::TRUNCATED, "TRUNCATED"},
        EnumeratorName{ui::AtlasProblem::BAD_MAGIC, "BAD_MAGIC"},
        EnumeratorName{ui::AtlasProblem::UNSUPPORTED_VERSION, "UNSUPPORTED_VERSION"},
        EnumeratorName{ui::AtlasProblem::WRONG_ENDIANNESS, "WRONG_ENDIANNESS"},
        EnumeratorName{ui::AtlasProblem::SIZE_MISMATCH, "SIZE_MISMATCH"},
        EnumeratorName{ui::AtlasProblem::CHECKSUM_MISMATCH, "CHECKSUM_MISMATCH"},
        EnumeratorName{ui::AtlasProblem::SECTION_OUT_OF_BOUNDS, "SECTION_OUT_OF_BOUNDS"},
        EnumeratorName{ui::AtlasProblem::MISALIGNED_SECTION, "MISALIGNED_SECTION"},
        EnumeratorName{ui::AtlasProblem::SECTION_CHECKSUM_MISMATCH, "SECTION_CHECKSUM_MISMATCH"},
        EnumeratorName{ui::AtlasProblem::UNKNOWN_COMPRESSION, "UNKNOWN_COMPRESSION"},
        EnumeratorName{ui::AtlasProblem::MISSING_SECTION, "MISSING_SECTION"},
        EnumeratorName{ui::AtlasProblem::DUPLICATE_SECTION, "DUPLICATE_SECTION"},
        EnumeratorName{ui::AtlasProblem::MALFORMED_SECTION, "MALFORMED_SECTION"},
    };
};

template<>
struct EnumerationTraits<ui::ParagraphDirection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ParagraphDirection::AUTOMATIC, "AUTOMATIC"},
        EnumeratorName{ui::ParagraphDirection::LEFT_TO_RIGHT, "LEFT_TO_RIGHT"},
        EnumeratorName{ui::ParagraphDirection::RIGHT_TO_LEFT, "RIGHT_TO_LEFT"},
    };
};

template<>
struct EnumerationTraits<ui::TextAlignment>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextAlignment::START, "START"},
        EnumeratorName{ui::TextAlignment::END, "END"},
        EnumeratorName{ui::TextAlignment::LEFT, "LEFT"},
        EnumeratorName{ui::TextAlignment::CENTER, "CENTER"},
        EnumeratorName{ui::TextAlignment::RIGHT, "RIGHT"},
    };
};

template<>
struct EnumerationTraits<ui::TextShaping>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextShaping::AUTOMATIC, "AUTOMATIC"},
        EnumeratorName{ui::TextShaping::BASIC, "BASIC"},
        EnumeratorName{ui::TextShaping::HARFBUZZ, "HARFBUZZ"},
    };
};

template<>
struct EnumerationTraits<ui::TextLayoutProblem>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextLayoutProblem::UNKNOWN_FONT, "UNKNOWN_FONT"},
        EnumeratorName{ui::TextLayoutProblem::INVALID_UTF8, "INVALID_UTF8"},
        EnumeratorName{ui::TextLayoutProblem::MISSING_GLYPHS, "MISSING_GLYPHS"},
        EnumeratorName{ui::TextLayoutProblem::NO_SHAPING_FONT, "NO_SHAPING_FONT"},
        EnumeratorName{ui::TextLayoutProblem::CACHE_FULL, "CACHE_FULL"},
    };
};

template<>
struct EnumerationTraits<ui::DrawTextureKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::DrawTextureKind::NONE, "NONE"},
        EnumeratorName{ui::DrawTextureKind::FONT_PAGE, "FONT_PAGE"},
        EnumeratorName{ui::DrawTextureKind::IMAGE, "IMAGE"},
    };
};

template<>
struct EnumerationTraits<ui::UiShaderMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiShaderMode::SOLID, "SOLID"},
        EnumeratorName{ui::UiShaderMode::SHAPE, "SHAPE"},
        EnumeratorName{ui::UiShaderMode::GLYPH, "GLYPH"},
        EnumeratorName{ui::UiShaderMode::GLYPH_SHADOW, "GLYPH_SHADOW"},
        EnumeratorName{ui::UiShaderMode::IMAGE, "IMAGE"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasEmbedding>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasEmbedding::EMBED_DIRECTIVE, "EMBED_DIRECTIVE"},
        EnumeratorName{ui::AtlasEmbedding::BYTE_LIST, "BYTE_LIST"},
    };
};

template<>
struct EnumerationTraits<ui::StyleColor>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::StyleColor::TEXT_NORMAL, "TEXT_NORMAL"},
        EnumeratorName{ui::StyleColor::TEXT_DISABLED, "TEXT_DISABLED"},
        EnumeratorName{ui::StyleColor::WINDOW_BACKGROUND, "WINDOW_BACKGROUND"},
        EnumeratorName{ui::StyleColor::CHILD_BACKGROUND, "CHILD_BACKGROUND"},
        EnumeratorName{ui::StyleColor::POPUP_BACKGROUND, "POPUP_BACKGROUND"},
        EnumeratorName{ui::StyleColor::BORDER, "BORDER"},
        EnumeratorName{ui::StyleColor::BORDER_SHADOW, "BORDER_SHADOW"},
        EnumeratorName{ui::StyleColor::FRAME_BACKGROUND, "FRAME_BACKGROUND"},
        EnumeratorName{ui::StyleColor::FRAME_BACKGROUND_HOVERED, "FRAME_BACKGROUND_HOVERED"},
        EnumeratorName{ui::StyleColor::FRAME_BACKGROUND_ACTIVE, "FRAME_BACKGROUND_ACTIVE"},
        EnumeratorName{ui::StyleColor::TITLE_BACKGROUND, "TITLE_BACKGROUND"},
        EnumeratorName{ui::StyleColor::TITLE_BACKGROUND_ACTIVE, "TITLE_BACKGROUND_ACTIVE"},
        EnumeratorName{ui::StyleColor::TITLE_BACKGROUND_COLLAPSED, "TITLE_BACKGROUND_COLLAPSED"},
        EnumeratorName{ui::StyleColor::MENU_BAR_BACKGROUND, "MENU_BAR_BACKGROUND"},
        EnumeratorName{ui::StyleColor::SCROLLBAR_BACKGROUND, "SCROLLBAR_BACKGROUND"},
        EnumeratorName{ui::StyleColor::SCROLLBAR_GRAB, "SCROLLBAR_GRAB"},
        EnumeratorName{ui::StyleColor::SCROLLBAR_GRAB_HOVERED, "SCROLLBAR_GRAB_HOVERED"},
        EnumeratorName{ui::StyleColor::SCROLLBAR_GRAB_ACTIVE, "SCROLLBAR_GRAB_ACTIVE"},
        EnumeratorName{ui::StyleColor::CHECK_MARK, "CHECK_MARK"},
        EnumeratorName{ui::StyleColor::SLIDER_GRAB, "SLIDER_GRAB"},
        EnumeratorName{ui::StyleColor::SLIDER_GRAB_ACTIVE, "SLIDER_GRAB_ACTIVE"},
        EnumeratorName{ui::StyleColor::BUTTON, "BUTTON"},
        EnumeratorName{ui::StyleColor::BUTTON_HOVERED, "BUTTON_HOVERED"},
        EnumeratorName{ui::StyleColor::BUTTON_ACTIVE, "BUTTON_ACTIVE"},
        EnumeratorName{ui::StyleColor::HEADER, "HEADER"},
        EnumeratorName{ui::StyleColor::HEADER_HOVERED, "HEADER_HOVERED"},
        EnumeratorName{ui::StyleColor::HEADER_ACTIVE, "HEADER_ACTIVE"},
        EnumeratorName{ui::StyleColor::SEPARATOR, "SEPARATOR"},
        EnumeratorName{ui::StyleColor::SEPARATOR_HOVERED, "SEPARATOR_HOVERED"},
        EnumeratorName{ui::StyleColor::SEPARATOR_ACTIVE, "SEPARATOR_ACTIVE"},
        EnumeratorName{ui::StyleColor::RESIZE_GRIP, "RESIZE_GRIP"},
        EnumeratorName{ui::StyleColor::RESIZE_GRIP_HOVERED, "RESIZE_GRIP_HOVERED"},
        EnumeratorName{ui::StyleColor::RESIZE_GRIP_ACTIVE, "RESIZE_GRIP_ACTIVE"},
        EnumeratorName{ui::StyleColor::INPUT_TEXT_CURSOR, "INPUT_TEXT_CURSOR"},
        EnumeratorName{ui::StyleColor::TAB, "TAB"},
        EnumeratorName{ui::StyleColor::TAB_HOVERED, "TAB_HOVERED"},
        EnumeratorName{ui::StyleColor::TAB_SELECTED, "TAB_SELECTED"},
        EnumeratorName{ui::StyleColor::TAB_SELECTED_OVERLINE, "TAB_SELECTED_OVERLINE"},
        EnumeratorName{ui::StyleColor::TAB_DIMMED, "TAB_DIMMED"},
        EnumeratorName{ui::StyleColor::TAB_DIMMED_SELECTED, "TAB_DIMMED_SELECTED"},
        EnumeratorName{ui::StyleColor::DOCKING_PREVIEW, "DOCKING_PREVIEW"},
        EnumeratorName{ui::StyleColor::DOCKING_EMPTY_BACKGROUND, "DOCKING_EMPTY_BACKGROUND"},
        EnumeratorName{ui::StyleColor::PLOT_LINES, "PLOT_LINES"},
        EnumeratorName{ui::StyleColor::PLOT_LINES_HOVERED, "PLOT_LINES_HOVERED"},
        EnumeratorName{ui::StyleColor::PLOT_HISTOGRAM, "PLOT_HISTOGRAM"},
        EnumeratorName{ui::StyleColor::PLOT_HISTOGRAM_HOVERED, "PLOT_HISTOGRAM_HOVERED"},
        EnumeratorName{ui::StyleColor::TABLE_HEADER_BACKGROUND, "TABLE_HEADER_BACKGROUND"},
        EnumeratorName{ui::StyleColor::TABLE_BORDER_STRONG, "TABLE_BORDER_STRONG"},
        EnumeratorName{ui::StyleColor::TABLE_BORDER_LIGHT, "TABLE_BORDER_LIGHT"},
        EnumeratorName{ui::StyleColor::TABLE_ROW_BACKGROUND, "TABLE_ROW_BACKGROUND"},
        EnumeratorName{ui::StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE, "TABLE_ROW_BACKGROUND_ALTERNATE"},
        EnumeratorName{ui::StyleColor::TEXT_LINK, "TEXT_LINK"},
        EnumeratorName{ui::StyleColor::TEXT_SELECTED_BACKGROUND, "TEXT_SELECTED_BACKGROUND"},
        EnumeratorName{ui::StyleColor::DRAG_DROP_TARGET, "DRAG_DROP_TARGET"},
        EnumeratorName{ui::StyleColor::NAVIGATION_HIGHLIGHT, "NAVIGATION_HIGHLIGHT"},
        EnumeratorName{ui::StyleColor::NAVIGATION_WINDOWING_HIGHLIGHT, "NAVIGATION_WINDOWING_HIGHLIGHT"},
        EnumeratorName{ui::StyleColor::NAVIGATION_WINDOWING_DIM_BACKGROUND, "NAVIGATION_WINDOWING_DIM_BACKGROUND"},
        EnumeratorName{ui::StyleColor::MODAL_WINDOW_DIM_BACKGROUND, "MODAL_WINDOW_DIM_BACKGROUND"},
    };
};

template<>
struct EnumerationTraits<ui::StyleVariable>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::StyleVariable::ALPHA, "ALPHA"},
        EnumeratorName{ui::StyleVariable::DISABLED_ALPHA, "DISABLED_ALPHA"},
        EnumeratorName{ui::StyleVariable::WINDOW_PADDING, "WINDOW_PADDING"},
        EnumeratorName{ui::StyleVariable::WINDOW_ROUNDING, "WINDOW_ROUNDING"},
        EnumeratorName{ui::StyleVariable::WINDOW_BORDER_SIZE, "WINDOW_BORDER_SIZE"},
        EnumeratorName{ui::StyleVariable::WINDOW_MINIMUM_SIZE, "WINDOW_MINIMUM_SIZE"},
        EnumeratorName{ui::StyleVariable::WINDOW_TITLE_ALIGN, "WINDOW_TITLE_ALIGN"},
        EnumeratorName{ui::StyleVariable::CHILD_ROUNDING, "CHILD_ROUNDING"},
        EnumeratorName{ui::StyleVariable::CHILD_BORDER_SIZE, "CHILD_BORDER_SIZE"},
        EnumeratorName{ui::StyleVariable::POPUP_ROUNDING, "POPUP_ROUNDING"},
        EnumeratorName{ui::StyleVariable::POPUP_BORDER_SIZE, "POPUP_BORDER_SIZE"},
        EnumeratorName{ui::StyleVariable::FRAME_PADDING, "FRAME_PADDING"},
        EnumeratorName{ui::StyleVariable::FRAME_ROUNDING, "FRAME_ROUNDING"},
        EnumeratorName{ui::StyleVariable::FRAME_BORDER_SIZE, "FRAME_BORDER_SIZE"},
        EnumeratorName{ui::StyleVariable::ITEM_SPACING, "ITEM_SPACING"},
        EnumeratorName{ui::StyleVariable::ITEM_INNER_SPACING, "ITEM_INNER_SPACING"},
        EnumeratorName{ui::StyleVariable::INDENT_SPACING, "INDENT_SPACING"},
        EnumeratorName{ui::StyleVariable::CELL_PADDING, "CELL_PADDING"},
        EnumeratorName{ui::StyleVariable::SCROLLBAR_SIZE, "SCROLLBAR_SIZE"},
        EnumeratorName{ui::StyleVariable::SCROLLBAR_ROUNDING, "SCROLLBAR_ROUNDING"},
        EnumeratorName{ui::StyleVariable::GRAB_MINIMUM_SIZE, "GRAB_MINIMUM_SIZE"},
        EnumeratorName{ui::StyleVariable::GRAB_ROUNDING, "GRAB_ROUNDING"},
        EnumeratorName{ui::StyleVariable::TAB_ROUNDING, "TAB_ROUNDING"},
        EnumeratorName{ui::StyleVariable::TAB_BORDER_SIZE, "TAB_BORDER_SIZE"},
        EnumeratorName{ui::StyleVariable::BUTTON_TEXT_ALIGN, "BUTTON_TEXT_ALIGN"},
        EnumeratorName{ui::StyleVariable::SELECTABLE_TEXT_ALIGN, "SELECTABLE_TEXT_ALIGN"},
        EnumeratorName{ui::StyleVariable::SEPARATOR_TEXT_BORDER_SIZE, "SEPARATOR_TEXT_BORDER_SIZE"},
        EnumeratorName{ui::StyleVariable::SEPARATOR_TEXT_ALIGN, "SEPARATOR_TEXT_ALIGN"},
        EnumeratorName{ui::StyleVariable::SEPARATOR_TEXT_PADDING, "SEPARATOR_TEXT_PADDING"},
        EnumeratorName{ui::StyleVariable::DOCKING_SEPARATOR_SIZE, "DOCKING_SEPARATOR_SIZE"},
    };
};

template<>
struct EnumerationTraits<ui::ThemeKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ThemeKind::DARK, "DARK"},
        EnumeratorName{ui::ThemeKind::LIGHT, "LIGHT"},
        EnumeratorName{ui::ThemeKind::CLASSIC, "CLASSIC"},
        EnumeratorName{ui::ThemeKind::HIGH_CONTRAST, "HIGH_CONTRAST"},
    };
};

template<>
struct EnumerationTraits<ui::UiWindowFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiWindowFlag::NO_TITLE_BAR, "NO_TITLE_BAR"},
        EnumeratorName{ui::UiWindowFlag::NO_RESIZE, "NO_RESIZE"},
        EnumeratorName{ui::UiWindowFlag::NO_MOVE, "NO_MOVE"},
        EnumeratorName{ui::UiWindowFlag::NO_SCROLLBAR, "NO_SCROLLBAR"},
        EnumeratorName{ui::UiWindowFlag::NO_SCROLL_WITH_MOUSE, "NO_SCROLL_WITH_MOUSE"},
        EnumeratorName{ui::UiWindowFlag::NO_COLLAPSE, "NO_COLLAPSE"},
        EnumeratorName{ui::UiWindowFlag::ALWAYS_AUTO_RESIZE, "ALWAYS_AUTO_RESIZE"},
        EnumeratorName{ui::UiWindowFlag::NO_BACKGROUND, "NO_BACKGROUND"},
        EnumeratorName{ui::UiWindowFlag::NO_SAVED_SETTINGS, "NO_SAVED_SETTINGS"},
        EnumeratorName{ui::UiWindowFlag::NO_MOUSE_INPUTS, "NO_MOUSE_INPUTS"},
        EnumeratorName{ui::UiWindowFlag::MENU_BAR, "MENU_BAR"},
        EnumeratorName{ui::UiWindowFlag::HORIZONTAL_SCROLLBAR, "HORIZONTAL_SCROLLBAR"},
        EnumeratorName{ui::UiWindowFlag::NO_FOCUS_ON_APPEARING, "NO_FOCUS_ON_APPEARING"},
        EnumeratorName{ui::UiWindowFlag::NO_BRING_TO_FRONT_ON_FOCUS, "NO_BRING_TO_FRONT_ON_FOCUS"},
        EnumeratorName{ui::UiWindowFlag::ALWAYS_VERTICAL_SCROLLBAR, "ALWAYS_VERTICAL_SCROLLBAR"},
        EnumeratorName{ui::UiWindowFlag::ALWAYS_HORIZONTAL_SCROLLBAR, "ALWAYS_HORIZONTAL_SCROLLBAR"},
        EnumeratorName{ui::UiWindowFlag::NO_NAVIGATION_INPUTS, "NO_NAVIGATION_INPUTS"},
        EnumeratorName{ui::UiWindowFlag::NO_NAVIGATION_FOCUS, "NO_NAVIGATION_FOCUS"},
        EnumeratorName{ui::UiWindowFlag::UNSAVED_DOCUMENT, "UNSAVED_DOCUMENT"},
        EnumeratorName{ui::UiWindowFlag::NO_DOCKING, "NO_DOCKING"},
    };
};

template<>
struct EnumerationTraits<ui::ChildFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ChildFlag::BORDERS, "BORDERS"},
        EnumeratorName{ui::ChildFlag::ALWAYS_USE_WINDOW_PADDING, "ALWAYS_USE_WINDOW_PADDING"},
        EnumeratorName{ui::ChildFlag::RESIZE_X, "RESIZE_X"},
        EnumeratorName{ui::ChildFlag::RESIZE_Y, "RESIZE_Y"},
        EnumeratorName{ui::ChildFlag::AUTO_RESIZE_X, "AUTO_RESIZE_X"},
        EnumeratorName{ui::ChildFlag::AUTO_RESIZE_Y, "AUTO_RESIZE_Y"},
        EnumeratorName{ui::ChildFlag::FRAME_STYLE, "FRAME_STYLE"},
    };
};

template<>
struct EnumerationTraits<ui::ButtonFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ButtonFlag::MOUSE_BUTTON_LEFT, "MOUSE_BUTTON_LEFT"},
        EnumeratorName{ui::ButtonFlag::MOUSE_BUTTON_RIGHT, "MOUSE_BUTTON_RIGHT"},
        EnumeratorName{ui::ButtonFlag::MOUSE_BUTTON_MIDDLE, "MOUSE_BUTTON_MIDDLE"},
        EnumeratorName{ui::ButtonFlag::REPEAT, "REPEAT"},
        EnumeratorName{ui::ButtonFlag::PRESS_ON_CLICK, "PRESS_ON_CLICK"},
        EnumeratorName{ui::ButtonFlag::ALLOW_OVERLAP, "ALLOW_OVERLAP"},
    };
};

template<>
struct EnumerationTraits<ui::SelectableFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SelectableFlag::NO_AUTO_CLOSE_POPUPS, "NO_AUTO_CLOSE_POPUPS"},
        EnumeratorName{ui::SelectableFlag::SPAN_ALL_COLUMNS, "SPAN_ALL_COLUMNS"},
        EnumeratorName{ui::SelectableFlag::ALLOW_DOUBLE_CLICK, "ALLOW_DOUBLE_CLICK"},
        EnumeratorName{ui::SelectableFlag::DISABLED, "DISABLED"},
        EnumeratorName{ui::SelectableFlag::ALLOW_OVERLAP, "ALLOW_OVERLAP"},
        EnumeratorName{ui::SelectableFlag::HIGHLIGHT, "HIGHLIGHT"},
    };
};

template<>
struct EnumerationTraits<ui::UiCondition>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiCondition::ALWAYS, "ALWAYS"},
        EnumeratorName{ui::UiCondition::ONCE, "ONCE"},
        EnumeratorName{ui::UiCondition::FIRST_USE_EVER, "FIRST_USE_EVER"},
        EnumeratorName{ui::UiCondition::APPEARING, "APPEARING"},
    };
};

template<>
struct EnumerationTraits<ui::ArrowDirection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ArrowDirection::LEFT, "LEFT"},
        EnumeratorName{ui::ArrowDirection::RIGHT, "RIGHT"},
        EnumeratorName{ui::ArrowDirection::UP, "UP"},
        EnumeratorName{ui::ArrowDirection::DOWN, "DOWN"},
    };
};

template<>
struct EnumerationTraits<ui::HoveredFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::HoveredFlag::CHILD_WINDOWS, "CHILD_WINDOWS"},
        EnumeratorName{ui::HoveredFlag::ROOT_WINDOW, "ROOT_WINDOW"},
        EnumeratorName{ui::HoveredFlag::ANY_WINDOW, "ANY_WINDOW"},
        EnumeratorName{ui::HoveredFlag::ALLOW_WHEN_BLOCKED_BY_ACTIVE_ITEM, "ALLOW_WHEN_BLOCKED_BY_ACTIVE_ITEM"},
        EnumeratorName{ui::HoveredFlag::ALLOW_WHEN_OVERLAPPED, "ALLOW_WHEN_OVERLAPPED"},
        EnumeratorName{ui::HoveredFlag::ALLOW_WHEN_DISABLED, "ALLOW_WHEN_DISABLED"},
    };
};

template<>
struct EnumerationTraits<ui::FocusedFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::FocusedFlag::CHILD_WINDOWS, "CHILD_WINDOWS"},
        EnumeratorName{ui::FocusedFlag::ROOT_WINDOW, "ROOT_WINDOW"},
        EnumeratorName{ui::FocusedFlag::ANY_WINDOW, "ANY_WINDOW"},
    };
};

template<>
struct EnumerationTraits<ui::UiFrameProblem>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiFrameProblem::FRAME_NOT_ACTIVE, "FRAME_NOT_ACTIVE"},
        EnumeratorName{ui::UiFrameProblem::WIDGET_OUTSIDE_WINDOW, "WIDGET_OUTSIDE_WINDOW"},
        EnumeratorName{ui::UiFrameProblem::UNBALANCED_STACK, "UNBALANCED_STACK"},
        EnumeratorName{ui::UiFrameProblem::END_WITHOUT_BEGIN, "END_WITHOUT_BEGIN"},
        EnumeratorName{ui::UiFrameProblem::IDENTIFIER_CONFLICT, "IDENTIFIER_CONFLICT"},
        EnumeratorName{ui::UiFrameProblem::CAPACITY_EXCEEDED, "CAPACITY_EXCEEDED"},
        EnumeratorName{ui::UiFrameProblem::TEXT_LAYOUT, "TEXT_LAYOUT"},
        EnumeratorName{ui::UiFrameProblem::NO_FONT, "NO_FONT"},
        EnumeratorName{ui::UiFrameProblem::NOT_IMPLEMENTED, "NOT_IMPLEMENTED"},
        EnumeratorName{ui::UiFrameProblem::INVALID_ARGUMENT, "INVALID_ARGUMENT"},
        EnumeratorName{ui::UiFrameProblem::PLATFORM_SERVICE, "PLATFORM_SERVICE"},
        EnumeratorName{ui::UiFrameProblem::ISSUES_DROPPED, "ISSUES_DROPPED"},
    };
};

template<>
struct EnumerationTraits<ui::InputTextFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::InputTextFlag::CHARACTERS_DECIMAL, "CHARACTERS_DECIMAL"},
        EnumeratorName{ui::InputTextFlag::CHARACTERS_HEXADECIMAL, "CHARACTERS_HEXADECIMAL"},
        EnumeratorName{ui::InputTextFlag::CHARACTERS_SCIENTIFIC, "CHARACTERS_SCIENTIFIC"},
        EnumeratorName{ui::InputTextFlag::CHARACTERS_UPPERCASE, "CHARACTERS_UPPERCASE"},
        EnumeratorName{ui::InputTextFlag::CHARACTERS_NO_BLANK, "CHARACTERS_NO_BLANK"},
        EnumeratorName{ui::InputTextFlag::ALLOW_TAB_INPUT, "ALLOW_TAB_INPUT"},
        EnumeratorName{ui::InputTextFlag::ENTER_RETURNS_TRUE, "ENTER_RETURNS_TRUE"},
        EnumeratorName{ui::InputTextFlag::ESCAPE_CLEARS_ALL, "ESCAPE_CLEARS_ALL"},
        EnumeratorName{ui::InputTextFlag::CONTROL_ENTER_FOR_NEW_LINE, "CONTROL_ENTER_FOR_NEW_LINE"},
        EnumeratorName{ui::InputTextFlag::READ_ONLY, "READ_ONLY"},
        EnumeratorName{ui::InputTextFlag::PASSWORD, "PASSWORD"},
        EnumeratorName{ui::InputTextFlag::ALWAYS_OVERWRITE, "ALWAYS_OVERWRITE"},
        EnumeratorName{ui::InputTextFlag::AUTO_SELECT_ALL, "AUTO_SELECT_ALL"},
        EnumeratorName{ui::InputTextFlag::NO_HORIZONTAL_SCROLL, "NO_HORIZONTAL_SCROLL"},
        EnumeratorName{ui::InputTextFlag::NO_UNDO_REDO, "NO_UNDO_REDO"},
        EnumeratorName{ui::InputTextFlag::ELIDE_LEFT, "ELIDE_LEFT"},
        EnumeratorName{ui::InputTextFlag::WORD_WRAP, "WORD_WRAP"},
    };
};

template<>
struct EnumerationTraits<ui::SliderFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SliderFlag::LOGARITHMIC, "LOGARITHMIC"},
        EnumeratorName{ui::SliderFlag::NO_ROUND_TO_FORMAT, "NO_ROUND_TO_FORMAT"},
        EnumeratorName{ui::SliderFlag::NO_INPUT, "NO_INPUT"},
        EnumeratorName{ui::SliderFlag::WRAP_AROUND, "WRAP_AROUND"},
        EnumeratorName{ui::SliderFlag::CLAMP_ON_INPUT, "CLAMP_ON_INPUT"},
        EnumeratorName{ui::SliderFlag::CLAMP_ZERO_RANGE, "CLAMP_ZERO_RANGE"},
        EnumeratorName{ui::SliderFlag::NO_SPEED_TWEAKS, "NO_SPEED_TWEAKS"},
    };
};

template<>
struct EnumerationTraits<ui::ColorEditFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ColorEditFlag::NO_ALPHA, "NO_ALPHA"},
        EnumeratorName{ui::ColorEditFlag::NO_PICKER, "NO_PICKER"},
        EnumeratorName{ui::ColorEditFlag::NO_OPTIONS, "NO_OPTIONS"},
        EnumeratorName{ui::ColorEditFlag::NO_SMALL_PREVIEW, "NO_SMALL_PREVIEW"},
        EnumeratorName{ui::ColorEditFlag::NO_INPUTS, "NO_INPUTS"},
        EnumeratorName{ui::ColorEditFlag::NO_TOOLTIP, "NO_TOOLTIP"},
        EnumeratorName{ui::ColorEditFlag::NO_LABEL, "NO_LABEL"},
        EnumeratorName{ui::ColorEditFlag::NO_SIDE_PREVIEW, "NO_SIDE_PREVIEW"},
        EnumeratorName{ui::ColorEditFlag::NO_DRAG_DROP, "NO_DRAG_DROP"},
        EnumeratorName{ui::ColorEditFlag::NO_BORDER, "NO_BORDER"},
        EnumeratorName{ui::ColorEditFlag::ALPHA_BAR, "ALPHA_BAR"},
        EnumeratorName{ui::ColorEditFlag::ALPHA_PREVIEW_HALF, "ALPHA_PREVIEW_HALF"},
        EnumeratorName{ui::ColorEditFlag::HDR, "HDR"},
        EnumeratorName{ui::ColorEditFlag::DISPLAY_RGB, "DISPLAY_RGB"},
        EnumeratorName{ui::ColorEditFlag::DISPLAY_HSV, "DISPLAY_HSV"},
        EnumeratorName{ui::ColorEditFlag::DISPLAY_HEXADECIMAL, "DISPLAY_HEXADECIMAL"},
        EnumeratorName{ui::ColorEditFlag::INTEGER_CHANNELS, "INTEGER_CHANNELS"},
        EnumeratorName{ui::ColorEditFlag::FLOAT_CHANNELS, "FLOAT_CHANNELS"},
        EnumeratorName{ui::ColorEditFlag::PICKER_HUE_BAR, "PICKER_HUE_BAR"},
        EnumeratorName{ui::ColorEditFlag::PICKER_HUE_WHEEL, "PICKER_HUE_WHEEL"},
        EnumeratorName{ui::ColorEditFlag::INPUT_RGB, "INPUT_RGB"},
        EnumeratorName{ui::ColorEditFlag::INPUT_HSV, "INPUT_HSV"},
    };
};

template<>
struct EnumerationTraits<ui::ComboFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ComboFlag::POPUP_ALIGN_LEFT, "POPUP_ALIGN_LEFT"},
        EnumeratorName{ui::ComboFlag::HEIGHT_SMALL, "HEIGHT_SMALL"},
        EnumeratorName{ui::ComboFlag::HEIGHT_REGULAR, "HEIGHT_REGULAR"},
        EnumeratorName{ui::ComboFlag::HEIGHT_LARGE, "HEIGHT_LARGE"},
        EnumeratorName{ui::ComboFlag::HEIGHT_LARGEST, "HEIGHT_LARGEST"},
        EnumeratorName{ui::ComboFlag::NO_ARROW_BUTTON, "NO_ARROW_BUTTON"},
        EnumeratorName{ui::ComboFlag::NO_PREVIEW, "NO_PREVIEW"},
        EnumeratorName{ui::ComboFlag::WIDTH_FIT_PREVIEW, "WIDTH_FIT_PREVIEW"},
    };
};

template<>
struct EnumerationTraits<ui::TreeNodeFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TreeNodeFlag::SELECTED, "SELECTED"},
        EnumeratorName{ui::TreeNodeFlag::FRAMED, "FRAMED"},
        EnumeratorName{ui::TreeNodeFlag::ALLOW_OVERLAP, "ALLOW_OVERLAP"},
        EnumeratorName{ui::TreeNodeFlag::NO_TREE_PUSH_ON_OPEN, "NO_TREE_PUSH_ON_OPEN"},
        EnumeratorName{ui::TreeNodeFlag::DEFAULT_OPEN, "DEFAULT_OPEN"},
        EnumeratorName{ui::TreeNodeFlag::OPEN_ON_DOUBLE_CLICK, "OPEN_ON_DOUBLE_CLICK"},
        EnumeratorName{ui::TreeNodeFlag::OPEN_ON_ARROW, "OPEN_ON_ARROW"},
        EnumeratorName{ui::TreeNodeFlag::LEAF, "LEAF"},
        EnumeratorName{ui::TreeNodeFlag::BULLET, "BULLET"},
        EnumeratorName{ui::TreeNodeFlag::FRAME_PADDING, "FRAME_PADDING"},
        EnumeratorName{ui::TreeNodeFlag::SPAN_AVAILABLE_WIDTH, "SPAN_AVAILABLE_WIDTH"},
        EnumeratorName{ui::TreeNodeFlag::SPAN_FULL_WIDTH, "SPAN_FULL_WIDTH"},
        EnumeratorName{ui::TreeNodeFlag::SPAN_TEXT_WIDTH, "SPAN_TEXT_WIDTH"},
        EnumeratorName{ui::TreeNodeFlag::SPAN_ALL_COLUMNS, "SPAN_ALL_COLUMNS"},
    };
};

template<>
struct EnumerationTraits<ui::TabBarFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TabBarFlag::REORDERABLE, "REORDERABLE"},
        EnumeratorName{ui::TabBarFlag::AUTO_SELECT_NEW_TABS, "AUTO_SELECT_NEW_TABS"},
        EnumeratorName{ui::TabBarFlag::TAB_LIST_POPUP_BUTTON, "TAB_LIST_POPUP_BUTTON"},
        EnumeratorName{ui::TabBarFlag::NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON, "NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON"},
        EnumeratorName{ui::TabBarFlag::NO_TAB_LIST_SCROLLING_BUTTONS, "NO_TAB_LIST_SCROLLING_BUTTONS"},
        EnumeratorName{ui::TabBarFlag::NO_TOOLTIP, "NO_TOOLTIP"},
        EnumeratorName{ui::TabBarFlag::DRAW_SELECTED_OVERLINE, "DRAW_SELECTED_OVERLINE"},
        EnumeratorName{ui::TabBarFlag::FITTING_POLICY_SHRINK, "FITTING_POLICY_SHRINK"},
        EnumeratorName{ui::TabBarFlag::FITTING_POLICY_SCROLL, "FITTING_POLICY_SCROLL"},
    };
};

template<>
struct EnumerationTraits<ui::TabItemFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TabItemFlag::UNSAVED_DOCUMENT, "UNSAVED_DOCUMENT"},
        EnumeratorName{ui::TabItemFlag::SET_SELECTED, "SET_SELECTED"},
        EnumeratorName{ui::TabItemFlag::NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON, "NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON"},
        EnumeratorName{ui::TabItemFlag::NO_PUSH_IDENTIFIER, "NO_PUSH_IDENTIFIER"},
        EnumeratorName{ui::TabItemFlag::NO_TOOLTIP, "NO_TOOLTIP"},
        EnumeratorName{ui::TabItemFlag::NO_REORDER, "NO_REORDER"},
        EnumeratorName{ui::TabItemFlag::LEADING, "LEADING"},
        EnumeratorName{ui::TabItemFlag::TRAILING, "TRAILING"},
    };
};

template<>
struct EnumerationTraits<ui::PopupFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::PopupFlag::MOUSE_BUTTON_LEFT, "MOUSE_BUTTON_LEFT"},
        EnumeratorName{ui::PopupFlag::MOUSE_BUTTON_RIGHT, "MOUSE_BUTTON_RIGHT"},
        EnumeratorName{ui::PopupFlag::MOUSE_BUTTON_MIDDLE, "MOUSE_BUTTON_MIDDLE"},
        EnumeratorName{ui::PopupFlag::NO_REOPEN, "NO_REOPEN"},
        EnumeratorName{ui::PopupFlag::NO_OPEN_OVER_EXISTING_POPUP, "NO_OPEN_OVER_EXISTING_POPUP"},
        EnumeratorName{ui::PopupFlag::NO_OPEN_OVER_ITEMS, "NO_OPEN_OVER_ITEMS"},
        EnumeratorName{ui::PopupFlag::ANY_POPUP_IDENTIFIER, "ANY_POPUP_IDENTIFIER"},
        EnumeratorName{ui::PopupFlag::ANY_POPUP_LEVEL, "ANY_POPUP_LEVEL"},
    };
};

template<>
struct EnumerationTraits<ui::TableFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TableFlag::RESIZABLE, "RESIZABLE"},
        EnumeratorName{ui::TableFlag::REORDERABLE, "REORDERABLE"},
        EnumeratorName{ui::TableFlag::HIDEABLE, "HIDEABLE"},
        EnumeratorName{ui::TableFlag::SORTABLE, "SORTABLE"},
        EnumeratorName{ui::TableFlag::NO_SAVED_SETTINGS, "NO_SAVED_SETTINGS"},
        EnumeratorName{ui::TableFlag::CONTEXT_MENU_IN_BODY, "CONTEXT_MENU_IN_BODY"},
        EnumeratorName{ui::TableFlag::ROW_BACKGROUND, "ROW_BACKGROUND"},
        EnumeratorName{ui::TableFlag::BORDERS_INNER_HORIZONTAL, "BORDERS_INNER_HORIZONTAL"},
        EnumeratorName{ui::TableFlag::BORDERS_OUTER_HORIZONTAL, "BORDERS_OUTER_HORIZONTAL"},
        EnumeratorName{ui::TableFlag::BORDERS_INNER_VERTICAL, "BORDERS_INNER_VERTICAL"},
        EnumeratorName{ui::TableFlag::BORDERS_OUTER_VERTICAL, "BORDERS_OUTER_VERTICAL"},
        EnumeratorName{ui::TableFlag::NO_BORDERS_IN_BODY, "NO_BORDERS_IN_BODY"},
        EnumeratorName{ui::TableFlag::NO_BORDERS_IN_BODY_UNTIL_RESIZE, "NO_BORDERS_IN_BODY_UNTIL_RESIZE"},
        EnumeratorName{ui::TableFlag::SIZING_FIXED_FIT, "SIZING_FIXED_FIT"},
        EnumeratorName{ui::TableFlag::SIZING_FIXED_SAME, "SIZING_FIXED_SAME"},
        EnumeratorName{ui::TableFlag::SIZING_STRETCH_PROPORTIONAL, "SIZING_STRETCH_PROPORTIONAL"},
        EnumeratorName{ui::TableFlag::SIZING_STRETCH_SAME, "SIZING_STRETCH_SAME"},
        EnumeratorName{ui::TableFlag::NO_HOST_EXTEND_X, "NO_HOST_EXTEND_X"},
        EnumeratorName{ui::TableFlag::NO_HOST_EXTEND_Y, "NO_HOST_EXTEND_Y"},
        EnumeratorName{ui::TableFlag::NO_KEEP_COLUMNS_VISIBLE, "NO_KEEP_COLUMNS_VISIBLE"},
        EnumeratorName{ui::TableFlag::PRECISE_WIDTHS, "PRECISE_WIDTHS"},
        EnumeratorName{ui::TableFlag::NO_CLIP, "NO_CLIP"},
        EnumeratorName{ui::TableFlag::PAD_OUTER_X, "PAD_OUTER_X"},
        EnumeratorName{ui::TableFlag::NO_PAD_OUTER_X, "NO_PAD_OUTER_X"},
        EnumeratorName{ui::TableFlag::NO_PAD_INNER_X, "NO_PAD_INNER_X"},
        EnumeratorName{ui::TableFlag::SCROLL_X, "SCROLL_X"},
        EnumeratorName{ui::TableFlag::SCROLL_Y, "SCROLL_Y"},
        EnumeratorName{ui::TableFlag::SORT_MULTIPLE, "SORT_MULTIPLE"},
        EnumeratorName{ui::TableFlag::SORT_TRISTATE, "SORT_TRISTATE"},
        EnumeratorName{ui::TableFlag::HIGHLIGHT_HOVERED_COLUMN, "HIGHLIGHT_HOVERED_COLUMN"},
    };
};

template<>
struct EnumerationTraits<ui::TableColumnFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TableColumnFlag::DISABLED, "DISABLED"},
        EnumeratorName{ui::TableColumnFlag::DEFAULT_HIDE, "DEFAULT_HIDE"},
        EnumeratorName{ui::TableColumnFlag::DEFAULT_SORT, "DEFAULT_SORT"},
        EnumeratorName{ui::TableColumnFlag::WIDTH_STRETCH, "WIDTH_STRETCH"},
        EnumeratorName{ui::TableColumnFlag::WIDTH_FIXED, "WIDTH_FIXED"},
        EnumeratorName{ui::TableColumnFlag::NO_RESIZE, "NO_RESIZE"},
        EnumeratorName{ui::TableColumnFlag::NO_REORDER, "NO_REORDER"},
        EnumeratorName{ui::TableColumnFlag::NO_HIDE, "NO_HIDE"},
        EnumeratorName{ui::TableColumnFlag::NO_CLIP, "NO_CLIP"},
        EnumeratorName{ui::TableColumnFlag::NO_SORT, "NO_SORT"},
        EnumeratorName{ui::TableColumnFlag::NO_SORT_ASCENDING, "NO_SORT_ASCENDING"},
        EnumeratorName{ui::TableColumnFlag::NO_SORT_DESCENDING, "NO_SORT_DESCENDING"},
        EnumeratorName{ui::TableColumnFlag::NO_HEADER_LABEL, "NO_HEADER_LABEL"},
        EnumeratorName{ui::TableColumnFlag::NO_HEADER_WIDTH, "NO_HEADER_WIDTH"},
        EnumeratorName{ui::TableColumnFlag::PREFER_SORT_ASCENDING, "PREFER_SORT_ASCENDING"},
        EnumeratorName{ui::TableColumnFlag::PREFER_SORT_DESCENDING, "PREFER_SORT_DESCENDING"},
        EnumeratorName{ui::TableColumnFlag::INDENT_ENABLE, "INDENT_ENABLE"},
        EnumeratorName{ui::TableColumnFlag::INDENT_DISABLE, "INDENT_DISABLE"},
        EnumeratorName{ui::TableColumnFlag::ANGLED_HEADER, "ANGLED_HEADER"},
    };
};

template<>
struct EnumerationTraits<ui::TableRowFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TableRowFlag::HEADERS, "HEADERS"},
    };
};

template<>
struct EnumerationTraits<ui::SortDirection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SortDirection::NONE, "NONE"},
        EnumeratorName{ui::SortDirection::ASCENDING, "ASCENDING"},
        EnumeratorName{ui::SortDirection::DESCENDING, "DESCENDING"},
    };
};

template<>
struct EnumerationTraits<ui::DragDropFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::DragDropFlag::SOURCE_NO_PREVIEW_TOOLTIP, "SOURCE_NO_PREVIEW_TOOLTIP"},
        EnumeratorName{ui::DragDropFlag::SOURCE_NO_DISABLE_HOVER, "SOURCE_NO_DISABLE_HOVER"},
        EnumeratorName{ui::DragDropFlag::SOURCE_NO_HOLD_TO_OPEN_OTHERS, "SOURCE_NO_HOLD_TO_OPEN_OTHERS"},
        EnumeratorName{ui::DragDropFlag::SOURCE_ALLOW_NULL_IDENTIFIER, "SOURCE_ALLOW_NULL_IDENTIFIER"},
        EnumeratorName{ui::DragDropFlag::SOURCE_EXTERNAL, "SOURCE_EXTERNAL"},
        EnumeratorName{ui::DragDropFlag::PAYLOAD_AUTO_EXPIRE, "PAYLOAD_AUTO_EXPIRE"},
        EnumeratorName{ui::DragDropFlag::ACCEPT_BEFORE_DELIVERY, "ACCEPT_BEFORE_DELIVERY"},
        EnumeratorName{ui::DragDropFlag::ACCEPT_NO_DRAW_DEFAULT_RECTANGLE, "ACCEPT_NO_DRAW_DEFAULT_RECTANGLE"},
        EnumeratorName{ui::DragDropFlag::ACCEPT_NO_PREVIEW_TOOLTIP, "ACCEPT_NO_PREVIEW_TOOLTIP"},
    };
};

template<>
struct EnumerationTraits<ui::DockNodeFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::DockNodeFlag::KEEP_ALIVE_ONLY, "KEEP_ALIVE_ONLY"},
        EnumeratorName{ui::DockNodeFlag::NO_DOCKING_OVER_CENTRAL_NODE, "NO_DOCKING_OVER_CENTRAL_NODE"},
        EnumeratorName{ui::DockNodeFlag::PASS_THROUGH_CENTRAL_NODE, "PASS_THROUGH_CENTRAL_NODE"},
        EnumeratorName{ui::DockNodeFlag::NO_DOCKING_SPLIT, "NO_DOCKING_SPLIT"},
        EnumeratorName{ui::DockNodeFlag::NO_RESIZE, "NO_RESIZE"},
        EnumeratorName{ui::DockNodeFlag::AUTO_HIDE_TAB_BAR, "AUTO_HIDE_TAB_BAR"},
        EnumeratorName{ui::DockNodeFlag::NO_UNDOCKING, "NO_UNDOCKING"},
    };
};

template<>
struct EnumerationTraits<ui::ShortcutFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ShortcutFlag::REPEAT, "REPEAT"},
        EnumeratorName{ui::ShortcutFlag::ROUTE_ACTIVE, "ROUTE_ACTIVE"},
        EnumeratorName{ui::ShortcutFlag::ROUTE_FOCUSED, "ROUTE_FOCUSED"},
        EnumeratorName{ui::ShortcutFlag::ROUTE_GLOBAL, "ROUTE_GLOBAL"},
        EnumeratorName{ui::ShortcutFlag::ROUTE_ALWAYS, "ROUTE_ALWAYS"},
    };
};

template<>
struct EnumerationTraits<ui::AccessibilityRole>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AccessibilityRole::GENERIC, "GENERIC"},
        EnumeratorName{ui::AccessibilityRole::WINDOW, "WINDOW"},
        EnumeratorName{ui::AccessibilityRole::DIALOG, "DIALOG"},
        EnumeratorName{ui::AccessibilityRole::GROUP, "GROUP"},
        EnumeratorName{ui::AccessibilityRole::STATIC_TEXT, "STATIC_TEXT"},
        EnumeratorName{ui::AccessibilityRole::LINK, "LINK"},
        EnumeratorName{ui::AccessibilityRole::BUTTON, "BUTTON"},
        EnumeratorName{ui::AccessibilityRole::CHECK_BOX, "CHECK_BOX"},
        EnumeratorName{ui::AccessibilityRole::RADIO_BUTTON, "RADIO_BUTTON"},
        EnumeratorName{ui::AccessibilityRole::SWITCH, "SWITCH"},
        EnumeratorName{ui::AccessibilityRole::SLIDER, "SLIDER"},
        EnumeratorName{ui::AccessibilityRole::SPIN_BUTTON, "SPIN_BUTTON"},
        EnumeratorName{ui::AccessibilityRole::TEXT_FIELD, "TEXT_FIELD"},
        EnumeratorName{ui::AccessibilityRole::COMBO_BOX, "COMBO_BOX"},
        EnumeratorName{ui::AccessibilityRole::LIST, "LIST"},
        EnumeratorName{ui::AccessibilityRole::LIST_ITEM, "LIST_ITEM"},
        EnumeratorName{ui::AccessibilityRole::MENU_BAR, "MENU_BAR"},
        EnumeratorName{ui::AccessibilityRole::MENU, "MENU"},
        EnumeratorName{ui::AccessibilityRole::MENU_ITEM, "MENU_ITEM"},
        EnumeratorName{ui::AccessibilityRole::TAB_LIST, "TAB_LIST"},
        EnumeratorName{ui::AccessibilityRole::TAB, "TAB"},
        EnumeratorName{ui::AccessibilityRole::TAB_PANEL, "TAB_PANEL"},
        EnumeratorName{ui::AccessibilityRole::TREE, "TREE"},
        EnumeratorName{ui::AccessibilityRole::TREE_ITEM, "TREE_ITEM"},
        EnumeratorName{ui::AccessibilityRole::TABLE, "TABLE"},
        EnumeratorName{ui::AccessibilityRole::ROW, "ROW"},
        EnumeratorName{ui::AccessibilityRole::CELL, "CELL"},
        EnumeratorName{ui::AccessibilityRole::COLUMN_HEADER, "COLUMN_HEADER"},
        EnumeratorName{ui::AccessibilityRole::TOOLTIP, "TOOLTIP"},
        EnumeratorName{ui::AccessibilityRole::PROGRESS_BAR, "PROGRESS_BAR"},
        EnumeratorName{ui::AccessibilityRole::SCROLL_BAR, "SCROLL_BAR"},
        EnumeratorName{ui::AccessibilityRole::SEPARATOR, "SEPARATOR"},
        EnumeratorName{ui::AccessibilityRole::IMAGE, "IMAGE"},
    };
};

template<>
struct EnumerationTraits<ui::AccessibilityLiveRegion>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AccessibilityLiveRegion::OFF, "OFF"},
        EnumeratorName{ui::AccessibilityLiveRegion::POLITE, "POLITE"},
        EnumeratorName{ui::AccessibilityLiveRegion::ASSERTIVE, "ASSERTIVE"},
    };
};

template<>
struct EnumerationTraits<ui::TextMotion>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextMotion::PREVIOUS_GRAPHEME, "PREVIOUS_GRAPHEME"},
        EnumeratorName{ui::TextMotion::NEXT_GRAPHEME, "NEXT_GRAPHEME"},
        EnumeratorName{ui::TextMotion::PREVIOUS_WORD, "PREVIOUS_WORD"},
        EnumeratorName{ui::TextMotion::NEXT_WORD, "NEXT_WORD"},
        EnumeratorName{ui::TextMotion::LINE_START, "LINE_START"},
        EnumeratorName{ui::TextMotion::LINE_END, "LINE_END"},
        EnumeratorName{ui::TextMotion::TEXT_START, "TEXT_START"},
        EnumeratorName{ui::TextMotion::TEXT_END, "TEXT_END"},
    };
};

} // namespace gfx

// =================================================================================================
// Verification: names, order and completeness of every description, at compile time
// =================================================================================================
namespace gfx::enumeration_verification
{

static_assert(verify<ui::UiSubsystem>(), "EnumerationTraits<ui::UiSubsystem>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::FontFormat>(), "EnumerationTraits<ui::FontFormat>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::FontDecoderKind>(), "EnumerationTraits<ui::FontDecoderKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SdfKind>(), "EnumerationTraits<ui::SdfKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasTexelFormat>(), "EnumerationTraits<ui::AtlasTexelFormat>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasCompression>(), "EnumerationTraits<ui::AtlasCompression>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasSectionKind>(), "EnumerationTraits<ui::AtlasSectionKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasPackerKind>(), "EnumerationTraits<ui::AtlasPackerKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextDirection>(), "EnumerationTraits<ui::TextDirection>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::CharacterSet>(), "EnumerationTraits<ui::CharacterSet>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::OutlineSegmentKind>(), "EnumerationTraits<ui::OutlineSegmentKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SdfGeneratorKind>(), "EnumerationTraits<ui::SdfGeneratorKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::EdgeColoring>(), "EnumerationTraits<ui::EdgeColoring>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasGlyphFlag>(), "EnumerationTraits<ui::AtlasGlyphFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasVerification>(), "EnumerationTraits<ui::AtlasVerification>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasProblem>(), "EnumerationTraits<ui::AtlasProblem>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasEmbedding>(), "EnumerationTraits<ui::AtlasEmbedding>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::DrawTextureKind>(), "EnumerationTraits<ui::DrawTextureKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::UiShaderMode>(), "EnumerationTraits<ui::UiShaderMode>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ParagraphDirection>(), "EnumerationTraits<ui::ParagraphDirection>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextAlignment>(), "EnumerationTraits<ui::TextAlignment>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextShaping>(), "EnumerationTraits<ui::TextShaping>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextLayoutProblem>(), "EnumerationTraits<ui::TextLayoutProblem>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::StyleColor>(), "EnumerationTraits<ui::StyleColor>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::StyleVariable>(), "EnumerationTraits<ui::StyleVariable>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ThemeKind>(), "EnumerationTraits<ui::ThemeKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::UiWindowFlag>(), "EnumerationTraits<ui::UiWindowFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ChildFlag>(), "EnumerationTraits<ui::ChildFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ButtonFlag>(), "EnumerationTraits<ui::ButtonFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SelectableFlag>(), "EnumerationTraits<ui::SelectableFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::UiCondition>(), "EnumerationTraits<ui::UiCondition>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ArrowDirection>(), "EnumerationTraits<ui::ArrowDirection>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::HoveredFlag>(), "EnumerationTraits<ui::HoveredFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::FocusedFlag>(), "EnumerationTraits<ui::FocusedFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::UiFrameProblem>(), "EnumerationTraits<ui::UiFrameProblem>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::InputTextFlag>(), "EnumerationTraits<ui::InputTextFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SliderFlag>(), "EnumerationTraits<ui::SliderFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ColorEditFlag>(), "EnumerationTraits<ui::ColorEditFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ComboFlag>(), "EnumerationTraits<ui::ComboFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TreeNodeFlag>(), "EnumerationTraits<ui::TreeNodeFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TabBarFlag>(), "EnumerationTraits<ui::TabBarFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TabItemFlag>(), "EnumerationTraits<ui::TabItemFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::PopupFlag>(), "EnumerationTraits<ui::PopupFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TableFlag>(), "EnumerationTraits<ui::TableFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TableColumnFlag>(), "EnumerationTraits<ui::TableColumnFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TableRowFlag>(), "EnumerationTraits<ui::TableRowFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SortDirection>(), "EnumerationTraits<ui::SortDirection>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::DragDropFlag>(), "EnumerationTraits<ui::DragDropFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::DockNodeFlag>(), "EnumerationTraits<ui::DockNodeFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ShortcutFlag>(), "EnumerationTraits<ui::ShortcutFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AccessibilityRole>(), "EnumerationTraits<ui::AccessibilityRole>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AccessibilityLiveRegion>(), "EnumerationTraits<ui::AccessibilityLiveRegion>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextMotion>(), "EnumerationTraits<ui::TextMotion>::NAMES is out of sync with the enumeration");
static_assert(ui::CHARACTER_SET_RANGES.size() == ENUMERATOR_COUNT<ui::CharacterSet>,
              "CHARACTER_SET_RANGES needs one range per CharacterSet");
static_assert(std::ranges::all_of(ui::CHARACTER_SET_RANGES, [](const ui::CharacterRange& range) { return range.first <= range.last; }),
              "every range of CHARACTER_SET_RANGES starts before it ends");

} // namespace gfx::enumeration_verification
