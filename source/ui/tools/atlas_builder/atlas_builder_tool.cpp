/// @file atlas_builder_tool.cpp
/// @brief gfx_ui_atlas_builder: builds a `.uiatlas` from a font (gfx::ui::AtlasBuilder), optionally writes
///        a depfile and a C++ module embedding the atlas. gfx_add_ui_atlas() (cmake/GfxUiAtlas.cmake) runs
///        it at build time; `gfx_ui_atlas_builder --help` lists its options.
import std;
import gfx.ui.atlas_tools;

namespace
{

using namespace gfx::ui;

/// What the command line asks for.
struct Options
{
    std::filesystem::path font{};
    std::filesystem::path output{};
    std::vector<std::filesystem::path> text_files{};
    std::filesystem::path depfile{};
    std::filesystem::path embed_module{};
    std::string module_name{};
    std::string constant{};
    std::string namespace_name{};
    AtlasEmbedding embedding = AtlasEmbedding::EMBED_DIRECTIVE;
    bool require_all_characters = false;
    bool quiet = false;
    AtlasBuildDescription description{};
};

constexpr std::string_view USAGE = R"(gfx_ui_atlas_builder: builds a .uiatlas font atlas (SDF, PSDF, MSDF or MTSDF).

  gfx_ui_atlas_builder --font <file> --output <file.uiatlas> [options]

Characters (at least one):
  --characters "<spec>"        CharacterSet names (BASIC_LATIN ARROWS ...), U+XXXX, U+XXXX-U+YYYY
  --text-file <file>           every character of a UTF-8 text file
Font:
  --face-index <n>             face of a collection (default 0)
  --variation <tag=value,...>  axis values of a variable font (wght=700,wdth=100)
  --named-instance <name>      or a named instance ("Bold")
  --decoder <kind>             FREETYPE (default) or STB_TRUETYPE
Distance field:
  --kind <kind>                SDF, PSDF, MSDF or MTSDF (default)
  --size <texels>              texels per em (default 32)
  --range <texels>             distance range (default 4)
  --padding <texels>           room around each glyph, inside its quad (default 0)
  --angle-threshold <radians>  edge coloring corners (default 3)
  --miter-limit <value>        (default 1; 0: none)
  --coloring <kind>            SIMPLE, INK_TRAP (default) or DISTANCE
  --generator <kind>           MSDFGEN (default) or BUILT_IN (SDF only)
  --texel-format <format>      R8_UNORM, R8G8B8A8_UNORM, R16_SFLOAT, R16G16B16A16_SFLOAT
Pages:
  --packer <kind>              TIGHT (default) or GRID
  --maximum-dimension <n>      largest page side (default 4096)
  --spacing <n>                texels between boxes (default 0)
  --no-power-of-two            page sides multiples of 4 instead of powers of two
Content:
  --no-shaping-font            no subset font for HarfBuzz (implies --no-kerning --no-ligatures)
  --no-kerning                 no pair kerning for the basic shaper
  --no-ligatures               no standard ligatures for the basic shaper
  --compression <kind>         ZSTD (default) or NONE
  --compression-level <n>      zstd level 1 to 22 (default 19)
  --threads <n>                threads generating fields (default: one per hardware thread)
Output:
  --require-all-characters     fail when the font lacks a requested character
  --depfile <file>             Make-style dependencies of the output
  --embed-module <file.cppm>   also write a module embedding the atlas, with:
    --module-name <name>       its name (editor.fonts)
    --constant <identifier>    the constant (std::array<std::byte, N>) holding the atlas
    --namespace <name>         the constant's namespace (default: global)
    --embedding <kind>         EMBED_DIRECTIVE (#embed, default) or BYTE_LIST
  --quiet                      print nothing but errors
)";

/// Throws for a malformed command line.
[[noreturn]] void
fail(std::string message)
{
    throw std::invalid_argument(std::move(message));
}

/// The enumerator named @p text.
template<class Enumeration>
auto
parse_enumerator(std::string_view option,
                 std::string_view text) -> Enumeration
{
    if (const std::optional<Enumeration> value = gfx::from_string<Enumeration>(text); value.has_value())
    {
        return *value;
    }
    std::string names;
    for (const auto& entry : gfx::EnumerationTraits<Enumeration>::NAMES)
    {
        names += std::format("{}{}", names.empty() ? "" : ", ", entry.name);
    }
    fail(std::format("{} {}: expected one of {}", option, text, names));
}

/// The number @p text.
template<class Number>
auto
parse_number(std::string_view option,
             std::string_view text) -> Number
{
    Number value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        fail(std::format("{} {}: expected a number", option, text));
    }
    return value;
}

/// "wght=700,wdth=100".
auto
parse_variation(std::string_view text) -> std::vector<FontAxisValue>
{
    std::vector<FontAxisValue> values;
    for (const auto part : std::views::split(text, ','))
    {
        const std::string_view entry{part.begin(), part.end()};
        const std::size_t equals = entry.find('=');
        if (equals == std::string_view::npos || equals == 0 || equals > 4)
        {
            fail(std::format("--variation {}: expected tag=value pairs (wght=700,wdth=100)", text));
        }
        values.push_back({.tag = font_tag(entry.substr(0, equals)), .value = parse_number<float>("--variation", entry.substr(equals + 1))});
    }
    return values;
}

/// Applies an option and its value.
using ValueOption = std::function<void(Options& options, std::string_view option, std::string_view value)>;

/// Applies an option without value.
using FlagOption = std::function<void(Options& options)>;

/// The options that take a value.
auto
value_options() -> const std::map<std::string_view, ValueOption>&
{
    static const std::map<std::string_view, ValueOption> table{
        {"--font", [](Options& options, std::string_view, std::string_view value) { options.font = value; }},
        {"--output", [](Options& options, std::string_view, std::string_view value) { options.output = value; }},
        {"--characters", [](Options& options, std::string_view, std::string_view value) { options.description.characters.add(CharacterSelection::parse(value)); }},
        {"--text-file", [](Options& options, std::string_view, std::string_view value) { options.text_files.emplace_back(value); }},
        {"--face-index", [](Options& options, std::string_view option, std::string_view value) { options.description.face_index = parse_number<std::uint32_t>(option, value); }},
        {"--variation", [](Options& options, std::string_view, std::string_view value) { options.description.variation = parse_variation(value); }},
        {"--named-instance", [](Options& options, std::string_view, std::string_view value) { options.description.named_instance = value; }},
        {"--decoder", [](Options& options, std::string_view option, std::string_view value) { options.description.decoder = parse_enumerator<FontDecoderKind>(option, value); }},
        {"--kind", [](Options& options, std::string_view option, std::string_view value) { options.description.field.kind = parse_enumerator<SdfKind>(option, value); }},
        {"--size", [](Options& options, std::string_view option, std::string_view value) { options.description.field.em_size = parse_number<double>(option, value); }},
        {"--range", [](Options& options, std::string_view option, std::string_view value) { options.description.field.distance_range = parse_number<double>(option, value); }},
        {"--padding", [](Options& options, std::string_view option, std::string_view value) { options.description.field.padding = parse_number<double>(option, value); }},
        {"--angle-threshold", [](Options& options, std::string_view option, std::string_view value) { options.description.field.angle_threshold = parse_number<double>(option, value); }},
        {"--miter-limit", [](Options& options, std::string_view option, std::string_view value) { options.description.field.miter_limit = parse_number<double>(option, value); }},
        {"--coloring", [](Options& options, std::string_view option, std::string_view value) { options.description.field.coloring = parse_enumerator<EdgeColoring>(option, value); }},
        {"--generator", [](Options& options, std::string_view option, std::string_view value) { options.description.generator = parse_enumerator<SdfGeneratorKind>(option, value); }},
        {"--texel-format", [](Options& options, std::string_view option, std::string_view value) { options.description.texel_format = parse_enumerator<AtlasTexelFormat>(option, value); }},
        {"--packer", [](Options& options, std::string_view option, std::string_view value) { options.description.packer = parse_enumerator<AtlasPackerKind>(option, value); }},
        {"--maximum-dimension", [](Options& options, std::string_view option, std::string_view value) { options.description.packing.maximum_dimension = parse_number<std::uint32_t>(option, value); }},
        {"--spacing", [](Options& options, std::string_view option, std::string_view value) { options.description.packing.spacing = parse_number<std::uint32_t>(option, value); }},
        {"--compression", [](Options& options, std::string_view option, std::string_view value) { options.description.compression = parse_enumerator<AtlasCompression>(option, value); }},
        {"--compression-level", [](Options& options, std::string_view option, std::string_view value) { options.description.compression_level = parse_number<int>(option, value); }},
        {"--threads", [](Options& options, std::string_view option, std::string_view value) { options.description.thread_count = parse_number<std::uint32_t>(option, value); }},
        {"--depfile", [](Options& options, std::string_view, std::string_view value) { options.depfile = value; }},
        {"--embed-module", [](Options& options, std::string_view, std::string_view value) { options.embed_module = value; }},
        {"--module-name", [](Options& options, std::string_view, std::string_view value) { options.module_name = value; }},
        {"--constant", [](Options& options, std::string_view, std::string_view value) { options.constant = value; }},
        {"--namespace", [](Options& options, std::string_view, std::string_view value) { options.namespace_name = value; }},
        {"--embedding", [](Options& options, std::string_view option, std::string_view value) { options.embedding = parse_enumerator<AtlasEmbedding>(option, value); }},
    };
    return table;
}

/// The options without value.
auto
flag_options() -> const std::map<std::string_view, FlagOption>&
{
    static const std::map<std::string_view, FlagOption> table{
        {"--no-power-of-two", [](Options& options) { options.description.packing.power_of_two = false; }},
        {"--no-shaping-font", [](Options& options)
        {
            options.description.shaping_font = false;
            options.description.kerning = false;
            options.description.ligatures = false;
        }},
        {"--no-kerning", [](Options& options) { options.description.kerning = false; }},
        {"--no-ligatures", [](Options& options) { options.description.ligatures = false; }},
        {"--require-all-characters", [](Options& options) { options.require_all_characters = true; }},
        {"--quiet", [](Options& options) { options.quiet = true; }},
    };
    return table;
}

/// The options of @p arguments, or nullopt for --help.
auto
parse_options(std::span<const std::string_view> arguments) -> std::optional<Options>
{
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string_view option = arguments[index];
        if (option == "--help" || option == "-h")
        {
            return std::nullopt;
        }
        if (const auto flag = flag_options().find(option); flag != flag_options().end())
        {
            flag->second(options);
            continue;
        }
        const auto handler = value_options().find(option);
        if (handler == value_options().end())
        {
            fail(std::format("unknown option {} (--help lists them)", option));
        }
        if (index + 1 >= arguments.size())
        {
            fail(std::format("{} needs a value", option));
        }
        handler->second(options, option, arguments[++index]);
    }
    if (options.font.empty() || options.output.empty())
    {
        fail("--font and --output are required (--help lists the options)");
    }
    if (!options.embed_module.empty() && (options.module_name.empty() || options.constant.empty()))
    {
        fail("--embed-module needs --module-name and --constant");
    }
    return options;
}

/// The content of the text file @p path.
auto
read_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        fail(std::format("the text file '{}' cannot be read", path.string()));
    }
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

/// Writes @p bytes to @p path, creating its directory.
void
write_file(const std::filesystem::path& path,
           std::span<const char> bytes)
{
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
    {
        fail(std::format("'{}' cannot be written", path.string()));
    }
}

/// A path as Make-style dependency files write it (absolute, forward slashes, spaces escaped).
auto
dependency_path(const std::filesystem::path& path) -> std::string
{
    std::string text;
    for (const char character : std::filesystem::absolute(path).generic_string())
    {
        if (character == ' ' || character == '#')
        {
            text += '\\';
        }
        text += character;
    }
    return text;
}

/// The module embedding @p atlas: a byte array (#embed of the atlas file, or byte literals) and the
/// exported std::array<std::byte, N> constant.
auto
embedding_module(const Options& options,
                 std::span<const std::byte> atlas) -> std::string
{
    const std::string bytes_name = options.constant + "_bytes";
    const bool named = !options.namespace_name.empty();
    std::string text = std::format("// Generated by gfx_ui_atlas_builder from {}: do not edit.\n"
                                   "// The atlas {}, embedded at compile time: read it with gfx::ui::AtlasView.\n"
                                   "export module {};\n\n"
                                   "import std;\n\n",
                                   options.font.filename().string(), options.output.filename().string(), options.module_name);

    // The bytes (not exported).
    text += named ? std::format("namespace {}\n{{\n\n", options.namespace_name) : std::string{};
    text += std::format("inline constexpr unsigned char {}[] = {{\n", bytes_name);
    if (options.embedding == AtlasEmbedding::EMBED_DIRECTIVE)
    {
        text += std::format("#embed \"{}\"\n", std::filesystem::absolute(options.output).generic_string());
    }
    else
    {
        for (std::size_t index = 0; index < atlas.size(); ++index)
        {
            const std::string_view separator = index % 16 != 0 ? " " : (index == 0 ? "    " : "\n    ");
            text += std::format("{}0x{:02X},", separator, std::to_integer<unsigned>(atlas[index]));
        }
        text += "\n";
    }
    text += "};\n";
    text += named ? std::format("\n}} // namespace {}\n", options.namespace_name) : std::string{};

    // The exported constant.
    text += named ? std::format("\nexport namespace {}\n{{\n\n", options.namespace_name) : std::string{"\nexport "};
    text += std::format("inline constexpr std::array<std::byte, sizeof({0})> {1} = std::bit_cast<std::array<std::byte, sizeof({0})>>({0});\n",
                        bytes_name, options.constant);
    text += named ? std::format("\n}} // namespace {}\n", options.namespace_name) : std::string{};
    return text;
}

/// Builds the atlas the options describe.
auto
run(const Options& options) -> int
{
    // Characters of the text files too.
    AtlasBuildDescription description = options.description;
    for (const std::filesystem::path& file : options.text_files)
    {
        description.characters.add_text(read_text(file));
    }
    const FontSource font = FontSource::from_file(options.font);
    const AtlasBuildResult result = build_atlas(font, description);
    const AtlasBuildReport& report = result.report;

    // Missing characters: a warning, or an error when every character is required.
    if (!report.missing_characters.empty())
    {
        std::string missing;
        for (const char32_t code_point : report.missing_characters | std::views::take(32))
        {
            missing += std::format(" {}", format_code_point(code_point));
        }
        if (report.missing_characters.size() > 32)
        {
            missing += std::format(" and {} more", report.missing_characters.size() - 32);
        }
        std::println(std::cerr, "gfx_ui_atlas_builder: {}: '{}' lacks {} character(s), drawn as .notdef:{}",
                     options.require_all_characters ? "error" : "warning", font.name(), report.missing_characters.size(), missing);
        if (options.require_all_characters)
        {
            return 1;
        }
    }

    // The atlas, its module and its dependencies.
    write_file(options.output, {reinterpret_cast<const char*>(result.atlas.data()), result.atlas.size()});
    if (!options.embed_module.empty())
    {
        const std::string module = embedding_module(options, result.atlas);
        write_file(options.embed_module, module);
    }
    if (!options.depfile.empty())
    {
        std::string dependencies = std::format("{}: {}", dependency_path(options.output), dependency_path(options.font));
        for (const std::filesystem::path& file : options.text_files)
        {
            dependencies += std::format(" {}", dependency_path(file));
        }
        dependencies += "\n";
        write_file(options.depfile, dependencies);
    }
    if (!options.quiet)
    {
        const AtlasDescriptor descriptor = AtlasView::parse(result.atlas, AtlasVerification::STRUCTURE)->descriptor();
        std::println("gfx_ui_atlas_builder: {} -> {}: {} glyphs, {} page(s) of {} x {} {} {}, {} kerning pairs, {} ligatures, "
                     "shaping font {} bytes, atlas {} bytes",
                     font.name(), options.output.filename().string(), report.glyph_count, report.page_count, report.page_width,
                     report.page_height, to_string(descriptor.kind), to_string(descriptor.texel_format), report.kerning_pair_count,
                     report.ligature_count, report.shaping_font_size, report.atlas_size);
    }
    return 0;
}

} // namespace

auto
main(int argument_count,
     char** argument_values) -> int
{
    try
    {
        const std::vector<std::string_view> arguments(argument_values + 1, argument_values + argument_count);
        const std::optional<Options> options = parse_options(arguments);
        if (!options.has_value())
        {
            std::print("{}", USAGE);
            return 0;
        }
        return run(*options);
    }
    catch (const std::exception& error)
    {
        std::println(std::cerr, "gfx_ui_atlas_builder: error: {}", error.what());
        return 1;
    }
}
