// The GPU backend's data-parallel primitives (phase G1) against exact CPU results, on each GPU of the machine and in both
// variants (subgroup arithmetic where the GPU has it, and the portable one), with the Vulkan validation layers: exclusive
// scans (one, two and three levels), reductions of boxes, stable sorts by rank and by radix, compactions, dispatch
// arguments, counts written by a pass, the same bits in both variants, the refusals.
#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<gfx::Instance> instance =
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx GPU primitives tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// A word that marks what a primitive must not write.
constexpr std::uint32_t UNTOUCHED = 0xDEADBEEFU;

/// The exclusive prefix sums of @p values (wrapping modulo 2³²), and their sum.
auto
serial_scan(std::span<const std::uint32_t> values) -> std::pair<std::vector<std::uint32_t>, std::uint32_t>
{
    std::vector<std::uint32_t> sums(values.size());
    std::uint32_t running = 0;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        sums[index] = running;
        running += values[index];
    }
    return {sums, running};
}

/// Random words: uniform below @p limit (0: every 32-bit value).
auto
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

/// The primitives on one GPU in one variant.
class GpuPrimitivesTest : public gfx::tests::VulkanDeviceTest,
                          public testing::WithParamInterface<std::tuple<std::size_t, GpuVariantChoice>>
{
protected:
    void SetUp() override
    {
        VulkanDeviceTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        m_primitives = std::make_unique<gpu::GpuPrimitives>(*m_device, gpu::capabilities(*m_device, {.subgroups = std::get<1>(GetParam())}));
    }

    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(std::get<0>(GetParam()));
    }

    void release_device_objects() override
    {
        m_primitives.reset();
        m_buffers.clear();
    }

    /// A device-local buffer of @p count words holding @p words (UNTOUCHED past them), kept until the test ends.
    auto words_buffer(std::size_t count,
                      std::span<const std::uint32_t> words = {}) -> gfx::Buffer&
    {
        std::vector<std::uint32_t> contents(std::max<std::size_t>(count, 4), UNTOUCHED);
        std::ranges::copy(words, contents.begin());
        return upload(std::as_bytes(std::span{contents}), gfx::BufferUsage::NONE);
    }

    /// A device-local buffer of @p bytes (with STORAGE, transfer and @p usage) holding @p bytes, kept until the test ends.
    auto upload(std::span<const std::byte> bytes,
                gfx::BufferUsage usage) -> gfx::Buffer&
    {
        std::unique_ptr<gfx::Buffer> staging = m_device->create_buffer(
            {.size = bytes.size(), .usage = gfx::BufferUsage::TRANSFER_SOURCE, .location = gfx::MemoryLocation::HOST_UPLOAD, .debug_name = "staging"});
        std::ranges::copy(bytes, staging->mapped_memory().begin());
        staging->flush_mapped_range();
        m_buffers.push_back(m_device->create_buffer(
            {.size = bytes.size(),
             .usage = gfx::BufferUsage::STORAGE | gfx::BufferUsage::TRANSFER_SOURCE | gfx::BufferUsage::TRANSFER_DESTINATION | usage,
             .debug_name = "test words"}));
        gfx::Buffer& buffer = *m_buffers.back();
        run<gfx::QueueType::COMPUTE>([&](gfx::ComputeRecorder& recorder) {
            const std::array regions{gfx::BufferCopyRegion{.size = bytes.size()}};
            recorder.copy_buffer(staging->handle(), buffer.handle(), regions);
        });
        return buffer;
    }

    /// Records @p recording after a barrier making earlier submissions' writes visible, runs it on the compute queue.
    template<class Recording>
    void record(Recording&& recording)
    {
        run<gfx::QueueType::COMPUTE>([&](gfx::ComputeRecorder& recorder) {
            const gfx::GlobalMemoryBarrier visible{.source_stages = gfx::PipelineStage::ALL_COMMANDS,
                                                   .source_access = gfx::MemoryAccess::TRANSFER_WRITE | gfx::MemoryAccess::SHADER_STORAGE_WRITE,
                                                   .destination_stages = gfx::PipelineStage::COMPUTE_SHADER,
                                                   .destination_access = gfx::MemoryAccess::SHADER_STORAGE_READ | gfx::MemoryAccess::SHADER_STORAGE_WRITE};
            recorder.barrier({.memory_barriers = std::span{&visible, 1}});
            recording(recorder);
        });
    }

    /// The first @p count words of @p buffer.
    auto words(const gfx::Buffer& buffer,
               std::size_t count) -> std::vector<std::uint32_t>
    {
        return read_buffer<std::uint32_t>(buffer, std::max<std::size_t>(count, 1), gfx::ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
    }

    std::unique_ptr<gpu::GpuPrimitives> m_primitives;
    std::vector<std::unique_ptr<gfx::Buffer>> m_buffers;
};

TEST_P(GpuPrimitivesTest, ScansMatchTheSerialSums)
{
    // The variant asked for runs (subgroups wherever the GPU has subgroup arithmetic, unless the portable one is asked).
    EXPECT_EQ(m_primitives->uses_subgroups(), std::get<1>(GetParam()) == GpuVariantChoice::AUTOMATIC && gpu::capabilities(*m_device).subgroups);

    // One level (up to a tile), two, three; wrapping sums; small values.
    for (const std::uint32_t count : {0U, 1U, 7U, 64U, 511U, 512U, 513U, 4096U, 100'003U, 600'001U})
    {
        const std::vector<std::uint32_t> values = random_words(count, count, count % 2 == 0 ? 0 : 1000);
        const auto [sums, total] = serial_scan(values);
        m_primitives->reserve(count);
        gfx::Buffer& input = words_buffer(count, values);
        gfx::Buffer& output = words_buffer(count + 1);
        gfx::Buffer& sum = words_buffer(1);
        record([&](gfx::ComputeRecorder& recorder) {
            m_primitives->exclusive_scan(recorder, gpu::span_of(input), gpu::span_of(output), {.maximum = count}, gpu::span_of(sum));
        });
        const std::vector<std::uint32_t> scanned = words(output, count + 1);
        EXPECT_TRUE(std::equal(sums.begin(), sums.end(), scanned.begin())) << count << " words";
        EXPECT_EQ(scanned[count], UNTOUCHED) << "nothing past the count";
        EXPECT_EQ(words(sum, 1)[0], total) << count << " words";

        // In place.
        record([&](gfx::ComputeRecorder& recorder) {
            m_primitives->exclusive_scan(recorder, gpu::span_of(input), gpu::span_of(input), {.maximum = count});
        });
        const std::vector<std::uint32_t> in_place = words(input, count);
        EXPECT_TRUE(std::equal(sums.begin(), sums.end(), in_place.begin())) << count << " words in place";
    }
}

TEST_P(GpuPrimitivesTest, BoundsAreTheSerialMinimumsAndMaximums)
{
    for (const std::uint32_t count : {0U, 1U, 2U, 513U, 70'000U, 300'001U})
    {
        // Boxes around random centers (minimum, maximum; w unused).
        std::mt19937 generator{count};
        std::uniform_real_distribution<float> coordinate{-1000.0F, 1000.0F};
        std::uniform_real_distribution<float> extent{0.0F, 5.0F};
        std::vector<std::array<float, 4>> boxes;
        std::array<float, 3> minimum{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        std::array<float, 3> maximum{-minimum[0], -minimum[1], -minimum[2]};
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::array<float, 4> low{};
            std::array<float, 4> high{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const float center = coordinate(generator);
                const float half = extent(generator);
                low[axis] = center - half;
                high[axis] = center + half;
                minimum[axis] = std::min(minimum[axis], low[axis]);
                maximum[axis] = std::max(maximum[axis], high[axis]);
            }
            boxes.push_back(low);
            boxes.push_back(high);
        }
        m_primitives->reserve(count);
        boxes.resize(std::max<std::size_t>(boxes.size(), 2));
        gfx::Buffer& input = upload(std::as_bytes(std::span{boxes}), gfx::BufferUsage::NONE);
        const std::array<std::array<float, 4>, 2> cleared{};
        gfx::Buffer& result = upload(std::as_bytes(std::span{cleared}), gfx::BufferUsage::NONE);
        record([&](gfx::ComputeRecorder& recorder) {
            m_primitives->reduce_bounds(recorder, gpu::span_of(input), gpu::span_of(result), {.maximum = count});
        });
        const std::vector<float> box = read_buffer<float>(result, 8, gfx::ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_EQ(box[axis], minimum[axis]) << count << " boxes, axis " << axis;
            EXPECT_EQ(box[4 + axis], maximum[axis]) << count << " boxes, axis " << axis;
        }
    }
}

TEST_P(GpuPrimitivesTest, SortsAreStableAndMatchStdStableSort)
{
    // Keys of every kind; values = the original indices, so stability shows.
    struct Case
    {
        std::uint32_t count;
        std::uint64_t limit;    // keys below it (0: any 32 bits)
        std::uint32_t key_bits;
        bool ascending;         // already sorted
    };
    for (const Case& sorted_case : {Case{0, 0, 32, false}, Case{1, 0, 32, false}, Case{100, 16, 4, false}, Case{4096, 0, 32, false},
                                    Case{4097, 0, 32, false}, Case{50'000, 16, 4, false}, Case{50'000, std::uint64_t{1} << 30U, 30, false},
                                    Case{200'003, 0, 32, false}, Case{200'003, 1000, 10, true}, Case{70'000, 3, 2, false}})
    {
        const std::uint32_t count = sorted_case.count;
        std::vector<std::uint32_t> keys = random_words(count, count + sorted_case.key_bits, sorted_case.limit);
        if (sorted_case.ascending)
        {
            std::ranges::sort(keys);
        }
        std::vector<std::uint32_t> values(count);
        std::ranges::iota(values, 0U);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> expected(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            expected[index] = {keys[index], index};
        }
        std::ranges::stable_sort(expected, {}, &std::pair<std::uint32_t, std::uint32_t>::first);

        m_primitives->reserve(count);
        gfx::Buffer& key_input = words_buffer(count, keys);
        gfx::Buffer& value_input = words_buffer(count, values);
        gfx::Buffer& key_output = words_buffer(count + 1);
        gfx::Buffer& value_output = words_buffer(count + 1);
        record([&](gfx::ComputeRecorder& recorder) {
            m_primitives->sort(recorder, gpu::span_of(key_input), gpu::span_of(value_input), gpu::span_of(key_output), gpu::span_of(value_output),
                               {.maximum = count}, sorted_case.key_bits);
        });
        const std::vector<std::uint32_t> sorted_keys = words(key_output, count + 1);
        const std::vector<std::uint32_t> sorted_values = words(value_output, count + 1);
        bool same = true;
        for (std::uint32_t index = 0; index < count && same; ++index)
        {
            same = sorted_keys[index] == expected[index].first && sorted_values[index] == expected[index].second;
            EXPECT_TRUE(same) << count << " keys of " << sorted_case.key_bits << " bits: element " << index << " is (" << sorted_keys[index] << ", "
                              << sorted_values[index] << "), expected (" << expected[index].first << ", " << expected[index].second << ")";
        }
        EXPECT_EQ(sorted_keys[count], UNTOUCHED);
        EXPECT_EQ(sorted_values[count], UNTOUCHED);
    }
}

TEST_P(GpuPrimitivesTest, AMillionKeysSortInOrder)
{
    // A million random 32-bit keys by radix (four passes); the time of the submission (recording and waiting included)
    // printed for information.
    constexpr std::uint32_t COUNT = 1U << 20U;
    const std::vector<std::uint32_t> keys = random_words(COUNT, 2026);
    std::vector<std::uint32_t> values(COUNT);
    std::ranges::iota(values, 0U);
    m_primitives->reserve(COUNT);
    gfx::Buffer& key_input = words_buffer(COUNT, keys);
    gfx::Buffer& value_input = words_buffer(COUNT, values);
    gfx::Buffer& key_output = words_buffer(COUNT);
    gfx::Buffer& value_output = words_buffer(COUNT);
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto start = std::chrono::steady_clock::now();
        record([&](gfx::ComputeRecorder& recorder) {
            m_primitives->sort(recorder, gpu::span_of(key_input), gpu::span_of(value_input), gpu::span_of(key_output), gpu::span_of(value_output),
                               {.maximum = COUNT});
        });
        std::cout << std::format("{} keys sorted in {:.2f} ms ({} variant)\n", COUNT,
                                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), std::get<1>(GetParam()));
    }
    const std::vector<std::uint32_t> sorted_keys = words(key_output, COUNT);
    const std::vector<std::uint32_t> sorted_values = words(value_output, COUNT);
    EXPECT_TRUE(std::ranges::is_sorted(sorted_keys));
    bool paired = true;
    for (std::uint32_t index = 0; index < COUNT && paired; ++index)
    {
        paired = keys[sorted_values[index]] == sorted_keys[index] && (index == 0 || sorted_keys[index - 1] != sorted_keys[index] ||
                                                                      sorted_values[index - 1] < sorted_values[index]);
    }
    EXPECT_TRUE(paired) << "each value follows its key, equal keys in their first order";
}

TEST_P(GpuPrimitivesTest, CompactionsKeepTheFlaggedElementsInOrder)
{
    for (const std::uint32_t count : {0U, 1U, 600U, 100'000U})
    {
        const std::vector<std::uint32_t> flags = random_words(count, count, 2);
        const std::vector<std::uint32_t> values = random_words(count, count + 1);
        std::vector<std::uint32_t> kept_values;
        std::vector<std::uint32_t> kept_indices;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (flags[index] != 0)
            {
                kept_values.push_back(values[index]);
                kept_indices.push_back(index);
            }
        }
        m_primitives->reserve(count);
        gfx::Buffer& flag_input = words_buffer(count, flags);
        gfx::Buffer& value_input = words_buffer(count, values);
        gfx::Buffer& value_output = words_buffer(count + 1);
        gfx::Buffer& index_output = words_buffer(count + 1);
        gfx::Buffer& counts = words_buffer(2);
        record([&](gfx::ComputeRecorder& recorder) {
            m_primitives->compact(recorder, gpu::span_of(flag_input), gpu::span_of(value_input), gpu::span_of(value_output), gpu::span_of(counts, 0),
                                  {.maximum = count});
            m_primitives->compact(recorder, gpu::span_of(flag_input), {}, gpu::span_of(index_output), gpu::span_of(counts, 1), {.maximum = count});
        });
        const std::vector<std::uint32_t> kept = words(counts, 2);
        EXPECT_EQ(kept[0], kept_values.size()) << count;
        EXPECT_EQ(kept[1], kept_indices.size()) << count;
        const std::vector<std::uint32_t> compacted_values = words(value_output, kept_values.size() + 1);
        const std::vector<std::uint32_t> compacted_indices = words(index_output, kept_indices.size() + 1);
        EXPECT_TRUE(std::equal(kept_values.begin(), kept_values.end(), compacted_values.begin())) << count;
        EXPECT_TRUE(std::equal(kept_indices.begin(), kept_indices.end(), compacted_indices.begin())) << count;
        EXPECT_EQ(compacted_values[kept_values.size()], UNTOUCHED);
    }
}

TEST_P(GpuPrimitivesTest, CountsWrittenByAPassAreHonored)
{
    // The dispatches are sized for the maximum; the work stops at the count in the word (a word above the maximum is
    // clamped to it).
    constexpr std::uint32_t MAXIMUM = 10'000;
    constexpr std::uint32_t COUNT = 5'555;
    m_primitives->reserve(MAXIMUM);
    const std::vector<std::uint32_t> values = random_words(MAXIMUM, 7, 1'000'000);
    const std::array count_words{COUNT, 777U, 20'000U, 0U};
    gfx::Buffer& counts = words_buffer(count_words.size(), count_words);
    gfx::Buffer& input = words_buffer(MAXIMUM, values);
    gfx::Buffer& scanned = words_buffer(MAXIMUM);
    gfx::Buffer& clamped = words_buffer(MAXIMUM);
    gfx::Buffer& sum = words_buffer(2);
    gfx::Buffer& paired = words_buffer(MAXIMUM, values);
    gfx::Buffer& sorted_keys = words_buffer(MAXIMUM);
    gfx::Buffer& sorted_values = words_buffer(MAXIMUM);
    gfx::Buffer& ranked_keys = words_buffer(MAXIMUM);
    gfx::Buffer& ranked_values = words_buffer(MAXIMUM);
    const std::array<std::uint32_t, 8> no_arguments{};
    gfx::Buffer& arguments = upload(std::as_bytes(std::span{no_arguments}), gfx::BufferUsage::INDIRECT);
    record([&](gfx::ComputeRecorder& recorder) {
        m_primitives->exclusive_scan(recorder, gpu::span_of(input), gpu::span_of(scanned), {.maximum = MAXIMUM, .word = gpu::span_of(counts, 0)},
                                     gpu::span_of(sum, 0));
        m_primitives->exclusive_scan(recorder, gpu::span_of(input), gpu::span_of(clamped), {.maximum = MAXIMUM, .word = gpu::span_of(counts, 2)},
                                     gpu::span_of(sum, 1));
        m_primitives->sort(recorder, gpu::span_of(input), gpu::span_of(paired), gpu::span_of(sorted_keys), gpu::span_of(sorted_values),
                           {.maximum = MAXIMUM, .word = gpu::span_of(counts, 0)});
        m_primitives->sort(recorder, gpu::span_of(input), gpu::span_of(paired), gpu::span_of(ranked_keys), gpu::span_of(ranked_values),
                           {.maximum = GPU_XPBD_RANK_SORT_LIMIT, .word = gpu::span_of(counts, 1)});
        m_primitives->dispatch_arguments(recorder, {.maximum = MAXIMUM, .word = gpu::span_of(counts, 0)}, gpu::span_of(arguments, 0));
        m_primitives->dispatch_arguments(recorder, {.maximum = MAXIMUM, .word = gpu::span_of(counts, 3)}, gpu::span_of(arguments, 4), 100);
    });

    // The scan of COUNT values; the clamped one of MAXIMUM.
    const auto [sums, total] = serial_scan(std::span{values}.first(COUNT));
    const std::vector<std::uint32_t> gpu_sums = words(scanned, MAXIMUM);
    EXPECT_TRUE(std::equal(sums.begin(), sums.end(), gpu_sums.begin()));
    EXPECT_EQ(gpu_sums[COUNT], UNTOUCHED);
    const auto [all_sums, all_total] = serial_scan(values);
    EXPECT_EQ(words(clamped, MAXIMUM), all_sums);
    EXPECT_EQ(words(sum, 2), (std::vector{total, all_total}));

    // The sorts of COUNT and 777 keys (the values were the keys too: equal pairs).
    for (const auto& [kept, keys_buffer] : {std::pair{COUNT, &sorted_keys}, std::pair{777U, &ranked_keys}})
    {
        std::vector<std::uint32_t> expected(values.begin(), values.begin() + kept);
        std::ranges::sort(expected);
        const std::vector<std::uint32_t> gpu_keys = words(*keys_buffer, kept + 1);
        EXPECT_TRUE(std::equal(expected.begin(), expected.end(), gpu_keys.begin())) << kept << " keys";
        EXPECT_EQ(gpu_keys[kept], UNTOUCHED) << kept << " keys";
    }

    // ⌈5555 / 64⌉ = 87 workgroups; 0 elements, 0 workgroups.
    EXPECT_EQ(words(arguments, 8), (std::vector<std::uint32_t>{87, 1, 1, 0, 0, 1, 1, 0}));
}

TEST_P(GpuPrimitivesTest, MisusesAreRefused)
{
    const std::uint32_t reserved = m_primitives->reserved();
    EXPECT_THROW(m_primitives->reserve(GPU_XPBD_MAXIMUM_ELEMENTS + 1), gfx::InvalidUsageError);
    gfx::Buffer& keys = words_buffer(4);
    gfx::Buffer& values = words_buffer(4);
    run<gfx::QueueType::COMPUTE>([&](gfx::ComputeRecorder& recorder) {
        EXPECT_THROW(m_primitives->exclusive_scan(recorder, gpu::span_of(keys), gpu::span_of(keys), {.maximum = reserved + 1}), gfx::InvalidUsageError);
        EXPECT_THROW(m_primitives->sort(recorder, gpu::span_of(keys), gpu::span_of(values), gpu::span_of(keys, 2), gpu::span_of(values), {.maximum = 2}),
                     gfx::InvalidUsageError)
            << "an output in an input's buffer";
        EXPECT_THROW(m_primitives->sort(recorder, gpu::span_of(keys), gpu::span_of(values), gpu::span_of(values), gpu::span_of(keys), {.maximum = 2}, 0),
                     gfx::InvalidUsageError);
        EXPECT_THROW(m_primitives->dispatch_arguments(recorder, {.maximum = 4}, gpu::span_of(keys)), gfx::InvalidUsageError) << "no count word";
    });
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuPrimitivesTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuVariantChoice::AUTOMATIC, GpuVariantChoice::PORTABLE)),
                         [](const testing::TestParamInfo<std::tuple<std::size_t, GpuVariantChoice>>& parameter) {
                             return std::format("Device{}_{}", std::get<0>(parameter.param), std::get<1>(parameter.param));
                         });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuPrimitivesTest);

} // namespace
