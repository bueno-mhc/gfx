#include <gtest/gtest.h>

import std;
import gfx.core.slot_table;

namespace
{

using gfx::backends::SlotName;
using gfx::backends::SlotTable;

struct Record
{
    std::uint64_t value = 0;
    std::uint64_t check = 0; // ~value: a torn or misplaced record would not match
};

auto
make_record(std::uint64_t value) -> Record
{
    return {.value = value, .check = ~value};
}

TEST(SlotTableTest, InsertedRecordsAreFoundByTheirName)
{
    SlotTable<Record, 24, 8> table;
    const SlotName first = table.insert(make_record(10)).value();
    const SlotName second = table.insert(make_record(20)).value();

    EXPECT_NE(first, second);
    EXPECT_NE(first.generation, 0U);
    ASSERT_NE(table.find(first), nullptr);
    EXPECT_EQ(table.find(first)->value, 10U);
    EXPECT_EQ(table.find(second)->value, 20U);
    EXPECT_EQ(table.size(), 2U);
    EXPECT_EQ(table.describe_missing(first), "");
}

TEST(SlotTableTest, RemovedNamesStopResolvingAndSayWhy)
{
    SlotTable<Record, 24, 8> table{0};
    const SlotName name = table.insert(make_record(7)).value();

    const std::optional<Record> removed = table.remove(name);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->value, 7U);
    EXPECT_EQ(table.find(name), nullptr);
    EXPECT_FALSE(table.remove(name));
    EXPECT_EQ(table.size(), 0U);
    EXPECT_EQ(table.describe_missing(name), "its object was destroyed (slot 0 generation 1 is free)");

    // The slot is reused with a new generation: the old name still does not resolve.
    const SlotName reused = table.insert(make_record(8)).value();
    EXPECT_EQ(reused.index, name.index);
    EXPECT_EQ(reused.generation, 2U);
    EXPECT_EQ(table.find(name), nullptr);
    EXPECT_EQ(table.describe_missing(name),
              "its object was destroyed (slot 0 now holds generation 2, the handle has generation 1)");

    EXPECT_EQ(table.describe_missing({}), "it is the null handle");
    EXPECT_EQ(table.describe_missing({.index = 5000, .generation = 1}),
              "slot 5000 was never issued (it does not belong to this device)");
    EXPECT_EQ(table.find(SlotName{}), nullptr);
}

TEST(SlotTableTest, SlotsAreReusedOnlyPastTheThresholdOldestFirst)
{
    SlotTable<Record, 24, 8> table{2};
    std::vector<SlotName> names;
    for (std::uint64_t value = 0; value < 4; ++value)
    {
        names.push_back(table.insert(make_record(value)).value());
    }
    (void)table.remove(names[2]);
    (void)table.remove(names[0]);

    // Two free slots: not more than the threshold, so a new slot is used.
    EXPECT_EQ(table.insert(make_record(4)).value().index, 4U);

    // Three free slots: the oldest one (2) is reused.
    (void)table.remove(names[1]);
    EXPECT_EQ(table.insert(make_record(5)).value().index, 2U);
}

TEST(SlotTableTest, GenerationsWrapAroundAndSkipZero)
{
    SlotTable<Record, 4, 2> table{0};
    std::vector<std::uint32_t> generations;
    for (int round = 0; round < 5; ++round)
    {
        const SlotName name = table.insert(make_record(1)).value();
        EXPECT_EQ(name.index, 0U);
        generations.push_back(name.generation);
        (void)table.remove(name);
    }
    EXPECT_EQ(generations, (std::vector<std::uint32_t>{1, 2, 3, 1, 2}));
}

TEST(SlotTableTest, FullTableRefusesInsertionUntilASlotIsFree)
{
    SlotTable<Record, 3, 8> table;
    std::vector<SlotName> names;
    for (std::uint64_t value = 0; value < 8; ++value)
    {
        names.push_back(table.insert(make_record(value)).value());
    }
    EXPECT_FALSE(table.insert(make_record(8)));

    // Every index is used: a freed slot is reused even below the threshold.
    (void)table.remove(names[3]);
    const std::optional<SlotName> name = table.insert(make_record(9));
    ASSERT_TRUE(name);
    EXPECT_EQ(name->index, 3U);
}

TEST(SlotTableTest, ChunksAreAllocatedOnDemandAcrossChunkBoundaries)
{
    using Table = SlotTable<Record, 24, 8>;
    Table table;
    std::vector<SlotName> names;
    for (std::uint64_t value = 0; value < 3 * std::uint64_t{Table::SLOTS_PER_CHUNK} + 5; ++value)
    {
        names.push_back(table.insert(make_record(value)).value());
    }
    for (std::uint64_t value = 0; value < names.size(); ++value)
    {
        ASSERT_EQ(table.find(names[value])->value, value);
    }
    EXPECT_EQ(table.size(), names.size());
}

TEST(SlotTableTest, ForEachVisitsLiveRecords)
{
    SlotTable<Record, 24, 8> table;
    const SlotName first = table.insert(make_record(1)).value();
    (void)table.insert(make_record(2));
    (void)table.insert(make_record(3));
    (void)table.remove(first);

    std::uint64_t sum = 0;
    table.for_each([&sum](SlotName, const Record& record) { sum += record.value; });
    EXPECT_EQ(sum, 5U);
}

TEST(SlotTableTest, ConcurrentInsertionsRemovalsAndLookups)
{
    SlotTable<Record, 24, 8> table{64};

    // Records that live through the whole test, looked up by reader threads.
    std::vector<SlotName> stable;
    for (std::uint64_t value = 0; value < 1000; ++value)
    {
        stable.push_back(table.insert(make_record(value)).value());
    }

    static constexpr int WRITERS = 6;
    static constexpr int READERS = 4;
    static constexpr int ITERATIONS = 20000;
    std::atomic<bool> writers_done{false};
    std::atomic<int> failures{0};
    {
        std::vector<std::jthread> writers;
        std::vector<std::jthread> readers;

        // Writers insert, check and remove their own records.
        for (int writer = 0; writer < WRITERS; ++writer)
        {
            writers.emplace_back([&table, &failures, writer]
            {
                std::deque<SlotName> mine;
                for (int iteration = 0; iteration < ITERATIONS; ++iteration)
                {
                    const std::uint64_t value = static_cast<std::uint64_t>(writer) << 32U | static_cast<std::uint64_t>(iteration);
                    mine.push_back(table.insert(make_record(value)).value());
                    const Record* record = table.find(mine.back());
                    if (record == nullptr || record->value != value || record->check != ~value)
                    {
                        failures.fetch_add(1);
                    }
                    if (mine.size() > 16)
                    {
                        const SlotName oldest = mine.front();
                        mine.pop_front();
                        if (!table.remove(oldest) || table.find(oldest) != nullptr)
                        {
                            failures.fetch_add(1);
                        }
                    }
                }
                for (const SlotName name : mine)
                {
                    (void)table.remove(name);
                }
            });
        }

        // Readers look the stable records up while the writers work.
        for (int reader = 0; reader < READERS; ++reader)
        {
            readers.emplace_back([&table, &stable, &failures, &writers_done]
            {
                while (!writers_done.load())
                {
                    for (std::uint64_t value = 0; value < stable.size(); ++value)
                    {
                        const Record* record = table.find(stable[value]);
                        if (record == nullptr || record->value != value || record->check != ~value)
                        {
                            failures.fetch_add(1);
                        }
                    }
                }
            });
        }

        // Wait for the writers, then stop the readers.
        writers.clear();
        writers_done.store(true);
    }

    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(table.size(), stable.size());
}

} // namespace
