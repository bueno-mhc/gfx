# The `.uiatlas` format, version 1.0

A `.uiatlas` file holds everything the UI library needs to draw the text of one font at one instance:
distance-field pages, glyph metrics and boxes, the character map, pair kerning and standard ligatures for
the basic shaper, and a subset of the font for HarfBuzz. No outline is decoded and no glyph is rasterized
when an atlas is loaded.

- **Writers**: `gfx::ui::write_atlas()` (`gfx.ui.atlas_tools`), used by `AtlasBuilder`, the atlas tool
  `gfx_ui_atlas_builder` and `gfx_add_ui_atlas()` ([cmake/GfxUiAtlas.cmake](../cmake/GfxUiAtlas.cmake)).
- **Readers**: `gfx::ui::AtlasView` (`gfx.ui.atlas_format`, part of `import gfx.ui;`), at run time and in
  constant expressions. Record offsets are defined once, in `gfx::ui::atlas_layout`
  ([atlas_format.cppm](../source/ui/font/atlas/atlas_format.cppm)).

## Conventions

| Topic | Rule |
|---|---|
| Byte order | Little-endian for every integer and float (IEEE 754 binary32). |
| Alignment | The header, the section table and every section start at a multiple of 8 bytes (`UI_ATLAS_ALIGNMENT`); the blob's size is a multiple of 8. Padding bytes are 0. |
| Offsets | From the first byte of the blob. |
| Checksums | xxHash64 (seed 0) of the bytes concerned (`gfx::ui::xxhash64`, checked against xxHash's reference implementation). |
| Enumerations | Stored as the value of the `gfx::ui` enumeration (`SdfKind`, `AtlasTexelFormat`, ...); these enumerations only ever gain enumerators at their end. |
| Glyph indices | Indices in the atlas's shaping font (the subset), not in the original font file. Glyph 0 is `.notdef`, drawn for characters the atlas lacks. |
| Font units | FACE_METRICS keeps the font's units (`units_per_em` converts them); every other length is in em (glyph planes, advances, kerning) or texels (boxes, distance range, padding). |

## Header (64 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | char[8] | Magic `GFXUIATL` (`UI_ATLAS_MAGIC`) |
| 8 | uint16 | Major version, 1 (`UI_ATLAS_VERSION_MAJOR`) |
| 10 | uint16 | Minor version, 0 (`UI_ATLAS_VERSION_MINOR`) |
| 12 | uint32 | Endianness tag `0x01020304` (`UI_ATLAS_ENDIANNESS_TAG`): reads `0x04030201` on a big-endian blob |
| 16 | uint32 | Flags (none defined: 0) |
| 20 | uint32 | Section count |
| 24 | uint64 | Total size of the blob |
| 32 | uint64 | xxHash64 of bytes 64 to the end (section table and sections) |
| 40 | uint64 | xxHash64 of the font file the atlas was built from |
| 48 | uint32 | Generator version (`UI_ATLAS_GENERATOR_VERSION`, raised whenever the same inputs would give other bytes) |
| 52 | uint32 | Offset of the section table (64) |
| 56 | uint64 | Reserved (0) |

## Section table

`section count` entries of 40 bytes, at the offset the header gives:

| Offset | Type | Field |
|---|---|---|
| 0 | uint32 | Kind (`AtlasSectionKind`); readers skip kinds they do not know |
| 4 | uint32 | Compression (`AtlasCompression`: 0 none, 1 one zstd frame) |
| 8 | uint64 | Offset of the section (aligned, after the table) |
| 16 | uint64 | Stored size (bytes in the blob) |
| 24 | uint64 | Uncompressed size |
| 32 | uint64 | xxHash64 of the stored bytes |

Writers place sections in this order: DESCRIPTOR, FACE_METRICS, GLYPHS, CHARACTER_MAP, KERNING,
LIGATURES, SHAPING_FONT, then one PIXELS section per page. DESCRIPTOR, FACE_METRICS, GLYPHS and
CHARACTER_MAP are required; KERNING, LIGATURES and SHAPING_FONT are omitted when empty. Only PIXELS
appears more than once. Every section but SHAPING_FONT and PIXELS is stored uncompressed, so readers
(constant expressions included) read it in place.

## DESCRIPTOR (64 bytes)

The parameters the atlas was built with.

| Offset | Type | Field |
|---|---|---|
| 0 | uint8 | `SdfKind`: SDF, PSDF, MSDF, MTSDF |
| 1 | uint8 | `AtlasTexelFormat`: R8_UNORM, R8G8B8A8_UNORM, R16_SFLOAT, R16G16B16A16_SFLOAT |
| 2 | uint8 | `AtlasPackerKind` |
| 3 | uint8 | `FontDecoderKind` |
| 4 | uint8 | `SdfGeneratorKind` |
| 5 | uint8 | `EdgeColoring` |
| 6 | uint8 | Options: bit 0 overlap support, bit 1 scanline (sign correction) pass |
| 7 | uint8 | Reserved |
| 8, 12 | uint32 | Page width and height, texels (every page has this size) |
| 16 | uint32 | Page count (= PIXELS sections) |
| 20 | uint32 | Glyph count (= GLYPHS records) |
| 24 | float | Em size: texels per em |
| 28 | float | Distance range: texels between the field's 0 and 1 |
| 32 | float | Padding: texels added around every glyph, inside its quad |
| 36 | float | Angle threshold of the edge coloring, radians |
| 40 | float | Miter limit |
| 44 | uint32 | Face index in a collection |
| 48 | uint32 | Spacing between boxes, texels |
| 52 | — | Reserved (0) |

SDF and PSDF pages have one channel (R8_UNORM, R16_SFLOAT); MSDF and MTSDF pages have four
(R8G8B8A8_UNORM, R16G16B16A16_SFLOAT); MSDF's fourth channel is 1.

## FACE_METRICS

| Offset | Type | Field |
|---|---|---|
| 0 | uint32 | Units per em |
| 4 | int32 | Ascender (OS/2 typographic metrics when the font sets USE_TYPO_METRICS, hhea otherwise) |
| 8 | int32 | Descender (negative) |
| 12 | int32 | Line gap |
| 16 | int32 | x-height (0 when the font gives none) |
| 20 | int32 | Cap height (0 when the font gives none) |
| 24 | int32 | Underline position: top of the underline |
| 28 | int32 | Underline thickness |
| 32 | uint32 | Axis count |
| 36 | uint32 | Size of the family name, bytes |
| 40 | uint32 | Size of the style name, bytes |
| 44 | — | Reserved |
| 48 | — | Axis records: uint32 OpenType tag, float value (8 bytes each), one per axis of a variable font, at the value the atlas was built at |
| — | — | Family name, then style name (UTF-8, not terminated) |

The style of an atlas built at a named instance is that instance's name ("Bold").

## Tables: GLYPHS, CHARACTER_MAP, KERNING, LIGATURES

Each starts with a uint32 record count and a uint32 record size, then the records. A later minor version
may grow records; readers use the stored record size and refuse sizes smaller than theirs.

**GLYPHS** (48-byte records, sorted by glyph index, every glyph of the shaping font):

| Offset | Type | Field |
|---|---|---|
| 0 | uint32 | Glyph index |
| 4 | uint16 | `AtlasGlyphFlag`: WHITESPACE (no outline, no box), COLOR_LAYERS (the font draws it in color; the atlas holds its monochrome outline), NO_OUTLINE (bitmap-only glyph, drawn as nothing) |
| 6 | uint16 | Page |
| 8 | float | Advance, em |
| 12–24 | float × 4 | Plane bounds: left, bottom, right, top, em from the pen position on the baseline, **y up** |
| 28–40 | float × 4 | Texel bounds: left, top, right, bottom, texels from the page's top-left corner, **y down** |
| 44 | — | Reserved |

The quad of a glyph goes from its plane bounds (scaled by the font size) and samples its texel bounds,
which run from the first texel center of its box to the last (half a texel inside the box). A glyph
without a box has zero bounds.

**CHARACTER_MAP** (8-byte records, sorted by code point): uint32 code point, uint32 glyph index — the
characters requested that the font has.

**KERNING** (12-byte records, sorted by left then right glyph): uint32 left glyph, uint32 right glyph, float
adjustment added to the left glyph's advance (em). For the basic shaper: pairs of characters of Latin,
Greek, Cyrillic and common punctuation, measured by shaping each pair with HarfBuzz (GPOS and `kern`).

**LIGATURES** (24-byte records, sorted by first component, longest first, then the other components):
uint32 component count (2 to `MAXIMUM_LIGATURE_COMPONENTS` = 4), uint32 components[4] (unused ones 0),
uint32 ligature glyph — the standard ligatures HarfBuzz forms for those scripts.

## SHAPING_FONT

An OpenType font (sfnt): the subset hb-subset makes of the original font, holding the requested
characters, every glyph its layout tables can substitute them with (ligatures, contextual and positional
forms: hb-subset's default feature set), its layout tables (GSUB, GPOS, GDEF), the outline of `.notdef`,
no hinting, and — for a variable font — every axis pinned to the atlas's instance (a static font). Its
glyph indices are the atlas's. zstd-compressed by default.

## PIXELS

One section per page, in page order: page width × height texels of the descriptor's texel format, rows
from the top, channels interleaved, no row padding; zstd-compressed by default. Distance values map 0.5 to
the outline, 1 to half the distance range inside and 0 as far outside (8-bit formats round
`clamp(value, 0, 1) × 255`; 16-bit formats store IEEE binary16 values, unclamped). Texels outside every box
are 0 (far outside).

## Versions and validation

Readers accept every minor version of their major version (unknown sections skipped, larger records read
by their known prefix) and refuse other major versions (`AtlasProblem::UNSUPPORTED_VERSION`, with both
versions in the message). `AtlasView::parse()` checks the header (magic, endianness, version, size), the
section table (bounds, alignment, compression, duplicates, required sections), the descriptor (values in
range, texel format fitting the kind, page sizes), the tables (sizes, ordering, references to glyphs and
pages) and, with `AtlasVerification::CHECKSUMS` (the default), every checksum; each failure is an
`AtlasReadError` that `describe_atlas_error()` explains.

## Reproducibility

The same font, description, generator version and toolchain give the same bytes, whatever the number of
threads (each glyph is generated on its own; zstd runs single-threaded at a fixed level). The libraries that
shape the bytes (FreeType, msdfgen, msdf-atlas-gen, HarfBuzz, zstd) are pinned. Decoders do not agree to the
byte: stb_truetype and FreeType give the same records, but stb_truetype truncates fractional CFF
coordinates FreeType rounds, and the two may start contours at other points, which changes msdfgen's edge
coloring (true distances stay within 2/255).
