// A cached layout allocates nothing, and neither does a draw list refilled every frame: this executable
// counts every operator new while it lays texts out and fills lists.
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

TEST(TextAllocationTest, DrawListsRefilledEveryFrameAllocateNothing)
{
    FontLibrary library;
    const FontAtlasHandle atlas = library.add_atlas(FontAtlas::load(gfx::tests::text_atlases::allocation_latin));
    const FontHandle font = library.add_font({.atlas = atlas});
    TextLayoutEngine engine{library};
    DrawList list;
    DebugOverlay overlay{{.visible = true}};
    const std::vector<std::string> lines{"60.0 fps   16.67 ms", "draws 12   dispatches 3"};
    const std::array points{gfx::Vector2{.x = 0, .y = 0}, gfx::Vector2{.x = 40, .y = 10}, gfx::Vector2{.x = 20, .y = 40}};

    // Every kind of primitive, clipped and transformed, with text and the overlay: the first frames grow
    // the list's arrays, the next ones reuse them.
    const auto fill = [&]
    {
        engine.new_frame();
        list.clear();
        list.push_clip_rectangle({.left = 0, .top = 0, .right = 300, .bottom = 200});
        list.push_transform(Transform2D::about({.x = 50, .y = 50}, Transform2D::rotation(0.3F)));
        list.add_rectangle_filled({.left = 0, .top = 0, .right = 100, .bottom = 30}, {.red = 1, .alpha = 1}, 6);
        list.add_rectangle({.left = 0, .top = 0, .right = 100, .bottom = 30}, {.green = 1, .alpha = 1}, 2);
        list.add_circle_filled({.x = 20, .y = 20}, 8, {.blue = 1, .alpha = 1});
        list.add_line({.x = 0, .y = 0}, {.x = 90, .y = 60}, {.red = 1, .alpha = 1}, 3);
        list.add_polyline(points, {.green = 1, .alpha = 1}, 2, true);
        list.add_convex_polygon_filled(points, {.blue = 1, .alpha = 1});
        list.add_image(4, {.left = 10, .top = 10, .right = 42, .bottom = 42});
        list.add_text(library, engine.layout("Hello, world", font), {.x = 5, .y = 60}, {.shadow_color = {.alpha = 0.5F}, .shadow_offset = {.x = 1, .y = 1}});
        list.pop_transform();
        list.pop_clip_rectangle();
        static_cast<void>(overlay.draw(list, engine, library, font, lines));
    };
    fill();
    fill();
    const std::size_t vertices = list.vertices().size();
    g_allocations = 0;
    g_counting = true;
    for (int frame = 0; frame < 100; ++frame)
    {
        fill();
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(list.vertices().size(), vertices);
    EXPECT_GT(vertices, 100U);
}

} // namespace
