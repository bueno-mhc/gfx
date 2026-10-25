// Spatial structures against brute force: the same scene of thousands of boxes, moved, removed and added step after
// step, through each of the six structures (and both kinds of sweep and prune): their overlapping pairs and their
// answers to box, sphere, segment, ray and frustum queries equal what testing every box finds; visitors stop the
// queries; stale handles are refused.
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

constexpr float PI = std::numbers::pi_v<float>;

/// The scene as brute force sees it: each live collider's box.
using Scene = std::map<ColliderHandle, Aabb>;

/// The pairs of overlapping boxes of @p scene, the smaller handle first.
auto
brute_pairs(const Scene& scene) -> std::set<std::pair<ColliderHandle, ColliderHandle>>
{
    std::set<std::pair<ColliderHandle, ColliderHandle>> pairs;
    for (auto first = scene.begin(); first != scene.end(); ++first)
    {
        for (auto second = std::next(first); second != scene.end(); ++second)
        {
            if (intersects(first->second, second->second))
            {
                pairs.insert({first->first, second->first});
            }
        }
    }
    return pairs;
}

/// The colliders of @p scene whose boxes pass @p test.
template<class Test>
auto
brute_query(const Scene& scene,
            Test&& test) -> std::set<ColliderHandle>
{
    std::set<ColliderHandle> found;
    for (const auto& [key, box] : scene)
    {
        if (test(box))
        {
            found.insert(key);
        }
    }
    return found;
}

/// A random box: mostly small, some large (grids list those apart, trees keep them high).
auto
random_box(Generator& generator) -> Aabb
{
    const float size = generator.between(0, 1) < 0.05F ? generator.between(10, 40) : generator.between(0.1F, 5);
    return Aabb::from_center(generator.vector(-100, 100), {.x = size * generator.between(0.3F, 1), .y = size * generator.between(0.3F, 1), .z = size * generator.between(0.3F, 1)});
}

template<class Structure>
class SpatialStructureTest : public ::testing::Test
{
};

using Structures = ::testing::Types<Bvh, Octree<6>, Quadtree<AxisPlane::XZ, 6>, Bsp, UniformGrid<4.0F>, SweepAndPrune<1>, SweepAndPrune<3>>;
TYPED_TEST_SUITE(SpatialStructureTest, Structures);

TYPED_TEST(SpatialStructureTest, QueriesAndPairsMatchBruteForce)
{
    TypeParam structure{};
    Scene scene;
    std::vector<std::pair<ColliderHandle, SpatialProxyHandle>> live;
    Generator generator{12345};
    std::uint32_t next_key = 1;
    const auto add = [&] {
        const ColliderHandle key = PhysicsHandles::make<ColliderTag>(next_key++);
        const Aabb box = random_box(generator);
        live.push_back({key, structure.insert(key, box)});
        scene[key] = box;
    };
    for (int index = 0; index < 1500; ++index)
    {
        add();
    }

    for (int step = 0; step < 8; ++step)
    {
        // Moves, removals, additions.
        for (auto& [key, proxy] : live)
        {
            if (generator.between(0, 1) < 0.3F)
            {
                const Aabb moved = Aabb::from_center(scene[key].center() + generator.vector(-3, 3), scene[key].half_extents());
                structure.update(proxy, moved);
                scene[key] = moved;
            }
        }
        for (std::size_t index = 0; index < live.size();)
        {
            if (generator.between(0, 1) < 0.05F)
            {
                structure.remove(live[index].second);
                scene.erase(live[index].first);
                live[index] = live.back();
                live.pop_back();
            }
            else
            {
                ++index;
            }
        }
        for (int index = 0; index < 60; ++index)
        {
            add();
        }
        ASSERT_EQ(structure.size(), scene.size()) << "step " << step;

        // Pairs: each once, the same as brute force.
        std::set<std::pair<ColliderHandle, ColliderHandle>> pairs;
        std::size_t reported = 0;
        structure.pairs([&](ColliderHandle first, ColliderHandle second) {
            pairs.insert(first < second ? std::pair{first, second} : std::pair{second, first});
            ++reported;
        });
        EXPECT_EQ(reported, pairs.size()) << "step " << step << ": a pair reported twice";
        EXPECT_EQ(pairs, brute_pairs(scene)) << "step " << step;

        // Queries of every kind.
        for (int sample = 0; sample < 12; ++sample)
        {
            const Aabb box = Aabb::from_center(generator.vector(-100, 100), generator.vector(1, 30));
            std::set<ColliderHandle> found;
            structure.query(box, [&found](ColliderHandle key) { return found.insert(key), true; });
            EXPECT_EQ(found, brute_query(scene, [&box](const Aabb& item) { return intersects(item, box); })) << "box query, step " << step;

            const Sphere sphere{.center = generator.vector(-100, 100), .radius = generator.between(1, 30)};
            found.clear();
            structure.query(sphere, [&found](ColliderHandle key) { return found.insert(key), true; });
            EXPECT_EQ(found, brute_query(scene, [&sphere](const Aabb& item) { return intersects(item, sphere); })) << "sphere query, step " << step;

            const Segment segment{.start = generator.vector(-120, 120), .end = generator.vector(-120, 120)};
            found.clear();
            structure.query(segment, [&found](ColliderHandle key) { return found.insert(key), true; });
            EXPECT_EQ(found, brute_query(scene, [&segment](const Aabb& item) { return intersects(segment, item); })) << "segment query, step " << step;

            const Ray ray{.origin = generator.vector(-120, 120), .direction = generator.direction()};
            const float maximum = sample % 3 == 0 ? std::numeric_limits<float>::infinity() : generator.between(10, 150);
            found.clear();
            bool distances = true;
            structure.query(ray, maximum, [&](ColliderHandle key, float distance) {
                distances = distances && distance == *ray_distance(ray, scene.at(key), maximum);
                return found.insert(key), true;
            });
            EXPECT_TRUE(distances) << "ray query distances, step " << step;
            EXPECT_EQ(found, brute_query(scene, [&](const Aabb& item) { return ray_distance(ray, item, maximum).has_value(); })) << "ray query, step " << step;

            const Transform camera{.position = generator.vector(-50, 50), .orientation = generator.rotation()};
            const Frustum frustum = Frustum::from_matrix(gfx::tests::perspective(PI / 3, 1.5F, 1, 80) * Matrix4::from_transform(inverse(camera)));
            found.clear();
            structure.query(frustum, [&found](ColliderHandle key) { return found.insert(key), true; });
            EXPECT_EQ(found, brute_query(scene, [&frustum](const Aabb& item) { return classify(frustum, item) != FrustumIntersection::OUTSIDE; }))
                << "frustum query, step " << step;
        }

        // Iteration gives every proxy with its box.
        Scene iterated;
        structure.iterate([&iterated](ColliderHandle key, const Aabb& box) { iterated[key] = box; });
        EXPECT_EQ(iterated, scene) << "step " << step;
    }

    // A visitor returning false stops the query at once.
    std::size_t visits = 0;
    structure.query(Aabb::from_center({}, {.x = 200, .y = 200, .z = 200}), [&visits](ColliderHandle) { return ++visits, false; });
    EXPECT_EQ(visits, 1U);

    // Handles: what a proxy holds; removed ones are refused.
    const auto [key, proxy] = live.front();
    EXPECT_EQ(structure.key(proxy), key);
    EXPECT_EQ(structure.box(proxy), scene[key]);
    structure.remove(proxy);
    EXPECT_THROW(structure.remove(proxy), PhysicsError);
    EXPECT_THROW((void)structure.box(proxy), PhysicsError);
    EXPECT_THROW(structure.update(SpatialProxyHandle{}, Aabb{}), PhysicsError);
}

TEST(SpatialStructureTest, TreesStayBalancedAndShallow)
{
    // A BVH of 4096 boxes inserted in sorted order (the worst case for a naive tree): the rotations keep it balanced.
    Bvh bvh;
    for (std::uint32_t index = 0; index < 4096; ++index)
    {
        bvh.insert(PhysicsHandles::make<ColliderTag>(index + 1), Aabb::from_center({.x = static_cast<float>(index)}, {.x = 0.4F, .y = 0.4F, .z = 0.4F}));
    }
    EXPECT_LE(bvh.height(), 20) << "log2(4096) = 12; an AVL tree is at most about 1.44 times that";

    // A BSP tree of the same boxes rebuilds itself whenever a split would go past about twice a balanced tree's depth:
    // 2 · bit_width(4096 / 8 + 1) + 4 = 24 (a balanced tree of leaves of 8 is 9 deep).
    Bsp bsp;
    for (std::uint32_t index = 0; index < 4096; ++index)
    {
        bsp.insert(PhysicsHandles::make<ColliderTag>(index + 1), Aabb::from_center({.x = static_cast<float>(index)}, {.x = 0.4F, .y = 0.4F, .z = 0.4F}));
    }
    EXPECT_LE(bsp.depth(), 24U);

    // A loose octree places a proxy by its size: small ones deep, a large one at the root.
    Octree<8> octree{Aabb::from_center({}, {.x = 128, .y = 128, .z = 128})};
    const SpatialProxyHandle small = octree.insert(PhysicsHandles::make<ColliderTag>(1), Aabb::from_center({.x = 10}, {.x = 0.25F, .y = 0.25F, .z = 0.25F}));
    const SpatialProxyHandle large = octree.insert(PhysicsHandles::make<ColliderTag>(2), Aabb::from_center({}, {.x = 200, .y = 1, .z = 1}));
    const SpatialProxyHandle outside = octree.insert(PhysicsHandles::make<ColliderTag>(3), Aabb::from_center({.x = 1000}, {.x = 1, .y = 1, .z = 1}));
    EXPECT_EQ(octree.level(small), 8U) << "cells of 1 at level 8 hold a box of 0.5";
    EXPECT_EQ(octree.level(large), 0U);
    EXPECT_EQ(octree.level(outside), 0U) << "outside the region: the root";
    std::set<ColliderHandle> found;
    octree.query(Aabb::from_center({.x = 1000}, {.x = 2, .y = 2, .z = 2}), [&found](ColliderHandle key) { return found.insert(key), true; });
    EXPECT_EQ(found, std::set<ColliderHandle>{PhysicsHandles::make<ColliderTag>(3)});
    EXPECT_THROW(Octree<8>{Aabb{}}, PhysicsError);
}

TEST(SpatialStructureTest, StructuresChosenAtRunTimeAnswerAsTheStructuresThemselves)
{
    // The same boxes in each kind of structure behind the virtual interface, and in a BVH used directly.
    Generator generator{31};
    Bvh direct;
    std::vector<std::pair<ColliderHandle, Aabb>> boxes;
    for (std::uint32_t index = 0; index < 300; ++index)
    {
        boxes.emplace_back(PhysicsHandles::make<ColliderTag>(index + 1), random_box(generator));
        direct.insert(boxes.back().first, boxes.back().second);
    }
    const auto sorted_pairs = [](const auto& structure) {
        std::set<std::pair<ColliderHandle, ColliderHandle>> pairs;
        structure.pairs([&pairs](ColliderHandle first, ColliderHandle second) { pairs.insert(first < second ? std::pair{first, second} : std::pair{second, first}); });
        return pairs;
    };
    const Aabb region = Aabb::from_center({}, {.x = 40, .y = 40, .z = 40});
    const Ray ray{.origin = {.x = -150}, .direction = normalized(Vector3{.x = 1, .y = 0.1F, .z = 0.05F})};
    std::set<ColliderHandle> expected_box;
    direct.query(region, [&expected_box](ColliderHandle key) { return expected_box.insert(key), true; });
    std::set<ColliderHandle> expected_ray;
    direct.query(ray, 300.0F, [&expected_ray](ColliderHandle key, float /*distance*/) { return expected_ray.insert(key), true; });
    for (const SpatialStructureKind kind : {SpatialStructureKind::BVH, SpatialStructureKind::OCTREE, SpatialStructureKind::QUADTREE, SpatialStructureKind::BSP,
                                            SpatialStructureKind::UNIFORM_GRID, SpatialStructureKind::SWEEP_AND_PRUNE})
    {
        const std::unique_ptr<SpatialStructure> structure = make_spatial_structure(kind);
        EXPECT_EQ(structure->kind(), kind);
        std::vector<SpatialProxyHandle> proxies;
        for (const auto& [key, box] : boxes)
        {
            proxies.push_back(structure->insert(key, box));
        }
        EXPECT_EQ(structure->size(), boxes.size()) << to_string(kind);
        EXPECT_EQ(sorted_pairs(*structure), sorted_pairs(direct)) << to_string(kind);
        std::set<ColliderHandle> found;
        structure->query(region, [&found](ColliderHandle key) { return found.insert(key), true; });
        EXPECT_EQ(found, expected_box) << to_string(kind);
        found.clear();
        structure->query(ray, 300.0F, [&found](ColliderHandle key, float /*distance*/) { return found.insert(key), true; });
        EXPECT_EQ(found, expected_ray) << to_string(kind);

        // The proxies hold what they were given; removed ones are refused; clear() forgets everything.
        EXPECT_EQ(structure->key(proxies[7]), boxes[7].first);
        EXPECT_EQ(structure->box(proxies[7]), boxes[7].second);
        structure->remove(proxies[7]);
        EXPECT_THROW(structure->remove(proxies[7]), PhysicsError) << to_string(kind);
        structure->clear();
        EXPECT_EQ(structure->size(), 0U);
    }

    // The adapter keeps the structure reachable.
    SpatialStructureAdapter<SweepAndPrune<3>> adapter;
    adapter.insert(PhysicsHandles::make<ColliderTag>(1), Aabb::from_center({}, {.x = 1, .y = 1, .z = 1}));
    EXPECT_EQ(adapter.structure().size(), 1U);
    EXPECT_EQ(adapter.kind(), SpatialStructureKind::SWEEP_AND_PRUNE);
}

} // namespace
