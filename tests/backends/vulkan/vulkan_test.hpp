#pragma once
// Fixtures of the Vulkan backend tests. They run on a real GPU (skipping when there is none), with the
// Vulkan validation layers enabled when they are installed: any validation error fails the test.
//
// Environment:
//   GFX_TEST_PHYSICAL_DEVICE=<index>  run on that physical device instead of the best one
//   GFX_TEST_VALIDATION=0             run without the validation layers

#include <cstdlib>
#include <gtest/gtest.h>

import std;
import gfx;

namespace gfx::tests
{

/// @brief Validation messages received by the debug callback (which may run on any thread).
class ValidationMessages
{
public:
    /// @brief Keeps validation messages and errors; prints other messages (e.g. the loader's warnings).
    void add(const DebugMessage& message)
    {
        const std::string text = std::format("[{} {}] {}", message.severity, message.type, message.text);
        if (message.type != DebugMessageType::VALIDATION && message.severity != DebugMessageSeverity::CRITICAL)
        {
            std::cout << "note: " << text << std::endl;
            return;
        }
        const std::scoped_lock lock{m_mutex};
        m_messages.push_back(text);
    }

    [[nodiscard]] auto take() -> std::vector<std::string>
    {
        const std::scoped_lock lock{m_mutex};
        return std::exchange(m_messages, {});
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_messages;
};

/// @brief Points the Vulkan loader at the validation layers found when the tests were configured,
///        unless the environment already names a layer directory.
inline void
use_configured_validation_layers()
{
    constexpr std::string_view DIRECTORY = GFX_TEST_VULKAN_LAYER_DIRECTORY;
    if (DIRECTORY.empty() || std::getenv("VK_ADD_LAYER_PATH") != nullptr)
    {
        return;
    }
#if defined(_WIN32)
    _putenv_s("VK_ADD_LAYER_PATH", GFX_TEST_VULKAN_LAYER_DIRECTORY);
#else
    setenv("VK_ADD_LAYER_PATH", GFX_TEST_VULKAN_LAYER_DIRECTORY, 0);
#endif
}

/// @brief Whether the tests should enable the validation layers.
inline auto
validation_requested() -> bool
{
    const char* value = std::getenv("GFX_TEST_VALIDATION");
    return value == nullptr || std::string_view{value} != "0";
}

/// @brief A Vulkan instance with validation, and the check that no validation error was reported.
class VulkanInstanceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        const BackendAvailability availability = backend_availability(BackendKind::VULKAN);
        if (!availability.available)
        {
            GTEST_SKIP() << availability.reason;
        }
        use_configured_validation_layers();

        // Validation errors and warnings reach the test; the layers may be missing.
        InstanceDescription description{
            .application_name = "gfx Vulkan tests",
            .validation_level = ValidationLevel::COMMANDS,
            .enable_validation = validation_requested(),
            .debug_message_severities = DebugMessageSeverity::WARNING | DebugMessageSeverity::CRITICAL,
            .debug_callback = [this](const DebugMessage& message) { m_validation_messages.add(message); },
        };
        try
        {
            m_instance = create_instance(BackendKind::VULKAN, description);
        }
        catch (const BackendCapabilityError& error)
        {
            if (!description.enable_validation)
            {
                throw;
            }
            std::cout << "note: running without the Vulkan validation layers: " << error.reason() << '\n';
            description.enable_validation = false;
            m_instance = create_instance(BackendKind::VULKAN, description);
        }
        m_validation_enabled = description.enable_validation;
    }

    void TearDown() override
    {
        // Every object is gone before the instance; then no validation message may remain.
        release();
        m_instance.reset();
        for (const std::string& message : m_validation_messages.take())
        {
            ADD_FAILURE() << "validation message: " << message;
        }
    }

    /// @brief Releases the objects of derived fixtures before the instance is destroyed.
    virtual void release() {}

    /// @brief The physical device the tests run on (GFX_TEST_PHYSICAL_DEVICE, or the best one).
    [[nodiscard]] auto physical_device() -> PhysicalDevice&
    {
        if (const char* index = std::getenv("GFX_TEST_PHYSICAL_DEVICE"))
        {
            return m_instance->physical_device(static_cast<std::size_t>(std::stoul(index)));
        }
        return m_instance->select_physical_device();
    }

    /// @brief Validation messages received so far (consumed: TearDown no longer reports them).
    [[nodiscard]] auto take_validation_messages() -> std::vector<std::string>
    {
        return m_validation_messages.take();
    }

    std::unique_ptr<Instance> m_instance;
    bool m_validation_enabled = false;

private:
    ValidationMessages m_validation_messages;
};

/// @brief A device on the selected physical device. Fixtures override device_description() to request
///        queues and features; tests that need a missing feature skip.
class VulkanDeviceTest : public VulkanInstanceTest
{
protected:
    void SetUp() override
    {
        VulkanInstanceTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        m_physical_device = &physical_device();
        for (const DeviceFeature feature : required_features())
        {
            if (!m_physical_device->supports(feature))
            {
                GTEST_SKIP() << "'" << m_physical_device->properties().name << "' does not support " << to_string(feature);
            }
        }
        DeviceDescription description = device_description();
        const std::vector<DeviceFeature> features = required_features();
        description.required_features = features;
        m_device = m_physical_device->create_device(description);
    }

    void release() override
    {
        release_device_objects();
        m_device.reset();
    }

    /// @brief Releases the objects of derived fixtures before the device is destroyed.
    virtual void release_device_objects() {}

    /// @brief How the device is created (queues, bindless capacities, ...).
    [[nodiscard]] virtual auto device_description() const -> DeviceDescription
    {
        static constexpr std::array<QueueRequest, 3> QUEUES{
            QueueRequest{.type = QueueType::GRAPHICS, .count = 1, .priority = 1.0F},
            QueueRequest{.type = QueueType::COMPUTE, .count = 1, .priority = 1.0F},
            QueueRequest{.type = QueueType::TRANSFER, .count = 1, .priority = 1.0F},
        };
        return {.queues = QUEUES, .debug_name = "test device"};
    }

    /// @brief Features the tests of a fixture need (the fixture skips when one is missing).
    [[nodiscard]] virtual auto required_features() const -> std::vector<DeviceFeature>
    {
        return {};
    }

    /// @brief Records @p recording into a fresh command buffer of queue type @p Type, submits it to the
    ///        first queue of that type and waits until the GPU has run it.
    template<QueueType Type = QueueType::GRAPHICS, class Recording>
    void run(Recording&& recording)
    {
        std::unique_ptr<CommandPool> pool = m_device->create_command_pool({.queue_type = Type, .debug_name = "test pool"});
        std::unique_ptr<CommandBuffer> command_buffer = pool->allocate(CommandBufferLevel::PRIMARY, "test commands");
        {
            Recorder<Type> recorder{*command_buffer};
            recording(recorder);
        }
        submit_and_wait(m_device->queue(Type), command_buffer->handle());
    }

    /// @brief Whether the first queues of types @p first and @p second may run on one native queue (one
    ///        family with a single queue): a submission waiting for another's later signal would then
    ///        deadlock, since one queue runs its submissions in order (the backend refuses the certain case).
    [[nodiscard]] auto queues_share_hardware(QueueType first, QueueType second) -> bool
    {
        const std::uint32_t family = m_device->queue(first).family_index();
        return family == m_device->queue(second).family_index() && m_physical_device->queue_families()[family].queue_count < 2;
    }

    /// @brief Submits @p command_buffer to @p queue and waits until it completes.
    void submit_and_wait(Queue& queue, CommandBufferHandle command_buffer)
    {
        std::unique_ptr<Fence> fence = m_device->create_fence({.debug_name = "test fence"});
        const std::array command_buffers{command_buffer};
        const std::array batches{SubmitDescription{.command_buffers = command_buffers}};
        queue.submit(batches, fence->handle());
        ASSERT_EQ(fence->wait(std::chrono::seconds{10}), WaitStatus::SIGNALED);
    }

    /// @brief The first @p count elements of @p buffer (left in state @p state), copied through a
    ///        readback buffer on the graphics queue.
    template<class Element>
    [[nodiscard]] auto read_buffer(const Buffer& buffer, std::size_t count, ResourceState state = ResourceState::TRANSFER_DESTINATION)
        -> std::vector<Element>
    {
        const std::uint64_t size = count * sizeof(Element);
        std::unique_ptr<Buffer> readback =
            m_device->create_buffer({.size = size, .usage = BufferUsage::TRANSFER_DESTINATION, .location = MemoryLocation::HOST_READBACK});
        run([&](GraphicsRecorder& recorder)
        {
            const std::array before{buffer_barrier(buffer.handle(), state, ResourceState::TRANSFER_SOURCE)};
            recorder.barrier({.buffer_barriers = before});
            const std::array regions{BufferCopyRegion{.size = size}};
            recorder.copy_buffer(buffer.handle(), readback->handle(), regions);
            const std::array after{buffer_barrier(readback->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::HOST_READ)};
            recorder.barrier({.buffer_barriers = after});
        });
        readback->invalidate_mapped_range();
        const std::span<Element> elements = readback->mapped_elements<Element>();
        return {elements.begin(), elements.begin() + static_cast<std::ptrdiff_t>(count)};
    }

    /// @brief The texels of level 0 of a 4-byte-per-texel image (in state @p state), as 32-bit words.
    [[nodiscard]] auto read_image(const Image& image, ResourceState state) -> std::vector<std::uint32_t>
    {
        const Extent extent = image.extent();
        const std::uint64_t size = std::uint64_t{extent.width} * extent.height * 4;
        std::unique_ptr<Buffer> readback =
            m_device->create_buffer({.size = size, .usage = BufferUsage::TRANSFER_DESTINATION, .location = MemoryLocation::HOST_READBACK});
        run([&](GraphicsRecorder& recorder)
        {
            const std::array before{image_barrier(image.handle(), state, ResourceState::TRANSFER_SOURCE)};
            recorder.barrier({.image_barriers = before});
            const std::array regions{BufferImageCopyRegion{.image_extent = extent}};
            recorder.copy_image_to_buffer(image.handle(), ImageLayout::TRANSFER_SOURCE, readback->handle(), regions);
            const std::array after{buffer_barrier(readback->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::HOST_READ)};
            recorder.barrier({.buffer_barriers = after});
        });
        readback->invalidate_mapped_range();
        const std::span<std::uint32_t> words = readback->mapped_elements<std::uint32_t>();
        return {words.begin(), words.end()};
    }

    PhysicalDevice* m_physical_device = nullptr;
    std::unique_ptr<Device> m_device;
};

/// @brief An RGBA8 color packed as the 32-bit word a little-endian readback holds.
[[nodiscard]] constexpr auto
rgba(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) noexcept -> std::uint32_t
{
    return red | (green << 8U) | (blue << 16U) | (alpha << 24U);
}

} // namespace gfx::tests
