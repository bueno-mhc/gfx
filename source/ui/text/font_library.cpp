/// @file font_library.cpp
/// @brief FontLibrary: handle tables of atlases and fonts (lookups here, never in an interface: defect 10).
module gfx.ui.text;

import std;
import gfx.core.handle_table;
import gfx.ui.errors;

namespace gfx::ui
{

namespace
{

/// An atlas of the library.
struct AtlasRecord
{
    std::shared_ptr<const FontAtlas> atlas{};
};

/// A font of the library.
struct FontRecord
{
    FontDescription description{};
};

/// Throws an InvalidUsageError of FontLibrary.
[[noreturn]] void
throw_library_error(std::string_view operation,
                    std::string problem,
                    std::string suggestion)
{
    throw InvalidUsageError({.backend = UiSubsystem::FONT, .operation = std::string{operation}, .problem = std::move(problem),
                             .suggestion = std::move(suggestion)});
}

} // namespace

struct FontLibrary::State
{
    backends::HandleTable<FontAtlasTag, AtlasRecord> atlases{UiSubsystem::FONT};
    backends::HandleTable<FontTag, FontRecord> fonts{UiSubsystem::FONT};
    std::mutex mutex{};
};

FontLibrary::FontLibrary()
    : m_state{std::make_unique<State>()}
{
}

FontLibrary::~FontLibrary() = default;

auto
FontLibrary::add_atlas(std::shared_ptr<const FontAtlas> atlas) -> FontAtlasHandle
{
    if (atlas == nullptr)
    {
        throw_library_error("FontLibrary::add_atlas", "the atlas is null", "load it with FontAtlas::load()");
    }
    return m_state->atlases.insert({.atlas = std::move(atlas)});
}

auto
FontLibrary::atlas(FontAtlasHandle handle) const noexcept -> std::shared_ptr<const FontAtlas>
{
    const AtlasRecord* record = m_state->atlases.find(handle);
    return record != nullptr ? record->atlas : nullptr;
}

void
FontLibrary::remove_atlas(FontAtlasHandle handle)
{
    // Not while a font refers to it.
    const std::scoped_lock lock{m_state->mutex};
    bool used = false;
    m_state->fonts.for_each([&](FontHandle, const FontRecord& record)
    {
        used = used || record.description.atlas == handle || std::ranges::contains(record.description.fallbacks, handle);
    });
    if (used)
    {
        throw_library_error("FontLibrary::remove_atlas", std::format("{} is used by a font of the library", handle),
                            "remove the fonts that use it first");
    }
    static_cast<void>(m_state->atlases.remove(handle));
}

auto
FontLibrary::add_font(FontDescription description) -> FontHandle
{
    // Every atlas known, a positive size.
    const std::scoped_lock lock{m_state->mutex};
    if (m_state->atlases.find(description.atlas) == nullptr)
    {
        throw_library_error("FontLibrary::add_font", std::format("the font's atlas {} is not in the library", description.atlas),
                            "add the atlas with FontLibrary::add_atlas() and use the handle it returns");
    }
    for (const FontAtlasHandle fallback : description.fallbacks)
    {
        if (m_state->atlases.find(fallback) == nullptr)
        {
            throw_library_error("FontLibrary::add_font", std::format("the fallback atlas {} is not in the library", fallback),
                                "add every fallback atlas with FontLibrary::add_atlas()");
        }
    }
    if (!(description.size > 0))
    {
        throw_library_error("FontLibrary::add_font", std::format("a font of {} pixels per em cannot be drawn", description.size),
                            "give a positive size");
    }
    return m_state->fonts.insert({.description = std::move(description)});
}

auto
FontLibrary::font(FontHandle handle) const noexcept -> const FontDescription*
{
    const FontRecord* record = m_state->fonts.find(handle);
    return record != nullptr ? &record->description : nullptr;
}

void
FontLibrary::remove_font(FontHandle handle) noexcept
{
    const std::scoped_lock lock{m_state->mutex};
    static_cast<void>(m_state->fonts.remove(handle));
}

auto
FontLibrary::atlas_count() const noexcept -> std::size_t
{
    return m_state->atlases.size();
}

auto
FontLibrary::font_count() const noexcept -> std::size_t
{
    return m_state->fonts.size();
}

} // namespace gfx::ui
