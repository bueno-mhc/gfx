// Internal partition declaring a class with a member of the other module's class template that no
// initializer names (as the Vulkan backend's partitions once declared VulkanFence with
// `FenceHandle m_handle;`). Writing `FenceHandle m_handle{};` here avoids the defect.
module gfx.defects.member_initializer:declaration;

import gfx.defects.member_initializer_base;

namespace defects
{

class Thing
{
public:
    explicit Thing(int value);
    [[nodiscard]] auto value() const noexcept -> int;

private:
    int m_value;
    FenceHandle m_handle;
};

} // namespace defects
