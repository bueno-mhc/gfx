module gfx.physics.backends.reference;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.world;
import :backend;

namespace gfx::physics::reference_backend
{

namespace
{

/// Pairs a task of the parallel narrow phase collides.
constexpr std::uint32_t PAIRS_PER_TASK = 32;

/// The manifolds of @p pair among @p manifolds (sorted by pair).
[[nodiscard]] auto
manifolds_of(std::span<const PairManifold> manifolds,
             const BroadPhasePair& pair) noexcept -> std::span<const PairManifold>
{
    const auto range = std::ranges::equal_range(manifolds, pair, {}, &PairManifold::pair);
    return {range.begin(), range.end()};
}

/// Whether the pair's manifolds have a point closer than the slop.
[[nodiscard]] auto
touches(std::span<const PairManifold> manifolds) noexcept -> bool
{
    return std::ranges::any_of(manifolds, [](const PairManifold& manifold) { return manifold.manifold.point_count > 0 && manifold.manifold.deepest() < LINEAR_SLOP; });
}

} // namespace

// ===================================================================================================== the narrow phase

void
ReferenceBackend::find_contacts(float seconds,
                                const WorldSnapshot& pending)
{
    // Last step's manifolds stay for pairs whose bodies all rest (asleep, static, still kinematic).
    m_previous.swap(m_manifolds);
    m_manifolds.clear();
    const std::span<const BroadPhasePair> pairs = m_broad_phase->pairs();
    const auto chunks = static_cast<std::uint32_t>((pairs.size() + PAIRS_PER_TASK - 1) / PAIRS_PER_TASK);
    if (m_collectors.size() < chunks)
    {
        m_collectors.resize(chunks);
        m_chunk_manifolds.resize(chunks);
    }

    // Each chunk of pairs collides into its own collector and list (the narrow phase is pure).
    const CollisionSettings base = pending.configuration.collision;
    const auto collide_chunk = [&](std::uint32_t chunk) {
        std::vector<PairManifold>& output = m_chunk_manifolds[chunk];
        ContactCollector& collector = m_collectors[chunk];
        output.clear();
        const std::size_t end = std::min(pairs.size(), static_cast<std::size_t>(chunk + 1) * PAIRS_PER_TASK);
        for (std::size_t index = static_cast<std::size_t>(chunk) * PAIRS_PER_TASK; index < end; ++index)
        {
            // Two colliders of different bodies, one of them moving, not kept apart by a joint.
            const BroadPhasePair& pair = pairs[index];
            const ColliderRecord* first = find(pending, pair.first);
            const ColliderRecord* second = find(pending, pair.second);
            const RigidBodyRecord* first_body = first != nullptr ? find(pending, first->body) : nullptr;
            const RigidBodyRecord* second_body = second != nullptr ? find(pending, second->body) : nullptr;
            if (first_body == nullptr || second_body == nullptr || first->body == second->body)
            {
                continue;
            }
            if (first_body->configuration.kind != BodyKind::DYNAMIC && second_body->configuration.kind != BodyKind::DYNAMIC)
            {
                continue;
            }
            if (joined(first->body, second->body))
            {
                continue;
            }
            const auto moving = [](const RigidBodyRecord& body) {
                return body.configuration.kind == BodyKind::DYNAMIC ? body.state.awake
                                                                    : body.configuration.kind == BodyKind::KINEMATIC && (body.state.linear_velocity != Vector3{} || body.state.angular_velocity != Vector3{});
            };
            if (!moving(*first_body) && !moving(*second_body))
            {
                for (const PairManifold& kept : manifolds_of(m_previous, pair))
                {
                    output.push_back(kept);
                }
                continue;
            }

            // Speculative contacts reach as far as continuous bodies move toward each other.
            const SolverBody& a = m_solver_bodies[slot(first->body)];
            const SolverBody& b = m_solver_bodies[slot(second->body)];
            CollisionSettings settings = base;
            if (first_body->configuration.motion_quality == MotionQuality::CONTINUOUS || second_body->configuration.motion_quality == MotionQuality::CONTINUOUS)
            {
                settings.speculative_distance += length(b.linear_velocity - a.linear_velocity) * seconds;
            }
            m_narrow_phase->collide({.shape = first->configuration.shape.get(), .pose = world_pose(pending, *first)},
                                    {.shape = second->configuration.shape.get(), .pose = world_pose(pending, *second)}, collector, settings);
            for (const ContactManifold& manifold : collector.manifolds())
            {
                // How fast the deepest point closes, before the solve.
                float approach = 0.0F;
                float deepest = std::numeric_limits<float>::max();
                for (const ContactPoint& point : manifold.contacts())
                {
                    if (point.separation < deepest)
                    {
                        deepest = point.separation;
                        const Vector3 relative = (b.linear_velocity + cross(b.angular_velocity, point.position - b.center)) - (a.linear_velocity + cross(a.angular_velocity, point.position - a.center));
                        approach = -dot(relative, manifold.normal);
                    }
                }
                output.push_back({.pair = pair, .manifold = manifold, .approach_speed = approach});
            }
        }
    };
    if (m_scheduler != nullptr && m_scheduler->concurrency() > 1 && chunks > 1)
    {
        const auto* shared = &collide_chunk;
        m_scheduler->parallel_for(chunks, 1, [shared](std::uint32_t first, std::uint32_t last) {
            for (std::uint32_t chunk = first; chunk < last; ++chunk)
            {
                (*shared)(chunk);
            }
        });
    }
    else
    {
        for (std::uint32_t chunk = 0; chunk < chunks; ++chunk)
        {
            collide_chunk(chunk);
        }
    }

    // The chunks in order; last step's impulses for the points that came back (warm starting).
    for (std::uint32_t chunk = 0; chunk < chunks; ++chunk)
    {
        m_manifolds.insert(m_manifolds.end(), m_chunk_manifolds[chunk].begin(), m_chunk_manifolds[chunk].end());
    }
    for (PairManifold& manifold : m_manifolds)
    {
        (void)m_manifold_cache.update({.first = manifold.pair.first, .second = manifold.pair.second, .sub_shape_first = manifold.manifold.sub_shape_first,
                                       .sub_shape_second = manifold.manifold.sub_shape_second},
                                      manifold.manifold);
    }
}

// ===================================================================================================== contact events

void
ReferenceBackend::report_contacts(WorldSnapshot& pending)
{
    // The touching pairs (a point within the slop), their contacts, BEGIN (and HIT) for those that start.
    m_touching.clear();
    const float hit_speed = pending.configuration.hit_event_speed;
    for (std::size_t index = 0; index < m_manifolds.size();)
    {
        const BroadPhasePair pair = m_manifolds[index].pair;
        const std::span<const PairManifold> manifolds = manifolds_of(std::span<const PairManifold>{m_manifolds}.subspan(index), pair);
        index += manifolds.size();
        if (!touches(manifolds))
        {
            continue;
        }
        const ColliderRecord* first = find(pending, pair.first);
        const ColliderRecord* second = find(pending, pair.second);
        const bool reported = first->configuration.contact_events && second->configuration.contact_events;
        m_touching.push_back({.pair = pair, .reported = reported});
        for (const PairManifold& manifold : manifolds)
        {
            pending.contacts.push_back({.collider = pair.first, .other = pair.second, .other_body = second->body, .manifold = manifold.manifold});
        }
        if (!reported || std::ranges::binary_search(m_was_touching, pair, {}, &TouchingPair::pair))
        {
            continue;
        }
        const PairManifold& deepest = *std::ranges::min_element(manifolds, {}, [](const PairManifold& manifold) { return manifold.manifold.deepest(); });
        const ContactPoint& point = *std::ranges::min_element(deepest.manifold.contacts(), {}, &ContactPoint::separation);
        pending.contact_events.push_back({.kind = ContactEventKind::BEGIN, .first = pair.first, .second = pair.second, .point = point.position, .normal = deepest.manifold.normal,
                                          .approach_speed = deepest.approach_speed});
        const float fastest = std::ranges::max(manifolds | std::views::transform(&PairManifold::approach_speed));
        if (fastest >= hit_speed)
        {
            pending.contact_events.push_back({.kind = ContactEventKind::HIT, .first = pair.first, .second = pair.second, .point = point.position, .normal = deepest.manifold.normal,
                                              .approach_speed = fastest});
        }
    }

    // END for the reported pairs that no longer touch.
    for (const TouchingPair& was : m_was_touching)
    {
        if (was.reported && !std::ranges::binary_search(m_touching, was.pair, {}, &TouchingPair::pair))
        {
            pending.contact_events.push_back({.kind = ContactEventKind::END, .first = was.pair.first, .second = was.pair.second, .point = {}, .normal = {}, .approach_speed = 0.0F});
        }
    }
    m_was_touching.swap(m_touching);
    pending.statistics.contacts = 0;
    for (const ContactInfo& contact : pending.contacts)
    {
        pending.statistics.contacts += contact.manifold.point_count;
    }
}

void
ReferenceBackend::forget_collider(ColliderHandle collider,
                                  WorldSnapshot& pending)
{
    // Its touches end now (the bodies it touched wake), its manifolds go, the triggers it was in see it leave.
    const auto involves = [collider](const BroadPhasePair& pair) { return pair.first == collider || pair.second == collider; };
    for (const TouchingPair& touching : m_was_touching)
    {
        if (!involves(touching.pair))
        {
            continue;
        }
        if (touching.reported)
        {
            pending.contact_events.push_back({.kind = ContactEventKind::END, .first = touching.pair.first, .second = touching.pair.second, .point = {}, .normal = {}, .approach_speed = 0.0F});
        }
        const ColliderHandle other = touching.pair.first == collider ? touching.pair.second : touching.pair.first;
        const RigidBodyHandle body = m_colliders[slot(other)].body;
        if (body && slot(body) < m_bodies.size())
        {
            m_bodies[slot(body)].woken = true;
        }
    }
    std::erase_if(m_was_touching, [&involves](const TouchingPair& touching) { return involves(touching.pair); });
    std::erase_if(m_manifolds, [&involves](const PairManifold& manifold) { return involves(manifold.pair); });
    for (TriggerEntry& trigger : m_triggers)
    {
        if (trigger.handle && std::erase(trigger.overlaps, collider) > 0)
        {
            pending.trigger_events.push_back({.kind = TriggerEventKind::EXIT, .trigger = trigger.handle, .collider = collider});
        }
    }
}

// ===================================================================================================== triggers

void
ReferenceBackend::update_triggers(WorldSnapshot& pending)
{
    if (std::ranges::none_of(m_triggers, [](const TriggerEntry& trigger) { return trigger.handle.is_valid(); }))
    {
        return;
    }

    // The colliders' boxes where the step left them.
    for (ColliderEntry& entry : m_colliders)
    {
        if (const ColliderRecord* record = entry.handle ? find(pending, entry.handle) : nullptr)
        {
            entry.box = collider_box(pending, *record);
        }
    }

    // Each trigger: the colliders (not on its own body) it overlaps; ENTER, STAY and EXIT in the colliders' order.
    for (TriggerEntry& trigger : m_triggers)
    {
        const TriggerRecord* record = trigger.handle ? find(pending, trigger.handle) : nullptr;
        if (record == nullptr)
        {
            continue;
        }
        const TriggerDescription& description = record->configuration;
        const RigidBodyRecord* owner = description.body ? find(pending, description.body) : nullptr;
        const Transform pose = owner != nullptr ? owner->state.pose * description.pose : description.pose;
        const Shape& shape = *description.shape;
        const Aabb box = shape.bounds(pose);
        m_overlaps.clear();
        for (const ColliderEntry& entry : m_colliders)
        {
            if (!entry.handle || (description.body && entry.body == description.body) || !intersects(box, entry.box))
            {
                continue;
            }
            const ColliderRecord* collider = find(pending, entry.handle);
            if (collider == nullptr || !should_collide(description.filter, collider->configuration.filter))
            {
                continue;
            }
            const Shape& other = *collider->configuration.shape;
            const Transform other_pose = world_pose(pending, *collider);
            const bool overlap = shape.is_convex()   ? shapes_overlap(shape, pose, other, other_pose)
                                 : other.is_convex() ? shapes_overlap(other, other_pose, shape, pose)
                                                     : true;
            if (overlap)
            {
                m_overlaps.push_back(entry.handle);
            }
        }
        std::ranges::sort(m_overlaps);
        std::size_t old_index = 0;
        std::size_t new_index = 0;
        while (old_index < trigger.overlaps.size() || new_index < m_overlaps.size())
        {
            if (new_index == m_overlaps.size() || (old_index < trigger.overlaps.size() && trigger.overlaps[old_index] < m_overlaps[new_index]))
            {
                pending.trigger_events.push_back({.kind = TriggerEventKind::EXIT, .trigger = trigger.handle, .collider = trigger.overlaps[old_index++]});
            }
            else if (old_index == trigger.overlaps.size() || m_overlaps[new_index] < trigger.overlaps[old_index])
            {
                pending.trigger_events.push_back({.kind = TriggerEventKind::ENTER, .trigger = trigger.handle, .collider = m_overlaps[new_index++]});
            }
            else
            {
                pending.trigger_events.push_back({.kind = TriggerEventKind::STAY, .trigger = trigger.handle, .collider = m_overlaps[new_index]});
                ++old_index;
                ++new_index;
            }
        }
        trigger.overlaps.assign(m_overlaps.begin(), m_overlaps.end());
    }
}

} // namespace gfx::physics::reference_backend
