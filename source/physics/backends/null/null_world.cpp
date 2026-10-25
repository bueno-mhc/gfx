// The null backend's world: every operation that would make or change something throws the BackendCapabilityError of
// PhysicsCapability::WORLD_OBJECTS, queries fail as UNSUPPORTED, and the getters report a world that never stepped.
module gfx.physics.backends.null;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.interface;

namespace gfx::physics::null_backend
{

namespace
{

class NullWorld final : public PhysicsWorld
{
public:
    explicit NullWorld(const PhysicsWorldDescription& description)
        : PhysicsWorld{PhysicsBackendKind::NULL_BACKEND}
        , m_handle{issue_world_handle()}
        , m_description{description}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> WorldHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto description() const -> PhysicsWorldDescription override
    {
        return m_description;
    }

    [[nodiscard]] auto create_rigid_body(const RigidBodyDescription& /*description*/) -> std::unique_ptr<RigidBody> override
    {
        refuse("PhysicsWorld::create_rigid_body");
    }

    [[nodiscard]] auto create_soft_body(const SoftBodyDescription& /*description*/) -> std::unique_ptr<SoftBody> override
    {
        refuse("PhysicsWorld::create_soft_body");
    }

    [[nodiscard]] auto create_joint(const JointDescription& /*description*/) -> std::unique_ptr<Joint> override
    {
        refuse("PhysicsWorld::create_joint");
    }

    [[nodiscard]] auto create_constraint(const ConstraintDescription& /*description*/) -> std::unique_ptr<Constraint> override
    {
        refuse("PhysicsWorld::create_constraint");
    }

    [[nodiscard]] auto create_trigger(const TriggerDescription& /*description*/) -> std::unique_ptr<Trigger> override
    {
        refuse("PhysicsWorld::create_trigger");
    }

    void set_gravity(Vector3 /*gravity*/) override
    {
        refuse("PhysicsWorld::set_gravity");
    }

    void set_solver_description(const SolverDescription& /*description*/) override
    {
        refuse("PhysicsWorld::set_solver_description");
    }

    void set_sleep_description(const SleepDescription& /*description*/) override
    {
        refuse("PhysicsWorld::set_sleep_description");
    }

    void step(float /*seconds*/) override
    {
        refuse("PhysicsWorld::step");
    }

    void begin_step(float /*seconds*/) override
    {
        refuse("PhysicsWorld::begin_step");
    }

    void wait_step() override
    {
        // No step ever runs.
    }

    [[nodiscard]] auto is_stepping() const noexcept -> bool override
    {
        return false;
    }

    [[nodiscard]] auto step_count() const noexcept -> std::uint64_t override
    {
        return 0;
    }

    [[nodiscard]] auto events() const -> PhysicsEvents override
    {
        return {};
    }

    [[nodiscard]] auto statistics() const -> StepStatistics override
    {
        return {};
    }

    [[nodiscard]] auto ray_cast(const Ray& /*ray*/,
                                float /*maximum*/,
                                const QueryOptions& /*options*/) const noexcept -> QueryResult<RayHit> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::ray_cast"));
    }

    [[nodiscard]] auto ray_cast_all(const Ray& /*ray*/,
                                    float /*maximum*/,
                                    std::span<RayHit> /*output*/,
                                    const QueryOptions& /*options*/) const noexcept -> QueryResult<std::span<RayHit>> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::ray_cast_all"));
    }

    [[nodiscard]] auto shape_cast(const Shape& /*shape*/,
                                  const Transform& /*pose*/,
                                  Vector3 /*translation*/,
                                  const QueryOptions& /*options*/) const noexcept -> QueryResult<ShapeCastHit> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::shape_cast"));
    }

    [[nodiscard]] auto overlap(const Shape& /*shape*/,
                               const Transform& /*pose*/,
                               std::span<ColliderHandle> /*output*/,
                               const QueryOptions& /*options*/) const noexcept -> QueryResult<std::span<ColliderHandle>> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::overlap"));
    }

    [[nodiscard]] auto overlap(const Aabb& /*box*/,
                               std::span<ColliderHandle> /*output*/,
                               const QueryOptions& /*options*/) const noexcept -> QueryResult<std::span<ColliderHandle>> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::overlap"));
    }

    [[nodiscard]] auto closest_point(Vector3 /*point*/,
                                     float /*maximum*/,
                                     const QueryOptions& /*options*/) const noexcept -> QueryResult<ClosestPointHit> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::closest_point"));
    }

    [[nodiscard]] auto contacts_of(RigidBodyHandle /*body*/,
                                   std::span<ContactInfo> /*output*/) const noexcept -> QueryResult<std::span<ContactInfo>> override
    {
        return std::unexpected(unsupported_query("PhysicsWorld::contacts_of"));
    }

    [[nodiscard]] auto broad_phase() -> BroadPhase& override
    {
        refuse("PhysicsWorld::broad_phase");
    }

    [[nodiscard]] auto narrow_phase() -> NarrowPhase& override
    {
        refuse("PhysicsWorld::narrow_phase");
    }

    [[nodiscard]] auto solver() -> Solver& override
    {
        refuse("PhysicsWorld::solver");
    }

    [[nodiscard]] auto integrator() -> Integrator& override
    {
        refuse("PhysicsWorld::integrator");
    }

    void replace_broad_phase(std::unique_ptr<BroadPhase> /*broad_phase*/) override
    {
        refuse("PhysicsWorld::replace_broad_phase");
    }

    void replace_narrow_phase(std::unique_ptr<NarrowPhase> /*narrow_phase*/) override
    {
        refuse("PhysicsWorld::replace_narrow_phase");
    }

    void replace_solver(std::unique_ptr<Solver> /*solver*/) override
    {
        refuse("PhysicsWorld::replace_solver");
    }

    void replace_integrator(std::unique_ptr<Integrator> /*integrator*/) override
    {
        refuse("PhysicsWorld::replace_integrator");
    }

    [[nodiscard]] auto soft_body_integrator() -> Integrator& override
    {
        refuse("PhysicsWorld::soft_body_integrator");
    }

    void replace_soft_body_integrator(std::unique_ptr<Integrator> /*integrator*/) override
    {
        refuse("PhysicsWorld::replace_soft_body_integrator");
    }

private:
    [[noreturn]] void refuse(std::string_view operation) const
    {
        throw_unsupported(operation, PhysicsCapability::WORLD_OBJECTS, "worlds of the null backend have no objects, steps or pipeline",
                          "create the world with a simulating backend (PhysicsBackendKind::BOX3D or REFERENCE), or write one on SteppedWorld "
                          "(gfx.physics.world)",
                          {.world = m_handle});
    }

private:
    WorldHandle m_handle{};
    PhysicsWorldDescription m_description{};
};

} // namespace

auto
unavailable_reason() -> std::string
{
    return {};
}

auto
create_physics_world(const PhysicsWorldDescription& description) -> std::unique_ptr<PhysicsWorld>
{
    return std::make_unique<NullWorld>(description);
}

} // namespace gfx::physics::null_backend
