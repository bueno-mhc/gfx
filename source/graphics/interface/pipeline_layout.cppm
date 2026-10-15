/// @file pipeline_layout.cppm
/// @brief PipelineLayout: the descriptor set layouts and push constants a pipeline uses, and the
///        bindings' names, which locate a binding once so that descriptor writes need no strings.
export module gfx.interface:pipeline_layout;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :backend_object;
import :descriptor_set_layout;

export namespace gfx
{

/// @brief Push constants visible to some stages. Two ranges of a layout never share a stage.
struct PushConstantRange
{
    ShaderStage stages = ShaderStage::NONE; ///< Stages that may read them.
    std::uint32_t offset = 0;               ///< First byte.
    std::uint32_t size = 0;                 ///< Bytes.

    bool operator==(const PushConstantRange&) const = default;
};

/// @brief One binding of a pipeline layout: a (set, binding) slot, its type, size, visibility and name.
struct LayoutBinding
{
    std::uint32_t set = 0;                                           ///< Descriptor set index.
    std::uint32_t binding = 0;                                       ///< Binding index inside the set.
    DescriptorType type = DescriptorType::UNIFORM_BUFFER;            ///< Kind of resource.
    std::uint32_t count = 1;                                         ///< Number of descriptors.
    ShaderStage stages = ShaderStage::NONE;                          ///< Stages that may access it.
    DescriptorBindingOption options = DescriptorBindingOption::NONE; ///< Descriptor indexing options.
    std::string name{};                                              ///< Name shaders use for it (may be empty).

    bool operator==(const LayoutBinding&) const = default;
};

/// @brief Where a binding lives, found once by name with PipelineLayout::find_binding() and then used
///        for every descriptor write (DescriptorSet::write()).
struct BindingLocation
{
    std::uint32_t set = 0;                                ///< Descriptor set index.
    std::uint32_t binding = 0;                            ///< Binding index inside the set.
    DescriptorType type = DescriptorType::UNIFORM_BUFFER; ///< Kind of resource it accepts.
    std::uint32_t count = 1;                              ///< Number of descriptors (array elements).

    bool operator==(const BindingLocation&) const = default;
};

/// @brief Everything a pipeline layout provides: every binding of every set, with names, and the
///        push-constant ranges. Pipeline creation compares shader layouts with it.
struct PipelineLayoutDefinition
{
    std::vector<LayoutBinding> bindings{};                 ///< Bindings of every set.
    std::vector<PushConstantRange> push_constant_ranges{}; ///< Push-constant ranges.

    /// @brief The binding at (@p set, @p binding), or nullptr.
    [[nodiscard]] auto find(std::uint32_t set, std::uint32_t binding) const noexcept -> const LayoutBinding*
    {
        const auto found = std::ranges::find_if(bindings, [set, binding](const LayoutBinding& candidate)
        {
            return candidate.set == set && candidate.binding == binding;
        });
        return found == bindings.end() ? nullptr : &*found;
    }

    /// @brief The binding called @p name, or nullptr (an empty name never matches).
    [[nodiscard]] auto find(std::string_view name) const noexcept -> const LayoutBinding*
    {
        if (name.empty())
        {
            return nullptr;
        }
        const auto found = std::ranges::find(bindings, name, &LayoutBinding::name);
        return found == bindings.end() ? nullptr : &*found;
    }

    /// @brief The push-constant range visible to @p stage, or nullptr.
    [[nodiscard]] auto push_constant_range(ShaderStage stage) const noexcept -> const PushConstantRange*
    {
        const auto found = std::ranges::find_if(push_constant_ranges, [stage](const PushConstantRange& range)
        {
            return has_all(range.stages, stage);
        });
        return found == push_constant_ranges.end() ? nullptr : &*found;
    }

    /// @brief Number of descriptor sets: highest set index used + 1 (0 without bindings).
    [[nodiscard]] auto set_count() const noexcept -> std::uint32_t
    {
        std::uint32_t count = 0;
        for (const LayoutBinding& binding : bindings)
        {
            count = std::max(count, binding.set + 1);
        }
        return count;
    }

    /// @brief The bindings of set @p set, as a DescriptorSetLayoutDescription expects them.
    [[nodiscard]] auto set_bindings(std::uint32_t set) const -> std::vector<DescriptorSetLayoutBinding>
    {
        std::vector<DescriptorSetLayoutBinding> set_bindings;
        for (const LayoutBinding& binding : bindings)
        {
            if (binding.set == set)
            {
                set_bindings.push_back({
                    .binding = binding.binding,
                    .type = binding.type,
                    .count = binding.count,
                    .stages = binding.stages,
                    .options = binding.options,
                    .name = binding.name,
                });
            }
        }
        return set_bindings;
    }

    bool operator==(const PipelineLayoutDefinition&) const = default;
};

/// @brief Definition of a pipeline layout made of @p set_layouts (index = set) and
///        @p push_constant_ranges. Backends return it from PipelineLayout::definition().
[[nodiscard]] auto
make_pipeline_layout_definition(std::span<const DescriptorSetLayout* const> set_layouts,
                                std::span<const PushConstantRange> push_constant_ranges) -> PipelineLayoutDefinition
{
    PipelineLayoutDefinition definition;

    // Every binding of every set, tagged with its set index.
    for (std::uint32_t set = 0; set < set_layouts.size(); ++set)
    {
        for (const DescriptorSetLayoutBinding& binding : set_layouts[set]->bindings())
        {
            definition.bindings.push_back({
                .set = set,
                .binding = binding.binding,
                .type = binding.type,
                .count = binding.count,
                .stages = binding.stages,
                .options = binding.options,
                .name = binding.name,
            });
        }
    }
    definition.push_constant_ranges.assign(push_constant_ranges.begin(), push_constant_ranges.end());
    return definition;
}

/// @brief How to create a PipelineLayout.
struct PipelineLayoutDescription
{
    std::span<const DescriptorSetLayoutHandle> set_layouts{};    ///< Layout of set 0, 1, ...
    std::span<const PushConstantRange> push_constant_ranges{};   ///< Push-constant ranges.
    std::string_view debug_name{};                               ///< Name shown by debugging tools.
};

/// @brief The descriptor set layouts and push constants pipelines use. Device::create_pipeline_layout_from()
///        derives one (and its set layouts) from the layouts reflected from shaders.
class PipelineLayout : public BackendObject
{
public:
    /// @brief Handle naming this layout in pipelines and when binding descriptor sets.
    [[nodiscard]] virtual auto handle() const noexcept -> PipelineLayoutHandle = 0;

    /// @brief Every binding of every set and the push constants (what pipeline creation compares
    ///        shader layouts with).
    [[nodiscard]] virtual auto definition() const noexcept -> const PipelineLayoutDefinition& = 0;

    /// @brief Locates the binding called @p name. Look a name up once and keep the location: writes
    ///        through a BindingLocation compare no strings.
    /// @throws InvalidUsageError when no binding has that name (the message lists the names).
    [[nodiscard]] auto find_binding(std::string_view name) const -> BindingLocation
    {
        // Found: its location.
        const PipelineLayoutDefinition& layout = definition();
        if (const LayoutBinding* binding = layout.find(name))
        {
            return {.set = binding->set, .binding = binding->binding, .type = binding->type, .count = binding->count};
        }

        // Not found: list the names it could have been.
        std::string names;
        for (const LayoutBinding& binding : layout.bindings)
        {
            if (!binding.name.empty())
            {
                names += std::format("{}'{}' (set {}, binding {})", names.empty() ? "" : ", ", binding.name, binding.set,
                                     binding.binding);
            }
        }
        throw InvalidUsageError({
            .backend = backend(),
            .operation = "PipelineLayout::find_binding",
            .problem = std::format("pipeline layout '{}' has no binding named '{}'", debug_name(), name),
            .suggestion = names.empty() ? std::string{"its bindings have no names: create it from shaders "
                                                      "(Device::create_pipeline_layout_from) or name them"}
                                        : std::format("its named bindings are {}", names),
        });
    }
};

/// @brief A pipeline layout together with the descriptor set layouts it was created from (one per set
///        index). Owns them all. Returned by Device::create_pipeline_layout_from().
class ReflectedPipelineLayout
{
public:
    /// @brief Takes ownership of the set layouts (index = set) and of the pipeline layout.
    ReflectedPipelineLayout(std::vector<std::unique_ptr<DescriptorSetLayout>> set_layouts,
                            std::unique_ptr<PipelineLayout> pipeline_layout)
        : m_set_layouts{std::move(set_layouts)}
        , m_pipeline_layout{std::move(pipeline_layout)}
    {}

    /// @brief The pipeline layout.
    [[nodiscard]] auto pipeline_layout() const noexcept -> PipelineLayout&
    {
        return *m_pipeline_layout;
    }

    /// @brief Handle naming the pipeline layout.
    [[nodiscard]] auto handle() const noexcept -> PipelineLayoutHandle
    {
        return m_pipeline_layout->handle();
    }

    /// @brief Every binding of every set and the push constants.
    [[nodiscard]] auto definition() const noexcept -> const PipelineLayoutDefinition&
    {
        return m_pipeline_layout->definition();
    }

    /// @brief Locates the binding called @p name (see PipelineLayout::find_binding()).
    [[nodiscard]] auto find_binding(std::string_view name) const -> BindingLocation
    {
        return m_pipeline_layout->find_binding(name);
    }

    /// @brief Number of descriptor sets.
    [[nodiscard]] auto set_count() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_set_layouts.size());
    }

    /// @brief Layout of descriptor set @p set (to allocate descriptor sets).
    /// @throws InvalidUsageError when the layout has no such set.
    [[nodiscard]] auto set_layout(std::uint32_t set) const -> DescriptorSetLayout&
    {
        // Reject set indices beyond the layout, with the valid range in the message.
        if (set >= m_set_layouts.size())
        {
            throw InvalidUsageError({
                .backend = m_pipeline_layout->backend(),
                .operation = "ReflectedPipelineLayout::set_layout",
                .problem = std::format("set {} does not exist; the pipeline layout has {} set(s)", set, m_set_layouts.size()),
                .suggestion = "",
            });
        }
        return *m_set_layouts[set];
    }

private:
    std::vector<std::unique_ptr<DescriptorSetLayout>> m_set_layouts;
    std::unique_ptr<PipelineLayout> m_pipeline_layout;
};

} // namespace gfx
