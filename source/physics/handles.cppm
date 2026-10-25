/// @file handles.cppm
/// @brief The handles of the physics and camera libraries: one tag per resource kind, so kinds cannot be mixed
///        (comparing a RigidBodyHandle with a ColliderHandle does not compile). Handle<Tag> comes from
///        gfx.core.handles, which this module re-exports: 32 bits (a slot index and a generation that detects stale
///        handles), issued by the object that owns the resource (a world, a spatial structure, a camera factory),
///        trivially copyable, comparable, hashable and formattable ("RigidBody(index 12, generation 3)"). No handle
///        holds a pointer or a lookup structure; backends map them to their own objects (Box3D's ids) privately.
export module gfx.physics.handles;

import std;
export import gfx.core.handles;

export namespace gfx::physics
{

// ================================================================================================= Tags

/// @brief Tag of WorldHandle.
struct WorldTag
{
    static constexpr std::string_view NAME = "World";
};

/// @brief Tag of RigidBodyHandle.
struct RigidBodyTag
{
    static constexpr std::string_view NAME = "RigidBody";
};

/// @brief Tag of SoftBodyHandle.
struct SoftBodyTag
{
    static constexpr std::string_view NAME = "SoftBody";
};

/// @brief Tag of ColliderHandle.
struct ColliderTag
{
    static constexpr std::string_view NAME = "Collider";
};

/// @brief Tag of ShapeHandle.
struct ShapeTag
{
    static constexpr std::string_view NAME = "Shape";
};

/// @brief Tag of ConstraintHandle.
struct ConstraintTag
{
    static constexpr std::string_view NAME = "Constraint";
};

/// @brief Tag of JointHandle.
struct JointTag
{
    static constexpr std::string_view NAME = "Joint";
};

/// @brief Tag of TriggerHandle.
struct TriggerTag
{
    static constexpr std::string_view NAME = "Trigger";
};

/// @brief Tag of SpatialStructureHandle.
struct SpatialStructureTag
{
    static constexpr std::string_view NAME = "SpatialStructure";
};

/// @brief Tag of SpatialProxyHandle.
struct SpatialProxyTag
{
    static constexpr std::string_view NAME = "SpatialProxy";
};

/// @brief Tag of BroadPhaseHandle.
struct BroadPhaseTag
{
    static constexpr std::string_view NAME = "BroadPhase";
};

/// @brief Tag of NarrowPhaseHandle.
struct NarrowPhaseTag
{
    static constexpr std::string_view NAME = "NarrowPhase";
};

/// @brief Tag of SolverHandle.
struct SolverTag
{
    static constexpr std::string_view NAME = "Solver";
};

/// @brief Tag of IntegratorHandle.
struct IntegratorTag
{
    static constexpr std::string_view NAME = "Integrator";
};

/// @brief Tag of ContactManifoldHandle.
struct ContactManifoldTag
{
    static constexpr std::string_view NAME = "ContactManifold";
};

/// @brief Tag of CameraHandle.
struct CameraTag
{
    static constexpr std::string_view NAME = "Camera";
};

/// @brief Tag of CameraControllerHandle.
struct CameraControllerTag
{
    static constexpr std::string_view NAME = "CameraController";
};

// ================================================================================================= Handles

using WorldHandle = Handle<WorldTag>;                       ///< Names a physics world.
using RigidBodyHandle = Handle<RigidBodyTag>;               ///< Names a rigid body of a world.
using SoftBodyHandle = Handle<SoftBodyTag>;                 ///< Names a soft body (cloth, rope, volume) of a world.
using ColliderHandle = Handle<ColliderTag>;                 ///< Names a collider: a shape attached to a body.
using ShapeHandle = Handle<ShapeTag>;                       ///< Names a shape (geometry shared by colliders).
using ConstraintHandle = Handle<ConstraintTag>;             ///< Names a constraint the solver orders (attachment, user).
using JointHandle = Handle<JointTag>;                       ///< Names a joint between two bodies.
using TriggerHandle = Handle<TriggerTag>;                   ///< Names a trigger volume.
using SpatialStructureHandle = Handle<SpatialStructureTag>; ///< Names a spatial structure.
using SpatialProxyHandle = Handle<SpatialProxyTag>;         ///< Names an item stored in a spatial structure.
using BroadPhaseHandle = Handle<BroadPhaseTag>;             ///< Names a broad phase.
using NarrowPhaseHandle = Handle<NarrowPhaseTag>;           ///< Names a narrow phase.
using SolverHandle = Handle<SolverTag>;                     ///< Names a solver.
using IntegratorHandle = Handle<IntegratorTag>;             ///< Names an integrator.
using ContactManifoldHandle = Handle<ContactManifoldTag>;   ///< Names a contact manifold of a world.
using CameraHandle = Handle<CameraTag>;                     ///< Names a camera.
using CameraControllerHandle = Handle<CameraControllerTag>; ///< Names a camera controller.

// ================================================================================================= Concepts

/// @brief Any handle of the physics library (the camera's excluded).
template<class Type>
concept PhysicsHandle =
    TaggedHandle<Type>
    && (std::same_as<Type, WorldHandle> || std::same_as<Type, RigidBodyHandle> || std::same_as<Type, SoftBodyHandle>
        || std::same_as<Type, ColliderHandle> || std::same_as<Type, ShapeHandle> || std::same_as<Type, ConstraintHandle>
        || std::same_as<Type, JointHandle> || std::same_as<Type, TriggerHandle> || std::same_as<Type, SpatialStructureHandle>
        || std::same_as<Type, SpatialProxyHandle> || std::same_as<Type, BroadPhaseHandle> || std::same_as<Type, NarrowPhaseHandle>
        || std::same_as<Type, SolverHandle> || std::same_as<Type, IntegratorHandle> || std::same_as<Type, ContactManifoldHandle>);

/// @brief A body: rigid or soft.
template<class Type>
concept BodyHandle = std::same_as<Type, RigidBodyHandle> || std::same_as<Type, SoftBodyHandle>;

/// @brief What collides or overlaps: a collider or a trigger.
template<class Type>
concept CollisionObjectHandle = std::same_as<Type, ColliderHandle> || std::same_as<Type, TriggerHandle>;

/// @brief What the solver orders: a constraint or a joint.
template<class Type>
concept ConstraintObjectHandle = std::same_as<Type, ConstraintHandle> || std::same_as<Type, JointHandle>;

/// @brief A spatial structure or an item stored in one.
template<class Type>
concept SpatialHandle = std::same_as<Type, SpatialStructureHandle> || std::same_as<Type, SpatialProxyHandle>;

/// @brief A camera or a camera controller.
template<class Type>
concept CameraObjectHandle = std::same_as<Type, CameraHandle> || std::same_as<Type, CameraControllerHandle>;

} // namespace gfx::physics

// Every physics handle instantiated here, once: the modules that use them (errors, the narrow phase, …) then share these
// instantiations instead of each making its own, which GCC 16 fails to merge (a module file it cannot read back, or an
// internal compiler error in the importer: §6.13 of the project memory).
template class gfx::Handle<gfx::physics::WorldTag>;
template class gfx::Handle<gfx::physics::RigidBodyTag>;
template class gfx::Handle<gfx::physics::SoftBodyTag>;
template class gfx::Handle<gfx::physics::ColliderTag>;
template class gfx::Handle<gfx::physics::ShapeTag>;
template class gfx::Handle<gfx::physics::ConstraintTag>;
template class gfx::Handle<gfx::physics::JointTag>;
template class gfx::Handle<gfx::physics::TriggerTag>;
template class gfx::Handle<gfx::physics::SpatialStructureTag>;
template class gfx::Handle<gfx::physics::SpatialProxyTag>;
template class gfx::Handle<gfx::physics::BroadPhaseTag>;
template class gfx::Handle<gfx::physics::NarrowPhaseTag>;
template class gfx::Handle<gfx::physics::SolverTag>;
template class gfx::Handle<gfx::physics::IntegratorTag>;
template class gfx::Handle<gfx::physics::ContactManifoldTag>;
template class gfx::Handle<gfx::physics::CameraTag>;
template class gfx::Handle<gfx::physics::CameraControllerTag>;
