// GpuXpbdBackend's contacts and events: the contacts the GPU reported after a step (contacts_report.comp) become the
// touching pairs and their ContactInfo (from the smaller collider handle to the larger), the pairs of last step whose
// bodies all rest are kept (the GPU skips them), and the differences make the BEGIN, HIT and END events; the triggers'
// overlaps (triggers_filter.comp) make ENTER, STAY and EXIT — as the reference backend reports them.
module gfx.physics.gpu_xpbd;

import std;
import gfx.interface;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.interface;
import gfx.physics.narrow_phase;
import gfx.physics.world;
import :resources;
import :narrow_phase;
import :solver;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The order of touching pairs and of contacts: by their colliders' handles.
[[nodiscard]] auto
pair_less(ColliderHandle first,
          ColliderHandle second,
          ColliderHandle other_first,
          ColliderHandle other_second) noexcept -> bool
{
    return first < other_first || (first == other_first && second < other_second);
}

/// The pair of @p first and @p second among @p pairs (sorted), or null.
[[nodiscard]] auto
find_pair(std::span<const TouchingPair> pairs,
          ColliderHandle first,
          ColliderHandle second) noexcept -> const TouchingPair*
{
    const auto found = std::ranges::lower_bound(pairs, std::pair{first, second}, [](const std::pair<ColliderHandle, ColliderHandle>& left,
                                                                                   const std::pair<ColliderHandle, ColliderHandle>& right) {
        return pair_less(left.first, left.second, right.first, right.second);
    }, [](const TouchingPair& pair) { return std::pair{pair.first, pair.second}; });
    return found != pairs.end() && found->first == first && found->second == second ? &*found : nullptr;
}

/// Whether @p body stays still without a narrow phase: static, or dynamic and asleep.
[[nodiscard]] auto
rests(const WorldSnapshot& pending,
      RigidBodyHandle body) noexcept -> bool
{
    const RigidBodyRecord* record = find(pending, body);
    return record != nullptr && (record->configuration.kind == BodyKind::STATIC || (record->configuration.kind == BodyKind::DYNAMIC && !record->state.awake));
}

/// Whether @p collider still exists and makes contact events.
[[nodiscard]] auto
makes_events(const WorldSnapshot& pending,
             ColliderHandle collider) noexcept -> bool
{
    const ColliderRecord* record = find(pending, collider);
    return record != nullptr && record->configuration.contact_events;
}

} // namespace

auto
GpuXpbdBackend::read_contacts(WorldSnapshot& pending,
                              std::uint32_t reported) -> std::uint32_t
{
    // The reports, each from the smaller collider handle to the larger (flipped when the narrow phase had them the other
    // way), sorted by pair and sub-shapes.
    m_report_readback.buffer().invalidate_mapped_range();
    const std::span<const std::byte> bytes = m_report_readback.bytes();
    const std::uint32_t count = std::min(reported, m_solver.report_capacity());
    m_reported.clear();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        GpuContactReport report{};
        std::memcpy(&report, bytes.data() + std::size_t{index} * sizeof(GpuContactReport), sizeof(report));
        if (report.first_collider >= m_colliders.size() || report.second_collider >= m_colliders.size())
        {
            continue;
        }
        const ColliderEntry& first = m_colliders[report.first_collider];
        const ColliderEntry& second = m_colliders[report.second_collider];
        if (!first.handle || !second.handle)
        {
            continue;
        }
        const bool flip = second.handle < first.handle;
        const ColliderEntry& smaller = flip ? second : first;
        const ColliderEntry& larger = flip ? first : second;
        const ContactManifold manifold = to_contact_manifold(report.manifold);
        m_reported.push_back({.first_body = smaller.body,
                              .contact = {.collider = smaller.handle, .other = larger.handle, .other_body = larger.body, .manifold = flip ? flipped(manifold) : manifold},
                              .approach_speed = report.approach_speed});
    }
    std::ranges::sort(m_reported, [](const ReportedContact& left, const ReportedContact& right) {
        const ContactInfo& a = left.contact;
        const ContactInfo& b = right.contact;
        if (a.collider != b.collider || a.other != b.other)
        {
            return pair_less(a.collider, a.other, b.collider, b.other);
        }
        return std::pair{a.manifold.sub_shape_first, a.manifold.sub_shape_second} < std::pair{b.manifold.sub_shape_first, b.manifold.sub_shape_second};
    });

    // The pairs touching now, BEGIN (and HIT) for those reported that did not touch before.
    m_touching.clear();
    m_touching_contacts.clear();
    const float hit_speed = pending.configuration.hit_event_speed;
    for (std::size_t index = 0; index < m_reported.size();)
    {
        const ContactInfo& head = m_reported[index].contact;
        std::size_t end = index + 1;
        while (end < m_reported.size() && m_reported[end].contact.collider == head.collider && m_reported[end].contact.other == head.other)
        {
            ++end;
        }
        const std::span<const ReportedContact> group = std::span<const ReportedContact>{m_reported}.subspan(index, end - index);
        const bool reported_pair = makes_events(pending, head.collider) && makes_events(pending, head.other);
        m_touching.push_back({.first = head.collider,
                              .second = head.other,
                              .first_body = m_reported[index].first_body,
                              .second_body = head.other_body,
                              .reported = reported_pair,
                              .first_contact = static_cast<std::uint32_t>(m_touching_contacts.size()),
                              .contact_count = static_cast<std::uint32_t>(group.size())});
        for (const ReportedContact& contact : group)
        {
            m_touching_contacts.push_back(contact.contact);
        }
        if (reported_pair && find_pair(m_was_touching, head.collider, head.other) == nullptr)
        {
            const ReportedContact& deepest = *std::ranges::min_element(group, {}, [](const ReportedContact& contact) { return contact.contact.manifold.deepest(); });
            const std::span<const ContactPoint> points = deepest.contact.manifold.contacts();
            const ContactPoint& point = *std::ranges::min_element(points, {}, &ContactPoint::separation);
            pending.contact_events.push_back({.kind = ContactEventKind::BEGIN,
                                              .first = head.collider,
                                              .second = head.other,
                                              .point = point.position,
                                              .normal = deepest.contact.manifold.normal,
                                              .approach_speed = deepest.approach_speed});
            const float fastest = std::ranges::max(group | std::views::transform(&ReportedContact::approach_speed));
            if (fastest >= hit_speed)
            {
                pending.contact_events.push_back({.kind = ContactEventKind::HIT,
                                                  .first = head.collider,
                                                  .second = head.other,
                                                  .point = point.position,
                                                  .normal = deepest.contact.manifold.normal,
                                                  .approach_speed = fastest});
            }
        }
        index = end;
    }

    // Last step's pairs the GPU did not report: kept while their colliders exist and their bodies all rest (the GPU skips
    // pairs of resting bodies), else ended.
    const std::size_t current = m_touching.size();
    std::uint32_t kept_points = 0;
    for (const TouchingPair& was : m_was_touching)
    {
        if (find_pair(std::span<const TouchingPair>{m_touching}.first(current), was.first, was.second) != nullptr)
        {
            continue;
        }
        if (find(pending, was.first) != nullptr && find(pending, was.second) != nullptr && rests(pending, was.first_body) && rests(pending, was.second_body))
        {
            TouchingPair kept = was;
            kept.first_contact = static_cast<std::uint32_t>(m_touching_contacts.size());
            for (std::uint32_t contact = 0; contact < was.contact_count; ++contact)
            {
                m_touching_contacts.push_back(m_was_touching_contacts[was.first_contact + contact]);
                kept_points += m_touching_contacts.back().manifold.point_count;
            }
            m_touching.push_back(kept);
        }
        else if (was.reported)
        {
            pending.contact_events.push_back({.kind = ContactEventKind::END, .first = was.first, .second = was.second, .point = {}, .normal = {}, .approach_speed = 0.0F});
        }
    }

    // The pairs sorted (their contacts stay where they are), the contacts published, this step's lists kept for the next.
    std::ranges::sort(m_touching, [](const TouchingPair& left, const TouchingPair& right) { return pair_less(left.first, left.second, right.first, right.second); });
    pending.contacts.clear();
    for (const TouchingPair& pair : m_touching)
    {
        for (std::uint32_t contact = 0; contact < pair.contact_count; ++contact)
        {
            pending.contacts.push_back(m_touching_contacts[pair.first_contact + contact]);
        }
    }
    std::swap(m_touching, m_was_touching);
    std::swap(m_touching_contacts, m_was_touching_contacts);
    return kept_points;
}

void
GpuXpbdBackend::forget_collider(ColliderHandle collider,
                                WorldSnapshot& pending)
{
    // Its touches end now; the dynamic bodies it touched wake (what rested on it may fall).
    for (const TouchingPair& pair : m_was_touching)
    {
        if (pair.first != collider && pair.second != collider)
        {
            continue;
        }
        if (pair.reported)
        {
            pending.contact_events.push_back({.kind = ContactEventKind::END, .first = pair.first, .second = pair.second, .point = {}, .normal = {}, .approach_speed = 0.0F});
        }
        const RigidBodyHandle other = pair.first == collider ? pair.second_body : pair.first_body;
        const RigidBodyRecord* record = other ? find(pending, other) : nullptr;
        if (record != nullptr && record->configuration.kind == BodyKind::DYNAMIC)
        {
            body_entry(other).woken = true;
        }
    }
    std::erase_if(m_was_touching, [collider](const TouchingPair& pair) { return pair.first == collider || pair.second == collider; });

    // The triggers it was in see it leave.
    for (TriggerEntry& trigger : m_triggers)
    {
        if (trigger.handle && std::erase(trigger.overlaps, collider) > 0)
        {
            pending.trigger_events.push_back({.kind = TriggerEventKind::EXIT, .trigger = trigger.handle, .collider = collider});
        }
    }
}

void
GpuXpbdBackend::read_triggers(WorldSnapshot& pending,
                              std::uint32_t overlaps)
{
    // The overlaps as (trigger slot, collider), sorted.
    m_overlaps.clear();
    if (m_solver.trigger_capacity() > 0)
    {
        m_trigger_readback.buffer().invalidate_mapped_range();
        const std::span<const std::byte> bytes = m_trigger_readback.bytes();
        const std::uint32_t count = std::min(overlaps, m_solver.trigger_capacity());
        for (std::uint32_t index = 0; index < count; ++index)
        {
            GpuTriggerOverlap overlap{};
            std::memcpy(&overlap, bytes.data() + std::size_t{index} * sizeof(GpuTriggerOverlap), sizeof(overlap));
            if (overlap.trigger < m_trigger_slots.size() && overlap.collider < m_colliders.size() && m_colliders[overlap.collider].handle)
            {
                m_overlaps.emplace_back(m_trigger_slots[overlap.trigger], m_colliders[overlap.collider].handle);
            }
        }
    }
    std::ranges::sort(m_overlaps);
    const auto duplicates = std::ranges::unique(m_overlaps);
    m_overlaps.erase(duplicates.begin(), duplicates.end());

    // Each trigger in slot order: EXIT, ENTER and STAY in its colliders' order.
    for (std::uint32_t trigger_slot = 0; trigger_slot < m_triggers.size(); ++trigger_slot)
    {
        TriggerEntry& trigger = m_triggers[trigger_slot];
        if (!trigger.handle)
        {
            continue;
        }
        const auto range = std::ranges::equal_range(m_overlaps, trigger_slot, {}, &std::pair<std::uint32_t, ColliderHandle>::first);
        auto now = range.begin();
        std::size_t before = 0;
        while (before < trigger.overlaps.size() || now != range.end())
        {
            if (now == range.end() || (before < trigger.overlaps.size() && trigger.overlaps[before] < now->second))
            {
                pending.trigger_events.push_back({.kind = TriggerEventKind::EXIT, .trigger = trigger.handle, .collider = trigger.overlaps[before++]});
            }
            else if (before == trigger.overlaps.size() || now->second < trigger.overlaps[before])
            {
                pending.trigger_events.push_back({.kind = TriggerEventKind::ENTER, .trigger = trigger.handle, .collider = (now++)->second});
            }
            else
            {
                pending.trigger_events.push_back({.kind = TriggerEventKind::STAY, .trigger = trigger.handle, .collider = (now++)->second});
                ++before;
            }
        }
        trigger.overlaps.clear();
        for (auto overlap = range.begin(); overlap != range.end(); ++overlap)
        {
            trigger.overlaps.push_back(overlap->second);
        }
    }
}

} // namespace gfx::physics::gpu_xpbd
