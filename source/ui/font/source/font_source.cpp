/// @file font_source.cpp
/// @brief FontSource: reading font files and holding font bytes.
module gfx.ui.atlas_tools;

import std;
import gfx.ui.atlas_format;
import :implementations;

namespace gfx::ui
{

FontSource::FontSource(std::shared_ptr<const std::vector<std::byte>> bytes,
                       std::string name)
    : m_bytes{std::move(bytes)}
    , m_name{std::move(name)}
    , m_hash{xxhash64(*m_bytes)}
{
}

auto
FontSource::from_file(const std::filesystem::path& path) -> FontSource
{
    // The whole file, in binary.
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file)
    {
        throw_font_usage_error("FontSource::from_file", std::format("the font file '{}' cannot be opened", path.string()),
                               "check the path, and that the file is readable");
    }
    const std::streamoff size = file.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        throw_font_usage_error("FontSource::from_file", std::format("the font file '{}' cannot be read", path.string()), "");
    }
    return FontSource{std::make_shared<const std::vector<std::byte>>(std::move(bytes)), path.filename().string()};
}

auto
FontSource::from_memory(std::span<const std::byte> bytes,
                        std::string name) -> FontSource
{
    return FontSource{std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end()), std::move(name)};
}

auto
FontSource::from_memory(std::vector<std::byte>&& bytes,
                        std::string name) -> FontSource
{
    return FontSource{std::make_shared<const std::vector<std::byte>>(std::move(bytes)), std::move(name)};
}

} // namespace gfx::ui
