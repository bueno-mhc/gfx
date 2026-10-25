// The broad phase: collision filters (layers, groups, a custom test), fattened boxes that keep small motions out of the
// structure, sorted pairs with the pairs that began and ended, the same pairs from every structure and for any number
// of threads; and the task schedulers (every index once, exceptions carried to the caller).
#include <gtest/gtest.h>

import std;
import gfx.physics.spatial;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::Generator;
using gfx::tests::PhysicsHandles;

auto
collider(std::uint32_t index) -> ColliderHandle
{
    return PhysicsHandles::make<ColliderTag>(index);
}

TEST(BroadPhaseTest, FiltersDecideWhichPairsCount)
{
    // Layers both ways; a shared positive group always collides, a shared negative group never.
    constexpr CollisionFilter PLAYER{.category = LayerMask::LAYER_1, .mask = LayerMask::LAYER_0 | LayerMask::LAYER_2};
    constexpr CollisionFilter WALL{.category = LayerMask::LAYER_0};
    constexpr CollisionFilter GHOST{.category = LayerMask::LAYER_3, .mask = LayerMask::LAYER_0};
    static_assert(should_collide(PLAYER, WALL) && should_collide(WALL, PLAYER));
    static_assert(!should_collide(PLAYER, GHOST), "the ghost is not in the player's mask");
    static_assert(should_collide(CollisionFilter{.category = LayerMask::LAYER_5, .mask = LayerMask::NONE, .group = 7},
                                 CollisionFilter{.category = LayerMask::LAYER_6, .mask = LayerMask::NONE, .group = 7}));
    static_assert(!should_collide(CollisionFilter{.group = -2}, CollisionFilter{.group = -2}) && should_collide(CollisionFilter{.group = -2}, CollisionFilter{.group = 3}));

    // Four overlapping boxes: the pairs the filters and the custom test accept.
    DefaultBroadPhase broad_phase;
    const Aabb box = Aabb::from_center({}, {.x = 1, .y = 1, .z = 1});
    broad_phase.insert(collider(1), box, PLAYER);
    broad_phase.insert(collider(2), box, WALL);
    broad_phase.insert(collider(3), box, GHOST);
    broad_phase.insert(collider(4), box, WALL);
    broad_phase.update();
    const std::vector<BroadPhasePair> expected{{collider(1), collider(2)}, {collider(1), collider(4)}, {collider(2), collider(3)}, {collider(2), collider(4)},
                                               {collider(3), collider(4)}};
    EXPECT_EQ(std::vector(broad_phase.pairs().begin(), broad_phase.pairs().end()), expected);
    broad_phase.set_custom_filter([](ColliderHandle first, ColliderHandle second) { return first != collider(1) && second != collider(1); });
    broad_phase.update();
    EXPECT_EQ(broad_phase.pairs().size(), 3U);
    EXPECT_EQ(broad_phase.ended_pairs().size(), 2U) << "the player's pairs ended";
    EXPECT_THROW(broad_phase.insert(ColliderHandle{}, box), PhysicsError);
    EXPECT_THROW(DefaultBroadPhase(Bvh{}, {.margin = -1}), PhysicsError);
}

TEST(BroadPhaseTest, FatBoxesAbsorbSmallMotions)
{
    // Moving within the margin leaves the structure alone; leaving it refits, stretched along the motion.
    DefaultBroadPhase broad_phase{Bvh{}, {.margin = 0.5F, .prediction = 2.0F}};
    const SpatialProxyHandle proxy = broad_phase.insert(collider(1), Aabb::from_center({}, {.x = 1, .y = 1, .z = 1}));
    EXPECT_EQ(broad_phase.fat_box(proxy), Aabb::from_center({}, {.x = 1.5F, .y = 1.5F, .z = 1.5F}));
    EXPECT_FALSE(broad_phase.move(proxy, Aabb::from_center({.x = 0.4F}, {.x = 1, .y = 1, .z = 1}), {.x = 0.4F}));
    EXPECT_TRUE(broad_phase.move(proxy, Aabb::from_center({.x = 1}, {.x = 1, .y = 1, .z = 1}), {.x = 0.6F}));
    const Aabb fat = broad_phase.fat_box(proxy);
    EXPECT_FLOAT_EQ(fat.maximum.x, 1 + 1 + 0.5F + 1.2F) << "the margin and twice the displacement ahead";
    EXPECT_FLOAT_EQ(fat.minimum.x, 1 - 1 - 0.5F) << "only the margin behind";

    // Pairs begin and end as the fat boxes meet and part.
    const SpatialProxyHandle other = broad_phase.insert(collider(2), Aabb::from_center({.x = 10}, {.x = 1, .y = 1, .z = 1}));
    broad_phase.update();
    EXPECT_TRUE(broad_phase.pairs().empty());
    broad_phase.move(other, Aabb::from_center({.x = 5.5F}, {.x = 1, .y = 1, .z = 1}), {.x = -4.5F});
    broad_phase.update();
    ASSERT_EQ(broad_phase.new_pairs().size(), 1U);
    EXPECT_EQ(broad_phase.new_pairs()[0], (BroadPhasePair{collider(1), collider(2)}));
    broad_phase.move(other, Aabb::from_center({.x = 30}, {.x = 1, .y = 1, .z = 1}));
    broad_phase.update();
    EXPECT_EQ(broad_phase.ended_pairs().size(), 1U);
    EXPECT_TRUE(broad_phase.new_pairs().empty());
}

/// The pairs a broad phase on @p Structure finds over a scene moved for a few steps, on @p scheduler.
template<class Structure>
auto
pairs_over_steps(TaskScheduler* scheduler,
                 Structure structure = Structure{}) -> std::vector<std::vector<BroadPhasePair>>
{
    SpatialBroadPhase<Structure> broad_phase{std::move(structure), {.proxies_per_task = 37}};
    Generator generator{2025};
    std::vector<std::pair<SpatialProxyHandle, Aabb>> proxies;
    for (std::uint32_t index = 0; index < 3000; ++index)
    {
        const Aabb box = Aabb::from_center(generator.vector(-80, 80), generator.vector(0.2F, 3));
        const CollisionFilter filter{.category = index % 7 == 0 ? LayerMask::LAYER_1 : LayerMask::LAYER_0, .mask = index % 11 == 0 ? LayerMask::LAYER_1 : ALL_LAYERS};
        proxies.push_back({broad_phase.insert(collider(index + 1), box, filter), box});
    }
    std::vector<std::vector<BroadPhasePair>> steps;
    for (int step = 0; step < 6; ++step)
    {
        for (auto& [proxy, box] : proxies)
        {
            const Vector3 displacement = generator.vector(-0.4F, 0.4F);
            box = Aabb::from_center(box.center() + displacement, box.half_extents());
            broad_phase.move(proxy, box, displacement);
        }
        broad_phase.update(scheduler);
        steps.emplace_back(broad_phase.pairs().begin(), broad_phase.pairs().end());
    }
    return steps;
}

TEST(BroadPhaseTest, EveryStructureAndThreadCountGivesTheSamePairs)
{
    // The structure's own pair search (no scheduler) against queries split over 1, 2 and 8 threads, and every structure
    // against the BVH.
    const auto reference = pairs_over_steps<Bvh>(nullptr);
    ASSERT_FALSE(reference.back().empty());
    ThreadTaskScheduler one{1};
    ThreadTaskScheduler two{2};
    ThreadTaskScheduler eight{8};
    InlineTaskScheduler inline_scheduler;
    EXPECT_EQ(pairs_over_steps<Bvh>(&one), reference);
    EXPECT_EQ(pairs_over_steps<Bvh>(&two), reference);
    EXPECT_EQ(pairs_over_steps<Bvh>(&eight), reference);
    EXPECT_EQ(pairs_over_steps<Bvh>(&inline_scheduler), reference);
    EXPECT_EQ(pairs_over_steps<Octree<7>>(&eight), reference);
    EXPECT_EQ(pairs_over_steps<Octree<7>>(nullptr), reference);
    EXPECT_EQ((pairs_over_steps<Quadtree<AxisPlane::XY, 7>>(&eight)), reference);
    EXPECT_EQ(pairs_over_steps<Bsp>(&eight), reference);
    EXPECT_EQ(pairs_over_steps<Bsp>(nullptr), reference);
    EXPECT_EQ(pairs_over_steps<UniformGrid<2.0F>>(&eight), reference);
    EXPECT_EQ(pairs_over_steps<UniformGrid<2.0F>>(nullptr), reference);
    EXPECT_EQ(pairs_over_steps<SweepAndPrune<3>>(&eight), reference);
    EXPECT_EQ(pairs_over_steps<SweepAndPrune<1>>(nullptr), reference);
}

TEST(TaskSchedulerTest, EveryIndexRunsOnceAndFailuresReachTheCaller)
{
    ThreadTaskScheduler scheduler{6};
    EXPECT_EQ(scheduler.concurrency(), 6U);
    for (std::uint32_t round = 0; round < 50; ++round)
    {
        // Every index once, in ranges of at least the minimum (but the last).
        const std::uint32_t count = 1 + round * 37;
        std::vector<std::atomic<std::uint32_t>> visits(count);
        std::atomic<bool> short_range = false;
        scheduler.parallel_for(count, 5, [&](std::uint32_t begin, std::uint32_t end) {
            short_range = short_range || (end - begin < 5 && end != count);
            for (std::uint32_t index = begin; index < end; ++index)
            {
                visits[index].fetch_add(1);
            }
        });
        EXPECT_TRUE(std::ranges::all_of(visits, [](const std::atomic<std::uint32_t>& value) { return value.load() == 1; })) << "round " << round;
        EXPECT_FALSE(short_range) << "round " << round;
    }

    // A range's exception reaches the caller; the scheduler goes on working.
    EXPECT_THROW(scheduler.parallel_for(1000, 10,
                                        [](std::uint32_t begin, std::uint32_t) {
                                            if (begin >= 500)
                                            {
                                                throw std::runtime_error("range failed");
                                            }
                                        }),
                 std::runtime_error);
    std::atomic<std::uint32_t> total = 0;
    scheduler.parallel_for(100, 1, [&total](std::uint32_t begin, std::uint32_t end) { total += end - begin; });
    EXPECT_EQ(total.load(), 100U);
    std::uint32_t inline_total = 0;
    InlineTaskScheduler{}.parallel_for(10, 3, [&inline_total](std::uint32_t begin, std::uint32_t end) { inline_total += end - begin; });
    EXPECT_EQ(inline_total, 10U);
}

TEST(TaskSchedulerTest, StartedWorkRunsOnceAndFinishWaitsForIt)
{
    // Work started from the caller and from a parallel loop, waited for in any order; failures reach finish().
    ThreadTaskScheduler scheduler{4};
    for (int round = 0; round < 50; ++round)
    {
        std::array<std::atomic<int>, 64> runs{};
        std::vector<TaskTicket> tickets;
        for (std::size_t index = 0; index < runs.size(); ++index)
        {
            tickets.push_back(scheduler.start([&runs, index] { runs[index].fetch_add(1); }));
        }
        std::atomic<int> looped{0};
        scheduler.parallel_for(100, 1, [&looped](std::uint32_t begin, std::uint32_t end) { looped.fetch_add(static_cast<int>(end - begin)); });
        for (std::size_t index = tickets.size(); index-- > 0;)
        {
            scheduler.finish(tickets[index]);
        }
        EXPECT_EQ(looped.load(), 100);
        for (const std::atomic<int>& count : runs)
        {
            ASSERT_EQ(count.load(), 1) << "round " << round;
        }
    }

    // A started piece that waits for another started piece (Box3D's tasks wait for each other through finish()).
    std::atomic<bool> inner_done{false};
    const TaskTicket outer = scheduler.start([&] {
        const TaskTicket inner = scheduler.start([&inner_done] { inner_done.store(true); });
        scheduler.finish(inner);
    });
    scheduler.finish(outer);
    EXPECT_TRUE(inner_done.load());
    const TaskTicket failing = scheduler.start([] { throw std::runtime_error("started work failed"); });
    EXPECT_THROW(scheduler.finish(failing), std::runtime_error);

    // The default (inline) runs at once and has nothing to wait for.
    int ran = 0;
    InlineTaskScheduler inline_scheduler;
    const TaskTicket at_once = inline_scheduler.start([&ran] { ++ran; });
    EXPECT_EQ(ran, 1);
    EXPECT_EQ(at_once.generation, 0U);
    inline_scheduler.finish(at_once);
}

} // namespace
