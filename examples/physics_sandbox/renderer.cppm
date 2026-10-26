/// @file renderer.cppm
/// @brief The sandbox's 3D renderer: physics shapes as lit, instanced meshes and debug lines, drawn with the graphics
///        library into a multisampled pass resolved into the frame's image.
///
/// Each shape is tessellated once (gfx.physics.debug_draw) and its mesh copied into device-local storage buffers shared
/// by every mesh; a frame lists instances (a shape at a pose, with a color), moving surfaces (soft bodies: their indices
/// stored once, their vertices written each frame) and lines, and render() draws one instanced call per mesh and one call
/// per surface: the vertex shader pulls vertices and instances from bindless storage buffers (no vertex input, no
/// descriptor set but the bindless table). Depth is reversed (cleared to 0, nearer is greater) for the camera's infinite
/// far plane.
export module physics_sandbox.renderer;

import std;
export import gfx;
export import gfx.physics;

export namespace physics_sandbox
{

/// The floor's grid lines are drawn on the instance (mesh.frag).
inline constexpr std::uint32_t INSTANCE_GRID = 1U;
/// The instance is tinted as highlighted (mesh.frag).
inline constexpr std::uint32_t INSTANCE_HIGHLIGHT = 2U;

/// How the renderer is set up.
struct SceneRendererDescription
{
    gfx::Format color_format = gfx::Format::B8G8R8A8_SRGB;     ///< The format of the images it renders into.
    gfx::SampleCount samples = gfx::SampleCount::FOUR;          ///< Samples per pixel (ONE: no multisampling).
    std::uint32_t frames_in_flight = 2;                         ///< Frames the GPU may still be drawing.
    std::uint64_t mesh_vertex_bytes = 32ULL << 20U;             ///< Storage for every mesh's vertices (24 bytes each).
    std::uint64_t mesh_index_bytes = 16ULL << 20U;              ///< Storage for every mesh's indices (4 bytes each).
    std::uint64_t frame_bytes = 8ULL << 20U;                    ///< Instances and lines of one frame.
    gfx::physics::TessellationDescription tessellation{};       ///< How finely round shapes are cut.
};

/// A frame's camera and light.
struct SceneView
{
    gfx::Matrix4 view_projection{};                                          ///< World space to clip space (reversed depth).
    gfx::Vector3 light_direction{.x = 0.35F, .y = 0.85F, .z = 0.4F};         ///< Toward the light (any length but 0).
    float light_strength = 0.85F;                                            ///< The directional light's share.
    std::array<float, 4> background{0.05F, 0.06F, 0.08F, 1.0F};              ///< Clear color (linear).
};

/// What the last frame drew.
struct SceneRendererStatistics
{
    std::uint32_t meshes = 0;          ///< Distinct meshes stored.
    std::uint64_t mesh_vertices = 0;   ///< Their vertices.
    std::uint64_t mesh_indices = 0;    ///< Their indices.
    std::uint32_t instances = 0;       ///< Instances drawn.
    std::uint32_t surfaces = 0;        ///< Moving surfaces drawn.
    std::uint32_t draw_calls = 0;      ///< Draw calls.
    std::uint32_t lines = 0;           ///< Line segments drawn.
    std::uint32_t skipped = 0;         ///< Instances or lines not drawn (storage full).
};

/// Draws shapes and lines; one per window, used on one thread.
class SceneRenderer
{
public:
    /// @param device The device it renders with (outlives the renderer).
    SceneRenderer(gfx::Device& device,
                  const SceneRendererDescription& description);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    auto operator=(const SceneRenderer&) -> SceneRenderer& = delete;

    /// @brief Starts frame @p frame (after FrameTimeline::begin_frame()): its instances and lines are empty.
    void begin_frame(std::uint64_t frame);

    /// @brief Draws @p shape at @p pose in @p color this frame (its mesh is made the first time the shape is seen).
    void add(const std::shared_ptr<const gfx::physics::Shape>& shape,
             const gfx::Transform& pose,
             gfx::physics::DebugColor color,
             std::uint32_t flags = 0);

    /// @brief Draws a surface whose vertices move (a soft body's) this frame, two-sided in @p color: @p positions in world
    ///        space and @p indices (three per triangle into them), the same for @p key every frame (copied the first time
    ///        @p key is seen); normals from the triangles around each vertex.
    void add_surface(const void* key,
                     std::span<const std::uint32_t> indices,
                     std::span<const gfx::Vector3> positions,
                     gfx::physics::DebugColor color);

    /// @brief This frame's lines (world space).
    [[nodiscard]] auto lines() noexcept -> gfx::physics::DebugLines&;

    /// @brief Records the copies of the meshes made since the last call (before the rendering pass).
    void prepare(gfx::GraphicsRecorder& recorder);

    /// @brief Records the scene's pass into @p target (a view of an image of @p extent, in COLOR_ATTACHMENT_WRITE state,
    ///        left there): cleared, the instances, the lines; with multisampling, resolved into it.
    void render(gfx::GraphicsRecorder& recorder,
                gfx::ImageViewHandle target,
                gfx::Extent extent,
                const SceneView& view);

    /// @brief Forgets every mesh (the next frame makes them again); call when no frame in flight draws them (after
    ///        Device::wait_idle()), e.g. when a scene is replaced.
    void clear_meshes();

    /// @brief What the last frame drew.
    [[nodiscard]] auto statistics() const noexcept -> const SceneRendererStatistics&;

    /// @brief Samples per pixel (the description's: the application picks a count the device supports).
    [[nodiscard]] auto samples() const noexcept -> gfx::SampleCount;

private:
    struct State;
    std::unique_ptr<State> m_state{};
};

} // namespace physics_sandbox
