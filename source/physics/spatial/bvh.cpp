// The dynamic AABB tree's changes: leaves inserted beside the sibling of least cost (Box2D's surface-area heuristic,
// with surface areas for 3D boxes), removed by replacing their parent with their sibling, and the path above a change
// refitted and balanced by rotations.
module gfx.physics.spatial;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;

namespace gfx::physics
{

using spatial_detail::NONE;

auto
Bvh::allocate_node() -> std::uint32_t
{
    if (!m_free_nodes.empty())
    {
        const std::uint32_t node = m_free_nodes.back();
        m_free_nodes.pop_back();
        m_nodes[node] = Node{};
        return node;
    }
    m_nodes.push_back(Node{});
    return static_cast<std::uint32_t>(m_nodes.size() - 1);
}

void
Bvh::free_node(std::uint32_t node)
{
    m_nodes[node].height = -1;
    m_free_nodes.push_back(node);
}

auto
Bvh::insert(ColliderHandle key,
            const Aabb& box) -> SpatialProxyHandle
{
    const std::uint32_t leaf = allocate_node();
    const auto [handle, slot] = m_proxies.add({.key = key, .node = leaf});
    m_nodes[leaf].box = box;
    m_nodes[leaf].proxy = slot;
    insert_leaf(leaf);
    return handle;
}

void
Bvh::remove(SpatialProxyHandle proxy)
{
    const std::uint32_t slot = m_proxies.slot_of(proxy, "Bvh::remove");
    const std::uint32_t leaf = m_proxies[slot].node;
    remove_leaf(leaf);
    free_node(leaf);
    m_proxies.release(slot);
}

auto
Bvh::update(SpatialProxyHandle proxy,
            const Aabb& box) -> bool
{
    const std::uint32_t leaf = m_proxies[m_proxies.slot_of(proxy, "Bvh::update")].node;
    remove_leaf(leaf);
    m_nodes[leaf].box = box;
    insert_leaf(leaf);
    return true;
}

void
Bvh::clear()
{
    m_proxies.clear();
    m_nodes.clear();
    m_free_nodes.clear();
    m_root = NONE;
}

void
Bvh::insert_leaf(std::uint32_t leaf)
{
    if (m_root == NONE)
    {
        m_root = leaf;
        m_nodes[leaf].parent = NONE;
        return;
    }

    // The sibling: descend while a child's cost (its growth plus what every ancestor already inherits) beats
    // making a new parent here.
    const Aabb leaf_box = m_nodes[leaf].box;
    std::uint32_t index = m_root;
    while (m_nodes[index].child1 != NONE)
    {
        const Node& node = m_nodes[index];
        const float area = node.box.surface_area();
        const float combined_area = node.box.merged(leaf_box).surface_area();
        const float cost = 2.0F * combined_area;
        const float inheritance = 2.0F * (combined_area - area);
        const auto child_cost = [&](std::uint32_t child) {
            const Aabb merged = m_nodes[child].box.merged(leaf_box);
            return (m_nodes[child].child1 == NONE ? merged.surface_area() : merged.surface_area() - m_nodes[child].box.surface_area()) + inheritance;
        };
        const float cost1 = child_cost(node.child1);
        const float cost2 = child_cost(node.child2);
        if (cost < cost1 && cost < cost2)
        {
            break;
        }
        index = cost1 < cost2 ? node.child1 : node.child2;
    }

    // A new parent for the sibling and the leaf.
    const std::uint32_t sibling = index;
    const std::uint32_t old_parent = m_nodes[sibling].parent;
    const std::uint32_t parent = allocate_node();
    m_nodes[parent].parent = old_parent;
    m_nodes[parent].box = m_nodes[sibling].box.merged(leaf_box);
    m_nodes[parent].height = m_nodes[sibling].height + 1;
    m_nodes[parent].child1 = sibling;
    m_nodes[parent].child2 = leaf;
    m_nodes[sibling].parent = parent;
    m_nodes[leaf].parent = parent;
    if (old_parent == NONE)
    {
        m_root = parent;
    }
    else if (m_nodes[old_parent].child1 == sibling)
    {
        m_nodes[old_parent].child1 = parent;
    }
    else
    {
        m_nodes[old_parent].child2 = parent;
    }

    // Refit and balance the path to the root.
    for (index = m_nodes[leaf].parent; index != NONE; index = m_nodes[index].parent)
    {
        index = balance(index);
        const Node& node = m_nodes[index];
        m_nodes[index].height = 1 + std::max(m_nodes[node.child1].height, m_nodes[node.child2].height);
        m_nodes[index].box = m_nodes[node.child1].box.merged(m_nodes[node.child2].box);
    }
}

void
Bvh::remove_leaf(std::uint32_t leaf)
{
    if (leaf == m_root)
    {
        m_root = NONE;
        return;
    }

    // The sibling takes the parent's place; the parent goes.
    const std::uint32_t parent = m_nodes[leaf].parent;
    const std::uint32_t grandparent = m_nodes[parent].parent;
    const std::uint32_t sibling = m_nodes[parent].child1 == leaf ? m_nodes[parent].child2 : m_nodes[parent].child1;
    if (grandparent == NONE)
    {
        m_root = sibling;
        m_nodes[sibling].parent = NONE;
        free_node(parent);
        return;
    }
    if (m_nodes[grandparent].child1 == parent)
    {
        m_nodes[grandparent].child1 = sibling;
    }
    else
    {
        m_nodes[grandparent].child2 = sibling;
    }
    m_nodes[sibling].parent = grandparent;
    free_node(parent);
    for (std::uint32_t index = grandparent; index != NONE; index = m_nodes[index].parent)
    {
        index = balance(index);
        const Node& node = m_nodes[index];
        m_nodes[index].height = 1 + std::max(m_nodes[node.child1].height, m_nodes[node.child2].height);
        m_nodes[index].box = m_nodes[node.child1].box.merged(m_nodes[node.child2].box);
    }
}

auto
Bvh::balance(std::uint32_t a) -> std::uint32_t
{
    // A rotation lifts the taller child when the heights of a's children differ by more than one (Box2D's Balance).
    Node& node_a = m_nodes[a];
    if (node_a.child1 == NONE || node_a.height < 2)
    {
        return a;
    }
    const std::uint32_t b = node_a.child1;
    const std::uint32_t c = node_a.child2;
    const std::int32_t balance = m_nodes[c].height - m_nodes[b].height;
    const auto rotate_up = [&](std::uint32_t up, std::uint32_t other) {
        // `up` (a child of a) takes a's place; a keeps `other` and the shorter of up's children.
        const std::uint32_t f = m_nodes[up].child1;
        const std::uint32_t g = m_nodes[up].child2;
        m_nodes[up].child1 = a;
        m_nodes[up].parent = m_nodes[a].parent;
        m_nodes[a].parent = up;
        const std::uint32_t above = m_nodes[up].parent;
        if (above == NONE)
        {
            m_root = up;
        }
        else if (m_nodes[above].child1 == a)
        {
            m_nodes[above].child1 = up;
        }
        else
        {
            m_nodes[above].child2 = up;
        }
        const bool keep_f = m_nodes[f].height > m_nodes[g].height;
        const std::uint32_t kept = keep_f ? f : g;
        const std::uint32_t moved = keep_f ? g : f;
        m_nodes[up].child2 = kept;
        if (m_nodes[a].child1 == up)
        {
            m_nodes[a].child1 = moved;
        }
        else
        {
            m_nodes[a].child2 = moved;
        }
        m_nodes[moved].parent = a;
        m_nodes[a].box = m_nodes[other].box.merged(m_nodes[moved].box);
        m_nodes[up].box = m_nodes[a].box.merged(m_nodes[kept].box);
        m_nodes[a].height = 1 + std::max(m_nodes[other].height, m_nodes[moved].height);
        m_nodes[up].height = 1 + std::max(m_nodes[a].height, m_nodes[kept].height);
        return up;
    };
    if (balance > 1)
    {
        return rotate_up(c, b);
    }
    if (balance < -1)
    {
        return rotate_up(b, c);
    }
    return a;
}

} // namespace gfx::physics
