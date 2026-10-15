/// @file descriptor_pool.cppm
/// @brief DescriptorPool: storage descriptor sets are allocated from (one per thread or per frame).
export module gfx.interface:descriptor_pool;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :descriptor_set;

export namespace gfx
{

/// @brief Number of descriptors of one type a pool provides.
struct DescriptorPoolSize
{
    DescriptorType type = DescriptorType::UNIFORM_BUFFER; ///< Descriptor type.
    std::uint32_t count = 0;                              ///< Descriptors of that type.
};

/// @brief How to create a DescriptorPool.
struct DescriptorPoolDescription
{
    std::uint32_t maximum_sets = 0;               ///< Sets that can be allocated at once.
    std::span<const DescriptorPoolSize> sizes{};  ///< Descriptors per type.
    bool update_after_bind = false;               ///< Sets may use DescriptorBindingOption::UPDATE_AFTER_BIND.
    bool free_individual_sets = false;            ///< Destroying a set returns its descriptors to the pool.
    std::string_view debug_name{};                ///< Name shown by debugging tools.
};

/// @brief Storage for descriptor sets. Not thread-safe: use one pool per thread (or per frame) and
///        reset it once the GPU no longer uses its sets.
class DescriptorPool : public BackendObject
{
public:
    /// @brief Handle naming this pool.
    [[nodiscard]] virtual auto handle() const noexcept -> DescriptorPoolHandle = 0;

    /// @brief Allocates a descriptor set.
    /// @param layout                    Layout of the set.
    /// @param variable_descriptor_count Size of the last binding when it has VARIABLE_DESCRIPTOR_COUNT.
    /// @throws OutOfMemoryError when the pool is exhausted.
    [[nodiscard]] virtual auto allocate(DescriptorSetLayoutHandle layout,
                                        std::uint32_t variable_descriptor_count = 0) -> std::unique_ptr<DescriptorSet> = 0;

    /// @brief Returns every set of the pool to it (their objects must be destroyed first).
    virtual void reset() = 0;
};

auto
DescriptorPool::allocate(DescriptorSetLayoutHandle /*layout*/,
                         std::uint32_t /*variable_descriptor_count*/) -> std::unique_ptr<DescriptorSet>
{
    throw_not_implemented("DescriptorPool::allocate");
}

void
DescriptorPool::reset()
{
    throw_not_implemented("DescriptorPool::reset");
}

} // namespace gfx
