// A cached layout allocates nothing: this executable counts every operator new while it lays texts out.
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.allocation_latin;

namespace
{

std::atomic<bool> g_counting{false};
std::atomic<std::uint64_t> g_allocations{0};

} // namespace

// Global allocation functions of this executable: counted while g_counting is set.
auto
operator new(std::size_t size) -> void*
{
    if (g_counting.load(std::memory_order_relaxed))
    {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size); memory != nullptr)
    {
        return memory;
    }
    throw std::bad_alloc{};
}

auto
operator new[](std::size_t size) -> void*
{
    return operator new(size);
}

void
operator delete(void* memory) noexcept
{
    std::free(memory);
}

void
operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void
operator delete(void* memory,
                std::size_t) noexcept
{
    std::free(memory);
}

void
operator delete[](void* memory,
                  std::size_t) noexcept
{
    std::free(memory);
}

namespace
{

using namespace gfx::ui;

TEST(TextAllocationTest, CachedLayoutsAllocateNothing)
{
    FontLibrary library;
    const FontAtlasHandle atlas = library.add_atlas(FontAtlas::load(gfx::tests::text_atlases::allocation_latin));
    const FontHandle font = library.add_font({.atlas = atlas});
    TextLayoutEngine engine{library};
    const std::array<std::string_view, 3> texts{"File", "The quick brown fox jumps over the lazy dog", "Settings and preferences"};
    const TextLayoutDescription wrapped{.wrap_width = 100, .alignment = TextAlignment::CENTER};

    // The first frame lays everything out (and allocates); the next frames find it all in the cache.
    for (const std::string_view text : texts)
    {
        static_cast<void>(engine.layout(text, font));
        static_cast<void>(engine.layout(text, font, wrapped));
    }
    std::size_t glyphs = 0;
    g_counting = true;
    for (int frame = 0; frame < 100; ++frame)
    {
        engine.new_frame();
        for (const std::string_view text : texts)
        {
            glyphs += engine.layout(text, font).glyphs.size();
            glyphs += engine.layout(text, font, wrapped).glyphs.size();
            glyphs += caret_position(engine.layout(text, font), 2).line;
        }
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_GT(glyphs, 0U);
    EXPECT_EQ(engine.statistics().misses, 6U);
}

} // namespace
