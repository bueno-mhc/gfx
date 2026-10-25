// A backend for the world tests, built on SteppedWorld: it records every edit applied to it (with the step that applied
// it), integrates dynamic and kinematic bodies with semi-implicit Euler (gravity, forces, impulses, damping), breaks every
// joint with finite thresholds at its first step, reports the colliders whose boxes overlap as contacts, refuses six-DOF
// joints and meshes on moving bodies (capability checks when edits are recorded), and can hold a step until released or
// fail one.
export module gfx.tests.physics_world_support;

import std;
import gfx.physics;

export namespace gfx::tests
{

/// An edit as the backend applied it.
struct AppliedEdit
{
    std::size_t kind = 0;    ///< The alternative of WorldEdit.
    std::uint64_t step = 0;  ///< The step that applied it (1 for the first).
    std::string target{};    ///< The handle it names, formatted ("RigidBody(index 0, generation 1)"), or empty.
    float value = 0.0F;      ///< SetBodyLinearVelocity: the velocity's x.
};

/// The index of @p Edit among WorldEdit's alternatives.
template<class Edit>
[[nodiscard]] auto
edit_kind() -> std::size_t
{
    return physics::WorldEdit{Edit{}}.index();
}

/// The recording backend.
class RecordingBackend final : public physics::SteppedWorldBackend
{
public:
    [[nodiscard]] auto identity() const noexcept -> BackendIdentity override
    {
        return physics::PhysicsBackendKind::CUSTOM;
    }

    [[nodiscard]] auto pipeline() const noexcept -> physics::PipelineKinds override
    {
        return {};
    }

    void check(const physics::WorldEdit& edit,
               const physics::RecordedWorld& recorded) const override
    {
        using namespace physics;

        // No six-DOF joints; meshes, height fields and compounds only on static bodies.
        const auto is_static_only = [](ShapeKind kind) {
            return kind == ShapeKind::TRIANGLE_MESH || kind == ShapeKind::HEIGHT_FIELD || kind == ShapeKind::COMPOUND;
        };
        if (const auto* joint = std::get_if<edits::CreateJoint>(&edit); joint != nullptr && joint_kind(joint->description) == JointKind::SIX_DEGREES_OF_FREEDOM)
        {
            throw_unsupported(identity(), "PhysicsWorld::create_joint", PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS, "the recording backend has none",
                              "use another joint");
        }
        if (const auto* collider = std::get_if<edits::CreateCollider>(&edit);
            collider != nullptr && is_static_only(collider->description.shape->kind()) && recorded.body_kind(collider->body) != BodyKind::STATIC)
        {
            throw_unsupported(identity(), "RigidBody::add_collider", PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, "meshes go on static bodies only",
                              "use convex shapes");
        }
        if (const auto* kind = std::get_if<edits::SetBodyKind>(&edit); kind != nullptr && kind->kind != BodyKind::STATIC)
        {
            for (const ShapeKind shape : recorded.collider_shape_kinds(kind->body))
            {
                if (is_static_only(shape))
                {
                    throw_unsupported(identity(), "RigidBody::set_kind", PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, "the body has a mesh", "keep it static");
                }
            }
        }
    }

    void apply(std::span<const physics::WorldEdit> edits,
               physics::WorldSnapshot& /*pending*/) override
    {
        using namespace physics;

        // Every edit logged; forces and impulses kept for the step.
        const std::scoped_lock lock{m_mutex};
        ++m_step;
        for (const WorldEdit& edit : edits)
        {
            m_applied.push_back({.kind = edit.index(), .step = m_step, .target = std::visit([](const auto& change) { return target_of(change); }, edit), .value = 0.0F});
            if (const auto* velocity = std::get_if<edits::SetBodyLinearVelocity>(&edit))
            {
                m_applied.back().value = velocity->velocity.x;
            }
            if (const auto* force = std::get_if<edits::ApplyForce>(&edit))
            {
                m_forces[force->body] += force->force;
            }
            else if (const auto* impulse = std::get_if<edits::ApplyLinearImpulse>(&edit))
            {
                m_impulses[impulse->body] += impulse->impulse;
            }
        }
    }

    void simulate(float seconds,
                  physics::WorldSnapshot& pending) override
    {
        using namespace physics;

        // A held step waits for its release; a failing one throws.
        {
            std::unique_lock lock{m_mutex};
            m_started = true;
            m_condition.notify_all();
            m_condition.wait(lock, [this] { return !m_holding; });
            if (std::exchange(m_fail_next, false))
            {
                throw std::runtime_error("the recording backend failed this step");
            }
        }

        // Semi-implicit Euler for dynamic bodies (gravity, forces, impulses, damping); kinematic bodies follow their velocity.
        for (RigidBodyRecord& body : pending.rigid_bodies)
        {
            if (!body.handle || body.configuration.kind == BodyKind::STATIC || !body.state.awake)
            {
                continue;
            }
            if (body.configuration.kind == BodyKind::DYNAMIC)
            {
                const float inverse_mass = body.mass.mass > 0.0F ? 1.0F / body.mass.mass : 1.0F;
                const Vector3 force = m_forces[body.handle];
                const Vector3 impulse = m_impulses[body.handle];
                body.state.linear_velocity += (pending.configuration.gravity * body.configuration.gravity_scale + force * inverse_mass) * seconds + impulse * inverse_mass;
                body.state.linear_velocity = body.state.linear_velocity / (1.0F + seconds * body.configuration.linear_damping);
            }
            body.state.pose.position += body.state.linear_velocity * seconds;
            const Vector3 spin = body.state.angular_velocity * (0.5F * seconds);
            const Quaternion& turn = body.state.pose.orientation;
            const Quaternion change = Quaternion{.x = spin.x, .y = spin.y, .z = spin.z, .w = 0.0F} * turn;
            body.state.pose.orientation = normalized(Quaternion{.x = turn.x + change.x, .y = turn.y + change.y, .z = turn.z + change.z, .w = turn.w + change.w});
        }
        m_forces.clear();
        m_impulses.clear();

        // Joints with finite thresholds break at once.
        for (JointRecord& joint : pending.joints)
        {
            const JointBreaking breaking = std::visit([](const auto& description) { return description.breaking; }, joint.configuration);
            if (joint.handle && joint.state.enabled && std::isfinite(breaking.force))
            {
                joint.state = {.enabled = false, .broken = true, .force = {.x = breaking.force + 1.0F}, .torque = {}};
                pending.joint_events.push_back({.kind = JointEventKind::BROKEN, .joint = joint.handle, .force = breaking.force + 1.0F, .torque = 0.0F});
            }
        }

        // Contacts: colliders of different bodies whose boxes (of the last step) overlap.
        for (std::size_t first = 0; first < pending.colliders.size(); ++first)
        {
            for (std::size_t second = first + 1; second < pending.colliders.size(); ++second)
            {
                const ColliderRecord& a = pending.colliders[first];
                const ColliderRecord& b = pending.colliders[second];
                if (a.handle && b.handle && a.body != b.body && intersects(a.world_bounds, b.world_bounds))
                {
                    pending.contacts.push_back({.collider = a.handle, .other = b.handle, .other_body = b.body, .manifold = {}});
                }
            }
        }
        pending.statistics.pairs = static_cast<std::uint32_t>(pending.contacts.size());
    }

    // ------------------------------------------------------------------------------------------------ test controls

    /// Holds the next steps in simulate() until release().
    void hold()
    {
        const std::scoped_lock lock{m_mutex};
        m_holding = true;
        m_started = false;
    }

    /// Waits until a held step is in simulate().
    void wait_until_held()
    {
        std::unique_lock lock{m_mutex};
        m_condition.wait(lock, [this] { return m_started; });
    }

    /// Lets the held step go on.
    void release()
    {
        {
            const std::scoped_lock lock{m_mutex};
            m_holding = false;
        }
        m_condition.notify_all();
    }

    /// The next step throws in simulate().
    void fail_next_step()
    {
        const std::scoped_lock lock{m_mutex};
        m_fail_next = true;
    }

    /// The edits applied so far.
    [[nodiscard]] auto applied() const -> std::vector<AppliedEdit>
    {
        const std::scoped_lock lock{m_mutex};
        return m_applied;
    }

private:
    template<class Change>
    [[nodiscard]] static auto target_of(const Change& change) -> std::string
    {
        if constexpr (requires { change.collider; })
        {
            return std::format("{}", change.collider);
        }
        else if constexpr (requires { change.joint; })
        {
            return std::format("{}", change.joint);
        }
        else if constexpr (requires { change.trigger; })
        {
            return std::format("{}", change.trigger);
        }
        else if constexpr (requires { change.soft_body; })
        {
            return std::format("{}", change.soft_body);
        }
        else if constexpr (requires { change.body; })
        {
            return std::format("{}", change.body);
        }
        else
        {
            return {};
        }
    }

private:
    mutable std::mutex m_mutex{};
    std::condition_variable m_condition{};
    bool m_holding = false;
    bool m_started = false;
    bool m_fail_next = false;
    std::uint64_t m_step = 0;
    std::vector<AppliedEdit> m_applied{};
    std::map<physics::RigidBodyHandle, Vector3> m_forces{};
    std::map<physics::RigidBodyHandle, Vector3> m_impulses{};
};

/// A world of a recording backend, and the backend.
struct RecordingWorld
{
    std::unique_ptr<physics::PhysicsWorld> world{};
    RecordingBackend* backend = nullptr;
};

/// A world of a new recording backend as @p description says.
[[nodiscard]] inline auto
make_recording_world(const physics::PhysicsWorldDescription& description = {}) -> RecordingWorld
{
    auto backend = std::make_unique<RecordingBackend>();
    RecordingBackend* raw = backend.get();
    return {.world = std::make_unique<physics::SteppedWorld>(description, std::move(backend)), .backend = raw};
}

} // namespace gfx::tests
