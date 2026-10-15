#include <gtest/gtest.h>

import std;
import gfx;

namespace {

using namespace gfx;

// Stands in for a backend device: only HandleIssuer descendants can encode and decode handles.
class TestIssuer final : public HandleIssuer 
{
public:
    template<class Tag>
    [[nodiscard]] static constexpr auto 
    create(std::uint32_t index, std::uint32_t generation) -> Handle<Tag> 
    {
        return Handle<Tag>::create(handle_key(), index, generation);
    }

    template<class Tag>
    [[nodiscard]] static constexpr auto 
    index_of(Handle<Tag> handle) -> std::uint32_t 
    {
        return handle.index(handle_key());
    }

    template<class Tag>
    [[nodiscard]] static constexpr auto 
    generation_of(Handle<Tag> handle) -> std::uint32_t 
    {
        return handle.generation(handle_key());
    }
};

template<GraphicsHandle Type>
constexpr auto accepted_by_graphics_commands(Type /*handle*/) -> bool {
    return true;
}

TEST(HandlesTest, DefaultHandleIsNull) 
{
    static_assert(!BufferHandle{}.is_valid());
    static_assert(!BufferHandle{});
    static_assert(BufferHandle{} == BufferHandle::null());
    EXPECT_FALSE(ImageHandle{}.is_valid());
}

TEST(HandlesTest, IndexAndGenerationRoundTrip) 
{
    constexpr auto handle = TestIssuer::create<BufferTag>(MAXIMUM_HANDLE_INDEX, MAXIMUM_HANDLE_GENERATION);
    static_assert(handle.is_valid());
    static_assert(TestIssuer::index_of(handle) == MAXIMUM_HANDLE_INDEX);
    static_assert(TestIssuer::generation_of(handle) == MAXIMUM_HANDLE_GENERATION);

    const auto runtime_handle = TestIssuer::create<ImageTag>(7, 1);
    EXPECT_EQ(TestIssuer::index_of(runtime_handle), 7U);
    EXPECT_EQ(TestIssuer::generation_of(runtime_handle), 1U);
}

TEST(HandlesTest, HandlesAreTriviallyCopyableFourByteValues) 
{
    static_assert(std::is_trivially_copyable_v<BufferHandle>);
    static_assert(sizeof(BufferHandle) == 4);
    static_assert(std::is_nothrow_default_constructible_v<BufferHandle>);
    static_assert(std::is_standard_layout_v<BufferHandle>);
    SUCCEED();
}

TEST(HandlesTest, HandlesOfTheSameKindCompareByValue) 
{
    constexpr auto first = TestIssuer::create<BufferTag>(1, 1);
    constexpr auto second = TestIssuer::create<BufferTag>(2, 1);
    static_assert(first == TestIssuer::create<BufferTag>(1, 1));
    static_assert(first != second);
    static_assert(first < second);
    static_assert(BufferHandle::null() < first);
    static_assert(TestIssuer::create<BufferTag>(1, 2) != first, "a reused slot is a different handle");
    SUCCEED();
}

TEST(HandlesTest, HandlesKeyUnorderedContainers) 
{
    std::unordered_set<BufferHandle> handles;
    handles.insert(TestIssuer::create<BufferTag>(1, 1));
    handles.insert(TestIssuer::create<BufferTag>(2, 1));
    handles.insert(TestIssuer::create<BufferTag>(1, 1));
    EXPECT_EQ(handles.size(), 2U);
    EXPECT_TRUE(handles.contains(TestIssuer::create<BufferTag>(2, 1)));
}

TEST(HandlesTest, FormattingShowsKindIndexAndGeneration) 
{
    EXPECT_EQ(std::format("{}", TestIssuer::create<BufferTag>(5, 3)), "Buffer(index 5, generation 3)");
    EXPECT_EQ(std::format("{}", GraphicsPipelineHandle::null()), "GraphicsPipeline(null)");
}

TEST(HandlesTest, OutOfRangeIndexIsRejected) 
{
    try {
        (void)TestIssuer::create<BufferTag>(MAXIMUM_HANDLE_INDEX + 1U, 1);
        FAIL() << "InvalidHandleError was not thrown";
    } catch (const InvalidHandleError& error) 
{
        EXPECT_EQ(error.handle_type(), "Buffer");
        EXPECT_NE(std::string_view{error.what()}.find("slot index 16777216 exceeds MAXIMUM_HANDLE_INDEX (16777215)"),
                  std::string_view::npos)
            << error.what();
    }
}

TEST(HandlesTest, GenerationZeroIsReservedForTheNullHandle) 
{
    EXPECT_THROW((void)TestIssuer::create<ImageTag>(0, 0), InvalidHandleError);
    EXPECT_THROW((void)TestIssuer::create<ImageTag>(0, MAXIMUM_HANDLE_GENERATION + 1U), InvalidHandleError);
}

TEST(HandlesTest, EveryHandleTypeSatisfiesHandleType) 
{
    static_assert(HandleType<BufferHandle> && HandleType<ImageHandle> && HandleType<ImageViewHandle>);
    static_assert(HandleType<SamplerHandle> && HandleType<DeviceMemoryHandle> && HandleType<ShaderModuleHandle>);
    static_assert(HandleType<DescriptorSetLayoutHandle> && HandleType<PipelineLayoutHandle>);
    static_assert(HandleType<GraphicsPipelineHandle> && HandleType<ComputePipelineHandle>);
    static_assert(HandleType<DescriptorPoolHandle> && HandleType<DescriptorSetHandle>);
    static_assert(HandleType<CommandPoolHandle> && HandleType<CommandBufferHandle> && HandleType<QueueHandle>);
    static_assert(HandleType<FenceHandle> && HandleType<SemaphoreHandle> && HandleType<TimelineSemaphoreHandle>);
    static_assert(HandleType<EventHandle> && HandleType<QueryPoolHandle> && HandleType<SwapchainHandle>);
    static_assert(!HandleType<std::uint32_t> && !HandleType<BufferTag>);
    SUCCEED();
}

TEST(HandlesTest, ConceptsClassifyHandlesByQueueUsage) 
{
    static_assert(GraphicsHandle<GraphicsPipelineHandle> && !ComputeHandle<GraphicsPipelineHandle>);
    static_assert(ComputeHandle<ComputePipelineHandle> && !GraphicsHandle<ComputePipelineHandle>);
    static_assert(GraphicsHandle<BufferHandle> && ComputeHandle<BufferHandle> && TransferHandle<BufferHandle>);
    static_assert(!TransferHandle<ImageViewHandle>);
    static_assert(!GraphicsHandle<FenceHandle> && !ComputeHandle<FenceHandle>);
    static_assert(ResourceHandle<BufferHandle> && ResourceHandle<ImageHandle> && !ResourceHandle<ImageViewHandle>);
    static_assert(accepted_by_graphics_commands(GraphicsPipelineHandle{}));
    SUCCEED();
}

TEST(HandlesTest, DistinctKindsDoNotMix) 
{
    static_assert(!std::is_convertible_v<BufferHandle, ImageHandle>);
    static_assert(!std::is_constructible_v<ImageHandle, BufferHandle>);
    static_assert(!std::is_constructible_v<BufferHandle, std::uint32_t>);
    static_assert(!std::is_convertible_v<BufferHandle, std::uint32_t>);
    static_assert(!std::equality_comparable_with<BufferHandle, ImageHandle>);
    SUCCEED();
}

TEST(HandlesTest, OnlyIssuersCanObtainAKey) 
{
    static_assert(!std::is_default_constructible_v<HandleKey>);
    SUCCEED();
}

} // namespace
