/// @file bidi.cpp
/// @brief Bidi levels, scripts and visual runs of a paragraph, with SheenBidi (UAX #9, UAX #24).
module;

#include <SheenBidi/SheenBidi.h>

module gfx.ui.text;

import std;
import :engine;

namespace gfx::ui::text_engine
{

struct BidiParagraph::State
{
    SBAlgorithmRef algorithm = nullptr;
    SBParagraphRef paragraph = nullptr;
    std::vector<std::uint8_t> levels{};
    std::vector<std::uint32_t> scripts{};
    std::uint8_t base_level = 0;

    State() = default;
    State(const State&) = delete;
    auto operator=(const State&) -> State& = delete;

    ~State()
    {
        release();
    }

    void release() noexcept
    {
        if (paragraph != nullptr)
        {
            SBParagraphRelease(paragraph);
            paragraph = nullptr;
        }
        if (algorithm != nullptr)
        {
            SBAlgorithmRelease(algorithm);
            algorithm = nullptr;
        }
    }
};

auto
is_mark(char32_t code_point) noexcept -> bool
{
    return SBGeneralCategoryIsMark(SBCodepointGetGeneralCategory(static_cast<SBCodepoint>(code_point)));
}

auto
is_common_or_inherited(char32_t code_point) noexcept -> bool
{
    const SBScript script = SBCodepointGetScript(static_cast<SBCodepoint>(code_point));
    return script == SBScriptZYYY || script == SBScriptZINH;
}

BidiParagraph::BidiParagraph()
    : m_state{std::make_unique<State>()}
{
}

BidiParagraph::~BidiParagraph() = default;

void
BidiParagraph::analyze(std::span<const std::uint32_t> code_points,
                       ParagraphDirection direction)
{
    State& state = *m_state;
    state.release();

    // Levels: the paragraph is one (the engine splits at paragraph separators first).
    const SBCodepointSequence sequence{.stringEncoding = SBStringEncodingUTF32,
                                       .stringBuffer = code_points.data(),
                                       .stringLength = code_points.size()};
    const SBLevel base = direction == ParagraphDirection::AUTOMATIC       ? SBLevelDefaultLTR
                         : direction == ParagraphDirection::LEFT_TO_RIGHT ? SBLevel{0}
                                                                          : SBLevel{1};
    state.algorithm = SBAlgorithmCreate(&sequence);
    state.paragraph = SBAlgorithmCreateParagraph(state.algorithm, 0, code_points.size(), base);
    state.base_level = SBParagraphGetBaseLevel(state.paragraph);
    const std::size_t length = SBParagraphGetLength(state.paragraph);
    const SBLevel* levels = SBParagraphGetLevelsPtr(state.paragraph);
    state.levels.assign(levels, levels + length);
    state.levels.resize(code_points.size(), state.base_level);

    // Scripts: common and inherited characters take their neighbors' script.
    state.scripts.assign(code_points.size(), SBScriptGetUnicodeTag(SBScriptZYYY));
    SBScriptLocatorRef locator = SBScriptLocatorCreate();
    SBScriptLocatorLoadCodepoints(locator, &sequence);
    while (SBScriptLocatorMoveNext(locator) != 0)
    {
        const SBScriptAgent* agent = SBScriptLocatorGetAgent(locator);
        const std::uint32_t tag = SBScriptGetUnicodeTag(agent->script);
        std::fill_n(state.scripts.begin() + static_cast<std::ptrdiff_t>(agent->offset), agent->length, tag);
    }
    SBScriptLocatorRelease(locator);
}

auto
BidiParagraph::levels() const noexcept -> std::span<const std::uint8_t>
{
    return m_state->levels;
}

auto
BidiParagraph::scripts() const noexcept -> std::span<const std::uint32_t>
{
    return m_state->scripts;
}

auto
BidiParagraph::base_level() const noexcept -> std::uint8_t
{
    return m_state->base_level;
}

void
BidiParagraph::line_runs(std::uint32_t begin,
                         std::uint32_t end,
                         std::vector<VisualRun>& runs) const
{
    // SheenBidi applies rules L1 (trailing whitespace) and L2 (reordering): its runs are in visual order.
    SBLineRef line = SBParagraphCreateLine(m_state->paragraph, begin, end - begin);
    const SBUInteger count = SBLineGetRunCount(line);
    const SBRun* line_runs = SBLineGetRunsPtr(line);
    for (SBUInteger index = 0; index < count; ++index)
    {
        runs.push_back({.begin = static_cast<std::uint32_t>(line_runs[index].offset),
                        .end = static_cast<std::uint32_t>(line_runs[index].offset + line_runs[index].length),
                        .level = line_runs[index].level});
    }
    SBLineRelease(line);
}

} // namespace gfx::ui::text_engine
