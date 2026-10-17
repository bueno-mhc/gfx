/// @file renderer.cppm
/// @brief gfx.ui.renderer: UiRenderer, which draws UiDrawData with the graphics abstraction. One graphics
///        pipeline on the bindless layout; vertices and indices of a frame written into one persistently
///        mapped buffer (a region per frame in flight) and pulled by the vertex shader; atlas pages
///        uploaded once as images of the bindless table; per draw command a scissor, push constants and an
///        indexed draw. The application owns the rendering pass, the swapchain and the submission.
export module gfx.ui.renderer;

import std;
import gfx;
import gfx.ui.enums;
import gfx.ui.text;
import gfx.ui.draw;

export namespace gfx::ui
{

/// @brief How a UiRenderer draws.
struct UiRendererDescription
{
    Format color_format = Format::B8G8R8A8_UNORM;    ///< Format of the color attachment drawn into (sRGB formats get linear colors).
    SampleCount samples = SampleCount::ONE;          ///< Samples of the attachment.
    std::uint32_t frames_in_flight = 2;              ///< Frames the GPU may still draw while the next is recorded.
    std::uint64_t geometry_bytes_per_frame = 8 << 20; ///< Bytes of vertices and indices one frame may draw.
    std::string_view debug_name = "ui";              ///< Prefix of its objects' names.
};

/// @brief What the last record() drew, and what is resident.
struct UiRendererStatistics
{
    std::uint32_t draw_calls = 0;        ///< Indexed draws.
    std::uint32_t vertices = 0;          ///< Vertices written.
    std::uint32_t indices = 0;           ///< Indices written.
    std::uint32_t skipped_commands = 0;  ///< Commands not drawn: their atlas page was not prepared, or the
                                         ///< frame's geometry did not fit geometry_bytes_per_frame.
    std::uint32_t resident_pages = 0;    ///< Atlas pages uploaded.
    std::uint64_t uploaded_bytes = 0;    ///< Texel bytes uploaded since the renderer was made.
};

/// @brief Draws UI draw data into a color attachment. One thread at a time; one renderer per pipeline
///        format.
///
/// @code
/// renderer.begin_frame(timeline.current_frame());     // after FrameTimeline::begin_frame()
/// renderer.prepare(recorder, draw_data);               // before the rendering pass: new atlas pages
/// gfx::RenderingScope scope = recorder.begin_rendering(pass);
/// renderer.record(scope, draw_data);
/// @endcode
class UiRenderer
{
public:
    /// @brief A renderer drawing with @p device the atlases of @p fonts (which must outlive it).
    UiRenderer(Device& device,
               const FontLibrary& fonts,
               const UiRendererDescription& description = {});

    UiRenderer(const UiRenderer&) = delete;
    auto operator=(const UiRenderer&) -> UiRenderer& = delete;
    ~UiRenderer();

    /// @brief Starts frame @p frame (FrameTimeline::current_frame()): its region of the geometry buffer is
    ///        reused, and the staging buffers of uploads the GPU has finished are released.
    void begin_frame(std::uint64_t frame);

    /// @brief Uploads the atlas pages @p data samples that are not resident yet; outside a rendering pass.
    void prepare(Recorder<QueueType::GRAPHICS>& recorder,
                 const UiDrawData& data);

    /// @brief Draws @p data in the current rendering pass of @p commands (viewport: the data's width and
    ///        height). Never throws for a command it cannot draw: it is skipped and counted.
    void record(RenderingCommands& commands,
                const UiDrawData& data);

    /// @brief What the last record() drew.
    [[nodiscard]] auto statistics() const noexcept -> const UiRendererStatistics&;

private:
    struct State;
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::ui
