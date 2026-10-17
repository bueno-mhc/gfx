/// @file debug_overlay.cpp
/// @brief DebugOverlay: the lines joined, laid out once per change, drawn over a rounded panel.
module gfx.ui.debug_overlay;

import std;

namespace gfx::ui
{

auto
DebugOverlay::draw(DrawList& list,
                   TextLayoutEngine& text,
                   const FontLibrary& fonts,
                   FontHandle font,
                   std::span<const std::string> lines) -> UiRectangle
{
    if (!m_description.visible || lines.empty())
    {
        return {};
    }

    // One text of every line (the buffer keeps its capacity), laid out (a cache hit when unchanged).
    m_text.clear();
    for (const std::string& line : lines)
    {
        if (!m_text.empty())
        {
            m_text += '\n';
        }
        m_text += line;
    }
    const TextLayout layout = text.layout(m_text, font);
    if (layout.lines.empty())
    {
        return {};
    }

    // The panel, then the text inside its padding.
    const float padding = m_description.padding;
    const UiRectangle panel{.left = m_description.position.x,
                            .top = m_description.position.y,
                            .right = m_description.position.x + layout.width + 2 * padding,
                            .bottom = m_description.position.y + layout.height + 2 * padding};
    list.add_rectangle_filled(panel, m_description.background, m_description.corner_radius);
    list.add_text(fonts, layout, {.x = panel.left + padding, .y = panel.top + padding}, m_description.text);
    return panel;
}

} // namespace gfx::ui
