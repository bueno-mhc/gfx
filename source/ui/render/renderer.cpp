/// @file renderer.cpp
/// @brief UiRenderer: the pipeline, the geometry buffer, resident atlas pages and the recording of draw data.
module gfx.ui.renderer;

import std;
import gfx;
import gfx.ui.errors;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.text;
import gfx.ui.draw;
import gfx.ui.shaders;

namespace gfx::ui
{

namespace
{

/// A page of an atlas: what a draw command samples.
struct PageKey
{
    FontAtlasHandle atlas{};
    std::uint32_t page = 0;

    [[nodiscard]] constexpr auto operator==(const PageKey&) const noexcept -> bool = default;
};

struct PageKeyHash
{
    [[nodiscard]] auto operator()(const PageKey& key) const noexcept -> std::size_t
    {
        return std::hash<FontAtlasHandle>{}(key.atlas) ^ (std::size_t{key.page} * 0x9E3779B97F4A7C15ULL);
    }
};

/// A page uploaded as an image (its atlas kept alive while the image exists).
struct ResidentPage
{
    std::shared_ptr<const FontAtlas> atlas{};
    std::unique_ptr<Image> image{};
    std::unique_ptr<ImageView> view{};
};

/// A staging buffer the GPU reads until the frame that uploaded from it completes.
struct PendingUpload
{
    std::uint64_t frame = 0;
    std::unique_ptr<Buffer> staging{};
};

/// The push constants of ui.vert and ui.frag.
struct Push
{
    std::array<float, 2> scale{};
    std::array<float, 2> translation{};
    std::uint32_t vertex_buffer = 0;
    std::uint32_t vertex_word = 0;
    std::uint32_t texture_index = 0;
    std::uint32_t sampler_index = 0;
    std::uint32_t flags = 0;
};

/// The image format of an atlas's texels.
constexpr auto
format_of(AtlasTexelFormat format) noexcept -> Format
{
    switch (format)
    {
        case AtlasTexelFormat::R8_UNORM:            return Format::R8_UNORM;
        case AtlasTexelFormat::R8G8B8A8_UNORM:      return Format::R8G8B8A8_UNORM;
        case AtlasTexelFormat::R16_SFLOAT:          return Format::R16_SFLOAT;
        case AtlasTexelFormat::R16G16B16A16_SFLOAT: return Format::R16G16B16A16_SFLOAT;
    }
    return Format::R8G8B8A8_UNORM;
}

} // namespace

struct UiRenderer::State
{
    State(Device& graphics_device,
          const FontLibrary& font_library,
          const UiRendererDescription& renderer_description)
        : device{graphics_device}
        , fonts{font_library}
        , description{renderer_description}
        , name{renderer_description.debug_name}
        , geometry{graphics_device, renderer_description.geometry_bytes_per_frame, renderer_description.frames_in_flight,
                   BufferUsage::STORAGE | BufferUsage::INDEX, MemoryLocation::HOST_UPLOAD, "ui geometry"}
    {
    }

    Device& device;
    const FontLibrary& fonts;
    UiRendererDescription description;
    std::string name;
    TransientBufferAllocator geometry;
    std::unique_ptr<ShaderModule> vertex{};
    std::unique_ptr<ShaderModule> fragment{};
    std::unique_ptr<GraphicsPipeline> pipeline{};
    std::unique_ptr<Sampler> sampler{};
    std::unordered_map<PageKey, ResidentPage, PageKeyHash> pages{};
    std::vector<PendingUpload> pending{};
    std::uint64_t frame = 0;
    bool srgb = false;
    UiRendererStatistics statistics{};

    /// Uploads page @p key: an image, a staging buffer, a copy between barriers.
    void upload(Recorder<QueueType::GRAPHICS>& recorder,
                const PageKey& key)
    {
        std::shared_ptr<const FontAtlas> atlas = fonts.atlas(key.atlas);
        if (atlas == nullptr || key.page >= atlas->page_count())
        {
            return;
        }
        const FontAtlasPage page = atlas->page(key.page);
        const std::string page_name = std::format("{} {} page {}", name, atlas->name(), key.page);
        ResidentPage resident{.atlas = atlas};
        resident.image = device.create_image({.format = format_of(page.format),
                                              .extent = {.width = page.width, .height = page.height, .depth = 1},
                                              .usage = ImageUsage::SAMPLED | ImageUsage::TRANSFER_DESTINATION,
                                              .debug_name = page_name});
        resident.view = device.create_image_view({.image = resident.image->handle(), .debug_name = page_name});
        std::unique_ptr<Buffer> staging = device.create_buffer({.size = page.texels.size(),
                                                                .usage = BufferUsage::TRANSFER_SOURCE,
                                                                .location = MemoryLocation::HOST_UPLOAD,
                                                                .debug_name = page_name});
        std::ranges::copy(page.texels, staging->mapped_memory().begin());

        // The copy, between the barriers that make it ready for sampling.
        const std::array before{image_barrier(resident.image->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.image_barriers = before});
        const std::array regions{BufferImageCopyRegion{.image_extent = {.width = page.width, .height = page.height, .depth = 1}}};
        recorder.copy_buffer_to_image(staging->handle(), resident.image->handle(), ImageLayout::TRANSFER_DESTINATION, regions);
        const std::array after{image_barrier(resident.image->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::SHADER_SAMPLED_READ)};
        recorder.barrier({.image_barriers = after});

        statistics.uploaded_bytes += page.texels.size();
        pending.push_back({.frame = frame, .staging = std::move(staging)});
        pages.emplace(key, std::move(resident));
        statistics.resident_pages = static_cast<std::uint32_t>(pages.size());
    }
};

UiRenderer::UiRenderer(Device& device,
                       const FontLibrary& fonts,
                       const UiRendererDescription& description)
    : m_state{std::make_unique<State>(device, fonts, description)}
{
    State& state = *m_state;
    state.srgb = format_properties(description.color_format).numeric_type == FormatNumericType::SRGB;
    state.vertex = device.create_shader_module(shaders::ui_vert);
    state.fragment = device.create_shader_module(shaders::ui_frag);

    // Alpha blending (colors are not premultiplied); no culling (transforms may mirror).
    const std::array formats{description.color_format};
    const std::array blending{ColorBlendAttachment{.blend = true,
                                                   .source_color_factor = BlendFactor::SOURCE_ALPHA,
                                                   .destination_color_factor = BlendFactor::ONE_MINUS_SOURCE_ALPHA,
                                                   .source_alpha_factor = BlendFactor::ONE,
                                                   .destination_alpha_factor = BlendFactor::ONE_MINUS_SOURCE_ALPHA}};
    const std::string pipeline_name = std::format("{} pipeline", state.name);
    state.pipeline = device.create_graphics_pipeline({.vertex_shader = state.vertex->handle(),
                                                      .fragment_shader = state.fragment->handle(),
                                                      .color_formats = formats,
                                                      .samples = description.samples,
                                                      .state = {.rasterization = {.cull_mode = CullMode::NONE},
                                                                .color_blend = blending,
                                                                .debug_name = pipeline_name}});
    const std::string sampler_name = std::format("{} sampler", state.name);
    state.sampler = device.create_sampler({.mipmap_mode = SamplerMipmapMode::NEAREST,
                                           .address_mode_u = SamplerAddressMode::CLAMP_TO_EDGE,
                                           .address_mode_v = SamplerAddressMode::CLAMP_TO_EDGE,
                                           .address_mode_w = SamplerAddressMode::CLAMP_TO_EDGE,
                                           .debug_name = sampler_name});
}

UiRenderer::~UiRenderer() = default;

void
UiRenderer::begin_frame(std::uint64_t frame)
{
    // The frame's geometry region; staging buffers of frames the GPU has finished go.
    State& state = *m_state;
    state.frame = frame;
    state.geometry.begin_frame(frame);
    std::erase_if(state.pending, [&](const PendingUpload& upload) { return upload.frame + state.description.frames_in_flight <= frame; });
}

void
UiRenderer::prepare(Recorder<QueueType::GRAPHICS>& recorder,
                    const UiDrawData& data)
{
    State& state = *m_state;
    for (const DrawList* list : data.lists)
    {
        for (const DrawCommand& command : list->commands())
        {
            if (command.texture.kind != DrawTextureKind::FONT_PAGE)
            {
                continue;
            }
            const PageKey key{.atlas = command.texture.atlas, .page = command.texture.page};
            if (!state.pages.contains(key))
            {
                state.upload(recorder, key);
            }
        }
    }
}

void
UiRenderer::record(RenderingCommands& commands,
                   const UiDrawData& data)
{
    State& state = *m_state;
    UiRendererStatistics& statistics = state.statistics;
    statistics.draw_calls = 0;
    statistics.vertices = 0;
    statistics.indices = 0;
    statistics.skipped_commands = 0;

    // Every list's vertices and indices in the frame's region (nothing drawn when it is too small).
    std::size_t vertex_count = 0;
    std::size_t index_count = 0;
    std::size_t command_count = 0;
    for (const DrawList* list : data.lists)
    {
        vertex_count += list->vertices().size();
        index_count += list->indices().size();
        command_count += list->commands().size();
    }
    if (index_count == 0 || !(data.width > 0 && data.height > 0))
    {
        return;
    }
    const std::uint64_t vertex_bytes = vertex_count * sizeof(UiVertex);
    const std::uint64_t index_bytes = index_count * sizeof(std::uint32_t);
    if (state.geometry.used_bytes() + vertex_bytes + index_bytes + 64 > state.geometry.bytes_per_frame())
    {
        statistics.skipped_commands = static_cast<std::uint32_t>(command_count);
        return;
    }
    const TransientAllocation vertices = state.geometry.allocate(vertex_bytes, 16);
    const TransientAllocation indices = state.geometry.allocate(index_bytes, 16);
    std::size_t vertex_base = 0;
    std::size_t index_base = 0;
    for (const DrawList* list : data.lists)
    {
        std::ranges::copy(std::as_bytes(list->vertices()), vertices.memory.begin() + static_cast<std::ptrdiff_t>(vertex_base * sizeof(UiVertex)));
        std::ranges::copy(std::as_bytes(list->indices()), indices.memory.begin() + static_cast<std::ptrdiff_t>(index_base * sizeof(std::uint32_t)));
        vertex_base += list->vertices().size();
        index_base += list->indices().size();
    }
    statistics.vertices = static_cast<std::uint32_t>(vertex_count);
    statistics.indices = static_cast<std::uint32_t>(index_count);

    // The pipeline, the viewport, the indices.
    commands.bind_pipeline(state.pipeline->handle());
    commands.set_viewport({.width = data.width, .height = data.height});
    commands.bind_index_buffer(indices.buffer, indices.offset, IndexType::UNSIGNED_INTEGER_32);
    Push push{.scale = {2.0F / data.width, 2.0F / data.height},
              .translation = {-1.0F, -1.0F},
              .vertex_buffer = vertices.buffer_index.value(),
              .vertex_word = static_cast<std::uint32_t>(vertices.offset / 4),
              .sampler_index = state.sampler->bindless_index().value(),
              .flags = state.srgb ? 1U : 0U};

    // Per command: its scissor (its clip inside the viewport), its texture, its triangles.
    vertex_base = 0;
    index_base = 0;
    for (const DrawList* list : data.lists)
    {
        for (const DrawCommand& command : list->commands())
        {
            const float left = std::clamp(std::floor(command.clip.left), 0.0F, data.width);
            const float top = std::clamp(std::floor(command.clip.top), 0.0F, data.height);
            const float right = std::clamp(std::ceil(command.clip.right), 0.0F, data.width);
            const float bottom = std::clamp(std::ceil(command.clip.bottom), 0.0F, data.height);
            if (command.index_count == 0 || right <= left || bottom <= top)
            {
                continue;
            }
            push.texture_index = 0;
            if (command.texture.kind == DrawTextureKind::FONT_PAGE)
            {
                const auto page = state.pages.find({.atlas = command.texture.atlas, .page = command.texture.page});
                if (page == state.pages.end())
                {
                    ++statistics.skipped_commands;
                    continue;
                }
                push.texture_index = page->second.view->sampled_index().value();
            }
            else if (command.texture.kind == DrawTextureKind::IMAGE)
            {
                push.texture_index = command.texture.image;
            }
            commands.set_scissor({.x = static_cast<std::int32_t>(left),
                                  .y = static_cast<std::int32_t>(top),
                                  .width = static_cast<std::uint32_t>(right - left),
                                  .height = static_cast<std::uint32_t>(bottom - top)});
            commands.push_constants(push);
            commands.draw_indexed({.index_count = command.index_count,
                                   .first_index = static_cast<std::uint32_t>(index_base) + command.first_index,
                                   .vertex_offset = static_cast<std::int32_t>(vertex_base)});
            ++statistics.draw_calls;
        }
        vertex_base += list->vertices().size();
        index_base += list->indices().size();
    }
}

auto
UiRenderer::statistics() const noexcept -> const UiRendererStatistics&
{
    return m_state->statistics;
}

} // namespace gfx::ui
