/// @file line_breaking.cpp
/// @brief Line break opportunities (UAX #14) and grapheme boundaries (UAX #29), with libunibreak.
module;

#include <graphemebreak.h>
#include <linebreak.h>

module gfx.ui.text;

import std;
import :engine;

namespace gfx::ui::text_engine
{

namespace
{

/// libunibreak's tables, initialized once.
void
initialize_libunibreak()
{
    static std::once_flag initialized;
    std::call_once(initialized, []
    {
        init_linebreak();
        init_graphemebreak();
    });
}

} // namespace

void
find_line_breaks(std::span<const std::uint32_t> code_points,
                 const std::string& language,
                 std::vector<char>& breaks)
{
    initialize_libunibreak();
    breaks.resize(code_points.size());
    set_linebreaks_utf32(code_points.data(), code_points.size(), language.empty() ? nullptr : language.c_str(), breaks.data());
}

void
find_grapheme_breaks(std::span<const std::uint32_t> code_points,
                     const std::string& language,
                     std::vector<char>& breaks)
{
    initialize_libunibreak();
    breaks.resize(code_points.size());
    set_graphemebreaks_utf32(code_points.data(), code_points.size(), language.empty() ? nullptr : language.c_str(), breaks.data());
}

} // namespace gfx::ui::text_engine
