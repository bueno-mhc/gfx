// GpuXpbdBackend's contacts and events: the contacts the GPU reported after a step (contacts_report.comp) become the
// touching pairs and their ContactInfo (from the smaller collider handle to the larger), the pairs of last step whose
// bodies all rest are kept (the GPU skips them), and the differences make the BEGIN, HIT and END events; the triggers'
// overlaps (triggers_filter.comp) make ENTER, STAY and EXIT — as the reference backend reports them.
module;

#define GFX_PROFILE_CATEGORY ::gfx::profiler::ProfilerCategory::PHYSICS
#include <gfx/profiler.hpp>

module gfx.physics.gpu_xpbd;

import std;
import gfx.profiler;
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

/// Whether @p pair is the pair of @p first and @p second.
[[nodiscard]] auto
same_pair(const TouchingPair& pair,
          ColliderHandle first,
          ColliderHandle second) noexcept -> bool
{
    return pair.first == first && pair.second == second;
}

/// Sorts @p entries by pair through @p scratch, stably (a pair's entries keep their order): a byte at a time, least
/// significant first, skipping the bytes every entry has the same. (A template: this unit does not spell std::vector —
/// GCC 16 then reads a cluster of :backend's module file it cannot, "failed to load pendings for 'std::vector'".)
template<class Entries>
void
sort_by_pair(Entries& entries,
             Entries& scratch)
{
    constexpr std::size_t BYTES = sizeof(std::uint64_t);
    constexpr std::size_t VALUES = 256;
    std::array<std::array<std::uint32_t, VALUES>, BYTES> histograms{};
    for (const ReportOrder& entry : entries)
    {
        for (std::size_t byte = 0; byte < BYTES; ++byte)
        {
            ++histograms[byte][(entry.pair >> (8U * byte)) & 0xFFU];
        }
    }
    scratch.resize(entries.size());
    for (std::size_t byte = 0; byte < BYTES; ++byte)
    {
        std::array<std::uint32_t, VALUES>& counts = histograms[byte];
        if (std::ranges::any_of(counts, [&entries](std::uint32_t count) { return count == entries.size(); }))
        {
            continue;
        }
        // Each value's first place, then each entry to its place in order.
        std::uint32_t place = 0;
        for (std::uint32_t& count : counts)
        {
            place += std::exchange(count, place);
        }
        for (const ReportOrder& entry : entries)
        {
            scratch[counts[(entry.pair >> (8U * byte)) & 0xFFU]++] = entry;
        }
        entries.swap(scratch);
    }
}

} // namespace

auto
GpuXpbdBackend::read_contacts(WorldSnapshot& pending,
                              std::uint32_t reported) -> std::uint32_t
{
    // The reports' keys: each pair from the smaller collider handle to the larger (flipped when the narrow phase had it the
    // other way), sorted by pair (radix) then sub-shapes (within a pair); each report converted once, in that order.
    m_report_readback.buffer().invalidate_mapped_range();
    const std::span<const std::byte> bytes = m_report_readback.bytes();
    const std::uint32_t count = std::min(reported, m_solver.report_capacity());
    const auto report_at = [&bytes](std::uint32_t index) {
        GpuContactReport report{};
        std::memcpy(&report, bytes.data() + std::size_t{index} * sizeof(GpuContactReport), sizeof(report));
        return report;
    };
    const auto rests = [this](RigidBodyHandle body) {
        const std::uint32_t body_slot = slot(body);
        return body_slot < m_body_rests.size() && m_body_rests[body_slot] != 0;
    };

    // A report's first words: first_collider, second_collider, reserved, approach_speed, then the manifold's first,
    // second, point_count, algorithm, sub_shape_first, sub_shape_second (GpuContactReport, GpuManifold). Each report's
    // key at its index (in parallel); those of colliders gone have no pair (a null first handle) and are left out.
    constexpr std::size_t HEADER_WORDS = 10;
    PROFILE_NAMED_SCOPE(sort_scope, "GPU XPBD: sort the contact reports");
    m_report_keys.resize(count);
    const auto make_keys = [&](std::uint32_t begin, std::uint32_t end) {
        for (std::uint32_t index = begin; index < end; ++index)
        {
            ReportKey& key = m_report_keys[index];
            key = {};
            std::array<std::uint32_t, HEADER_WORDS> header{};
            std::memcpy(header.data(), bytes.data() + std::size_t{index} * sizeof(GpuContactReport), sizeof(header));
            const std::uint32_t first_slot = header[0];
            const std::uint32_t second_slot = header[1];
            if (first_slot >= m_colliders.size() || second_slot >= m_colliders.size())
            {
                continue;
            }
            const ColliderEntry& first = m_colliders[first_slot];
            const ColliderEntry& second = m_colliders[second_slot];
            if (!first.handle || !second.handle)
            {
                continue;
            }
            const bool flip = second.handle < first.handle;
            const ColliderHandle smaller = flip ? second.handle : first.handle;
            const ColliderHandle larger = flip ? first.handle : second.handle;
            key = {.pair = (ordering_value(smaller) << 32U) | ordering_value(larger),
                   .first = smaller,
                   .second = larger,
                   .first_body = flip ? second.body : first.body,
                   .second_body = flip ? first.body : second.body,
                   .sub_shape_first = flip ? header[9] : header[8],
                   .sub_shape_second = flip ? header[8] : header[9],
                   .report = index,
                   .flip = flip,
                   .reported = first.contact_events && second.contact_events,
                   .approach_speed = std::bit_cast<float>(header[3])};
        }
    };
    for_each_range(m_scheduler, count, FRAMEWORK_PARALLEL_RANGE, make_keys);
    m_report_order.clear();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (m_report_keys[index].first)
        {
            m_report_order.push_back({.pair = m_report_keys[index].pair, .key = index});
        }
    }
    sort_by_pair(m_report_order, m_report_order_scratch);
    sort_scope.end();

    // The pairs touching now: the groups of the sorted keys (a contact per key, at the key's place).
    PROFILE_NAMED_SCOPE(pairs_scope, "GPU XPBD: touching pairs");
    m_touching.clear();
    for (std::size_t index = 0; index < m_report_order.size();)
    {
        std::size_t end = index + 1;
        while (end < m_report_order.size() && m_report_order[end].pair == m_report_order[index].pair)
        {
            ++end;
        }
        if (end - index > 1)
        {
            // A pair's manifolds (compound children, triangles) by sub-shapes.
            std::sort(m_report_order.begin() + static_cast<std::ptrdiff_t>(index), m_report_order.begin() + static_cast<std::ptrdiff_t>(end),
                      [this](const ReportOrder& left, const ReportOrder& right) {
                          const ReportKey& a = m_report_keys[left.key];
                          const ReportKey& b = m_report_keys[right.key];
                          return std::tuple{a.sub_shape_first, a.sub_shape_second, a.report} < std::tuple{b.sub_shape_first, b.sub_shape_second, b.report};
                      });
        }
        const ReportKey& head = m_report_keys[m_report_order[index].key];
        m_touching.push_back({.first = head.first,
                              .second = head.second,
                              .first_body = head.first_body,
                              .second_body = head.second_body,
                              .reported = head.reported,
                              .resting = false,
                              .first_contact = static_cast<std::uint32_t>(index),
                              .contact_count = static_cast<std::uint32_t>(end - index)});
        index = end;
    }

    // Last step's pairs this step did not report are carried over when the GPU skipped them: their bodies all rested after
    // the last step (their contacts were kept) and still rest, their colliders exist. Whether any is decides where the
    // contacts are converted (this step's and last step's pairs walked side by side).
    const std::size_t current = m_touching.size();
    const auto carried = [&](const TouchingPair& was) {
        return was.resting && find(pending, was.first) != nullptr && find(pending, was.second) != nullptr && rests(was.first_body) && rests(was.second_body);
    };
    bool any_carried = false;
    {
        std::size_t now_index = 0;
        for (const TouchingPair& was : m_was_touching)
        {
            while (now_index < current && pair_less(m_touching[now_index].first, m_touching[now_index].second, was.first, was.second))
            {
                ++now_index;
            }
            if (!(now_index < current && same_pair(m_touching[now_index], was.first, was.second)) && carried(was))
            {
                any_carried = true;
                break;
            }
        }
    }

    // Each report converted at its place (in parallel: each writes its own contact): into the published list itself when
    // nothing is carried over (its elements reused: replaces_contacts()), else into the backend's list, merged below.
    auto& converted = any_carried ? m_touching_contacts : pending.contacts;
    converted.resize(m_report_order.size());
    const auto convert = [&](std::uint32_t begin, std::uint32_t end) {
        for (std::uint32_t place = begin; place < end; ++place)
        {
            const ReportKey& key = m_report_keys[m_report_order[place].key];
            const ContactManifold manifold = to_contact_manifold(report_at(key.report).manifold);
            converted[place] = {.collider = key.first, .other = key.second, .other_body = key.second_body, .manifold = key.flip ? flipped(manifold) : manifold};
        }
    };
    for_each_range(m_scheduler, m_report_order.size(), FRAMEWORK_PARALLEL_RANGE, convert);

    // BEGIN (and HIT) for the pairs reported that did not touch last step: at the deepest point of their deepest manifold.
    const float hit_speed = pending.configuration.hit_event_speed;
    std::size_t was_index = 0;
    for (std::size_t pair_index = 0; pair_index < current; ++pair_index)
    {
        const TouchingPair& pair = m_touching[pair_index];
        while (was_index < m_was_touching.size() && pair_less(m_was_touching[was_index].first, m_was_touching[was_index].second, pair.first, pair.second))
        {
            ++was_index;
        }
        if (!pair.reported || (was_index < m_was_touching.size() && same_pair(m_was_touching[was_index], pair.first, pair.second)))
        {
            continue;
        }
        std::uint32_t deepest = pair.first_contact;
        float deepest_separation = std::numeric_limits<float>::infinity();
        float fastest = -std::numeric_limits<float>::infinity();
        for (std::uint32_t place = pair.first_contact; place < pair.first_contact + pair.contact_count; ++place)
        {
            const float separation = converted[place].manifold.deepest();
            if (separation < deepest_separation)
            {
                deepest_separation = separation;
                deepest = place;
            }
            fastest = std::max(fastest, m_report_keys[m_report_order[place].key].approach_speed);
        }
        const ContactInfo& deepest_contact = converted[deepest];
        const std::span<const ContactPoint> points = deepest_contact.manifold.contacts();
        const ContactPoint& point = *std::ranges::min_element(points, {}, &ContactPoint::separation);
        pending.contact_events.push_back({.kind = ContactEventKind::BEGIN,
                                          .first = pair.first,
                                          .second = pair.second,
                                          .point = point.position,
                                          .normal = deepest_contact.manifold.normal,
                                          .approach_speed = m_report_keys[m_report_order[deepest].key].approach_speed});
        if (fastest >= hit_speed)
        {
            pending.contact_events.push_back({.kind = ContactEventKind::HIT,
                                              .first = pair.first,
                                              .second = pair.second,
                                              .point = point.position,
                                              .normal = deepest_contact.manifold.normal,
                                              .approach_speed = fastest});
        }
    }
    pairs_scope.end();

    // Last step's pairs this step did not report: carried over (with their contacts) or ended.
    PROFILE_NAMED_SCOPE(kept_scope, "GPU XPBD: kept and ended pairs");
    std::uint32_t kept_points = 0;
    std::size_t now_index = 0;
    for (const TouchingPair& was : m_was_touching)
    {
        while (now_index < current && pair_less(m_touching[now_index].first, m_touching[now_index].second, was.first, was.second))
        {
            ++now_index;
        }
        if (now_index < current && same_pair(m_touching[now_index], was.first, was.second))
        {
            continue;
        }
        if (carried(was))
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
    kept_scope.end();

    // With pairs carried over: the pairs sorted (the carried ones were appended) and their contacts copied in that order.
    PROFILE_NAMED_SCOPE(publish_scope, "GPU XPBD: publish the contacts");
    if (any_carried)
    {
        std::ranges::sort(m_touching, [](const TouchingPair& left, const TouchingPair& right) { return pair_less(left.first, left.second, right.first, right.second); });
        pending.contacts.clear();
        pending.contacts.reserve(m_touching_contacts.size());
        for (TouchingPair& pair : m_touching)
        {
            const auto first = static_cast<std::uint32_t>(pending.contacts.size());
            for (std::uint32_t contact = 0; contact < pair.contact_count; ++contact)
            {
                pending.contacts.push_back(m_touching_contacts[pair.first_contact + contact]);
            }
            pair.first_contact = first;
        }
    }
    publish_scope.end();

    // What the next step keeps of these pairs: the contacts of those whose bodies all rest now (the GPU skips them).
    PROFILE_SCOPE("GPU XPBD: keep the resting pairs");
    m_was_touching_contacts.clear();
    for (TouchingPair& pair : m_touching)
    {
        pair.resting = rests(pair.first_body) && rests(pair.second_body);
        if (pair.resting)
        {
            const auto first = static_cast<std::uint32_t>(m_was_touching_contacts.size());
            const auto from = pending.contacts.begin() + static_cast<std::ptrdiff_t>(pair.first_contact);
            m_was_touching_contacts.insert(m_was_touching_contacts.end(), from, from + static_cast<std::ptrdiff_t>(pair.contact_count));
            pair.first_contact = first;
        }
    }
    std::swap(m_touching, m_was_touching);
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
