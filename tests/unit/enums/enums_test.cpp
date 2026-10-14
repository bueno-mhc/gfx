#include <gtest/gtest.h>

import std;
import gfx;

namespace {

using namespace gfx;

TEST(EnumsTest, BitmaskOperatorsCombineBits) 
{
    constexpr auto usage = BufferUsage::VERTEX | BufferUsage::TRANSFER_DESTINATION;
    static_assert(has_all(usage, BufferUsage::VERTEX));
    static_assert(has_any(usage, BufferUsage::VERTEX | BufferUsage::INDEX));
    static_assert(!has_any(usage, BufferUsage::INDEX));
    static_assert((usage & BufferUsage::VERTEX) == BufferUsage::VERTEX);
    static_assert((usage ^ BufferUsage::VERTEX) == BufferUsage::TRANSFER_DESTINATION);
    static_assert(is_empty(usage & BufferUsage::INDEX));
    EXPECT_TRUE(has_all(usage, BufferUsage::NONE));
}

TEST(EnumsTest, ComplementKeepsOnlyNamedBits) 
{
    static_assert(~ImageAspect::COLOR == (ImageAspect::DEPTH | ImageAspect::STENCIL));
    static_assert(~ColorComponent::NONE == ColorComponent::ALL);
    EXPECT_TRUE(is_empty(~ColorComponent::ALL));
}

TEST(EnumsTest, CompoundAssignmentsUpdateTheLeftOperand) 
{
    auto stages = ShaderStage::VERTEX;
    stages |= ShaderStage::FRAGMENT;
    EXPECT_EQ(stages, ShaderStage::VERTEX | ShaderStage::FRAGMENT);
    stages &= ShaderStage::FRAGMENT;
    EXPECT_EQ(stages, ShaderStage::FRAGMENT);
    stages ^= ShaderStage::FRAGMENT;
    EXPECT_TRUE(is_empty(stages));
}

TEST(EnumsTest, CompositeEnumeratorsAreUnionsOfTheirBits) 
{
    static_assert(has_all(ShaderStage::ALL_GRAPHICS, ShaderStage::VERTEX | ShaderStage::FRAGMENT));
    static_assert(!has_any(ShaderStage::ALL_GRAPHICS, ShaderStage::COMPUTE));
    static_assert(has_all(ShaderStage::ALL, ShaderStage::ALL_GRAPHICS | ShaderStage::COMPUTE | ShaderStage::CALLABLE));
    EXPECT_EQ(ColorComponent::ALL, ColorComponent::RED | ColorComponent::GREEN | ColorComponent::BLUE | ColorComponent::ALPHA);
}

TEST(EnumsTest, SequentialEnumeratorsAreSpelledByName) 
{
    static_assert(to_string(Format::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM");
    static_assert(to_string(BackendKind::NULL_BACKEND) == "NULL_BACKEND");
    static_assert(to_string(ResourceState::PRESENT) == "PRESENT");
    EXPECT_EQ(to_string(static_cast<QueueType>(200)), "<invalid>");
}

TEST(EnumsTest, BitmasksAreSpelledAsUnions) 
{
    static_assert(to_string(ColorComponent::ALL) == "RED | GREEN | BLUE | ALPHA");
    EXPECT_EQ(to_string(ShaderStage::VERTEX | ShaderStage::FRAGMENT), "VERTEX | FRAGMENT");
    EXPECT_EQ(to_string(ShaderStage::NONE), "NONE");
    EXPECT_EQ(to_string(static_cast<ImageAspect>(0x81)), "COLOR | 0x80");
}

TEST(EnumsTest, StdFormatUsesTheSpellings) 
{
    EXPECT_EQ(std::format("{}", Format::D32_SFLOAT), "D32_SFLOAT");
    EXPECT_EQ(std::format("[{:>8}]", QueueType::COMPUTE), "[ COMPUTE]");
    EXPECT_EQ(std::format("{}", MemoryAccess::TRANSFER_READ | MemoryAccess::HOST_WRITE), "TRANSFER_READ | HOST_WRITE");
}

TEST(EnumsTest, ConceptsClassifyEnumerations) 
{
    static_assert(BitmaskEnumeration<ShaderStage> && !SequentialEnumeration<ShaderStage>);
    static_assert(SequentialEnumeration<Format> && !BitmaskEnumeration<Format>);
    static_assert(!DescribedEnumeration<std::byte>);   // an enumeration gfx does not describe
    static_assert(!DescribedEnumeration<int>);
    SUCCEED();
}

TEST(EnumsTest, EnumeratorCountsMatchTheTables) 
{
    static_assert(ENUMERATOR_COUNT<BackendKind> == 4);
    static_assert(ENUMERATOR_COUNT<Format> == FORMAT_PROPERTIES.size());
    static_assert(ENUMERATOR_COUNT<ResourceState> == RESOURCE_STATE_SYNCHRONIZATION.size());
    SUCCEED();
}

TEST(EnumsTest, FormatPropertiesDescribeTexelBlocks) 
{
    static_assert(format_properties(Format::R8G8B8A8_UNORM).bytes_per_block == 4);
    constexpr const FormatProperties& bc7 = format_properties(Format::BC7_SRGB_BLOCK);
    static_assert(bc7.block_width == 4 && bc7.block_height == 4 && bc7.bytes_per_block == 16);
    static_assert(bc7.numeric_type == FormatNumericType::SRGB);
    constexpr const FormatProperties& depth_stencil = format_properties(Format::D24_UNORM_S8_UINT);
    static_assert(depth_stencil.aspects == (ImageAspect::DEPTH | ImageAspect::STENCIL));
    EXPECT_EQ(format_properties(Format::R32G32B32_SFLOAT).component_count, 3);
}

TEST(EnumsTest, EveryDefinedFormatHasConsistentProperties) 
{
    for (const FormatProperties& properties : FORMAT_PROPERTIES) 
{
        SCOPED_TRACE(to_string(properties.format));
        if (properties.format == Format::UNDEFINED) 
{
            EXPECT_TRUE(is_empty(properties.aspects));
            continue;
        }
        EXPECT_GE(properties.bytes_per_block, 1);
        EXPECT_GE(properties.block_width, 1);
        EXPECT_GE(properties.block_height, 1);
        EXPECT_GE(properties.component_count, 1);
        EXPECT_FALSE(is_empty(properties.aspects));
        EXPECT_NE(properties.numeric_type, FormatNumericType::NONE);
        const bool color = has_any(properties.aspects, ImageAspect::COLOR);
        const bool depth_or_stencil = has_any(properties.aspects, ImageAspect::DEPTH | ImageAspect::STENCIL);
        EXPECT_NE(color, depth_or_stencil) << "a format is either color or depth/stencil";
    }
}

TEST(EnumsTest, ResourceStatesMapToSynchronizationScopes) 
{
    constexpr const ResourceStateSynchronization& write = resource_state_synchronization(ResourceState::COLOR_ATTACHMENT_WRITE);
    static_assert(write.writes && write.layout == ImageLayout::COLOR_ATTACHMENT);
    static_assert(write.stages == PipelineStage::COLOR_ATTACHMENT_OUTPUT);

    constexpr const ResourceStateSynchronization& sampled = resource_state_synchronization(ResourceState::FRAGMENT_SHADER_SAMPLED_READ);
    static_assert(!sampled.writes && sampled.layout == ImageLayout::SHADER_READ_ONLY);

    static_assert(resource_state_synchronization(ResourceState::VERTEX_BUFFER).categories == ResourceCategory::BUFFER);
    EXPECT_EQ(resource_state_synchronization(ResourceState::PRESENT).layout, ImageLayout::PRESENT_SOURCE);
}

TEST(EnumsTest, ImageStatesRequireAnImageLayout) 
{
    for (const ResourceStateSynchronization& entry : RESOURCE_STATE_SYNCHRONIZATION) 
{
        SCOPED_TRACE(to_string(entry.state));
        if (entry.state != ResourceState::UNDEFINED && has_any(entry.categories, ResourceCategory::IMAGE)) 
{
            EXPECT_NE(entry.layout, ImageLayout::UNDEFINED);
        }
    }
}

TEST(EnumsTest, HandleConstantsSplitThirtyTwoBits) 
{
    static_assert(HANDLE_INDEX_BITS + HANDLE_GENERATION_BITS == 32);
    static_assert(MAXIMUM_HANDLE_INDEX == 0xFF'FFFFU);
    static_assert(MAXIMUM_HANDLE_GENERATION == 0xFFU);
    static_assert(WAIT_INDEFINITELY == std::chrono::nanoseconds::max());
    SUCCEED();
}

} // namespace
