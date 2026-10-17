# ==============================================================================
# gfx_add_ui_atlas(): builds a .uiatlas font atlas (SDF, PSDF, MSDF or MTSDF) at build time with the
# atlas tool (gfx_ui_atlas_builder) and, optionally, embeds it into a C++ module of the target.
# ==============================================================================
include_guard(GLOBAL)

# Values the tool accepts for the options that name an enumerator (checked when CMake configures).
set(_GFX_UI_ATLAS_KINDS SDF PSDF MSDF MTSDF)
set(_GFX_UI_ATLAS_DECODERS FREETYPE STB_TRUETYPE)
set(_GFX_UI_ATLAS_GENERATORS MSDFGEN BUILT_IN)
set(_GFX_UI_ATLAS_COLORINGS SIMPLE INK_TRAP DISTANCE)
set(_GFX_UI_ATLAS_TEXEL_FORMATS R8_UNORM R8G8B8A8_UNORM R16_SFLOAT R16G16B16A16_SFLOAT)
set(_GFX_UI_ATLAS_PACKERS TIGHT GRID)
set(_GFX_UI_ATLAS_COMPRESSIONS ZSTD NONE)
set(_GFX_UI_ATLAS_EMBEDDINGS EMBED_DIRECTIVE BYTE_LIST)

# gfx_add_ui_atlas(<target>
#     NAME <identifier>                atlas <identifier>.uiatlas; with EMBED, also the C++ constant
#     FONT <file>                      TrueType, OpenType (CFF, CFF2), collection, WOFF or WOFF2 font
#     [CHARACTERS <spec>...]           CharacterSet names (BASIC_LATIN LATIN_1_SUPPLEMENT ARROWS ...),
#                                      code points U+00B1, ranges U+2190-U+21FF
#     [TEXT_FILES <file>...]           every character of UTF-8 text files (rebuilt when they change)
#     [FACE_INDEX <n>]                 face of a collection
#     [VARIATION <tag=value>...]       axis values of a variable font (wght=700)
#     [NAMED_INSTANCE <name>]          or one of its named instances (Bold)
#     [DECODER FREETYPE|STB_TRUETYPE]  default FREETYPE
#     [KIND SDF|PSDF|MSDF|MTSDF]       default MTSDF
#     [SIZE <texels per em>]           default 32
#     [RANGE <texels>]                 distance range, default 4
#     [PADDING <texels>]               room around each glyph inside its quad, default 0
#     [ANGLE_THRESHOLD <radians>] [MITER_LIMIT <value>] [COLORING SIMPLE|INK_TRAP|DISTANCE]
#     [GENERATOR MSDFGEN|BUILT_IN] [TEXEL_FORMAT R8_UNORM|R8G8B8A8_UNORM|R16_SFLOAT|R16G16B16A16_SFLOAT]
#     [PACKER TIGHT|GRID] [MAXIMUM_DIMENSION <n>] [SPACING <n>] [NO_POWER_OF_TWO]
#     [NO_SHAPING_FONT] [NO_KERNING] [NO_LIGATURES] [COMPRESSION ZSTD|NONE] [COMPRESSION_LEVEL <n>]
#     [REQUIRE_ALL_CHARACTERS]         fail the build when the font lacks a character
#     [OUTPUT_VARIABLE <variable>]     receives the path of the .uiatlas
#     [EMBED                           also generate a module holding the atlas:
#      [MODULE <name>]                 its name (default: <target>.<identifier>, made identifiers)
#      [NAMESPACE <namespace>]         namespace of the constant (default: global)
#      [EMBEDDING EMBED_DIRECTIVE|BYTE_LIST]  default #embed on GCC 15 and Clang 19 or later
#      [PUBLIC]])                      export the module to the target's consumers (default: private)
#
# The module exports `inline constexpr std::array<std::byte, N> <identifier>`, which gfx::ui::AtlasView
# reads at run time and in constant expressions:
#   static_assert(gfx::ui::AtlasView::parse(editor_font)->descriptor().kind == gfx::ui::SdfKind::MTSDF);
# The atlas is rebuilt when the font, a text file or the tool changes (depfile). The same inputs and tool
# version give the same bytes.
function(gfx_add_ui_atlas target)
    cmake_parse_arguments(PARSE_ARGV 1 argument
        "NO_POWER_OF_TWO;NO_SHAPING_FONT;NO_KERNING;NO_LIGATURES;REQUIRE_ALL_CHARACTERS;EMBED;PUBLIC"
        "NAME;FONT;FACE_INDEX;NAMED_INSTANCE;DECODER;KIND;SIZE;RANGE;PADDING;ANGLE_THRESHOLD;MITER_LIMIT;COLORING;GENERATOR;TEXEL_FORMAT;PACKER;MAXIMUM_DIMENSION;SPACING;COMPRESSION;COMPRESSION_LEVEL;OUTPUT_VARIABLE;MODULE;NAMESPACE;EMBEDDING"
        "CHARACTERS;TEXT_FILES;VARIATION")
    set(context "gfx_add_ui_atlas(${target})")
    if(argument_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${context}: unknown arguments ${argument_UNPARSED_ARGUMENTS}.")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "${context}: ${target} is not a target.")
    endif()
    if(NOT TARGET gfx_ui_atlas_builder)
        message(FATAL_ERROR "${context}: the atlas tool is not built.\n"
                            "  Suggestion: configure with GFX_BUILD_UI=ON and GFX_BUILD_UI_ATLAS_TOOLS=ON.")
    endif()

    # The description, checked now rather than when the build runs the tool.
    if(NOT argument_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "${context}: NAME '${argument_NAME}' must be a C++ identifier (letters, digits, underscores).")
    endif()
    set(name "${argument_NAME}")
    if(NOT argument_FONT)
        message(FATAL_ERROR "${context}: no FONT was given.")
    endif()
    cmake_path(ABSOLUTE_PATH argument_FONT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE font)
    if(NOT EXISTS "${font}")
        message(FATAL_ERROR "${context}: the font '${font}' does not exist.")
    endif()
    if(NOT argument_CHARACTERS AND NOT argument_TEXT_FILES)
        message(FATAL_ERROR "${context}: give CHARACTERS (BASIC_LATIN, U+00B1, U+2190-U+21FF) or TEXT_FILES.")
    endif()
    if(argument_VARIATION AND argument_NAMED_INSTANCE)
        message(FATAL_ERROR "${context}: give VARIATION or NAMED_INSTANCE, not both.")
    endif()
    foreach(pair IN ITEMS "DECODER;DECODERS" "KIND;KINDS" "GENERATOR;GENERATORS" "COLORING;COLORINGS" "TEXEL_FORMAT;TEXEL_FORMATS"
                          "PACKER;PACKERS" "COMPRESSION;COMPRESSIONS" "EMBEDDING;EMBEDDINGS")
        list(GET pair 0 option)
        list(GET pair 1 values)
        if(DEFINED argument_${option} AND NOT argument_${option} IN_LIST _GFX_UI_ATLAS_${values})
            message(FATAL_ERROR "${context}: ${option} ${argument_${option}} is not one of: ${_GFX_UI_ATLAS_${values}}.")
        endif()
    endforeach()
    foreach(option IN ITEMS SIZE RANGE PADDING ANGLE_THRESHOLD MITER_LIMIT)
        if(DEFINED argument_${option} AND NOT argument_${option} MATCHES "^[0-9]+(\\.[0-9]+)?$")
            message(FATAL_ERROR "${context}: ${option} ${argument_${option}} is not a number.")
        endif()
    endforeach()
    foreach(option IN ITEMS FACE_INDEX MAXIMUM_DIMENSION SPACING COMPRESSION_LEVEL)
        if(DEFINED argument_${option} AND NOT argument_${option} MATCHES "^[0-9]+$")
            message(FATAL_ERROR "${context}: ${option} ${argument_${option}} is not a whole number.")
        endif()
    endforeach()
    foreach(variation IN LISTS argument_VARIATION)
        if(NOT variation MATCHES "^[A-Za-z0-9 ]+=-?[0-9]+(\\.[0-9]+)?$")
            message(FATAL_ERROR "${context}: VARIATION ${variation} is not <tag>=<value> (wght=700).")
        endif()
    endforeach()

    # The tool's options.
    set(arguments --font "${font}")
    foreach(characters IN LISTS argument_CHARACTERS)
        list(APPEND arguments --characters "${characters}")
    endforeach()
    set(text_files "")
    foreach(file IN LISTS argument_TEXT_FILES)
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE text_file)
        list(APPEND text_files "${text_file}")
        list(APPEND arguments --text-file "${text_file}")
    endforeach()
    if(argument_VARIATION)
        list(JOIN argument_VARIATION "," variation)
        list(APPEND arguments --variation "${variation}")
    endif()
    foreach(pair IN ITEMS "FACE_INDEX;--face-index" "NAMED_INSTANCE;--named-instance" "DECODER;--decoder" "KIND;--kind" "SIZE;--size"
                          "RANGE;--range" "PADDING;--padding" "ANGLE_THRESHOLD;--angle-threshold" "MITER_LIMIT;--miter-limit"
                          "COLORING;--coloring" "GENERATOR;--generator" "TEXEL_FORMAT;--texel-format" "PACKER;--packer"
                          "MAXIMUM_DIMENSION;--maximum-dimension" "SPACING;--spacing" "COMPRESSION;--compression"
                          "COMPRESSION_LEVEL;--compression-level")
        list(GET pair 0 option)
        list(GET pair 1 flag)
        if(DEFINED argument_${option})
            list(APPEND arguments ${flag} "${argument_${option}}")
        endif()
    endforeach()
    foreach(pair IN ITEMS "NO_POWER_OF_TWO;--no-power-of-two" "NO_SHAPING_FONT;--no-shaping-font" "NO_KERNING;--no-kerning"
                          "NO_LIGATURES;--no-ligatures" "REQUIRE_ALL_CHARACTERS;--require-all-characters")
        list(GET pair 0 option)
        list(GET pair 1 flag)
        if(argument_${option})
            list(APPEND arguments ${flag})
        endif()
    endforeach()

    # Outputs: the atlas, its depfile, and the module when it is embedded.
    set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/gfx_ui_atlases/${target}")
    file(MAKE_DIRECTORY "${output_directory}")
    set(atlas "${output_directory}/${name}.uiatlas")
    set(depfile "${output_directory}/${name}.uiatlas.d")
    set(outputs "${atlas}")
    if(argument_EMBED)
        if(NOT argument_MODULE)
            string(MAKE_C_IDENTIFIER "${target}" module_target)
            set(argument_MODULE "${module_target}.${name}")
        endif()
        if(NOT argument_EMBEDDING)
            set(argument_EMBEDDING BYTE_LIST)
            if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
               OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19))
                set(argument_EMBEDDING EMBED_DIRECTIVE)
            endif()
        endif()
        string(REPLACE "." "_" module_file_name "${argument_MODULE}")
        set(module_file "${output_directory}/${module_file_name}.cppm")
        list(APPEND outputs "${module_file}")
        list(APPEND arguments --embed-module "${module_file}" --module-name "${argument_MODULE}" --constant "${name}"
                              --embedding "${argument_EMBEDDING}")
        if(argument_NAMESPACE)
            list(APPEND arguments --namespace "${argument_NAMESPACE}")
        endif()
    endif()
    cmake_path(GET font FILENAME font_name)
    add_custom_command(
        OUTPUT ${outputs}
        COMMAND gfx_ui_atlas_builder ${arguments} --output "${atlas}" --depfile "${depfile}" --quiet
        DEPENDS gfx_ui_atlas_builder "${font}" ${text_files}
        DEPFILE "${depfile}"
        COMMENT "Building UI atlas ${name} from ${font_name}"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    # The target depends on the atlas; with EMBED, it compiles the module (`#embed` is a C++26
    # extension in C++23: the warning is turned off for that file only).
    target_sources(${target} PRIVATE "${atlas}")
    if(argument_EMBED)
        set(visibility PRIVATE)
        if(argument_PUBLIC)
            set(visibility PUBLIC)
        endif()
        target_sources(${target} ${visibility}
            FILE_SET gfx_ui_atlas_${name} TYPE CXX_MODULES
            BASE_DIRS "${output_directory}"
            FILES "${module_file}")
        if(argument_EMBEDDING STREQUAL "EMBED_DIRECTIVE" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            set_source_files_properties("${module_file}" TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "-Wno-c++26-extensions")
        endif()
    endif()
    if(argument_OUTPUT_VARIABLE)
        set(${argument_OUTPUT_VARIABLE} "${atlas}" PARENT_SCOPE)
    endif()
endfunction()
