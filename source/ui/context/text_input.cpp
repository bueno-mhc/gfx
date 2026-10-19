/// @file text_input.cpp
/// @brief Text fields: the field being edited (a TextEditor), its activation by click or Tab, mouse selection,
///        keys and shortcuts (by the layout's key codes), filters, the clipboard, the IME composition, scrolling
///        and drawing; input_text() over a std::string or a fixed buffer, input_text_multiline().
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import gfx.ui.unicode;
import :state;

namespace gfx::ui
{

namespace
{

/// Whether @p rectangle contains @p point.
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// What the field's filters make of @p code_point: itself, another (uppercase), or nothing.
auto
filter_character(char32_t code_point,
                 InputTextFlag flags) noexcept -> std::optional<char32_t>
{
    const auto among = [code_point](std::u32string_view allowed) { return allowed.find(code_point) != std::u32string_view::npos; };
    if (has_any(flags, InputTextFlag::CHARACTERS_DECIMAL) && !among(U"0123456789.+-*/"))
    {
        return std::nullopt;
    }
    if (has_any(flags, InputTextFlag::CHARACTERS_SCIENTIFIC) && !among(U"0123456789.+-*/eE"))
    {
        return std::nullopt;
    }
    if (has_any(flags, InputTextFlag::CHARACTERS_HEXADECIMAL) && !among(U"0123456789abcdefABCDEF"))
    {
        return std::nullopt;
    }
    if (has_any(flags, InputTextFlag::CHARACTERS_NO_BLANK) && (code_point == U' ' || code_point == U'\t'))
    {
        return std::nullopt;
    }
    if (has_any(flags, InputTextFlag::CHARACTERS_UPPERCASE) && code_point >= U'a' && code_point <= U'z')
    {
        return code_point - U'a' + U'A';
    }
    return code_point;
}

/// The UTF-8 bytes of @p code_point appended to @p text.
void
append_utf8(std::string& text,
            char32_t code_point)
{
    if (code_point < 0x80)
    {
        text.push_back(static_cast<char>(code_point));
    }
    else if (code_point < 0x800)
    {
        text.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else if (code_point < 0x10000)
    {
        text.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else
    {
        text.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

/// @p text through the field's filters, into @p output.
void
filter_text(std::string_view text,
            InputTextFlag flags,
            std::string& output)
{
    output.clear();
    for (std::size_t offset = 0; offset < text.size();)
    {
        const Utf8CodePoint code_point = decode_utf8(text, offset);
        offset += code_point.size;
        if (const std::optional<char32_t> kept = filter_character(code_point.code_point, flags))
        {
            append_utf8(output, *kept);
        }
    }
}

/// How the text shown maps to the text edited: password masks (one '*' per grapheme) and an IME composition
/// shown in place of the selection (or at the cursor), as committing it will place it.
struct DisplayMap
{
    const TextBoundaries* boundaries = nullptr;
    bool masked = false;
    std::uint32_t composition_at = 0;
    std::uint32_t composition_size = 0;
    std::uint32_t replaced = 0; ///< Bytes of the edited text the composition hides (the selection).

    /// The shown offset of edited offset @p offset.
    [[nodiscard]] auto to_display(std::uint32_t offset) const noexcept -> std::uint32_t
    {
        if (masked)
        {
            std::uint32_t graphemes = 0;
            for (std::uint32_t position = 0; position < offset;)
            {
                position = boundaries->next_grapheme(position);
                ++graphemes;
            }
            return graphemes;
        }
        if (composition_size == 0 || offset <= composition_at)
        {
            return offset;
        }
        return offset >= composition_at + replaced ? offset - replaced + composition_size : composition_at;
    }

    /// The edited offset of shown offset @p offset.
    [[nodiscard]] auto to_editor(std::uint32_t offset) const noexcept -> std::uint32_t
    {
        if (masked)
        {
            std::uint32_t position = 0;
            for (std::uint32_t grapheme = 0; grapheme < offset; ++grapheme)
            {
                position = boundaries->next_grapheme(position);
            }
            return position;
        }
        if (composition_size == 0 || offset <= composition_at)
        {
            return offset;
        }
        return offset >= composition_at + composition_size ? offset - composition_size + replaced : composition_at;
    }
};

} // namespace

void
ContextState::start_text_edit(std::uint32_t id,
                              std::string_view value,
                              InputTextFlag flags,
                              bool multiline,
                              bool numeric,
                              std::size_t capacity) noexcept
{
    TextEditState& edit = text_edit;
    try
    {
        edit.editor.reset({.capacity = capacity, .single_line = !multiline});
        edit.editor.set_text(value);
        edit.initial.assign(edit.editor.text());
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text", "the text could not be copied for editing");
        return;
    }
    edit.id = id;
    edit.flags = flags;
    edit.multiline = multiline;
    edit.numeric = numeric;
    edit.scroll_x = 0;
    edit.changed_time = time;
    edit.preferred_x = -1;
    edit.mouse_selecting = false;
    edit.started_frame = frame;
    edit.editor.set_overwrite(has_any(flags, InputTextFlag::ALWAYS_OVERWRITE));
    if (has_any(flags, InputTextFlag::AUTO_SELECT_ALL))
    {
        edit.editor.select_all();
    }
}

void
ContextState::end_text_edit(bool committed) noexcept
{
    // What ended, for the field to read this frame (numeric fields apply it).
    TextEditState& edit = text_edit;
    if (edit.id == 0)
    {
        return;
    }
    edit.ended_id = edit.id;
    edit.ended_committed = committed;
    try
    {
        edit.ended_text.assign(committed ? edit.editor.text() : std::string_view{edit.initial});
    }
    catch (...)
    {
        edit.ended_text.clear();
    }
    if (active_id == edit.id)
    {
        clear_active();
    }
    edit.id = 0;
    edit.mouse_selecting = false;
}

namespace
{

/// The pressed count of key @p key this frame.
auto
presses(const ContextState& state,
        Key key) noexcept -> std::uint32_t
{
    return state.keys_pressed[std::to_underlying(key)];
}

/// Whether the layout's key for letter @p letter (lowercase) was pressed: shortcuts follow the keyboard layout.
auto
letter_pressed(const ContextState& state,
               char letter) noexcept -> bool
{
    return state.key_codes_pressed[static_cast<std::uint8_t>(letter)] > 0;
}

} // namespace

auto
text_field(ContextState& state,
           WindowState& window,
           const TextFieldRequest& request) noexcept -> TextFieldResult
{
    TextFieldResult result{};
    TextEditState& edit = state.text_edit;
    const InputTextFlag flags = request.flags;
    const bool editable = !has_any(flags, InputTextFlag::READ_ONLY);
    const bool password = has_any(flags, InputTextFlag::PASSWORD);
    DrawList& list = state.list_of(window);

    // Editing that ended earlier this frame (a click elsewhere at the frame's start).
    if (edit.ended_id == request.id)
    {
        result.ended = true;
        result.committed = edit.ended_committed;
    }
    const std::string_view start_text = request.numeric ? request.edit_text : request.text;

    // The mouse: over the field (the text cursor shape), a click starts editing or moves the cursor.
    const bool hovered = state.item_hoverable(window, request.frame, request.id);
    if (hovered)
    {
        state.hovered_id = request.id;
        state.last_item.hovered = true;
        state.cursor = CursorShape::I_BEAM;
    }
    bool started = false;
    if (hovered && state.mouse[0].clicked && edit.id != request.id)
    {
        state.start_text_edit(request.id, start_text, flags, request.multiline, request.numeric, request.capacity);
        state.set_active(request.id, window);
        state.take_focus(request.id);
        started = true;
    }

    // The keyboard: Tab into the field, or INPUT (Return) or a gamepad's ACTIVATE on it while it has the focus, starts
    // editing (all selected); so does an assistive technology's CLICK.
    const bool return_pressed = presses(state, Key::RETURN) > 0 || presses(state, Key::KEYPAD_ENTER) > 0;
    const bool external_activate = has_any(state.navigation.external, UiNavigationCommand::ACTIVATE) && state.navigation_pressed(UiNavigationCommand::ACTIVATE);
    const bool external_cancel = has_any(state.navigation.external, UiNavigationCommand::CANCEL);
    const bool input_command = state.navigation_pressed(UiNavigationCommand::INPUT) || external_activate;
    const bool requested = state.item_activated(request.id);
    if (edit.id != request.id && !state.disabled
        && ((state.focus_id == request.id && state.focus_visible && (state.focus_arrived_id == request.id || input_command)) || requested))
    {
        state.start_text_edit(request.id, start_text, flags, request.multiline, request.numeric, request.capacity);
        state.set_active(request.id, window);
        state.take_focus(request.id);
        state.focus_visible = !requested || state.focus_visible;
        edit.editor.select_all();
        state.focus_arrived_id = 0;
        started = input_command || requested;
    }

    // An assistive technology sets its text (not a number's: its widget does).
    std::string_view set_text{};
    bool text_set = false;
    if (const UiAccessibilityRequest* set = state.item_request(AccessibilityAction::SET_VALUE); set != nullptr && editable && !request.numeric)
    {
        if (edit.id == request.id)
        {
            try
            {
                edit.editor.set_text(set->text);
            }
            catch (...)
            {
                state.report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text", "the text could not be set");
            }
        }
        else
        {
            set_text = set->text;
            text_set = true;
        }
    }
    const bool editing = edit.id == request.id;
    if (editing)
    {
        state.active_id_alive = true;
        edit.frame = request.frame;
    }

    // Editing that INPUT started this frame (a slider's typed input) does not take that key as a confirmation.
    started = started || (editing && edit.started_frame == state.frame && input_command);

    // The text shown: the edited text (with the IME composition at the cursor), masked for passwords.
    const std::string_view source = editing ? edit.editor.text() : request.text;
    const auto display_of = [&](std::string_view text, DisplayMap& map) -> std::string_view
    {
        map = DisplayMap{.boundaries = editing ? &edit.editor.boundaries() : nullptr};
        try
        {
            if (password)
            {
                const TextBoundaries* boundaries = map.boundaries;
                if (boundaries == nullptr)
                {
                    edit.mask_boundaries.analyze(text);
                    boundaries = &edit.mask_boundaries;
                }
                edit.display.assign(boundaries->grapheme_count(), '*');
                map.masked = editing;
                return edit.display;
            }
            if (editing && !state.composition.empty())
            {
                const auto [begin, end] = edit.editor.selection();
                edit.display.assign(text.substr(0, begin));
                edit.display.append(state.composition);
                edit.display.append(text.substr(end));
                map.composition_at = begin;
                map.composition_size = static_cast<std::uint32_t>(state.composition.size());
                map.replaced = end - begin;
                return edit.display;
            }
        }
        catch (...)
        {
            return text;
        }
        return text;
    };
    DisplayMap map{};
    std::string_view shown = display_of(source, map);
    const auto lay_out = [&](std::string_view text) { return state.layout_text(text, request.wrap_width, request.multiline ? "input_text_multiline" : "input_text"); };
    TextLayout layout = lay_out(shown);

    // Where the text starts: scrolled while edited; with ELIDE_LEFT, a text too long for the field shows its end (and
    // editing starts scrolled there, so it does not jump).
    const bool elide_left = !request.multiline && has_any(flags, InputTextFlag::ELIDE_LEFT);
    if (editing && elide_left && edit.started_frame == state.frame)
    {
        edit.scroll_x = std::max(0.0F, layout.width + 1.0F - request.clip.width());
    }
    const auto origin = [&]
    {
        const bool elided = !editing && elide_left && layout.width > request.clip.width();
        return Vector2{.x = elided ? request.clip.right - layout.width - 1.0F : request.origin.x - (editing ? edit.scroll_x : 0.0F), .y = request.origin.y};
    };
    const auto offset_at = [&](Vector2 point)
    {
        const Vector2 start = origin();
        return map.to_editor(hit_test(layout, point.x - start.x, point.y - start.y));
    };

    // Mouse selection: a click places the cursor (Shift extends), a double click selects a word, a drag extends.
    // A field that starts selecting everything (a slider's typed input, AUTO_SELECT_ALL) keeps it on its first click.
    const bool place = started ? !has_any(flags, InputTextFlag::AUTO_SELECT_ALL) : edit.started_frame != state.frame;
    if (editing && hovered && state.mouse[0].clicked && place)
    {
        const std::uint32_t offset = offset_at(state.mouse_position);
        if (state.mouse[0].double_clicked)
        {
            edit.editor.select_word(offset);
        }
        else
        {
            edit.editor.set_cursor(offset, has_any(state.modifiers, KeyModifier::SHIFT) && !started);
        }
        edit.mouse_selecting = true;
        edit.changed_time = state.time;
    }
    if (editing && edit.mouse_selecting)
    {
        if (state.mouse[0].down)
        {
            if (!state.mouse[0].clicked)
            {
                edit.editor.set_cursor(offset_at(state.mouse_position), true);
            }
        }
        else
        {
            edit.mouse_selecting = false;
        }
    }

    // Keys and typed text while editing.
    bool end = false;
    bool commit = false;
    if (editing && !started)
    {
        TextEditor& editor = edit.editor;
        const std::uint64_t revision = editor.revision();
        const std::uint32_t cursor_before = editor.cursor();
        const std::uint32_t anchor_before = editor.anchor();
        const bool shift = has_any(state.modifiers, KeyModifier::SHIFT);
        const bool control = has_any(state.modifiers, KeyModifier::CONTROL | KeyModifier::SUPER);
        const auto repeat = [&](Key key, auto action)
        {
            for (std::uint32_t press = 0; press < presses(state, key); ++press)
            {
                action();
            }
        };
        const auto copy = [&]
        {
            if (editor.has_selection() && !password)
            {
                try
                {
                    state.services->set_clipboard_text(editor.selected_text());
                }
                catch (...)
                {
                    state.report(UiFrameProblem::PLATFORM_SERVICE, "input_text", "the clipboard could not be written");
                }
            }
        };
        const auto paste = [&]
        {
            if (!editable)
            {
                return;
            }
            try
            {
                // A paste is an undo step of its own (set_cursor() in place ends the typing that merges).
                const std::string text = state.services->clipboard_text();
                filter_text(text, flags, edit.filtered);
                editor.set_cursor(editor.cursor(), true);
                static_cast<void>(editor.insert(edit.filtered));
                editor.set_cursor(editor.cursor(), true);
            }
            catch (...)
            {
                state.report(UiFrameProblem::PLATFORM_SERVICE, "input_text", "the clipboard could not be read");
            }
        };
        const auto erase = [&](TextMotion motion)
        {
            if (editable)
            {
                try
                {
                    static_cast<void>(editor.erase(motion));
                }
                catch (...)
                {
                    state.report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text", "the undo history could not grow");
                }
            }
        };

        // Motions (Control: by words; Shift: extending the selection).
        repeat(Key::LEFT, [&] { editor.move(control ? TextMotion::PREVIOUS_WORD : TextMotion::PREVIOUS_GRAPHEME, shift); });
        repeat(Key::RIGHT, [&] { editor.move(control ? TextMotion::NEXT_WORD : TextMotion::NEXT_GRAPHEME, shift); });
        repeat(Key::HOME, [&] { editor.move(control || !request.multiline ? TextMotion::TEXT_START : TextMotion::LINE_START, shift); });
        repeat(Key::END, [&] { editor.move(control || !request.multiline ? TextMotion::TEXT_END : TextMotion::LINE_END, shift); });
        if (request.multiline)
        {
            // Up and Down: the line above or below, at the column kept since the last horizontal motion; Page Up
            // and Page Down by the visible lines.
            const auto vertical = [&](float lines)
            {
                const CaretPosition caret = caret_position(layout, map.to_display(editor.cursor()));
                if (edit.preferred_x < 0)
                {
                    edit.preferred_x = caret.x;
                }
                const float y = caret.top + caret.height * 0.5F + lines * std::max(caret.height, 1.0F);
                editor.set_cursor(map.to_editor(hit_test(layout, edit.preferred_x, std::max(y, 0.0F))), shift);
            };
            const float page = std::max(1.0F, std::floor(request.clip.height() / std::max(state.current_font().line_height, 1.0F)) - 1);
            repeat(Key::UP, [&] { vertical(-1); });
            repeat(Key::DOWN, [&] { vertical(1); });
            repeat(Key::PAGE_UP, [&] { vertical(-page); });
            repeat(Key::PAGE_DOWN, [&] { vertical(page); });
            if (presses(state, Key::LEFT) + presses(state, Key::RIGHT) + presses(state, Key::HOME) + presses(state, Key::END) > 0)
            {
                edit.preferred_x = -1;
            }
        }

        // Erasing, the clipboard, undo, selecting everything.
        repeat(Key::BACKSPACE, [&] { erase(control ? TextMotion::PREVIOUS_WORD : TextMotion::PREVIOUS_GRAPHEME); });
        if (shift && presses(state, Key::DELETE_FORWARD) > 0)
        {
            copy();
            erase(TextMotion::NEXT_GRAPHEME);
        }
        else
        {
            repeat(Key::DELETE_FORWARD, [&] { erase(control ? TextMotion::NEXT_WORD : TextMotion::NEXT_GRAPHEME); });
        }
        if (control && letter_pressed(state, 'a'))
        {
            editor.select_all();
        }
        if ((control && letter_pressed(state, 'c')) || (control && presses(state, Key::INSERT) > 0))
        {
            copy();
        }
        if (control && letter_pressed(state, 'x') && !password)
        {
            copy();
            if (editable)
            {
                static_cast<void>(editor.erase_selection());
            }
        }
        if ((control && letter_pressed(state, 'v')) || (shift && presses(state, Key::INSERT) > 0))
        {
            paste();
        }
        if (editable && !has_any(flags, InputTextFlag::NO_UNDO_REDO))
        {
            if (control && letter_pressed(state, 'z'))
            {
                static_cast<void>(shift ? editor.redo() : editor.undo());
            }
            if (control && letter_pressed(state, 'y'))
            {
                static_cast<void>(editor.redo());
            }
        }

        // Typed text, through the filters (not while Control alone is held: those are shortcuts; Control+Alt is
        // AltGr). A frame's input has no order between keys and text: motions and erasing apply first, then the
        // text, then Return, Tab and Escape.
        const bool alternate_graphic = control && has_any(state.modifiers, KeyModifier::ALT);
        if (!state.typed_text.empty() && editable && (!control || alternate_graphic))
        {
            try
            {
                filter_text(state.typed_text, flags, edit.filtered);
                static_cast<void>(editor.insert(edit.filtered));
            }
            catch (...)
            {
                state.report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text", "the text could not grow");
            }
        }

        // Return confirms (multi-line: types a line break, with Control or without as the flags say), as a gamepad's
        // ACTIVATE does; Tab types a tab or leaves; Escape (a gamepad's CANCEL) reverts (or clears) and leaves.
        if (external_activate)
        {
            end = true;
            commit = true;
            result.entered = true;
        }
        if (return_pressed)
        {
            const bool control_breaks = has_any(flags, InputTextFlag::CONTROL_ENTER_FOR_NEW_LINE);
            if (request.multiline && control == control_breaks)
            {
                if (editable)
                {
                    static_cast<void>(editor.insert("\n"));
                }
            }
            else
            {
                end = true;
                commit = true;
                result.entered = true;
            }
        }
        if (presses(state, Key::TAB) > 0)
        {
            if (has_any(flags, InputTextFlag::ALLOW_TAB_INPUT) && editable)
            {
                static_cast<void>(editor.insert("\t"));
                state.focus_step = 0;
            }
            else
            {
                end = true;
                commit = true;
            }
        }
        if (presses(state, Key::ESCAPE) > 0 || external_cancel)
        {
            if (has_any(flags, InputTextFlag::ESCAPE_CLEARS_ALL) && !editor.text().empty())
            {
                editor.select_all();
                static_cast<void>(editor.erase_selection());
            }
            else
            {
                try
                {
                    editor.set_text(edit.initial);
                }
                catch (...)
                {
                    state.report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text", "the text could not be reverted");
                }
                end = true;
            }
        }

        result.changed = editor.revision() != revision;
        if (result.changed || editor.cursor() != cursor_before || editor.anchor() != anchor_before)
        {
            edit.changed_time = state.time;
        }
        if (result.changed)
        {
            shown = display_of(editor.text(), map);
            layout = lay_out(shown);
        }
    }

    // Single-line fields scroll horizontally to keep the cursor visible.
    const std::string_view value = editing ? edit.editor.text() : request.text;
    CaretPosition caret = caret_position(layout, editing ? map.to_display(edit.editor.cursor()) : 0);
    if (editing && map.composition_size > 0)
    {
        // The cursor in the composition: its code points from the composition's start.
        std::uint32_t offset = map.composition_at;
        const std::string_view composition = state.composition;
        for (std::int32_t code_point = 0; code_point < state.composition_cursor && offset - map.composition_at < composition.size(); ++code_point)
        {
            offset += decode_utf8(composition, offset - map.composition_at).size;
        }
        caret = caret_position(layout, offset);
    }
    if (editing && !request.multiline && !has_any(flags, InputTextFlag::NO_HORIZONTAL_SCROLL))
    {
        const float visible = std::max(1.0F, request.clip.width());
        if (caret.x - edit.scroll_x > visible - 1.0F)
        {
            edit.scroll_x = caret.x - visible + 1.0F;
        }
        if (caret.x < edit.scroll_x)
        {
            edit.scroll_x = caret.x;
        }
        edit.scroll_x = std::clamp(edit.scroll_x, 0.0F, std::max(0.0F, layout.width + 1.0F - visible));
    }

    // Drawing: the frame, the selection, the text (or the hint), the composition's underline, the cursor.
    const Style& style = state.style;
    if (request.framed)
    {
        // One color whether hovered or edited (as Dear ImGui's): the selection and the cursor show the editing.
        state.draw_frame(list, request.frame, state.color(StyleColor::FRAME_BACKGROUND), style.frame_rounding);
        state.draw_focus(list, request.frame, request.id, style.frame_rounding);
    }
    list.push_clip_rectangle(request.clip);
    const Vector2 start = origin();
    if (editing && edit.editor.has_selection() && map.composition_size == 0)
    {
        const auto [begin, end_offset] = edit.editor.selection();
        const std::uint32_t selection_begin = map.to_display(begin);
        const std::uint32_t selection_end = map.to_display(end_offset);
        const float newline_width = std::max(2.0F, state.current_font().size * 0.3F);
        for (const LaidOutLine& line : layout.lines)
        {
            const std::uint32_t from = std::max(selection_begin, line.text_begin);
            const std::uint32_t to = std::min(selection_end, line.text_end);
            if (from > to || (from == to && selection_end <= line.text_end))
            {
                continue;
            }
            const float left = caret_position(layout, from).x;
            const float right = caret_position(layout, to).x + (selection_end > line.text_end ? newline_width : 0.0F);
            list.add_rectangle_filled({.left = start.x + left, .top = start.y + line.top, .right = start.x + right, .bottom = start.y + line.top + line.height},
                                      state.color(StyleColor::TEXT_SELECTED_BACKGROUND));
        }
    }
    if (value.empty() && !editing && !request.hint.empty())
    {
        state.draw_text(list, state.layout_text(request.hint, request.wrap_width, "input_text"), start, state.color(StyleColor::TEXT_DISABLED));
    }
    else if (request.centered && !editing)
    {
        const Vector2 centered{.x = request.clip.left + std::max(0.0F, request.clip.width() - layout.width) * 0.5F, .y = start.y};
        state.draw_text(list, layout, centered, state.color(StyleColor::TEXT_NORMAL));
    }
    else
    {
        state.draw_text(list, layout, start, state.color(StyleColor::TEXT_NORMAL));
    }
    if (editing && map.composition_size > 0)
    {
        const CaretPosition from = caret_position(layout, map.composition_at);
        const CaretPosition to = caret_position(layout, map.composition_at + map.composition_size);
        const float y = start.y + from.top + from.height - 1.0F;
        list.add_rectangle_filled({.left = start.x + from.x, .top = y, .right = start.x + std::max(to.x, from.x + 1.0F), .bottom = y + 1.0F},
                                  state.color(StyleColor::TEXT_NORMAL));
    }
    if (editing)
    {
        const double since = state.time - edit.changed_time;
        const bool blink_on = since < TEXT_CURSOR_VISIBLE_SECONDS || std::fmod(since, TEXT_CURSOR_BLINK_SECONDS) < TEXT_CURSOR_VISIBLE_SECONDS;
        const float height = caret.height > 0 ? caret.height : state.current_font().line_height;
        edit.cursor = {.left = start.x + caret.x, .top = start.y + caret.top, .right = start.x + caret.x + 1.0F, .bottom = start.y + caret.top + height};
        if (blink_on)
        {
            list.add_rectangle_filled(edit.cursor, state.color(StyleColor::INPUT_TEXT_CURSOR));
        }
        state.text_input_wanted = true;
        state.text_input_area = edit.cursor;
        result.cursor = edit.cursor;
    }
    list.pop_clip_rectangle();

    // Editing ends now (Return, Tab, Escape), or goes on.
    result.size = {.x = layout.width, .y = std::max(layout.height, state.current_font().line_height)};
    if (end)
    {
        state.end_text_edit(commit);
        result.ended = true;
        result.committed = commit;
        result.text = edit.ended_text;
        result.changed = edit.ended_text != request.text;
        return result;
    }
    result.active = editing;
    result.text = editing ? edit.editor.text() : result.ended ? std::string_view{edit.ended_text} : text_set ? set_text : request.text;
    result.changed = (result.changed || result.ended || text_set) && result.text != request.text;
    return result;
}

namespace
{

/// The single-line field of input_text(): its frame, the field, the label after it.
auto
single_line_field(ContextState& state,
                  WindowState& window,
                  std::string_view label,
                  std::string_view text,
                  std::size_t capacity,
                  const InputTextOptions& options,
                  std::string_view operation) noexcept -> TextFieldResult
{
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const float width = state.take_item_width(window);
    const float height = state.current_font().line_height + style.frame_padding.y * 2;
    const TextLayout label_layout = state.layout_text(ContextState::display_text(label), 0, operation);
    const Vector2 position = window.cursor;
    const UiRectangle frame{.left = position.x, .top = position.y, .right = position.x + width, .bottom = position.y + height};
    const float total = width + (label_layout.width > 0 ? style.item_inner_spacing.x + label_layout.width : 0.0F);
    state.item_size(window, {.x = total, .y = height}, style.frame_padding.y);
    if (!state.item_add(window, frame, id, true, operation, label))
    {
        return {.text = text};
    }
    const UiRectangle clip{.left = frame.left + style.frame_padding.x, .top = frame.top, .right = frame.right - style.frame_padding.x, .bottom = frame.bottom};
    const TextFieldResult result = text_field(state, window, {.id = id,
                                                              .frame = frame,
                                                              .origin = {.x = frame.left + style.frame_padding.x, .y = frame.top + style.frame_padding.y},
                                                              .clip = clip,
                                                              .text = text,
                                                              .hint = options.hint,
                                                              .flags = options.flags,
                                                              .capacity = capacity,
                                                              .label = label});
    const bool password = has_any(options.flags, InputTextFlag::PASSWORD);
    const bool read_only = has_any(options.flags, InputTextFlag::READ_ONLY);
    static_cast<void>(state.describe_item(AccessibilityRole::TEXT_FIELD,
                                          (password ? AccessibilityState::PROTECTED : AccessibilityState::NONE)
                                              | (read_only ? AccessibilityState::READ_ONLY : AccessibilityState::NONE),
                                          AccessibilityAction::CLICK | (read_only ? AccessibilityAction::NONE : AccessibilityAction::SET_VALUE),
                                          password ? std::string_view{} : result.text));
    state.draw_text(state.list_of(window), label_layout, {.x = frame.right + style.item_inner_spacing.x, .y = frame.top + style.frame_padding.y},
                    state.color(StyleColor::TEXT_NORMAL));
    if (result.changed)
    {
        state.mark_edited(id);
    }
    return result;
}

} // namespace

auto
UiFrame::input_text(std::string_view label,
                    std::string& text,
                    const InputTextOptions& options) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("input_text");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const TextFieldResult result = single_line_field(state, *window, label, text, 0, options, "input_text");
    if (result.changed)
    {
        try
        {
            text.assign(result.text);
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text", "the text could not grow", label);
            return false;
        }
    }
    return has_any(options.flags, InputTextFlag::ENTER_RETURNS_TRUE) ? result.entered : result.changed;
}

auto
UiFrame::input_text(std::string_view label,
                    std::span<char> buffer,
                    const InputTextOptions& options) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("input_text");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    if (buffer.empty())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "input_text", "a buffer needs room for the terminating NUL", label);
        return false;
    }

    // The NUL-terminated text, edited within the buffer's size minus the NUL.
    const std::string_view text{buffer.data(), static_cast<std::size_t>(std::ranges::find(buffer, '\0') - buffer.begin())};
    const TextFieldResult result = single_line_field(state, *window, label, text, buffer.size() - 1, options, "input_text");
    if (result.changed)
    {
        const std::size_t size = std::min(result.text.size(), buffer.size() - 1);
        std::ranges::copy(result.text.substr(0, size), buffer.begin());
        buffer[size] = '\0';
    }
    return has_any(options.flags, InputTextFlag::ENTER_RETURNS_TRUE) ? result.entered : result.changed;
}

auto
UiFrame::input_text_multiline(std::string_view label,
                              std::string& text,
                              const InputTextOptions& options) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* parent = state.current_window("input_text_multiline");
    if (parent == nullptr || parent->skip_items)
    {
        return false;
    }

    // A framed child region that scrolls (its content: the laid-out text), the label on its right.
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const float line = state.current_font().line_height;
    const float available = state.content_right(*parent) - parent->cursor.x;
    const float width = options.size.x > 0   ? options.size.x
                        : options.size.x < 0 ? std::max(4.0F, available + options.size.x)
                                             : state.take_item_width(*parent);
    const float height = options.size.y > 0 ? options.size.y : line * 8 + style.frame_padding.y * 2;
    const TextLayout label_layout = state.layout_text(ContextState::display_text(label), 0, "input_text_multiline");
    const Vector2 label_position{.x = parent->cursor.x + width + style.item_inner_spacing.x, .y = parent->cursor.y + style.frame_padding.y};
    bool changed = false;
    bool entered = false;
    const bool wrap = has_any(options.flags, InputTextFlag::WORD_WRAP);
    if (begin_child(label, {.x = width, .y = height}, ChildFlag::FRAME_STYLE, wrap ? UiWindowFlag::NONE : UiWindowFlag::HORIZONTAL_SCROLLBAR))
    {
        WindowState& child = state.windows[state.window_stack.back()];
        const UiRectangle visible = child.hit.intersection(child.clip);
        if (state.item_add(child, visible, id, true, "input_text_multiline", label))
        {
            const float wrap_width = wrap ? std::max(1.0F, child.inner.width() - child.padding.x * 2) : 0.0F;
            const TextFieldResult result = text_field(state, child, {.id = id,
                                                                     .frame = visible,
                                                                     .origin = child.cursor_start,
                                                                     .clip = child.clip,
                                                                     .text = text,
                                                                     .hint = options.hint,
                                                                     .flags = options.flags,
                                                                     .wrap_width = wrap_width,
                                                                     .multiline = true,
                                                                     .framed = false,
                                                                     .label = label});
            const bool read_only = has_any(options.flags, InputTextFlag::READ_ONLY);
            static_cast<void>(state.describe_item(AccessibilityRole::TEXT_FIELD,
                                                  AccessibilityState::MULTILINE | (read_only ? AccessibilityState::READ_ONLY : AccessibilityState::NONE),
                                                  AccessibilityAction::CLICK | (read_only ? AccessibilityAction::NONE : AccessibilityAction::SET_VALUE),
                                                  result.text));
            dummy(result.size);
            if (result.changed)
            {
                try
                {
                    text.assign(result.text);
                    changed = true;
                    state.mark_edited(id);
                }
                catch (...)
                {
                    state.report(UiFrameProblem::CAPACITY_EXCEEDED, "input_text_multiline", "the text could not grow", label);
                }
            }
            entered = result.entered;

            // The cursor stays in view: the region scrolls to it.
            if (result.active)
            {
                const float top = child.inner.top + child.padding.y;
                const float bottom = child.inner.bottom - child.padding.y;
                if (result.cursor.top < top)
                {
                    set_scroll_y(child.scroll.y - (top - result.cursor.top));
                }
                else if (result.cursor.bottom > bottom)
                {
                    set_scroll_y(child.scroll.y + (result.cursor.bottom - bottom));
                }
            }
        }
    }
    end_child();
    state.draw_text(state.list_of(*parent), label_layout, label_position, state.color(StyleColor::TEXT_NORMAL));
    if (label_layout.width > 0)
    {
        parent->cursor_maximum.x = std::max(parent->cursor_maximum.x, label_position.x + label_layout.width);
    }
    return has_any(options.flags, InputTextFlag::ENTER_RETURNS_TRUE) ? entered : changed;
}

} // namespace gfx::ui
