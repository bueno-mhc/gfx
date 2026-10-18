// A cached layout allocates nothing, nor does the layout of a text that changes every frame (it reuses the storage
// of an earlier one), nor a draw list refilled every frame, nor an immediate-mode UI after its first frames: this
// executable counts every operator new while it lays texts out, fills lists and runs frames.
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

/// Writes @p number as the last four characters of @p text.
void
write_counter(std::string& text,
              std::uint32_t number)
{
    for (std::size_t digit = 0; digit < 4; ++digit)
    {
        text[text.size() - 1 - digit] = static_cast<char>('0' + number % 10);
        number /= 10;
    }
}

TEST(TextAllocationTest, TextsChangingEveryFrameAllocateNothingAfterTheFirstFrames)
{
    // A text of the same length that differs every frame (a counter), laid out by the basic shaper, wrapped, by
    // HarfBuzz and in a right-to-left paragraph, next to a text that stays: every layout is a miss, made in the
    // storage of layouts made earlier.
    FontLibrary library;
    const FontAtlasHandle atlas = library.add_atlas(FontAtlas::load(gfx::tests::text_atlases::allocation_latin));
    const FontHandle font = library.add_font({.atlas = atlas});
    const FontHandle harfbuzz = library.add_font({.atlas = atlas, .shaping = TextShaping::HARFBUZZ});
    TextLayoutEngine engine{library};
    const TextLayoutDescription wrapped{.wrap_width = 120, .alignment = TextAlignment::CENTER};
    const TextLayoutDescription right_to_left{.direction = ParagraphDirection::RIGHT_TO_LEFT};
    std::string text = "The quick brown fox jumps over the lazy dog 0000";
    std::size_t glyphs = 0;
    std::size_t lines = 0;
    TextLayoutProblem problems = TextLayoutProblem::NONE;
    const auto lay_out = [&](std::uint32_t frame)
    {
        engine.new_frame();
        write_counter(text, frame);
        glyphs += engine.layout("File", font).glyphs.size();
        for (const TextLayout layout : {engine.layout(text, font), engine.layout(text, font, wrapped), engine.layout(text, harfbuzz),
                                        engine.layout(text, font, right_to_left)})
        {
            glyphs += layout.glyphs.size();
            lines += layout.lines.size();
            problems |= layout.problems;
        }
    };
    for (std::uint32_t frame = 0; frame < 30; ++frame)
    {
        lay_out(frame);
    }
    g_allocations = 0;
    g_counting = true;
    for (std::uint32_t frame = 30; frame < 130; ++frame)
    {
        lay_out(frame);
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(problems, TextLayoutProblem::NONE);
    EXPECT_EQ(glyphs, 130U * (4U + 4U * 48U)) << "a glyph for each of the 48 characters in every layout of the counter";
    EXPECT_GT(lines, 130U * 5U) << "the wrapped layout has several lines";
    EXPECT_EQ(engine.statistics().misses, 1U + 4U * 130U);
}

TEST(TextAllocationTest, LayoutsMadeWhileTheCacheIsFullAllocateNothingAfterTheFirstFrames)
{
    // Four new texts every frame in a cache of two entries: two replace the layouts of the frame before, two live
    // until the next frame (CACHE_FULL), in storage kept from frame to frame.
    FontLibrary library;
    const FontAtlasHandle atlas = library.add_atlas(FontAtlas::load(gfx::tests::text_atlases::allocation_latin));
    const FontHandle font = library.add_font({.atlas = atlas});
    TextLayoutEngine engine{library, {.cache_capacity = 2}};
    std::array<std::string, 4> texts{"The first text of frame 0000", "The second text of frame 0000", "The third text of frame 0000",
                                     "The fourth text of frame 0000"};
    std::size_t full = 0;
    const auto lay_out = [&](std::uint32_t frame)
    {
        engine.new_frame();
        for (std::string& text : texts)
        {
            write_counter(text, frame);
            full += has_any(engine.layout(text, font).problems, TextLayoutProblem::CACHE_FULL) ? 1U : 0U;
        }
    };
    for (std::uint32_t frame = 0; frame < 30; ++frame)
    {
        lay_out(frame);
    }
    g_allocations = 0;
    g_counting = true;
    for (std::uint32_t frame = 30; frame < 130; ++frame)
    {
        lay_out(frame);
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(full, 2U * 130U);
    EXPECT_EQ(engine.statistics().overflows, 2U * 130U);
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

TEST(TextAllocationTest, ImmediateModeFramesAllocateNothingAfterTheFirstOnes)
{
    // Two windows with every widget of phase U4, a child region, styles pushed, columns and groups; the mouse
    // moves over the items and clicks them, so hovered, held and pressed states change from frame to frame.
    UiContext ui;
    ui.load_font(FontAtlas::load(gfx::tests::text_atlases::allocation_latin), 16.0F);
    bool checked = false;
    bool switched = true;
    bool selected = false;
    int choice = 0;
    std::uint32_t presses = 0;
    const auto build = [&](UiFrame& frame, std::uint32_t frame_number)
    {
        frame.set_next_window_position({.x = 10, .y = 10}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 360, .y = 420}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Everything"))
        {
            frame.text("Frame {} of {}", frame_number % 8, 8);
            frame.text_colored({.red = 1, .alpha = 1}, "{:.2f} ms", 16.67);
            frame.text_disabled("disabled {}", 1);
            frame.text_wrapped("A long line of text that wraps at the end of the content region of the window.");
            frame.bullet_text("bullet {}", 2);
            frame.label("Speed", "{} m/s", 12);
            presses += frame.button("Button") ? 1U : 0U;
            frame.same_line();
            presses += frame.small_button("Small") ? 1U : 0U;
            frame.same_line();
            presses += frame.arrow_button("arrow", ArrowDirection::LEFT) ? 1U : 0U;
            frame.checkbox("Check", checked);
            frame.toggle("Switch", switched);
            frame.radio_button("One", choice, 0);
            frame.same_line();
            frame.radio_button("Two", choice, 1);
            frame.selectable("Selectable", &selected);
            frame.separator();
            frame.separator_text("Section");
            frame.push_style_color(StyleColor::BUTTON, {.green = 1, .alpha = 1});
            frame.push_style_variable(StyleVariable::FRAME_PADDING, gfx::Vector2{.x = 10, .y = 6});
            presses += frame.button("Styled") ? 1U : 0U;
            frame.pop_style_variable();
            frame.pop_style_color();
            frame.begin_group();
            frame.bullet();
            frame.text_unformatted("in a group");
            frame.end_group();
            frame.columns(2, "two");
            frame.text_unformatted("left");
            frame.next_column();
            frame.text_unformatted("right");
            frame.columns(1);
            frame.indent();
            frame.dummy({.x = 10, .y = 10});
            frame.unindent();
            for (std::int32_t row = 0; row < 3; ++row)
            {
                frame.push_id(row);
                presses += frame.invisible_button("row", {.x = 100, .y = 8}) ? 1U : 0U;
                frame.pop_id();
            }
            frame.begin_disabled();
            static_cast<void>(frame.button("Disabled"));
            frame.end_disabled();
            if (frame.begin_child("log", {.x = 0, .y = 60}, ChildFlag::BORDERS))
            {
                for (std::uint32_t line = 0; line < 10; ++line)
                {
                    frame.text("log {}", line);
                }
            }
            frame.end_child();
        }
        frame.end_window();
        frame.set_next_window_position({.x = 400, .y = 10}, UiCondition::FIRST_USE_EVER);
        frame.begin_window("Second", UiWindowFlag::ALWAYS_AUTO_RESIZE);
        frame.text_unformatted("auto-sized");
        frame.end_window();
    };

    // The input of frame n: the mouse sweeps the first window, pressing on even frames and releasing on odd ones
    // at the same place.
    std::array<UiMouseButtonEvent, 1> click{};
    const auto input = [&](std::uint32_t frame_number)
    {
        const std::uint32_t step = frame_number / 2;
        const gfx::Vector2 mouse{.x = 30.0F + static_cast<float>((step * 37) % 300), .y = 40.0F + static_cast<float>((step * 53) % 360)};
        const bool down = frame_number % 2 == 0;
        click[0] = {.button = gfx::MouseButton::LEFT, .down = down, .position = mouse};
        return UiFrameInput{.time = static_cast<double>(frame_number) / 60.0,
                            .display_size = {.x = 800, .y = 600},
                            .mouse_present = true,
                            .mouse_position = mouse,
                            .mouse_buttons = click,
                            .mouse_buttons_held = down ? gfx::MouseButtonMask::LEFT : gfx::MouseButtonMask::NONE};
    };
    const auto run = [&](std::uint32_t frame_number)
    {
        build(ui.begin_frame(input(frame_number)), frame_number);
        return ui.end_frame();
    };
    for (std::uint32_t frame_number = 0; frame_number < 30; ++frame_number)
    {
        EXPECT_TRUE(run(frame_number).ok()) << frame_number;
    }
    g_allocations = 0;
    g_counting = true;
    std::uint32_t problems = 0;
    for (std::uint32_t frame_number = 30; frame_number < 130; ++frame_number)
    {
        problems += run(frame_number).ok() ? 0U : 1U;
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(problems, 0U);
    EXPECT_GT(presses, 0U) << "the clicks reached widgets";
    EXPECT_EQ(ui.last_report().statistics.allocations, 0U);
}

TEST(TextAllocationTest, StructureWidgetsAllocateNothingWhileTheMouseMovesOverThem)
{
    // A menu bar, trees, collapsing headers, a tab bar, an open popup and a tooltip every frame, item tooltips and
    // drag and drop sources and targets, the mouse sweeping over them.
    UiContext ui;
    ui.load_font(FontAtlas::load(gfx::tests::text_atlases::allocation_latin), 16.0F);
    bool autosave = true;
    bool header = true;
    std::array<bool, 3> tabs{true, true, true};
    const auto build = [&](UiFrame& frame, std::uint32_t frame_number)
    {
        frame.set_next_window_position({.x = 10, .y = 10}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 500, .y = 560}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Structure", UiWindowFlag::MENU_BAR))
        {
            if (frame.begin_menu_bar())
            {
                if (frame.begin_menu("File"))
                {
                    frame.menu_item("Save", "Ctrl+S");
                    frame.menu_item("Autosave", {}, &autosave);
                    frame.end_menu();
                }
                frame.end_menu_bar();
            }
            if (frame.begin_tab_bar("Tabs", TabBarFlag::REORDERABLE | TabBarFlag::TAB_LIST_POPUP_BUTTON))
            {
                for (std::size_t tab = 0; tab < tabs.size(); ++tab)
                {
                    frame.push_id(static_cast<std::int32_t>(tab));
                    if (frame.begin_tab_item("Tab", tabs[tab]))
                    {
                        frame.text("Tab {} content", tab);
                        frame.end_tab_item();
                    }
                    frame.pop_id();
                }
                frame.end_tab_bar();
            }
            frame.set_next_item_open(true, UiCondition::ONCE);
            if (frame.tree_node("Root"))
            {
                for (std::int32_t node = 0; node < 4; ++node)
                {
                    frame.push_id(node);
                    if (frame.tree_node("Node", TreeNodeFlag::OPEN_ON_ARROW))
                    {
                        frame.tree_pop();
                    }
                    frame.item_tooltip("A node");
                    frame.pop_id();
                }
                frame.tree_pop();
            }
            frame.collapsing_header("Header", header);
            frame.button("Source");
            if (frame.begin_drag_drop_source())
            {
                frame.set_drag_drop_payload("number", 1);
                frame.end_drag_drop_source();
            }
            frame.button("Target");
            if (frame.begin_drag_drop_target())
            {
                static_cast<void>(frame.accept_drag_drop_payload("number"));
                frame.end_drag_drop_target();
            }
            if (frame_number == 0)
            {
                frame.open_popup("popup");
            }
            if (frame.begin_popup("popup"))
            {
                frame.text("Popup at frame {:03}", frame_number);
                frame.end_popup();
            }
            frame.tooltip("Always shown");
        }
        frame.end_window();
    };

    // The mouse moves over the window without pressing.
    const auto input = [&](std::uint32_t frame_number)
    {
        const gfx::Vector2 mouse{.x = 30.0F + static_cast<float>((frame_number * 37) % 460), .y = 60.0F + static_cast<float>((frame_number * 53) % 500)};
        return UiFrameInput{.time = static_cast<double>(frame_number) / 60.0, .display_size = {.x = 800, .y = 600}, .mouse_present = true, .mouse_position = mouse};
    };
    const auto run = [&](std::uint32_t frame_number)
    {
        build(ui.begin_frame(input(frame_number)), frame_number);
        return ui.end_frame();
    };
    for (std::uint32_t frame_number = 0; frame_number < 30; ++frame_number)
    {
        const UiFrameReport report = run(frame_number);
        EXPECT_TRUE(report.ok()) << frame_number << ": " << (report.issues.empty() ? std::string_view{} : report.issues.front().detail);
    }
    g_allocations = 0;
    g_counting = true;
    std::uint32_t problems = 0;
    for (std::uint32_t frame_number = 30; frame_number < 130; ++frame_number)
    {
        problems += run(frame_number).ok() ? 0U : 1U;
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(problems, 0U);
    EXPECT_EQ(ui.last_report().statistics.allocations, 0U);
    EXPECT_GE(ui.draw_data().lists.size(), 3U) << "the window, the popup and the tooltip";
}

TEST(TextAllocationTest, InputWidgetsAllocateNothingWhileTheMouseMovesOverThem)
{
    // Every input widget of phase U5 (text fields of each kind, numbers, sliders, drags, components, colors, a
    // closed combo, a list box), formatted values changing every frame, the mouse sweeping over them.
    UiContext ui;
    ui.load_font(FontAtlas::load(gfx::tests::text_atlases::allocation_latin), 16.0F);
    std::string name = "name";
    std::string secret = "secret";
    std::string notes = "first line\nsecond line";
    std::array<char, 16> buffer{'b', 'u', 'f', '\0'};
    std::int32_t count = 3;
    float ratio = 0.5F;
    double precise = 1.25;
    std::uint64_t large = 1'000'000;
    std::array<float, 3> position{1, 2, 3};
    Color color = Color::from_rgba8(200, 100, 50, 128);
    std::size_t fruit = 1;
    std::size_t item = 0;
    const std::array<std::string_view, 3> fruits{"apple", "banana", "cherry"};
    const std::array<std::string_view, 5> items{"one", "two", "three", "four", "five"};
    const auto build = [&](UiFrame& frame, std::uint32_t frame_number)
    {
        frame.set_next_window_position({.x = 10, .y = 10}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 500, .y = 580}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Inputs"))
        {
            ratio = static_cast<float>(frame_number % 10) / 10.0F;
            frame.input_text("Name", name, {.hint = "your name"});
            frame.input_text("Secret", secret, {.flags = InputTextFlag::PASSWORD});
            frame.input_text("Buffer", buffer);
            frame.input_text_multiline("Notes", notes, {.size = {.x = 300, .y = 60}});
            frame.input_number("Count", count, 1, 10);
            frame.input_number("Precise", precise, 0.0, 0.0, "{:.3f} s");
            frame.slider("Ratio", ratio, 0.0F, 1.0F, "{:.2f}");
            frame.slider("Large", large, std::uint64_t{0}, std::uint64_t{10'000'000}, {}, SliderFlag::LOGARITHMIC);
            frame.drag("Precise drag", precise, 0.01F);
            frame.drag("Position", position, 0.1F);
            frame.color_edit("Color", color);
            frame.color_edit("Hexadecimal", color, ColorEditFlag::DISPLAY_HEXADECIMAL);
            frame.set_next_item_width(200);
            frame.color_picker("Picker", color, ColorEditFlag::ALPHA_BAR | ColorEditFlag::NO_INPUTS);
            frame.color_button("Swatch", color, ColorEditFlag::ALPHA_PREVIEW_HALF);
            frame.combo("Fruit", fruit, fruits);
            frame.list_box("Items", item, items, 3);
        }
        frame.end_window();
    };

    // The mouse moves over the window without pressing (a press would start editing a field or open a popup).
    const auto input = [&](std::uint32_t frame_number)
    {
        const gfx::Vector2 mouse{.x = 30.0F + static_cast<float>((frame_number * 37) % 460), .y = 40.0F + static_cast<float>((frame_number * 53) % 540)};
        return UiFrameInput{.time = static_cast<double>(frame_number) / 60.0, .display_size = {.x = 800, .y = 600}, .mouse_present = true, .mouse_position = mouse};
    };
    const auto run = [&](std::uint32_t frame_number)
    {
        build(ui.begin_frame(input(frame_number)), frame_number);
        return ui.end_frame();
    };
    for (std::uint32_t frame_number = 0; frame_number < 30; ++frame_number)
    {
        EXPECT_TRUE(run(frame_number).ok()) << frame_number;
    }
    g_allocations = 0;
    g_counting = true;
    std::uint32_t problems = 0;
    for (std::uint32_t frame_number = 30; frame_number < 130; ++frame_number)
    {
        problems += run(frame_number).ok() ? 0U : 1U;
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(problems, 0U);
    EXPECT_EQ(ui.last_report().statistics.allocations, 0U);
}

TEST(TextAllocationTest, ImmediateModeTextsChangingEveryFrameAllocateNothing)
{
    // Texts formatted from the frame number, all of the same length, change every frame: none is found in the text
    // layout cache, each is laid out in the storage of a text of an earlier frame.
    UiContext ui;
    ui.load_font(FontAtlas::load(gfx::tests::text_atlases::allocation_latin), 16.0F);
    const auto run = [&](std::uint32_t frame_number)
    {
        UiFrame& frame = ui.begin_frame({.time = static_cast<double>(frame_number) / 60.0, .display_size = {.x = 800, .y = 600}});
        frame.set_next_window_position({.x = 10, .y = 10}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 300, .y = 200}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Counters"))
        {
            frame.text("Frame {:04}", frame_number);
            frame.label("Time", "{:07.3f} s", static_cast<double>(frame_number) / 60.0);
            frame.text_wrapped("Frame {:04} of a text long enough to wrap at the end of the content region of the window.", frame_number);
        }
        frame.end_window();
        return ui.end_frame();
    };
    for (std::uint32_t frame_number = 0; frame_number < 30; ++frame_number)
    {
        EXPECT_TRUE(run(frame_number).ok()) << frame_number;
    }
    g_allocations = 0;
    g_counting = true;
    std::uint32_t problems = 0;
    for (std::uint32_t frame_number = 30; frame_number < 130; ++frame_number)
    {
        problems += run(frame_number).ok() ? 0U : 1U;
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(problems, 0U);
    EXPECT_EQ(ui.last_report().statistics.allocations, 0U);
}

TEST(TextAllocationTest, LargeClippedTablesAllocateNothingWhileTheyScrollAndTheMouseMovesOverThem)
{
    // A scrolling table of 10000 rows through a list clipper (a frozen header row, sortable, resizable, reorderable,
    // hideable columns, spanning selectables, alternating rows, borders), scrolled every frame; plots, a progress bar,
    // a splitter and a resizable child region; the mouse sweeping over them.
    UiContext ui;
    ui.load_font(FontAtlas::load(gfx::tests::text_atlases::allocation_latin), 16.0F);
    std::array<float, 100> values{};
    for (std::size_t value = 0; value < values.size(); ++value)
    {
        values[value] = std::sin(static_cast<float>(value) * 0.3F);
    }
    float left = 120;
    float right = 200;
    std::uint32_t submitted = 0;
    const auto build = [&](UiFrame& frame, std::uint32_t frame_number)
    {
        frame.set_next_window_position({.x = 10, .y = 10}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 560, .y = 580}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Tables"))
        {
            constexpr TableFlag FLAGS = TableFlag::SCROLL_Y | TableFlag::ROW_BACKGROUND | TableFlag::BORDERS | TableFlag::RESIZABLE | TableFlag::REORDERABLE
                                        | TableFlag::HIDEABLE | TableFlag::SORTABLE;
            if (frame.begin_table("large", 4, FLAGS, {.x = 0, .y = 300}))
            {
                frame.table_setup_column("Index", TableColumnFlag::DEFAULT_SORT);
                frame.table_setup_column("Name");
                frame.table_setup_column("Value");
                frame.table_setup_column("Flag", TableColumnFlag::WIDTH_FIXED, 40.0F);
                frame.table_setup_scroll_freeze(0, 1);
                frame.table_headers_row();
                static_cast<void>(frame.table_sort_specifications());
                frame.set_scroll_y(static_cast<float>((frame_number % 50) * 2977));
                UiListClipper clipper;
                clipper.begin(frame, 10000);
                while (clipper.step())
                {
                    for (std::uint32_t row = clipper.display_start(); row < clipper.display_end(); ++row)
                    {
                        frame.table_next_row();
                        frame.table_next_column();
                        frame.push_id(static_cast<std::int32_t>(row));
                        static_cast<void>(frame.selectable("##row", row % 7 == 0, SelectableFlag::SPAN_ALL_COLUMNS));
                        frame.pop_id();
                        frame.same_line();
                        frame.text("{}", row);
                        frame.table_next_column();
                        frame.text_unformatted("name");
                        frame.table_next_column();
                        frame.text("{:.2f}", static_cast<double>(row) * 0.25);
                        frame.table_next_column();
                        frame.text_unformatted(row % 2 == 0 ? "yes" : "no");
                        ++submitted;
                    }
                }
                frame.end_table();
            }
            frame.plot_lines("Wave", values, {.size = {.x = 240, .y = 50}});
            frame.plot_histogram("Bars", std::span{values}.first(20), {.size = {.x = 240, .y = 50}});
            frame.progress_bar(static_cast<float>(frame_number % 100) / 100.0F);
            frame.progress_bar(-1.0F);
            static_cast<void>(frame.splitter("split", true, 8.0F, left, right, 40.0F, 40.0F));
            if (frame.begin_child("left", {.x = left, .y = 60}, ChildFlag::BORDERS | ChildFlag::RESIZE_Y))
            {
                frame.text_unformatted("Left");
            }
            frame.end_child();
            frame.same_line(0, 8.0F);
            if (frame.begin_child("right", {.x = right, .y = 60}, ChildFlag::BORDERS))
            {
                frame.text_unformatted("Right");
            }
            frame.end_child();
        }
        frame.end_window();
    };

    // The mouse moves over the table without pressing, the table scrolls: a cycle of 50 frames (the first two warm
    // up). Not over the plots: their tooltips' texts, longer than the rows', would land in cache entries whose storage
    // held short texts (a limitation of the text layout cache, not of tables).
    const auto input = [&](std::uint32_t frame_number)
    {
        const std::uint32_t step = frame_number % 50;
        const gfx::Vector2 mouse{.x = 30.0F + static_cast<float>((step * 37) % 520), .y = 40.0F + static_cast<float>((step * 53) % 280)};
        return UiFrameInput{.time = static_cast<double>(frame_number) / 60.0, .display_size = {.x = 800, .y = 620}, .mouse_present = true, .mouse_position = mouse};
    };
    const auto run = [&](std::uint32_t frame_number)
    {
        build(ui.begin_frame(input(frame_number)), frame_number);
        return ui.end_frame();
    };
    for (std::uint32_t frame_number = 0; frame_number < 100; ++frame_number)
    {
        const UiFrameReport report = run(frame_number);
        EXPECT_TRUE(report.ok()) << frame_number << ": " << (report.issues.empty() ? std::string_view{} : report.issues.front().detail);
    }
    g_allocations = 0;
    g_counting = true;
    std::uint32_t problems = 0;
    submitted = 0;
    for (std::uint32_t frame_number = 100; frame_number < 200; ++frame_number)
    {
        problems += run(frame_number).ok() ? 0U : 1U;
    }
    g_counting = false;
    EXPECT_EQ(g_allocations.load(), 0U);
    EXPECT_EQ(problems, 0U);
    EXPECT_EQ(ui.last_report().statistics.allocations, 0U);
    EXPECT_LT(submitted, 100U * 25U) << "about the rows in view each frame";
}

} // namespace
