// TextBoundaries and TextEditor: grapheme clusters (combining marks, emoji sequences, flags), words, motions,
// selection, inserting and erasing, clean UTF-8, the capacity, single-line mode, overwriting, undo and redo.
#include <gtest/gtest.h>

import std;
import gfx.ui;

namespace
{

using namespace gfx::ui;

/// The grapheme boundaries of @p text, in order.
auto
grapheme_boundaries(std::string_view text) -> std::vector<std::uint32_t>
{
    TextBoundaries boundaries;
    boundaries.analyze(text);
    std::vector<std::uint32_t> result{0};
    for (std::uint32_t offset = 0; offset < text.size();)
    {
        offset = boundaries.next_grapheme(offset);
        result.push_back(offset);
    }
    return result;
}

TEST(TextBoundariesTest, GraphemesKeepCombiningMarksEmojiSequencesAndFlagsTogether)
{
    // "e" + U+0301 COMBINING ACUTE ACCENT is one grapheme (3 bytes); the rest are ASCII.
    EXPECT_EQ(grapheme_boundaries("aéb"), (std::vector<std::uint32_t>{0, 1, 4, 5}));

    // A woman technologist (woman, zero width joiner, laptop): one grapheme of 11 bytes.
    EXPECT_EQ(grapheme_boundaries("\U0001F469‍\U0001F4BBx"), (std::vector<std::uint32_t>{0, 11, 12}));

    // The flag of Brazil: two regional indicators, one grapheme; two flags are two graphemes.
    EXPECT_EQ(grapheme_boundaries("\U0001F1E7\U0001F1F7\U0001F1E7\U0001F1F7"), (std::vector<std::uint32_t>{0, 8, 16}));

    // CR LF is one grapheme.
    EXPECT_EQ(grapheme_boundaries("a\r\nb"), (std::vector<std::uint32_t>{0, 1, 3, 4}));

    TextBoundaries boundaries;
    boundaries.analyze("aéb");
    EXPECT_EQ(boundaries.grapheme_count(), 3U);
    EXPECT_FALSE(boundaries.is_grapheme_boundary(2)) << "inside the accented e";
    EXPECT_EQ(boundaries.previous_grapheme(4), 1U);
    EXPECT_EQ(boundaries.previous_grapheme(0), 0U);
    EXPECT_EQ(boundaries.next_grapheme(5), 5U);
}

TEST(TextBoundariesTest, WordsSkipSpacesAndPunctuationIsAWordOfItsOwn)
{
    // Offsets:         0     6 7     13 15
    const std::string_view text = "hello, world  foo";
    TextBoundaries boundaries;
    boundaries.analyze(text);
    EXPECT_EQ(boundaries.next_word(0), 5U) << "to the comma";
    EXPECT_EQ(boundaries.next_word(5), 7U) << "past the comma and the space";
    EXPECT_EQ(boundaries.next_word(7), 14U) << "past two spaces";
    EXPECT_EQ(boundaries.next_word(14), 17U);
    EXPECT_EQ(boundaries.next_word(17), 17U);
    EXPECT_EQ(boundaries.previous_word(17), 14U);
    EXPECT_EQ(boundaries.previous_word(14), 7U) << "back over the spaces to the start of the word";
    EXPECT_EQ(boundaries.previous_word(9), 7U) << "inside a word: its start";
    EXPECT_EQ(boundaries.previous_word(7), 5U) << "the comma";
    EXPECT_EQ(boundaries.previous_word(1), 0U);
    EXPECT_EQ(boundaries.word_at(9), (std::pair<std::uint32_t, std::uint32_t>{7, 12}));
    EXPECT_EQ(boundaries.word_at(12), (std::pair<std::uint32_t, std::uint32_t>{12, 14})) << "the spaces";

    // Words of other scripts and accented words are words too.
    boundaries.analyze("café мир");
    EXPECT_EQ(boundaries.next_word(0), 6U);
    EXPECT_EQ(boundaries.next_word(6), 12U);
}

TEST(TextEditorTest, MotionsMoveByGraphemesWordsAndLines)
{
    TextEditor editor;
    editor.set_text("one tést\nsecond line");
    EXPECT_EQ(editor.cursor(), editor.text().size()) << "at the end after set_text()";
    editor.move(TextMotion::TEXT_START);
    EXPECT_EQ(editor.cursor(), 0U);
    editor.move(TextMotion::NEXT_WORD);
    EXPECT_EQ(editor.cursor(), 4U);
    editor.move(TextMotion::NEXT_GRAPHEME);
    editor.move(TextMotion::NEXT_GRAPHEME);
    EXPECT_EQ(editor.cursor(), 7U) << "the é is two bytes";
    editor.move(TextMotion::LINE_END);
    EXPECT_EQ(editor.cursor(), 9U);
    editor.move(TextMotion::NEXT_GRAPHEME);
    editor.move(TextMotion::LINE_END);
    EXPECT_EQ(editor.cursor(), editor.text().size());
    editor.move(TextMotion::LINE_START);
    EXPECT_EQ(editor.cursor(), 10U);
    editor.move(TextMotion::PREVIOUS_GRAPHEME);
    editor.move(TextMotion::LINE_START);
    EXPECT_EQ(editor.cursor(), 0U);

    // set_cursor() snaps into a grapheme's start.
    editor.set_cursor(6);
    EXPECT_EQ(editor.cursor(), 5U);
}

TEST(TextEditorTest, SelectionsExtendCollapseAndAreReplaced)
{
    TextEditor editor;
    editor.set_text("hello world");
    editor.move(TextMotion::TEXT_START);
    editor.move(TextMotion::NEXT_WORD, true);
    EXPECT_EQ(editor.selected_text(), "hello ");
    EXPECT_EQ(editor.anchor(), 0U);

    // A motion without extending collapses the selection to its end first.
    editor.move(TextMotion::NEXT_GRAPHEME);
    EXPECT_FALSE(editor.has_selection());
    EXPECT_EQ(editor.cursor(), 6U);

    // Typing replaces the selection.
    editor.set_selection(0, 5);
    EXPECT_TRUE(editor.insert("goodbye"));
    EXPECT_EQ(editor.text(), "goodbye world");
    EXPECT_EQ(editor.cursor(), 7U);
    editor.select_word(9);
    EXPECT_EQ(editor.selected_text(), "world");
    editor.select_all();
    EXPECT_EQ(editor.selected_text(), "goodbye world");
}

TEST(TextEditorTest, ErasingRemovesGraphemesWordsOrTheSelection)
{
    TextEditor editor;
    editor.set_text("façade é \U0001F469‍\U0001F4BB end");
    EXPECT_TRUE(editor.erase(TextMotion::PREVIOUS_WORD));
    EXPECT_EQ(editor.text(), "façade é \U0001F469‍\U0001F4BB ");
    EXPECT_TRUE(editor.erase(TextMotion::PREVIOUS_GRAPHEME));
    EXPECT_TRUE(editor.erase(TextMotion::PREVIOUS_GRAPHEME)) << "the whole emoji sequence";
    EXPECT_EQ(editor.text(), "façade é ");
    editor.erase(TextMotion::PREVIOUS_GRAPHEME);
    EXPECT_TRUE(editor.erase(TextMotion::PREVIOUS_GRAPHEME)) << "the e with its accent";
    EXPECT_EQ(editor.text(), "façade ");
    editor.move(TextMotion::TEXT_START);
    EXPECT_TRUE(editor.erase(TextMotion::NEXT_WORD));
    EXPECT_EQ(editor.text(), "");
    EXPECT_FALSE(editor.erase(TextMotion::NEXT_GRAPHEME)) << "nothing after the cursor";
}

TEST(TextEditorTest, InsertedTextIsCleanedAndCutToTheCapacity)
{
    TextEditor editor{{.capacity = 9}};
    EXPECT_TRUE(editor.insert("a\x01" "b\r\nc\xFF"));
    EXPECT_EQ(editor.text(), "ab\nc\xEF\xBF\xBD") << "control characters dropped, CR LF one break, an invalid byte as U+FFFD";
    EXPECT_TRUE(editor.insert("ééé"));
    EXPECT_EQ(editor.text().size(), 9U) << "seven bytes and one two-byte é";
    EXPECT_EQ(editor.text(), "ab\nc\xEF\xBF\xBDé") << "cut at a character";
    EXPECT_FALSE(editor.insert("x")) << "full";

    // Single-line editors turn breaks into spaces.
    TextEditor line{{.single_line = true}};
    line.insert("one\ntwo\r\nthree");
    EXPECT_EQ(line.text(), "one two three");
}

TEST(TextEditorTest, OverwritingReplacesTheGraphemesAfterTheCursor)
{
    TextEditor editor;
    editor.set_text("abcd");
    editor.set_cursor(1);
    editor.set_overwrite(true);
    editor.insert("XY");
    EXPECT_EQ(editor.text(), "aXYd");
    editor.move(TextMotion::TEXT_END);
    editor.insert("Z");
    EXPECT_EQ(editor.text(), "aXYdZ") << "appended at the end";
}

TEST(TextEditorTest, UndoRevertsTypingWordByWordAndRedoAppliesItAgain)
{
    TextEditor editor;
    for (const char character : std::string_view{"hello world"})
    {
        editor.insert({&character, 1});
    }
    EXPECT_TRUE(editor.can_undo());
    EXPECT_TRUE(editor.undo());
    EXPECT_EQ(editor.text(), "hello ") << "the second word";
    EXPECT_TRUE(editor.undo());
    EXPECT_EQ(editor.text(), "");
    EXPECT_FALSE(editor.undo());
    EXPECT_TRUE(editor.redo());
    EXPECT_TRUE(editor.redo());
    EXPECT_EQ(editor.text(), "hello world");
    EXPECT_FALSE(editor.redo());

    // Erasing in a row is one edit; a motion ends it; a new edit clears what could be redone.
    editor.erase(TextMotion::PREVIOUS_GRAPHEME);
    editor.erase(TextMotion::PREVIOUS_GRAPHEME);
    editor.erase(TextMotion::PREVIOUS_GRAPHEME);
    EXPECT_EQ(editor.text(), "hello wo");
    editor.undo();
    EXPECT_EQ(editor.text(), "hello world");
    EXPECT_EQ(editor.cursor(), 11U) << "the cursor is back too";
    editor.undo();
    editor.insert("!");
    EXPECT_FALSE(editor.can_redo());

    // A limited history forgets the oldest edits.
    TextEditor short_history{{.undo_levels = 2}};
    short_history.insert("a ");
    short_history.move(TextMotion::TEXT_START);
    short_history.insert("b ");
    short_history.move(TextMotion::TEXT_END);
    short_history.insert("c");
    EXPECT_TRUE(short_history.undo());
    EXPECT_TRUE(short_history.undo());
    EXPECT_FALSE(short_history.undo());
    EXPECT_EQ(short_history.text(), "a ");
}

TEST(TextEditorTest, TheRevisionCountsChanges)
{
    TextEditor editor;
    const std::uint64_t start = editor.revision();
    editor.set_text("x");
    editor.move(TextMotion::TEXT_START);
    EXPECT_EQ(editor.revision(), start + 1) << "motions change nothing";
    editor.insert("y");
    EXPECT_EQ(editor.revision(), start + 2);
}

} // namespace
