// The sandbox's renderer: meshes tessellated once and stored in shared device-local buffers, a frame's instances and
// lines in a per-frame host-visible buffer, one instanced draw per mesh in a multisampled pass with reversed depth.
module physics_sandbox.renderer;

import std;
import gfx;
import gfx.physics;
import physics_sandbox.shaders; // the shaders, compiled at build time by gfx_add_shaders()

namespace physics_sandbox
{

namespace
{

constexpr std::uint32_t VERTEX_WORDS = 6;      ///< A mesh vertex: position, normal (mesh.vert).
constexpr std::uint32_t INSTANCE_WORDS = 9;    ///< An instance: translation, orientation, color, flags (mesh.vert).
constexpr std::uint32_t LINE_VERTEX_WORDS = 4; ///< A line vertex: position, color (line.vert).
constexpr gfx::Format DEPTH_FORMAT = gfx::Format::D32_SFLOAT;
constexpr std::uint64_t STAGING_BYTES = 16ULL << 20U; ///< Mesh bytes copied per frame at most (the rest waits a frame).

/// mesh.vert's and mesh.frag's push constants.
struct MeshPush
{
    std::array<float, 16> view_projection{};
    std::array<float, 4> light{};
    std::uint32_t vertex_buffer = 0;
    std::uint32_t instance_buffer = 0;
    std::uint32_t instance_word = 0;
    std::uint32_t flags = 0;
};
static_assert(sizeof(MeshPush) == 96);

/// line.vert's push constants.
struct LinePush
{
    std::array<float, 16> view_projection{};
    std::uint32_t line_buffer = 0;
    std::uint32_t line_word = 0;
    std::uint32_t flags = 0;
    std::uint32_t padding = 0;
};
static_assert(sizeof(LinePush) == 80);

/// A mesh in the shared buffers (its vertices from first_vertex, its indices from first_index, mesh-relative).
struct StoredMesh
{
    std::shared_ptr<const gfx::physics::Shape> shape{}; ///< Kept alive: the lookup is by its address.
    std::uint32_t first_vertex = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    bool two_sided = false;
    bool ready = false; ///< Copied into the buffers.
};

/// A mesh made but not copied yet.
struct PendingMesh
{
    std::uint32_t mesh = 0;
    gfx::physics::DebugMesh data{};
};

/// A frame's instance.
struct Instance
{
    std::uint32_t mesh = 0;
    gfx::Transform pose{};
    std::uint32_t color = 0;
    std::uint32_t flags = 0;
};

[[nodiscard]] auto
matrix_floats(const gfx::Matrix4& matrix) noexcept -> std::array<float, 16>
{
    return std::bit_cast<std::array<float, 16>>(matrix);
}

/// Stores @p value's bits at @p word of @p memory.
void
store(std::span<std::byte> memory,
      std::size_t word,
      std::uint32_t value) noexcept
{
    std::memcpy(memory.data() + word * 4, &value, 4);
}

void
store(std::span<std::byte> memory,
      std::size_t word,
      float value) noexcept
{
    store(memory, word, std::bit_cast<std::uint32_t>(value));
}

} // namespace

struct SceneRenderer::State
{
    State(gfx::Device& graphics_device,
          const SceneRendererDescription& renderer_description)
        : device{graphics_device}
        , description{renderer_description}
        , uploader{graphics_device, STAGING_BYTES, renderer_description.frames_in_flight}
        , frame_data{graphics_device, renderer_description.frame_bytes, renderer_description.frames_in_flight, gfx::BufferUsage::STORAGE,
                     gfx::MemoryLocation::HOST_UPLOAD, "sandbox frame data"}
    {
    }

    gfx::Device& device;
    SceneRendererDescription description;
    bool srgb = false;
    gfx::Uploader uploader;
    gfx::TransientBufferAllocator frame_data;
    std::unique_ptr<gfx::Buffer> vertices{};
    std::unique_ptr<gfx::Buffer> indices{};
    std::uint64_t stored_vertices = 0;
    std::uint64_t stored_indices = 0;
    std::unordered_map<const gfx::physics::Shape*, std::uint32_t> lookup{};
    std::vector<StoredMesh> meshes{};
    std::vector<PendingMesh> pending{};
    std::vector<Instance> instances{};
    std::vector<std::uint32_t> order{};
    gfx::physics::DebugLines lines{};
    std::unique_ptr<gfx::ShaderModule> mesh_vertex{};
    std::unique_ptr<gfx::ShaderModule> mesh_fragment{};
    std::unique_ptr<gfx::ShaderModule> line_vertex{};
    std::unique_ptr<gfx::ShaderModule> line_fragment{};
    std::unique_ptr<gfx::GraphicsPipeline> mesh_pipeline{};
    std::unique_ptr<gfx::GraphicsPipeline> two_sided_pipeline{};
    std::unique_ptr<gfx::GraphicsPipeline> line_pipeline{};
    std::unique_ptr<gfx::Image> color_image{};
    std::unique_ptr<gfx::ImageView> color_view{};
    std::unique_ptr<gfx::Image> depth_image{};
    std::unique_ptr<gfx::ImageView> depth_view{};
    gfx::Extent target_extent{};
    SceneRendererStatistics statistics{};

    /// The multisampled color and the depth images for @p extent (made again when the size changes).
    void prepare_targets(gfx::Extent extent)
    {
        if (depth_image != nullptr && target_extent == extent)
        {
            return;
        }
        target_extent = extent;
        const bool multisampled = description.samples != gfx::SampleCount::ONE;
        color_view.reset();
        color_image.reset();
        if (multisampled)
        {
            color_image = device.create_image({.format = description.color_format,
                                               .extent = extent,
                                               .samples = description.samples,
                                               .usage = gfx::ImageUsage::COLOR_ATTACHMENT | gfx::ImageUsage::TRANSIENT_ATTACHMENT,
                                               .debug_name = "sandbox color"});
            color_view = device.create_image_view({.image = color_image->handle(), .bindless = false, .debug_name = "sandbox color"});
        }
        depth_view.reset();
        depth_image = device.create_image({.format = DEPTH_FORMAT,
                                           .extent = extent,
                                           .samples = description.samples,
                                           .usage = gfx::ImageUsage::DEPTH_STENCIL_ATTACHMENT | gfx::ImageUsage::TRANSIENT_ATTACHMENT,
                                           .debug_name = "sandbox depth"});
        depth_view = device.create_image_view(
            {.image = depth_image->handle(), .range = {.aspects = gfx::ImageAspect::DEPTH}, .bindless = false, .debug_name = "sandbox depth"});
    }
};

SceneRenderer::SceneRenderer(gfx::Device& device,
                             const SceneRendererDescription& description)
    : m_state{std::make_unique<State>(device, description)}
{
    State& state = *m_state;
    state.srgb = gfx::format_properties(description.color_format).numeric_type == gfx::FormatNumericType::SRGB;

    // The shared mesh buffers: read by the vertex shader (vertices) and as the index buffer.
    state.vertices = device.create_buffer({.size = description.mesh_vertex_bytes,
                                           .usage = gfx::BufferUsage::STORAGE | gfx::BufferUsage::TRANSFER_DESTINATION,
                                           .debug_name = "sandbox mesh vertices"});
    state.indices = device.create_buffer({.size = description.mesh_index_bytes,
                                          .usage = gfx::BufferUsage::INDEX | gfx::BufferUsage::TRANSFER_DESTINATION,
                                          .bindless = false,
                                          .debug_name = "sandbox mesh indices"});

    // Lit solids culled from behind (one-sided) or not (two-sided meshes), lines over them; depth reversed.
    state.mesh_vertex = device.create_shader_module(shaders::mesh_vert);
    state.mesh_fragment = device.create_shader_module(shaders::mesh_frag);
    state.line_vertex = device.create_shader_module(shaders::line_vert);
    state.line_fragment = device.create_shader_module(shaders::line_frag);
    const std::array formats{description.color_format};
    constexpr gfx::DepthStencilState TESTED_AND_WRITTEN{.depth_test = true, .depth_write = true, .depth_compare = gfx::CompareOperation::GREATER_OR_EQUAL};
    constexpr gfx::DepthStencilState TESTED{.depth_test = true, .depth_write = false, .depth_compare = gfx::CompareOperation::GREATER_OR_EQUAL};
    state.mesh_pipeline = device.create_graphics_pipeline({.vertex_shader = state.mesh_vertex->handle(),
                                                           .fragment_shader = state.mesh_fragment->handle(),
                                                           .color_formats = formats,
                                                           .depth_format = DEPTH_FORMAT,
                                                           .samples = description.samples,
                                                           .state = {.rasterization = {.cull_mode = gfx::CullMode::BACK},
                                                                     .depth_stencil = TESTED_AND_WRITTEN,
                                                                     .debug_name = "sandbox meshes"}});
    state.two_sided_pipeline = device.create_graphics_pipeline({.vertex_shader = state.mesh_vertex->handle(),
                                                                .fragment_shader = state.mesh_fragment->handle(),
                                                                .color_formats = formats,
                                                                .depth_format = DEPTH_FORMAT,
                                                                .samples = description.samples,
                                                                .state = {.rasterization = {.cull_mode = gfx::CullMode::NONE},
                                                                          .depth_stencil = TESTED_AND_WRITTEN,
                                                                          .debug_name = "sandbox two-sided meshes"}});
    state.line_pipeline = device.create_graphics_pipeline({.vertex_shader = state.line_vertex->handle(),
                                                           .fragment_shader = state.line_fragment->handle(),
                                                           .color_formats = formats,
                                                           .depth_format = DEPTH_FORMAT,
                                                           .samples = description.samples,
                                                           .state = {.topology = gfx::PrimitiveTopology::LINE_LIST,
                                                                     .rasterization = {.cull_mode = gfx::CullMode::NONE},
                                                                     .depth_stencil = TESTED,
                                                                     .debug_name = "sandbox lines"}});
}

SceneRenderer::~SceneRenderer() = default;

void
SceneRenderer::begin_frame(std::uint64_t frame)
{
    State& state = *m_state;
    state.uploader.begin_frame(frame);
    state.frame_data.begin_frame(frame);
    state.instances.clear();
    state.lines.clear();
}

void
SceneRenderer::add(const std::shared_ptr<const gfx::physics::Shape>& shape,
                   const gfx::Transform& pose,
                   gfx::physics::DebugColor color,
                   std::uint32_t flags)
{
    // The shape's mesh: made the first time it is seen, copied in prepare().
    State& state = *m_state;
    auto [found, inserted] = state.lookup.try_emplace(shape.get(), static_cast<std::uint32_t>(state.meshes.size()));
    if (inserted)
    {
        PendingMesh pending{.mesh = found->second, .data = gfx::physics::tessellate(*shape, state.description.tessellation)};
        state.meshes.push_back({.shape = shape,
                                .first_vertex = 0,
                                .first_index = 0,
                                .index_count = static_cast<std::uint32_t>(pending.data.indices.size()),
                                .two_sided = pending.data.two_sided,
                                .ready = false});
        state.pending.push_back(std::move(pending));
    }
    state.instances.push_back({.mesh = found->second, .pose = pose, .color = color.packed(), .flags = flags});
}

auto
SceneRenderer::lines() noexcept -> gfx::physics::DebugLines&
{
    return m_state->lines;
}

void
SceneRenderer::prepare(gfx::GraphicsRecorder& recorder)
{
    // Each mesh waiting, while this frame's staging and the shared buffers have room (the rest waits a frame).
    State& state = *m_state;
    std::uint64_t staged = 0;
    std::size_t copied = 0;
    for (; copied < state.pending.size(); ++copied)
    {
        PendingMesh& pending = state.pending[copied];
        StoredMesh& mesh = state.meshes[pending.mesh];
        const std::uint64_t vertex_bytes = pending.data.vertices.size() * VERTEX_WORDS * 4;
        const std::uint64_t index_bytes = pending.data.indices.size() * 4;
        if (staged + vertex_bytes + index_bytes + 64 > STAGING_BYTES)
        {
            break;
        }
        if ((state.stored_vertices + pending.data.vertices.size()) * VERTEX_WORDS * 4 > state.description.mesh_vertex_bytes
            || (state.stored_indices + pending.data.indices.size()) * 4 > state.description.mesh_index_bytes)
        {
            // Never drawn: the buffers are full.
            continue;
        }
        staged += vertex_bytes + index_bytes;

        // The vertices as words (position, normal), the indices as they are.
        std::vector<float> words;
        words.reserve(pending.data.vertices.size() * VERTEX_WORDS);
        for (const gfx::physics::DebugVertex& vertex : pending.data.vertices)
        {
            words.insert(words.end(), {vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x, vertex.normal.y, vertex.normal.z});
        }
        mesh.first_vertex = static_cast<std::uint32_t>(state.stored_vertices);
        mesh.first_index = static_cast<std::uint32_t>(state.stored_indices);
        state.uploader.upload_buffer(recorder, state.vertices->handle(), state.stored_vertices * VERTEX_WORDS * 4, words,
                                     gfx::ResourceState::VERTEX_SHADER_STORAGE_READ, gfx::ResourceState::VERTEX_SHADER_STORAGE_READ);
        state.uploader.upload_buffer(recorder, state.indices->handle(), state.stored_indices * 4, pending.data.indices,
                                     gfx::ResourceState::INDEX_BUFFER, gfx::ResourceState::INDEX_BUFFER);
        state.stored_vertices += pending.data.vertices.size();
        state.stored_indices += pending.data.indices.size();
        mesh.ready = true;
    }
    state.pending.erase(state.pending.begin(), state.pending.begin() + static_cast<std::ptrdiff_t>(copied));
}

void
SceneRenderer::render(gfx::GraphicsRecorder& recorder,
                      gfx::ImageViewHandle target,
                      gfx::Extent extent,
                      const SceneView& view)
{
    State& state = *m_state;
    SceneRendererStatistics& statistics = state.statistics;
    statistics.meshes = static_cast<std::uint32_t>(state.meshes.size());
    statistics.mesh_vertices = state.stored_vertices;
    statistics.mesh_indices = state.stored_indices;
    statistics.instances = 0;
    statistics.draw_calls = 0;
    statistics.lines = 0;
    statistics.skipped = 0;
    if (extent.width == 0 || extent.height == 0)
    {
        return;
    }
    state.prepare_targets(extent);

    // The instances of ready meshes, grouped by mesh (one-sided first), into the frame's buffer.
    state.order.clear();
    for (std::uint32_t index = 0; index < state.instances.size(); ++index)
    {
        if (state.meshes[state.instances[index].mesh].ready)
        {
            state.order.push_back(index);
        }
        else
        {
            ++statistics.skipped;
        }
    }
    std::ranges::sort(state.order, {},
                      [&state](std::uint32_t index)
                      {
                          const std::uint32_t mesh = state.instances[index].mesh;
                          return std::pair{state.meshes[mesh].two_sided, mesh};
                      });
    const std::uint64_t instance_bytes = state.order.size() * INSTANCE_WORDS * 4;
    const std::uint64_t line_bytes = state.lines.vertices().size() * LINE_VERTEX_WORDS * 4;
    if (state.frame_data.used_bytes() + instance_bytes + line_bytes + 64 > state.frame_data.bytes_per_frame())
    {
        statistics.skipped += static_cast<std::uint32_t>(state.order.size() + state.lines.line_count());
        state.order.clear();
        state.lines.clear();
    }
    gfx::TransientAllocation instances{};
    if (!state.order.empty())
    {
        instances = state.frame_data.allocate(instance_bytes, 16);
        for (std::size_t slot = 0; slot < state.order.size(); ++slot)
        {
            const Instance& instance = state.instances[state.order[slot]];
            const std::size_t word = slot * INSTANCE_WORDS;
            store(instances.memory, word, instance.pose.position.x);
            store(instances.memory, word + 1, instance.pose.position.y);
            store(instances.memory, word + 2, instance.pose.position.z);
            store(instances.memory, word + 3, instance.pose.orientation.x);
            store(instances.memory, word + 4, instance.pose.orientation.y);
            store(instances.memory, word + 5, instance.pose.orientation.z);
            store(instances.memory, word + 6, instance.pose.orientation.w);
            store(instances.memory, word + 7, instance.color);
            store(instances.memory, word + 8, instance.flags);
        }
    }
    gfx::TransientAllocation lines{};
    if (!state.lines.vertices().empty())
    {
        lines = state.frame_data.allocate(line_bytes, 16);
        std::size_t word = 0;
        for (const gfx::physics::DebugLineVertex& vertex : state.lines.vertices())
        {
            store(lines.memory, word, vertex.position.x);
            store(lines.memory, word + 1, vertex.position.y);
            store(lines.memory, word + 2, vertex.position.z);
            store(lines.memory, word + 3, vertex.color.packed());
            word += LINE_VERTEX_WORDS;
        }
    }

    // The pass: the multisampled color (resolved into the target) or the target itself, the depth cleared to 0.
    const bool multisampled = state.color_image != nullptr;
    std::array<gfx::ImageBarrier, 2> barriers{
        gfx::image_barrier(state.depth_image->handle(), gfx::ResourceState::UNDEFINED, gfx::ResourceState::DEPTH_STENCIL_ATTACHMENT_WRITE,
                           {.aspects = gfx::ImageAspect::DEPTH}),
        gfx::ImageBarrier{}};
    if (multisampled)
    {
        barriers[1] = gfx::image_barrier(state.color_image->handle(), gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE);
    }
    recorder.barrier({.image_barriers = std::span{barriers}.first(multisampled ? 2 : 1)});
    const std::array color{gfx::ColorAttachment{.view = multisampled ? state.color_view->handle() : target,
                                                .load = gfx::LoadOperation::CLEAR,
                                                .store = multisampled ? gfx::StoreOperation::DONT_CARE : gfx::StoreOperation::STORE,
                                                .clear_color = view.background,
                                                .resolve_view = multisampled ? target : gfx::ImageViewHandle{},
                                                .resolve_mode = multisampled ? gfx::ResolveMode::AVERAGE : gfx::ResolveMode::NONE}};
    gfx::RenderingScope scope = recorder.begin_rendering({
        .area = {.width = extent.width, .height = extent.height},
        .color_attachments = color,
        .depth_stencil_attachment = gfx::DepthStencilAttachment{.view = state.depth_view->handle(),
                                                                .depth_store = gfx::StoreOperation::DONT_CARE,
                                                                .clear_depth = 0.0F},
        .debug_name = "sandbox scene",
    });
    scope.set_viewport({.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)});
    scope.set_scissor({.width = extent.width, .height = extent.height});

    // One instanced draw per mesh.
    const std::array<float, 16> view_projection = matrix_floats(view.view_projection);
    const gfx::Vector3 light = gfx::normalized(view.light_direction);
    if (!state.order.empty())
    {
        const MeshPush push{.view_projection = view_projection,
                            .light = {light.x, light.y, light.z, view.light_strength},
                            .vertex_buffer = state.vertices->bindless_index().value(),
                            .instance_buffer = instances.buffer_index.value(),
                            .instance_word = static_cast<std::uint32_t>(instances.offset / 4),
                            .flags = state.srgb ? 1U : 0U};
        scope.bind_index_buffer(state.indices->handle(), 0, gfx::IndexType::UNSIGNED_INTEGER_32);
        std::optional<bool> bound_two_sided{};
        for (std::size_t first = 0; first < state.order.size();)
        {
            const std::uint32_t mesh_index = state.instances[state.order[first]].mesh;
            std::size_t end = first + 1;
            while (end < state.order.size() && state.instances[state.order[end]].mesh == mesh_index)
            {
                ++end;
            }
            const StoredMesh& mesh = state.meshes[mesh_index];
            if (bound_two_sided != mesh.two_sided)
            {
                scope.bind_pipeline(mesh.two_sided ? state.two_sided_pipeline->handle() : state.mesh_pipeline->handle());
                scope.push_constants(push);
                bound_two_sided = mesh.two_sided;
            }
            scope.draw_indexed({.index_count = mesh.index_count,
                                .instance_count = static_cast<std::uint32_t>(end - first),
                                .first_index = mesh.first_index,
                                .vertex_offset = static_cast<std::int32_t>(mesh.first_vertex),
                                .first_instance = static_cast<std::uint32_t>(first)});
            ++statistics.draw_calls;
            first = end;
        }
        statistics.instances = static_cast<std::uint32_t>(state.order.size());
    }

    // The lines over the solids.
    if (!state.lines.vertices().empty())
    {
        scope.bind_pipeline(state.line_pipeline->handle());
        scope.push_constants(LinePush{.view_projection = view_projection,
                                      .line_buffer = lines.buffer_index.value(),
                                      .line_word = static_cast<std::uint32_t>(lines.offset / 4),
                                      .flags = state.srgb ? 1U : 0U});
        scope.draw({.vertex_count = static_cast<std::uint32_t>(state.lines.vertices().size())});
        ++statistics.draw_calls;
        statistics.lines = static_cast<std::uint32_t>(state.lines.line_count());
    }
}

void
SceneRenderer::clear_meshes()
{
    State& state = *m_state;
    state.lookup.clear();
    state.meshes.clear();
    state.pending.clear();
    state.stored_vertices = 0;
    state.stored_indices = 0;
}

auto
SceneRenderer::statistics() const noexcept -> const SceneRendererStatistics&
{
    return m_state->statistics;
}

auto
SceneRenderer::samples() const noexcept -> gfx::SampleCount
{
    return m_state->description.samples;
}

} // namespace physics_sandbox
