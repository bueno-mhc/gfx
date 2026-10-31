// What the tests of the GPU backend's building blocks share: the physical devices to run on, a fixture making a device on
// one of them with the primitives in one variant, buffers filled from the CPU and read back, random words.
#pragma once

#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;

namespace gfx::tests
{

/// @brief Every physical device of the machine that runs gfx.
inline auto
gpu_physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!backend_availability(BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<Instance> instance = create_instance(BackendKind::VULKAN, {.application_name = "gfx GPU tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// @brief A word that marks what a pass must not write.
inline constexpr std::uint32_t UNTOUCHED = 0xDEADBEEFU;

/// @brief Random words: uniform below @p limit (0: every 32-bit value).
inline auto
random_words(std::size_t count,
             std::uint32_t seed,
             std::uint64_t limit = 0) -> std::vector<std::uint32_t>
{
    std::mt19937 generator{seed};
    std::vector<std::uint32_t> words(count);
    for (std::uint32_t& word : words)
    {
        word = limit == 0 ? static_cast<std::uint32_t>(generator()) : static_cast<std::uint32_t>(generator() % limit);
    }
    return words;
}

/// @brief A device on one GPU and the GPU backend's primitives in one variant (the test's parameter).
class GpuTest : public VulkanDeviceTest,
                public testing::WithParamInterface<std::tuple<std::size_t, physics::GpuVariantChoice>>
{
protected:
    void SetUp() override
    {
        VulkanDeviceTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        m_primitives = std::make_unique<physics::gpu_xpbd::GpuPrimitives>(*m_device,
                                                                          physics::gpu_xpbd::capabilities(*m_device, {.subgroups = std::get<1>(GetParam())}));
    }

    [[nodiscard]] auto physical_device() -> PhysicalDevice& override
    {
        return m_instance->physical_device(std::get<0>(GetParam()));
    }

    void release_device_objects() override
    {
        release_gpu_objects();
        m_primitives.reset();
        m_buffers.clear();
    }

    /// @brief Releases the objects of derived fixtures (before the primitives and the buffers).
    virtual void release_gpu_objects() {}

    /// @brief A device-local buffer of @p count words holding @p words (UNTOUCHED past them), kept until the test ends.
    auto words_buffer(std::size_t count,
                      std::span<const std::uint32_t> words = {}) -> Buffer&
    {
        std::vector<std::uint32_t> contents(std::max<std::size_t>(count, 4), UNTOUCHED);
        std::ranges::copy(words, contents.begin());
        return upload(std::as_bytes(std::span{contents}), BufferUsage::NONE);
    }

    /// @brief A device-local buffer of @p bytes (with STORAGE, transfer and @p usage) holding @p bytes, kept until the
    ///        test ends.
    auto upload(std::span<const std::byte> bytes,
                BufferUsage usage) -> Buffer&
    {
        std::unique_ptr<Buffer> staging = m_device->create_buffer(
            {.size = bytes.size(), .usage = BufferUsage::TRANSFER_SOURCE, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "staging"});
        std::ranges::copy(bytes, staging->mapped_memory().begin());
        staging->flush_mapped_range();
        m_buffers.push_back(m_device->create_buffer({.size = bytes.size(),
                                                     .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION | usage,
                                                     .debug_name = "test data"}));
        Buffer& buffer = *m_buffers.back();
        run<QueueType::COMPUTE>([&](ComputeRecorder& recorder) {
            const std::array regions{BufferCopyRegion{.size = bytes.size()}};
            recorder.copy_buffer(staging->handle(), buffer.handle(), regions);
        });
        return buffer;
    }

    /// @brief A device-local buffer holding @p elements (at least 16 bytes), kept until the test ends.
    template<class Element>
    auto elements_buffer(std::span<const Element> elements) -> Buffer&
    {
        std::vector<std::byte> bytes(std::max<std::size_t>(elements.size_bytes(), 16));
        std::ranges::copy(std::as_bytes(elements), bytes.begin());
        return upload(bytes, BufferUsage::NONE);
    }

    /// @brief Records @p recording after a barrier making earlier submissions' writes visible, runs it on the compute queue.
    template<class Recording>
    void record(Recording&& recording)
    {
        run<QueueType::COMPUTE>([&](ComputeRecorder& recorder) {
            const GlobalMemoryBarrier visible{.source_stages = PipelineStage::ALL_COMMANDS,
                                              .source_access = MemoryAccess::TRANSFER_WRITE | MemoryAccess::SHADER_STORAGE_WRITE,
                                              .destination_stages = PipelineStage::COMPUTE_SHADER,
                                              .destination_access = MemoryAccess::SHADER_STORAGE_READ | MemoryAccess::SHADER_STORAGE_WRITE};
            recorder.barrier({.memory_barriers = std::span{&visible, 1}});
            recording(recorder);
        });
    }

    /// @brief The first @p count words of @p buffer.
    auto words(const Buffer& buffer,
               std::size_t count) -> std::vector<std::uint32_t>
    {
        return read<std::uint32_t>(buffer, count);
    }

    /// @brief The first @p count elements of @p buffer (written by compute passes).
    template<class Element>
    auto read(const Buffer& buffer,
              std::size_t count) -> std::vector<Element>
    {
        std::vector<Element> elements = read_buffer<Element>(buffer, std::max<std::size_t>(count, 1), ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
        elements.resize(count);
        return elements;
    }

    std::unique_ptr<physics::gpu_xpbd::GpuPrimitives> m_primitives;
    std::vector<std::unique_ptr<Buffer>> m_buffers;
};

/// @brief "Device<n>_<variant>".
inline auto
gpu_test_name(const testing::TestParamInfo<std::tuple<std::size_t, physics::GpuVariantChoice>>& parameter) -> std::string
{
    return std::format("Device{}_{}", std::get<0>(parameter.param), std::get<1>(parameter.param));
}

} // namespace gfx::tests
