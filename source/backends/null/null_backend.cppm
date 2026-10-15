/// @file null_backend.cppm
/// @brief Module gfx.backends.null: the backend without a GPU. Its instance exists but has no physical
///        device, so every attempt to go further reports that no device supports the backend.
export module gfx.backends.null;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;

namespace gfx::null
{

/// @brief Instance of the null backend: no physical device.
class NullInstance final : public Instance
{
public:
    explicit NullInstance(const InstanceDescription& description)
        : Instance{}
    {
        set_validation_level(description.validation_level);
    }

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::NULL_BACKEND;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
    }

    [[nodiscard]] auto physical_device_count() const noexcept -> std::size_t override
    {
        return 0;
    }

    [[nodiscard]] auto physical_device(std::size_t index) -> PhysicalDevice& override
    {
        throw InvalidUsageError({
            .backend = BackendKind::NULL_BACKEND,
            .operation = "Instance::physical_device",
            .problem = std::format("physical device {} does not exist; the null backend has no physical device", index),
            .suggestion = "use a backend with a GPU (gfx::available_backends())",
        });
    }

private:
    std::string m_debug_name{"null instance"};
};

} // namespace gfx::null

export namespace gfx::null
{

/// @brief Why the null backend cannot run: never (an empty string).
[[nodiscard]] auto unavailable_reason() -> std::string
{
    return {};
}

/// @brief Creates an instance of the null backend.
[[nodiscard]] auto create_instance(const InstanceDescription& description) -> std::unique_ptr<Instance>
{
    return std::make_unique<NullInstance>(description);
}

} // namespace gfx::null
