// The BSP tree's changes: placing a proxy (down to the node whose plane cuts it, or to a leaf), splitting full leaves
// at the plane of least surface-area cost, and rebuilding after as many changes as proxies.
module gfx.physics.spatial;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;

namespace gfx::physics
{

using spatial_detail::NONE;

namespace
{

[[nodiscard]] auto
component(Vector3 vector,
          std::uint32_t axis) noexcept -> float
{
    return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
}

/// Bins of a split search.
constexpr std::uint32_t BIN_COUNT = 16;

} // namespace

Bsp::Bsp()
{
    m_nodes.push_back(Node{});
}

auto
Bsp::insert(ColliderHandle key,
            const Aabb& box) -> SpatialProxyHandle
{
    const auto [handle, slot] = m_proxies.add({.key = key, .box = box});
    place(slot);
    if (++m_changes > std::max<std::uint32_t>(64, m_proxies.size()) || m_unbalanced)
    {
        rebuild();
    }
    return handle;
}

void
Bsp::remove(SpatialProxyHandle proxy)
{
    const std::uint32_t slot = m_proxies.slot_of(proxy, "Bsp::remove");
    unlink(slot);
    m_proxies.release(slot);
    if (++m_changes > std::max<std::uint32_t>(64, m_proxies.size()))
    {
        rebuild();
    }
}

auto
Bsp::update(SpatialProxyHandle proxy,
            const Aabb& box) -> bool
{
    const std::uint32_t slot = m_proxies.slot_of(proxy, "Bsp::update");
    const std::uint32_t before = m_proxies[slot].node;
    unlink(slot);
    m_proxies[slot].box = box;
    place(slot);
    const bool moved = m_proxies[slot].node != before;
    if ((moved && ++m_changes > std::max<std::uint32_t>(64, m_proxies.size())) || m_unbalanced)
    {
        rebuild();
    }
    return moved;
}

void
Bsp::clear()
{
    m_proxies.clear();
    m_nodes.assign(1, Node{});
    m_changes = 0;
    m_unbalanced = false;
}

auto
Bsp::depth() const noexcept -> std::uint32_t
{
    std::uint32_t deepest = 0;
    for (const Node& node : m_nodes)
    {
        deepest = std::max(deepest, node.depth);
    }
    return deepest;
}

void
Bsp::link(std::uint32_t slot,
          std::uint32_t node)
{
    Proxy& proxy = m_proxies[slot];
    proxy.node = node;
    proxy.previous = NONE;
    proxy.next = m_nodes[node].head;
    if (proxy.next != NONE)
    {
        m_proxies[proxy.next].previous = slot;
    }
    m_nodes[node].head = slot;
    ++m_nodes[node].count;
}

void
Bsp::unlink(std::uint32_t slot)
{
    const Proxy& proxy = m_proxies[slot];
    Node& node = m_nodes[proxy.node];
    if (proxy.previous != NONE)
    {
        m_proxies[proxy.previous].next = proxy.next;
    }
    else
    {
        node.head = proxy.next;
    }
    if (proxy.next != NONE)
    {
        m_proxies[proxy.next].previous = proxy.previous;
    }
    --node.count;
}

void
Bsp::place(std::uint32_t slot)
{
    // Down the planes the box is wholly on one side of; every node on the way grows around it.
    const Aabb box = m_proxies[slot].box;
    std::uint32_t index = 0;
    while (true)
    {
        m_nodes[index].bounds = m_nodes[index].bounds.merged(box);
        const Node& node = m_nodes[index];
        if (node.axis == 3)
        {
            break;
        }
        if (component(box.maximum, node.axis) <= node.split)
        {
            index = node.first;
        }
        else if (component(box.minimum, node.axis) >= node.split)
        {
            index = node.first + 1;
        }
        else
        {
            break;
        }
    }
    link(slot, index);
    if (m_nodes[index].axis == 3 && m_nodes[index].count > LEAF_CAPACITY && m_nodes[index].depth < MAXIMUM_DEPTH
        && m_nodes[index].count >= 2 * m_nodes[index].split_attempt)
    {
        // A split beyond about twice a balanced tree's depth (inserts in sorted order grow one side): rebuild instead.
        const std::uint32_t limit = 2 * static_cast<std::uint32_t>(std::bit_width(m_proxies.size() / LEAF_CAPACITY + 1)) + 4;
        if (m_nodes[index].depth + 1 > limit)
        {
            m_unbalanced = true;
        }
        else
        {
            split(index);
        }
    }
}

void
Bsp::split(std::uint32_t index)
{
    // The binned surface-area cost of each candidate plane: proxies below, above, and cut (they stay here).
    m_nodes[index].split_attempt = m_nodes[index].count;
    Aabb centers = Aabb::empty();
    for (std::uint32_t slot = m_nodes[index].head; slot != NONE; slot = m_proxies[slot].next)
    {
        centers = centers.merged(m_proxies[slot].box.center());
    }
    const Vector3 spread = centers.extents();
    const std::uint32_t axis = spread.x >= spread.y && spread.x >= spread.z ? 0 : spread.y >= spread.z ? 1 : 2;
    const float low = component(centers.minimum, axis);
    const float width = component(spread, axis);
    if (!(width > 0.0F))
    {
        return;
    }
    float best_cost = m_nodes[index].bounds.surface_area() * static_cast<float>(m_nodes[index].count);
    float best_split = 0.0F;
    bool found = false;
    for (std::uint32_t bin = 1; bin < BIN_COUNT; ++bin)
    {
        const float plane = low + width * static_cast<float>(bin) / static_cast<float>(BIN_COUNT);
        Aabb below = Aabb::empty();
        Aabb above = Aabb::empty();
        std::uint32_t below_count = 0;
        std::uint32_t above_count = 0;
        std::uint32_t cut = 0;
        for (std::uint32_t slot = m_nodes[index].head; slot != NONE; slot = m_proxies[slot].next)
        {
            const Aabb& box = m_proxies[slot].box;
            if (component(box.maximum, axis) <= plane)
            {
                below = below.merged(box);
                ++below_count;
            }
            else if (component(box.minimum, axis) >= plane)
            {
                above = above.merged(box);
                ++above_count;
            }
            else
            {
                ++cut;
            }
        }
        if (below_count == 0 || above_count == 0)
        {
            continue;
        }
        const float cost = below.surface_area() * static_cast<float>(below_count) + above.surface_area() * static_cast<float>(above_count)
                           + m_nodes[index].bounds.surface_area() * static_cast<float>(cut);
        if (cost < best_cost)
        {
            best_cost = cost;
            best_split = plane;
            found = true;
        }
    }
    if (!found)
    {
        return;
    }

    // Two leaves below; the node's proxies placed again from it (those the plane cuts stay).
    const auto children = static_cast<std::uint32_t>(m_nodes.size());
    const std::uint32_t child_depth = m_nodes[index].depth + 1;
    m_nodes.push_back(Node{.depth = child_depth});
    m_nodes.push_back(Node{.depth = child_depth});
    Node& node = m_nodes[index];
    node.axis = axis;
    node.split = best_split;
    node.first = children;
    std::uint32_t slot = node.head;
    node.head = NONE;
    node.count = 0;
    while (slot != NONE)
    {
        const std::uint32_t next = m_proxies[slot].next;
        const Aabb& box = m_proxies[slot].box;
        std::uint32_t target = index;
        if (component(box.maximum, axis) <= best_split)
        {
            target = children;
        }
        else if (component(box.minimum, axis) >= best_split)
        {
            target = children + 1;
        }
        m_nodes[target].bounds = m_nodes[target].bounds.merged(box);
        link(slot, target);
        slot = next;
    }
    for (const std::uint32_t child : {children, children + 1})
    {
        if (m_nodes[child].count > LEAF_CAPACITY && m_nodes[child].depth < MAXIMUM_DEPTH)
        {
            split(child);
        }
    }
}

void
Bsp::rebuild()
{
    // Every proxy back into a fresh root, which splits as it fills.
    m_nodes.assign(1, Node{});
    m_changes = 0;
    m_unbalanced = false;
    for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
    {
        if (m_proxies.alive(slot))
        {
            m_nodes[0].bounds = m_nodes[0].bounds.merged(m_proxies[slot].box);
            link(slot, 0);
        }
    }
    if (m_nodes[0].count > LEAF_CAPACITY)
    {
        split(0);
    }
}

} // namespace gfx::physics
