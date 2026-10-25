/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx physics and camera libraries (the camera's live here too: one
///        file for both). No other file of either library defines either; the enumeration machinery (traits,
///        verification, bitmask operators, spelling and parsing) comes from gfx.core.enums, which this module
///        re-exports.
///
/// Conventions: enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform headers
/// define as macros are avoided (NEAR_PLANE, FAR_PLANE), which a CTest test checks.
export module gfx.physics.enums;

import std;
export import gfx.core.enums;

export namespace gfx::physics
{

// The bitmask operators and helpers of gfx, found by argument-dependent lookup for the bitmask
// enumerations of this namespace too.
using gfx::operator|;
using gfx::operator&;
using gfx::operator^;
using gfx::operator~;
using gfx::operator|=;
using gfx::operator&=;
using gfx::operator^=;
using gfx::has_all;
using gfx::has_any;
using gfx::is_empty;
using gfx::to_string;

// =================================================================================================
// Enumerations
// =================================================================================================

/// @brief A physics backend: what create_physics_world() builds a world on. Errors name it ("[gfx::physics::BOX3D] ...").
enum class PhysicsBackendKind : std::uint8_t
{
    BOX3D,        ///< Box3D (Erin Catto, MIT): rigid bodies, joints, sensors, queries; soft bodies through the abstraction.
    REFERENCE,    ///< The abstraction's own components: spatial broad phase, GJK/EPA, sequential impulses, integrators.
    NULL_BACKEND, ///< A world that throws BackendCapabilityError for everything: the template of custom backends.
    CUSTOM,       ///< The backend of GFX_PHYSICS_CUSTOM_BACKEND_TARGET (module gfx.physics.backends.custom).
};

/// @brief A part of the physics library that is not a backend: errors of the abstraction's own components name it
///        ("[gfx::physics::SPATIAL] Bvh::remove: ...").
enum class PhysicsSubsystem : std::uint8_t
{
    GEOMETRY,     ///< Primitives, shapes, convex hulls.
    SPATIAL,      ///< Spatial structures and broad phases.
    NARROW_PHASE, ///< GJK, EPA, manifolds, analytic tests.
    DYNAMICS,     ///< Solvers, constraints, integrators.
    SOFT_BODIES,  ///< Particles, XPBD, cloth, ropes, volumes.
    WORLD,        ///< Worlds, their objects, stepping, queries, events.
};

/// @brief A capability a physics backend may lack: the capability a BackendCapabilityError names.
enum class PhysicsCapability : std::uint8_t
{
    SOFT_BODIES,                   ///< Particle-based soft bodies (cloth, ropes).
    VOLUMETRIC_SOFT_BODIES,        ///< Tetrahedral soft bodies.
    SIX_DEGREES_OF_FREEDOM_JOINTS, ///< Generic joints with each of the six axes free, limited or locked.
    DYNAMIC_TRIANGLE_MESHES,       ///< Triangle meshes on kinematic or dynamic bodies.
    DYNAMIC_HEIGHT_FIELDS,         ///< Height fields on kinematic or dynamic bodies.
    DYNAMIC_COMPOUNDS,             ///< Baked compound shapes on kinematic or dynamic bodies.
    CYLINDER_SHAPES,               ///< Exact cylinders (not hull approximations).
    CONE_SHAPES,                   ///< Exact cones (not hull approximations).
    SEPARATE_STATIC_FRICTION,      ///< A static friction coefficient different from the dynamic one.
    CUSTOM_BROAD_PHASE,            ///< Replacing the world's broad phase.
    CUSTOM_NARROW_PHASE,           ///< Replacing the world's narrow phase or adding collision algorithms.
    CUSTOM_SOLVER,                 ///< Replacing the world's constraint solver.
    CUSTOM_INTEGRATOR,             ///< Replacing the world's integration of rigid bodies.
    CONTINUOUS_COLLISION,          ///< Continuous collision detection for fast bodies.
    LARGE_WORLDS,                  ///< Positions in double precision.
    WORLD_OBJECTS,                 ///< Anything a world does: worlds of the null backend have none.
    ROUNDED_HULLS,                 ///< Boxes and hulls rounded by a radius.
    LARGE_HULLS,                   ///< Hulls of more vertices than the backend holds (Box3D: 128; cylinders and cones of more than 64 sides).
    POSED_HEIGHT_FIELDS,           ///< Height fields at a pose other than their body's frame.
    WIDE_JOINT_LIMITS,             ///< Hinge and twist limits beyond the backend's range (Box3D: ±0.99π).
    USER_CONSTRAINTS,              ///< Constraints the application defines (UserConstraint).
};

/// @brief How a rigid body moves.
enum class BodyKind : std::uint8_t
{
    STATIC,    ///< Never moves; infinite mass.
    KINEMATIC, ///< Moves by its velocity only (set by the application); pushes dynamic bodies, never pushed.
    DYNAMIC,   ///< Moved by forces, impulses, contacts and joints.
};

/// @brief The geometry of a shape.
enum class ShapeKind : std::uint8_t
{
    SPHERE,        ///< A center and a radius.
    CAPSULE,       ///< A segment and a radius (rounded ends).
    CYLINDER,      ///< An axis, a half height and a radius.
    CONE,          ///< An axis, a height and a base radius.
    BOX,           ///< Half extents about a center.
    CONVEX_HULL,   ///< The convex hull of points.
    TRIANGLE_MESH, ///< Triangles, with a bounding volume hierarchy.
    HEIGHT_FIELD,  ///< A grid of heights.
    COMPOUND,      ///< Child shapes with their own poses.
};

/// @brief A joint between two bodies (or a body and the world).
enum class JointKind : std::uint8_t
{
    DISTANCE,               ///< Keeps two points at a distance (DistanceMode: rod, spring, rope, stub).
    HINGE,                  ///< One rotational degree of freedom about an axis.
    BALL_AND_SOCKET,        ///< Three rotational degrees of freedom about a point.
    SLIDER,                 ///< One translational degree of freedom along an axis (prismatic).
    FIXED,                  ///< No degree of freedom (weld).
    CONE_TWIST,             ///< A ball and socket with a swing cone and a twist range.
    SIX_DEGREES_OF_FREEDOM, ///< Each of the three translations and rotations free, limited or locked.
};

/// @brief How a distance joint holds its distance.
enum class DistanceMode : std::uint8_t
{
    ROD,    ///< Exactly (rigid).
    SPRING, ///< Softly: a stiffness and a damping.
    ROPE,   ///< At most (slack when shorter).
    STUB,   ///< At zero: the two points held together (a zero-length rod).
};

/// @brief What a constraint the solver orders comes from.
enum class ConstraintKind : std::uint8_t
{
    CONTACT,    ///< A contact point of a manifold.
    JOINT,      ///< A joint.
    ATTACHMENT, ///< A soft-body particle pinned to the world or a rigid body.
    USER,       ///< A constraint of the application (reference backend).
};

/// @brief What a row of a joint or of a user constraint keeps (ConstraintRow).
enum class ConstraintRowKind : std::uint8_t
{
    EQUALITY,   ///< Its error at 0 (a point held, an axis aligned).
    INEQUALITY, ///< Its error at least 0 (a limit; approaching it is allowed until it is reached).
    MOTOR,      ///< Its velocity at a target speed, with at most a force (no position).
    SPRING,     ///< Its error pulled toward 0 softly, at a frequency with a damping ratio.
};

/// @brief How constraints are solved.
enum class SolverKind : std::uint8_t
{
    SEQUENTIAL_IMPULSE, ///< Sequential impulses with warm starting and position correction (the reference).
    SOFT_STEP,          ///< Box3D's sub-stepped soft-constraint solver.
    XPBD,               ///< Extended position-based dynamics (soft bodies).
};

/// @brief How states advance in time.
enum class IntegratorKind : std::uint8_t
{
    SEMI_IMPLICIT_EULER, ///< Velocity first, then position from the new velocity (symplectic Euler).
    VERLET,              ///< Position Verlet: from the current and the previous positions.
    RUNGE_KUTTA_4,       ///< Classical fourth-order Runge-Kutta (four force evaluations per step).
    XPBD,                ///< Prediction and velocity update around constraint projection.
};

/// @brief A spatial acceleration structure.
enum class SpatialStructureKind : std::uint8_t
{
    BVH,             ///< Dynamic bounding volume hierarchy (an AABB tree).
    OCTREE,          ///< Loose octree.
    QUADTREE,        ///< Loose quadtree over two axes.
    BSP,             ///< Binary space partitioning by planes.
    UNIFORM_GRID,    ///< Hashed grid of uniform cells.
    SWEEP_AND_PRUNE, ///< Sorted interval endpoints along axes.
};

/// @brief What finds candidate pairs.
enum class BroadPhaseKind : std::uint8_t
{
    BVH,             ///< A spatial broad phase on a BVH.
    OCTREE,          ///< A spatial broad phase on an octree.
    QUADTREE,        ///< A spatial broad phase on a quadtree.
    BSP,             ///< A spatial broad phase on a BSP tree.
    UNIFORM_GRID,    ///< A spatial broad phase on a uniform grid.
    SWEEP_AND_PRUNE, ///< A spatial broad phase on sweep and prune.
    BRUTE_FORCE,     ///< Every pair (tests and tiny scenes).
    BACKEND,         ///< The backend's own (Box3D: its dynamic tree).
};

/// @brief What computes contacts of candidate pairs.
enum class NarrowPhaseKind : std::uint8_t
{
    GJK_EPA, ///< The abstraction's: GJK, EPA, clipping, analytic fast paths.
    BACKEND, ///< The backend's own.
};

/// @brief How a body's motion is checked for collisions.
enum class MotionQuality : std::uint8_t
{
    DISCRETE,   ///< At the end of each step.
    CONTINUOUS, ///< Along the path of the step (no tunneling through thin geometry).
};

/// @brief What a trigger event reports.
enum class TriggerEventKind : std::uint8_t
{
    ENTER, ///< A collider began to overlap the trigger.
    STAY,  ///< A collider still overlaps the trigger (reported every step).
    EXIT,  ///< A collider stopped overlapping the trigger (or was destroyed).
};

/// @brief Why a query has no result.
enum class QueryFailureKind : std::uint8_t
{
    NO_HIT,            ///< Nothing was hit or found.
    INVALID_HANDLE,    ///< A handle names no live object.
    STEP_IN_PROGRESS,  ///< The world is stepping: the query returns at once; ask again after wait_step().
    UNSUPPORTED,       ///< The backend cannot answer it (QueryFailure::capability names what it lacks).
    CAPACITY_EXCEEDED, ///< More results than the output holds.
};

/// @brief Where a volume is with respect to a frustum.
enum class FrustumIntersection : std::uint8_t
{
    OUTSIDE,      ///< Entirely outside (culled).
    INTERSECTING, ///< Partly inside (or not provably outside).
    INSIDE,       ///< Entirely inside.
};

/// @brief The planes of a frustum, in the order of Frustum::planes.
enum class FrustumPlane : std::uint8_t
{
    LEFT,       ///< Left.
    RIGHT,      ///< Right.
    BOTTOM,     ///< Bottom.
    TOP,        ///< Top.
    NEAR_PLANE, ///< Near (NEAR is a Windows macro).
    FAR_PLANE,  ///< Far (FAR is a Windows macro).
};

/// @brief Where a volume is with respect to a plane.
enum class PlaneSide : std::uint8_t
{
    FRONT,        ///< Entirely on the side the normal points to.
    BACK,         ///< Entirely on the other side.
    INTERSECTING, ///< On both sides.
};

/// @brief The clip-space depth range of a projection matrix.
enum class DepthRange : std::uint8_t
{
    ZERO_TO_ONE,         ///< Vulkan, Direct3D, Metal, WebGPU.
    NEGATIVE_ONE_TO_ONE, ///< OpenGL.
};

/// @brief What GJK found about two convex cores.
enum class GjkStatus : std::uint8_t
{
    SEPARATED,   ///< The cores are apart: GjkResult::distance and the closest points hold.
    OVERLAPPING, ///< The cores touch or overlap: EPA gives the penetration.
};

/// @brief How EPA ended.
enum class EpaStatus : std::uint8_t
{
    CONVERGED,        ///< The penetration depth is within the tolerance.
    ITERATION_LIMIT,  ///< The iterations ran out: the best face found so far.
    CAPACITY_REACHED, ///< The polytope filled its fixed storage: the best face found so far.
    DEGENERATE,       ///< The cores only touch, or the polytope collapsed: depth 0 along the best normal known.
};

/// @brief What computed a contact manifold.
enum class ContactAlgorithm : std::uint8_t
{
    SPHERE_SPHERE,     ///< Two spheres (analytic).
    SPHERE_CAPSULE,    ///< A sphere and a capsule (analytic).
    CAPSULE_CAPSULE,   ///< Two capsules (analytic: closest points of their segments).
    SPHERE_BOX,        ///< A sphere and a box (analytic).
    BOX_BOX,           ///< Two boxes (separating axis test, face clipping).
    TRIANGLE_TRIANGLE, ///< Two triangles of meshes or height fields (separating axis test).
    GJK,               ///< Any two convex shapes apart or touching: GJK distance, face clipping.
    EPA,               ///< Any two convex shapes whose cores overlap: GJK then EPA, face clipping.
};

/// @brief An axis of a joint's relative motion (the joint's frame): its translations, then its rotations.
enum class JointAxis : std::uint8_t
{
    LINEAR_X,  ///< Translation along x.
    LINEAR_Y,  ///< Translation along y.
    LINEAR_Z,  ///< Translation along z.
    ANGULAR_X, ///< Rotation about x (twist, for cone-twist joints).
    ANGULAR_Y, ///< Rotation about y.
    ANGULAR_Z, ///< Rotation about z.
};

/// @brief How a joint lets an axis move.
enum class AxisMotion : std::uint8_t
{
    FREE,    ///< Freely.
    LIMITED, ///< Between limits.
    LOCKED,  ///< Not at all.
};

/// @brief Who answers a world query.
enum class QueryEngine : std::uint8_t
{
    BACKEND,     ///< The backend's own queries (Box3D's).
    ABSTRACTION, ///< The abstraction's: its spatial structure and narrow phase (GJK).
};

/// @brief What a contact event reports.
enum class ContactEventKind : std::uint8_t
{
    BEGIN, ///< Two colliders started touching.
    END,   ///< They stopped touching (or one was destroyed).
    HIT,   ///< They hit each other faster than the world's hit threshold.
};

/// @brief What a joint event reports.
enum class JointEventKind : std::uint8_t
{
    BROKEN, ///< Its force or torque passed its threshold: the joint is disabled (its object stays valid).
};

/// @brief What a sleep event reports.
enum class SleepEventKind : std::uint8_t
{
    FELL_ASLEEP, ///< The body (with its island) stopped moving and sleeps.
    WOKE_UP,     ///< The body moves again.
};

/// @brief A phase of a world step (StepStatistics times each).
enum class StepPhase : std::uint8_t
{
    EDITS,        ///< Applying the edits recorded since the last step.
    BROAD_PHASE,  ///< Finding candidate pairs.
    NARROW_PHASE, ///< Computing contacts.
    SOLVER,       ///< Solving constraints and integrating rigid bodies.
    SOFT_BODIES,  ///< Simulating soft bodies.
    EVENTS,       ///< Gathering events and the state getters show.
};

/// @brief A constraint between soft-body particles.
enum class SoftBodyConstraintKind : std::uint8_t
{
    STRETCH,  ///< Keeps two particles at their rest distance (cloth's structural edges, a rope's segments).
    SHEAR,    ///< A distance across a cloth's cells.
    BENDING,  ///< A distance across neighboring segments or cells (a rope's every other particle, a cloth's).
    VOLUME,   ///< Keeps a tetrahedron's volume (four particles).
    DIHEDRAL, ///< Keeps the angle between two triangles sharing an edge (four particles: the edge's, then the two others).
    TETHER,   ///< Keeps a particle within its rest distance of another (a long-range attachment to a pinned particle).
};

/// @brief A plane of two coordinate axes (a quadtree's).
enum class AxisPlane : std::uint8_t
{
    XY, ///< X and Y (Z ignored).
    XZ, ///< X and Z (Y ignored): flat worlds with +Y up.
    YZ, ///< Y and Z (X ignored).
};

/// @brief The axes along or about which a rigid body may not move (world axes).
enum class MotionLock : std::uint8_t
{
    NONE = 0, ///< No bit.
    LINEAR_X  = 1U << 0, ///< No translation along x.
    LINEAR_Y  = 1U << 1, ///< No translation along y.
    LINEAR_Z  = 1U << 2, ///< No translation along z.
    ANGULAR_X = 1U << 3, ///< No rotation about x.
    ANGULAR_Y = 1U << 4, ///< No rotation about y.
    ANGULAR_Z = 1U << 5, ///< No rotation about z.
};

/// @brief Collision layers (64 bits): what a collider belongs to and what it collides with (CollisionFilter).
enum class LayerMask : std::uint64_t
{
    NONE = 0, ///< No bit.
    LAYER_0  = 1ULL << 0, ///< Layer 0.
    LAYER_1  = 1ULL << 1, ///< Layer 1.
    LAYER_2  = 1ULL << 2, ///< Layer 2.
    LAYER_3  = 1ULL << 3, ///< Layer 3.
    LAYER_4  = 1ULL << 4, ///< Layer 4.
    LAYER_5  = 1ULL << 5, ///< Layer 5.
    LAYER_6  = 1ULL << 6, ///< Layer 6.
    LAYER_7  = 1ULL << 7, ///< Layer 7.
    LAYER_8  = 1ULL << 8, ///< Layer 8.
    LAYER_9  = 1ULL << 9, ///< Layer 9.
    LAYER_10 = 1ULL << 10, ///< Layer 10.
    LAYER_11 = 1ULL << 11, ///< Layer 11.
    LAYER_12 = 1ULL << 12, ///< Layer 12.
    LAYER_13 = 1ULL << 13, ///< Layer 13.
    LAYER_14 = 1ULL << 14, ///< Layer 14.
    LAYER_15 = 1ULL << 15, ///< Layer 15.
    LAYER_16 = 1ULL << 16, ///< Layer 16.
    LAYER_17 = 1ULL << 17, ///< Layer 17.
    LAYER_18 = 1ULL << 18, ///< Layer 18.
    LAYER_19 = 1ULL << 19, ///< Layer 19.
    LAYER_20 = 1ULL << 20, ///< Layer 20.
    LAYER_21 = 1ULL << 21, ///< Layer 21.
    LAYER_22 = 1ULL << 22, ///< Layer 22.
    LAYER_23 = 1ULL << 23, ///< Layer 23.
    LAYER_24 = 1ULL << 24, ///< Layer 24.
    LAYER_25 = 1ULL << 25, ///< Layer 25.
    LAYER_26 = 1ULL << 26, ///< Layer 26.
    LAYER_27 = 1ULL << 27, ///< Layer 27.
    LAYER_28 = 1ULL << 28, ///< Layer 28.
    LAYER_29 = 1ULL << 29, ///< Layer 29.
    LAYER_30 = 1ULL << 30, ///< Layer 30.
    LAYER_31 = 1ULL << 31, ///< Layer 31.
    LAYER_32 = 1ULL << 32, ///< Layer 32.
    LAYER_33 = 1ULL << 33, ///< Layer 33.
    LAYER_34 = 1ULL << 34, ///< Layer 34.
    LAYER_35 = 1ULL << 35, ///< Layer 35.
    LAYER_36 = 1ULL << 36, ///< Layer 36.
    LAYER_37 = 1ULL << 37, ///< Layer 37.
    LAYER_38 = 1ULL << 38, ///< Layer 38.
    LAYER_39 = 1ULL << 39, ///< Layer 39.
    LAYER_40 = 1ULL << 40, ///< Layer 40.
    LAYER_41 = 1ULL << 41, ///< Layer 41.
    LAYER_42 = 1ULL << 42, ///< Layer 42.
    LAYER_43 = 1ULL << 43, ///< Layer 43.
    LAYER_44 = 1ULL << 44, ///< Layer 44.
    LAYER_45 = 1ULL << 45, ///< Layer 45.
    LAYER_46 = 1ULL << 46, ///< Layer 46.
    LAYER_47 = 1ULL << 47, ///< Layer 47.
    LAYER_48 = 1ULL << 48, ///< Layer 48.
    LAYER_49 = 1ULL << 49, ///< Layer 49.
    LAYER_50 = 1ULL << 50, ///< Layer 50.
    LAYER_51 = 1ULL << 51, ///< Layer 51.
    LAYER_52 = 1ULL << 52, ///< Layer 52.
    LAYER_53 = 1ULL << 53, ///< Layer 53.
    LAYER_54 = 1ULL << 54, ///< Layer 54.
    LAYER_55 = 1ULL << 55, ///< Layer 55.
    LAYER_56 = 1ULL << 56, ///< Layer 56.
    LAYER_57 = 1ULL << 57, ///< Layer 57.
    LAYER_58 = 1ULL << 58, ///< Layer 58.
    LAYER_59 = 1ULL << 59, ///< Layer 59.
    LAYER_60 = 1ULL << 60, ///< Layer 60.
    LAYER_61 = 1ULL << 61, ///< Layer 61.
    LAYER_62 = 1ULL << 62, ///< Layer 62.
    LAYER_63 = 1ULL << 63, ///< Layer 63.
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief Every collision layer.
inline constexpr LayerMask ALL_LAYERS = static_cast<LayerMask>(~std::uint64_t{0});

/// @brief Contact points a manifold holds at most.
inline constexpr std::uint32_t MAXIMUM_MANIFOLD_POINTS = 4;

/// @brief Default approach speed (units per second) under which contacts do not bounce.
inline constexpr float DEFAULT_RESTITUTION_THRESHOLD = 1.0F;

/// @brief Tolerance of geometric tests (parallel directions, degenerate segments, plane distances), in units.
inline constexpr float GEOMETRIC_TOLERANCE = 1.0e-6F;

/// @brief Vertices a supporting face (a polygon used to build contact manifolds) holds at most.
inline constexpr std::uint32_t MAXIMUM_FACE_VERTICES = 32;

/// @brief The sub-shape of a shape that has none (a convex shape): ContactManifold::sub_shape_first and _second.
inline constexpr std::uint32_t NO_SUB_SHAPE = 0xFFFFFFFFU;

/// @brief GJK iterations at most (smooth shapes converge linearly; polytopes in a few iterations).
inline constexpr std::uint32_t MAXIMUM_GJK_ITERATIONS = 64;

/// @brief Vertices an EPA polytope holds at most.
inline constexpr std::uint32_t MAXIMUM_EPA_VERTICES = 64;

/// @brief Faces an EPA polytope holds at most.
inline constexpr std::uint32_t MAXIMUM_EPA_FACES = 128;

/// @brief Mesh triangles that meet within about 5° are flat for active edges (TriangleMeshDescription::active_edge_cosine).
inline constexpr float DEFAULT_ACTIVE_EDGE_COSINE = 0.996F;

/// @brief Standard gravity, in meters per second squared (PhysicsWorldDescription::gravity is -STANDARD_GRAVITY along y).
inline constexpr float STANDARD_GRAVITY = 9.80665F;

/// @brief Rows of a joint or of a user constraint at most (a six-degrees-of-freedom joint: three per axis).
inline constexpr std::uint32_t MAXIMUM_CONSTRAINT_ROWS = 18;

/// @brief A SolverJoint's second body when it holds to the world.
inline constexpr std::uint32_t NO_SOLVER_BODY = 0xFFFFFFFFU;

/// @brief The overlap and the joint error, in meters, position correction leaves (contacts rest there without jitter).
inline constexpr float LINEAR_SLOP = 0.005F;

/// @brief The angular joint error, in radians (2°), position correction leaves.
inline constexpr float ANGULAR_SLOP = 0.034906585F;

/// @brief How far, in meters, a position correction moves a body at most in one iteration.
inline constexpr float MAXIMUM_LINEAR_CORRECTION = 0.2F;

/// @brief How far, in radians (8°), a position correction turns a body at most in one iteration.
inline constexpr float MAXIMUM_ANGULAR_CORRECTION = 0.13962634F;

/// @brief The fraction of a position error a correction iteration removes (non-linear Gauss–Seidel).
inline constexpr float POSITION_CORRECTION_FACTOR = 0.2F;

} // namespace gfx::physics

export namespace gfx::camera
{

// The bitmask operators and helpers of gfx, found by argument-dependent lookup for the bitmask
// enumerations of this namespace too.
using gfx::operator|;
using gfx::operator&;
using gfx::operator^;
using gfx::operator~;
using gfx::operator|=;
using gfx::operator&=;
using gfx::operator^=;
using gfx::has_all;
using gfx::has_any;
using gfx::is_empty;
using gfx::to_string;

// =================================================================================================
// Enumerations
// =================================================================================================

/// @brief A camera implementation: what create_camera() builds. Errors name it ("[gfx::camera::STANDARD] ...").
enum class CameraBackendKind : std::uint8_t
{
    STANDARD, ///< The library's camera: symmetric perspective and orthographic projections, every clip convention.
    CUSTOM,   ///< An application's camera (a class derived from Camera, constructed directly).
};

/// @brief The kind of a camera projection (the alternative a CameraProjection holds).
enum class CameraProjectionKind : std::uint8_t
{
    PERSPECTIVE,  ///< Perspective: a vertical field of view and an aspect ratio.
    ORTHOGRAPHIC, ///< Orthographic: the bounds of a box seen along -Z.
};

/// @brief A camera controller (the alternative a CameraControllerDescription holds).
enum class CameraControllerKind : std::uint8_t
{
    FREE_FLY,     ///< Six degrees of freedom: turns about the camera's own axes, moves along them.
    FIRST_PERSON, ///< Yaw about the world's up axis, clamped pitch, no roll; moves on the horizontal plane.
    ORBIT,        ///< Turns around a target at a distance; zooms the distance, pans the target.
};

/// @brief Where +Y points in normalized device coordinates (what a projection matrix produces).
enum class NormalizedDeviceYAxis : std::uint8_t
{
    UP,   ///< +Y up: Direct3D, Metal, WebGPU, OpenGL, Vulkan with a negative-height viewport.
    DOWN, ///< +Y down: Vulkan with a positive-height viewport.
};

/// @brief The frame a rotation axis or a movement is given in.
enum class ReferenceFrame : std::uint8_t
{
    WORLD, ///< World space.
    LOCAL, ///< The camera's own axes: +X right, +Y up, -Z forward.
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief A far plane at infinity (PerspectiveProjection::far_plane): the projection maps it to the far depth.
inline constexpr float INFINITE_FAR_PLANE = std::numeric_limits<float>::infinity();

} // namespace gfx::camera

// =================================================================================================
// Enumeration descriptions
// =================================================================================================
export namespace gfx
{

template<>
struct EnumerationTraits<physics::PhysicsBackendKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::physics"; ///< Errors name these "gfx::physics::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{physics::PhysicsBackendKind::BOX3D, "BOX3D"},
        EnumeratorName{physics::PhysicsBackendKind::REFERENCE, "REFERENCE"},
        EnumeratorName{physics::PhysicsBackendKind::NULL_BACKEND, "NULL_BACKEND"},
        EnumeratorName{physics::PhysicsBackendKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<physics::PhysicsSubsystem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::physics"; ///< Errors name these "gfx::physics::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{physics::PhysicsSubsystem::GEOMETRY, "GEOMETRY"},
        EnumeratorName{physics::PhysicsSubsystem::SPATIAL, "SPATIAL"},
        EnumeratorName{physics::PhysicsSubsystem::NARROW_PHASE, "NARROW_PHASE"},
        EnumeratorName{physics::PhysicsSubsystem::DYNAMICS, "DYNAMICS"},
        EnumeratorName{physics::PhysicsSubsystem::SOFT_BODIES, "SOFT_BODIES"},
        EnumeratorName{physics::PhysicsSubsystem::WORLD, "WORLD"},
    };
};

template<>
struct EnumerationTraits<physics::PhysicsCapability>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::PhysicsCapability::SOFT_BODIES, "SOFT_BODIES"},
        EnumeratorName{physics::PhysicsCapability::VOLUMETRIC_SOFT_BODIES, "VOLUMETRIC_SOFT_BODIES"},
        EnumeratorName{physics::PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS, "SIX_DEGREES_OF_FREEDOM_JOINTS"},
        EnumeratorName{physics::PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, "DYNAMIC_TRIANGLE_MESHES"},
        EnumeratorName{physics::PhysicsCapability::DYNAMIC_HEIGHT_FIELDS, "DYNAMIC_HEIGHT_FIELDS"},
        EnumeratorName{physics::PhysicsCapability::DYNAMIC_COMPOUNDS, "DYNAMIC_COMPOUNDS"},
        EnumeratorName{physics::PhysicsCapability::CYLINDER_SHAPES, "CYLINDER_SHAPES"},
        EnumeratorName{physics::PhysicsCapability::CONE_SHAPES, "CONE_SHAPES"},
        EnumeratorName{physics::PhysicsCapability::SEPARATE_STATIC_FRICTION, "SEPARATE_STATIC_FRICTION"},
        EnumeratorName{physics::PhysicsCapability::CUSTOM_BROAD_PHASE, "CUSTOM_BROAD_PHASE"},
        EnumeratorName{physics::PhysicsCapability::CUSTOM_NARROW_PHASE, "CUSTOM_NARROW_PHASE"},
        EnumeratorName{physics::PhysicsCapability::CUSTOM_SOLVER, "CUSTOM_SOLVER"},
        EnumeratorName{physics::PhysicsCapability::CUSTOM_INTEGRATOR, "CUSTOM_INTEGRATOR"},
        EnumeratorName{physics::PhysicsCapability::CONTINUOUS_COLLISION, "CONTINUOUS_COLLISION"},
        EnumeratorName{physics::PhysicsCapability::LARGE_WORLDS, "LARGE_WORLDS"},
        EnumeratorName{physics::PhysicsCapability::WORLD_OBJECTS, "WORLD_OBJECTS"},
        EnumeratorName{physics::PhysicsCapability::ROUNDED_HULLS, "ROUNDED_HULLS"},
        EnumeratorName{physics::PhysicsCapability::LARGE_HULLS, "LARGE_HULLS"},
        EnumeratorName{physics::PhysicsCapability::POSED_HEIGHT_FIELDS, "POSED_HEIGHT_FIELDS"},
        EnumeratorName{physics::PhysicsCapability::WIDE_JOINT_LIMITS, "WIDE_JOINT_LIMITS"},
        EnumeratorName{physics::PhysicsCapability::USER_CONSTRAINTS, "USER_CONSTRAINTS"},
    };
};

template<>
struct EnumerationTraits<physics::BodyKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::BodyKind::STATIC, "STATIC"},
        EnumeratorName{physics::BodyKind::KINEMATIC, "KINEMATIC"},
        EnumeratorName{physics::BodyKind::DYNAMIC, "DYNAMIC"},
    };
};

template<>
struct EnumerationTraits<physics::ShapeKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::ShapeKind::SPHERE, "SPHERE"},
        EnumeratorName{physics::ShapeKind::CAPSULE, "CAPSULE"},
        EnumeratorName{physics::ShapeKind::CYLINDER, "CYLINDER"},
        EnumeratorName{physics::ShapeKind::CONE, "CONE"},
        EnumeratorName{physics::ShapeKind::BOX, "BOX"},
        EnumeratorName{physics::ShapeKind::CONVEX_HULL, "CONVEX_HULL"},
        EnumeratorName{physics::ShapeKind::TRIANGLE_MESH, "TRIANGLE_MESH"},
        EnumeratorName{physics::ShapeKind::HEIGHT_FIELD, "HEIGHT_FIELD"},
        EnumeratorName{physics::ShapeKind::COMPOUND, "COMPOUND"},
    };
};

template<>
struct EnumerationTraits<physics::JointKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::JointKind::DISTANCE, "DISTANCE"},
        EnumeratorName{physics::JointKind::HINGE, "HINGE"},
        EnumeratorName{physics::JointKind::BALL_AND_SOCKET, "BALL_AND_SOCKET"},
        EnumeratorName{physics::JointKind::SLIDER, "SLIDER"},
        EnumeratorName{physics::JointKind::FIXED, "FIXED"},
        EnumeratorName{physics::JointKind::CONE_TWIST, "CONE_TWIST"},
        EnumeratorName{physics::JointKind::SIX_DEGREES_OF_FREEDOM, "SIX_DEGREES_OF_FREEDOM"},
    };
};

template<>
struct EnumerationTraits<physics::DistanceMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::DistanceMode::ROD, "ROD"},
        EnumeratorName{physics::DistanceMode::SPRING, "SPRING"},
        EnumeratorName{physics::DistanceMode::ROPE, "ROPE"},
        EnumeratorName{physics::DistanceMode::STUB, "STUB"},
    };
};

template<>
struct EnumerationTraits<physics::ConstraintKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::ConstraintKind::CONTACT, "CONTACT"},
        EnumeratorName{physics::ConstraintKind::JOINT, "JOINT"},
        EnumeratorName{physics::ConstraintKind::ATTACHMENT, "ATTACHMENT"},
        EnumeratorName{physics::ConstraintKind::USER, "USER"},
    };
};

template<>
struct EnumerationTraits<physics::ConstraintRowKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::ConstraintRowKind::EQUALITY, "EQUALITY"},
        EnumeratorName{physics::ConstraintRowKind::INEQUALITY, "INEQUALITY"},
        EnumeratorName{physics::ConstraintRowKind::MOTOR, "MOTOR"},
        EnumeratorName{physics::ConstraintRowKind::SPRING, "SPRING"},
    };
};

template<>
struct EnumerationTraits<physics::SolverKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::SolverKind::SEQUENTIAL_IMPULSE, "SEQUENTIAL_IMPULSE"},
        EnumeratorName{physics::SolverKind::SOFT_STEP, "SOFT_STEP"},
        EnumeratorName{physics::SolverKind::XPBD, "XPBD"},
    };
};

template<>
struct EnumerationTraits<physics::IntegratorKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::IntegratorKind::SEMI_IMPLICIT_EULER, "SEMI_IMPLICIT_EULER"},
        EnumeratorName{physics::IntegratorKind::VERLET, "VERLET"},
        EnumeratorName{physics::IntegratorKind::RUNGE_KUTTA_4, "RUNGE_KUTTA_4"},
        EnumeratorName{physics::IntegratorKind::XPBD, "XPBD"},
    };
};

template<>
struct EnumerationTraits<physics::SpatialStructureKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::SpatialStructureKind::BVH, "BVH"},
        EnumeratorName{physics::SpatialStructureKind::OCTREE, "OCTREE"},
        EnumeratorName{physics::SpatialStructureKind::QUADTREE, "QUADTREE"},
        EnumeratorName{physics::SpatialStructureKind::BSP, "BSP"},
        EnumeratorName{physics::SpatialStructureKind::UNIFORM_GRID, "UNIFORM_GRID"},
        EnumeratorName{physics::SpatialStructureKind::SWEEP_AND_PRUNE, "SWEEP_AND_PRUNE"},
    };
};

template<>
struct EnumerationTraits<physics::BroadPhaseKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::BroadPhaseKind::BVH, "BVH"},
        EnumeratorName{physics::BroadPhaseKind::OCTREE, "OCTREE"},
        EnumeratorName{physics::BroadPhaseKind::QUADTREE, "QUADTREE"},
        EnumeratorName{physics::BroadPhaseKind::BSP, "BSP"},
        EnumeratorName{physics::BroadPhaseKind::UNIFORM_GRID, "UNIFORM_GRID"},
        EnumeratorName{physics::BroadPhaseKind::SWEEP_AND_PRUNE, "SWEEP_AND_PRUNE"},
        EnumeratorName{physics::BroadPhaseKind::BRUTE_FORCE, "BRUTE_FORCE"},
        EnumeratorName{physics::BroadPhaseKind::BACKEND, "BACKEND"},
    };
};

template<>
struct EnumerationTraits<physics::NarrowPhaseKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::NarrowPhaseKind::GJK_EPA, "GJK_EPA"},
        EnumeratorName{physics::NarrowPhaseKind::BACKEND, "BACKEND"},
    };
};

template<>
struct EnumerationTraits<physics::MotionQuality>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::MotionQuality::DISCRETE, "DISCRETE"},
        EnumeratorName{physics::MotionQuality::CONTINUOUS, "CONTINUOUS"},
    };
};

template<>
struct EnumerationTraits<physics::TriggerEventKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::TriggerEventKind::ENTER, "ENTER"},
        EnumeratorName{physics::TriggerEventKind::STAY, "STAY"},
        EnumeratorName{physics::TriggerEventKind::EXIT, "EXIT"},
    };
};

template<>
struct EnumerationTraits<physics::QueryFailureKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::QueryFailureKind::NO_HIT, "NO_HIT"},
        EnumeratorName{physics::QueryFailureKind::INVALID_HANDLE, "INVALID_HANDLE"},
        EnumeratorName{physics::QueryFailureKind::STEP_IN_PROGRESS, "STEP_IN_PROGRESS"},
        EnumeratorName{physics::QueryFailureKind::UNSUPPORTED, "UNSUPPORTED"},
        EnumeratorName{physics::QueryFailureKind::CAPACITY_EXCEEDED, "CAPACITY_EXCEEDED"},
    };
};

template<>
struct EnumerationTraits<physics::FrustumIntersection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::FrustumIntersection::OUTSIDE, "OUTSIDE"},
        EnumeratorName{physics::FrustumIntersection::INTERSECTING, "INTERSECTING"},
        EnumeratorName{physics::FrustumIntersection::INSIDE, "INSIDE"},
    };
};

template<>
struct EnumerationTraits<physics::FrustumPlane>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::FrustumPlane::LEFT, "LEFT"},
        EnumeratorName{physics::FrustumPlane::RIGHT, "RIGHT"},
        EnumeratorName{physics::FrustumPlane::BOTTOM, "BOTTOM"},
        EnumeratorName{physics::FrustumPlane::TOP, "TOP"},
        EnumeratorName{physics::FrustumPlane::NEAR_PLANE, "NEAR_PLANE"},
        EnumeratorName{physics::FrustumPlane::FAR_PLANE, "FAR_PLANE"},
    };
};

template<>
struct EnumerationTraits<physics::PlaneSide>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::PlaneSide::FRONT, "FRONT"},
        EnumeratorName{physics::PlaneSide::BACK, "BACK"},
        EnumeratorName{physics::PlaneSide::INTERSECTING, "INTERSECTING"},
    };
};

template<>
struct EnumerationTraits<physics::DepthRange>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::DepthRange::ZERO_TO_ONE, "ZERO_TO_ONE"},
        EnumeratorName{physics::DepthRange::NEGATIVE_ONE_TO_ONE, "NEGATIVE_ONE_TO_ONE"},
    };
};

template<>
struct EnumerationTraits<physics::GjkStatus>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::GjkStatus::SEPARATED, "SEPARATED"},
        EnumeratorName{physics::GjkStatus::OVERLAPPING, "OVERLAPPING"},
    };
};

template<>
struct EnumerationTraits<physics::EpaStatus>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::EpaStatus::CONVERGED, "CONVERGED"},
        EnumeratorName{physics::EpaStatus::ITERATION_LIMIT, "ITERATION_LIMIT"},
        EnumeratorName{physics::EpaStatus::CAPACITY_REACHED, "CAPACITY_REACHED"},
        EnumeratorName{physics::EpaStatus::DEGENERATE, "DEGENERATE"},
    };
};

template<>
struct EnumerationTraits<physics::ContactAlgorithm>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::ContactAlgorithm::SPHERE_SPHERE, "SPHERE_SPHERE"},
        EnumeratorName{physics::ContactAlgorithm::SPHERE_CAPSULE, "SPHERE_CAPSULE"},
        EnumeratorName{physics::ContactAlgorithm::CAPSULE_CAPSULE, "CAPSULE_CAPSULE"},
        EnumeratorName{physics::ContactAlgorithm::SPHERE_BOX, "SPHERE_BOX"},
        EnumeratorName{physics::ContactAlgorithm::BOX_BOX, "BOX_BOX"},
        EnumeratorName{physics::ContactAlgorithm::TRIANGLE_TRIANGLE, "TRIANGLE_TRIANGLE"},
        EnumeratorName{physics::ContactAlgorithm::GJK, "GJK"},
        EnumeratorName{physics::ContactAlgorithm::EPA, "EPA"},
    };
};

template<>
struct EnumerationTraits<physics::JointAxis>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::JointAxis::LINEAR_X, "LINEAR_X"},
        EnumeratorName{physics::JointAxis::LINEAR_Y, "LINEAR_Y"},
        EnumeratorName{physics::JointAxis::LINEAR_Z, "LINEAR_Z"},
        EnumeratorName{physics::JointAxis::ANGULAR_X, "ANGULAR_X"},
        EnumeratorName{physics::JointAxis::ANGULAR_Y, "ANGULAR_Y"},
        EnumeratorName{physics::JointAxis::ANGULAR_Z, "ANGULAR_Z"},
    };
};

template<>
struct EnumerationTraits<physics::AxisMotion>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::AxisMotion::FREE, "FREE"},
        EnumeratorName{physics::AxisMotion::LIMITED, "LIMITED"},
        EnumeratorName{physics::AxisMotion::LOCKED, "LOCKED"},
    };
};

template<>
struct EnumerationTraits<physics::QueryEngine>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::QueryEngine::BACKEND, "BACKEND"},
        EnumeratorName{physics::QueryEngine::ABSTRACTION, "ABSTRACTION"},
    };
};

template<>
struct EnumerationTraits<physics::ContactEventKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::ContactEventKind::BEGIN, "BEGIN"},
        EnumeratorName{physics::ContactEventKind::END, "END"},
        EnumeratorName{physics::ContactEventKind::HIT, "HIT"},
    };
};

template<>
struct EnumerationTraits<physics::JointEventKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::JointEventKind::BROKEN, "BROKEN"},
    };
};

template<>
struct EnumerationTraits<physics::SleepEventKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::SleepEventKind::FELL_ASLEEP, "FELL_ASLEEP"},
        EnumeratorName{physics::SleepEventKind::WOKE_UP, "WOKE_UP"},
    };
};

template<>
struct EnumerationTraits<physics::StepPhase>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::StepPhase::EDITS, "EDITS"},
        EnumeratorName{physics::StepPhase::BROAD_PHASE, "BROAD_PHASE"},
        EnumeratorName{physics::StepPhase::NARROW_PHASE, "NARROW_PHASE"},
        EnumeratorName{physics::StepPhase::SOLVER, "SOLVER"},
        EnumeratorName{physics::StepPhase::SOFT_BODIES, "SOFT_BODIES"},
        EnumeratorName{physics::StepPhase::EVENTS, "EVENTS"},
    };
};

template<>
struct EnumerationTraits<physics::SoftBodyConstraintKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::SoftBodyConstraintKind::STRETCH, "STRETCH"},
        EnumeratorName{physics::SoftBodyConstraintKind::SHEAR, "SHEAR"},
        EnumeratorName{physics::SoftBodyConstraintKind::BENDING, "BENDING"},
        EnumeratorName{physics::SoftBodyConstraintKind::VOLUME, "VOLUME"},
        EnumeratorName{physics::SoftBodyConstraintKind::DIHEDRAL, "DIHEDRAL"},
        EnumeratorName{physics::SoftBodyConstraintKind::TETHER, "TETHER"},
    };
};

template<>
struct EnumerationTraits<physics::AxisPlane>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{physics::AxisPlane::XY, "XY"},
        EnumeratorName{physics::AxisPlane::XZ, "XZ"},
        EnumeratorName{physics::AxisPlane::YZ, "YZ"},
    };
};

template<>
struct EnumerationTraits<physics::MotionLock>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{physics::MotionLock::LINEAR_X, "LINEAR_X"},
        EnumeratorName{physics::MotionLock::LINEAR_Y, "LINEAR_Y"},
        EnumeratorName{physics::MotionLock::LINEAR_Z, "LINEAR_Z"},
        EnumeratorName{physics::MotionLock::ANGULAR_X, "ANGULAR_X"},
        EnumeratorName{physics::MotionLock::ANGULAR_Y, "ANGULAR_Y"},
        EnumeratorName{physics::MotionLock::ANGULAR_Z, "ANGULAR_Z"},
    };
};

template<>
struct EnumerationTraits<physics::LayerMask>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{physics::LayerMask::LAYER_0, "LAYER_0"},
        EnumeratorName{physics::LayerMask::LAYER_1, "LAYER_1"},
        EnumeratorName{physics::LayerMask::LAYER_2, "LAYER_2"},
        EnumeratorName{physics::LayerMask::LAYER_3, "LAYER_3"},
        EnumeratorName{physics::LayerMask::LAYER_4, "LAYER_4"},
        EnumeratorName{physics::LayerMask::LAYER_5, "LAYER_5"},
        EnumeratorName{physics::LayerMask::LAYER_6, "LAYER_6"},
        EnumeratorName{physics::LayerMask::LAYER_7, "LAYER_7"},
        EnumeratorName{physics::LayerMask::LAYER_8, "LAYER_8"},
        EnumeratorName{physics::LayerMask::LAYER_9, "LAYER_9"},
        EnumeratorName{physics::LayerMask::LAYER_10, "LAYER_10"},
        EnumeratorName{physics::LayerMask::LAYER_11, "LAYER_11"},
        EnumeratorName{physics::LayerMask::LAYER_12, "LAYER_12"},
        EnumeratorName{physics::LayerMask::LAYER_13, "LAYER_13"},
        EnumeratorName{physics::LayerMask::LAYER_14, "LAYER_14"},
        EnumeratorName{physics::LayerMask::LAYER_15, "LAYER_15"},
        EnumeratorName{physics::LayerMask::LAYER_16, "LAYER_16"},
        EnumeratorName{physics::LayerMask::LAYER_17, "LAYER_17"},
        EnumeratorName{physics::LayerMask::LAYER_18, "LAYER_18"},
        EnumeratorName{physics::LayerMask::LAYER_19, "LAYER_19"},
        EnumeratorName{physics::LayerMask::LAYER_20, "LAYER_20"},
        EnumeratorName{physics::LayerMask::LAYER_21, "LAYER_21"},
        EnumeratorName{physics::LayerMask::LAYER_22, "LAYER_22"},
        EnumeratorName{physics::LayerMask::LAYER_23, "LAYER_23"},
        EnumeratorName{physics::LayerMask::LAYER_24, "LAYER_24"},
        EnumeratorName{physics::LayerMask::LAYER_25, "LAYER_25"},
        EnumeratorName{physics::LayerMask::LAYER_26, "LAYER_26"},
        EnumeratorName{physics::LayerMask::LAYER_27, "LAYER_27"},
        EnumeratorName{physics::LayerMask::LAYER_28, "LAYER_28"},
        EnumeratorName{physics::LayerMask::LAYER_29, "LAYER_29"},
        EnumeratorName{physics::LayerMask::LAYER_30, "LAYER_30"},
        EnumeratorName{physics::LayerMask::LAYER_31, "LAYER_31"},
        EnumeratorName{physics::LayerMask::LAYER_32, "LAYER_32"},
        EnumeratorName{physics::LayerMask::LAYER_33, "LAYER_33"},
        EnumeratorName{physics::LayerMask::LAYER_34, "LAYER_34"},
        EnumeratorName{physics::LayerMask::LAYER_35, "LAYER_35"},
        EnumeratorName{physics::LayerMask::LAYER_36, "LAYER_36"},
        EnumeratorName{physics::LayerMask::LAYER_37, "LAYER_37"},
        EnumeratorName{physics::LayerMask::LAYER_38, "LAYER_38"},
        EnumeratorName{physics::LayerMask::LAYER_39, "LAYER_39"},
        EnumeratorName{physics::LayerMask::LAYER_40, "LAYER_40"},
        EnumeratorName{physics::LayerMask::LAYER_41, "LAYER_41"},
        EnumeratorName{physics::LayerMask::LAYER_42, "LAYER_42"},
        EnumeratorName{physics::LayerMask::LAYER_43, "LAYER_43"},
        EnumeratorName{physics::LayerMask::LAYER_44, "LAYER_44"},
        EnumeratorName{physics::LayerMask::LAYER_45, "LAYER_45"},
        EnumeratorName{physics::LayerMask::LAYER_46, "LAYER_46"},
        EnumeratorName{physics::LayerMask::LAYER_47, "LAYER_47"},
        EnumeratorName{physics::LayerMask::LAYER_48, "LAYER_48"},
        EnumeratorName{physics::LayerMask::LAYER_49, "LAYER_49"},
        EnumeratorName{physics::LayerMask::LAYER_50, "LAYER_50"},
        EnumeratorName{physics::LayerMask::LAYER_51, "LAYER_51"},
        EnumeratorName{physics::LayerMask::LAYER_52, "LAYER_52"},
        EnumeratorName{physics::LayerMask::LAYER_53, "LAYER_53"},
        EnumeratorName{physics::LayerMask::LAYER_54, "LAYER_54"},
        EnumeratorName{physics::LayerMask::LAYER_55, "LAYER_55"},
        EnumeratorName{physics::LayerMask::LAYER_56, "LAYER_56"},
        EnumeratorName{physics::LayerMask::LAYER_57, "LAYER_57"},
        EnumeratorName{physics::LayerMask::LAYER_58, "LAYER_58"},
        EnumeratorName{physics::LayerMask::LAYER_59, "LAYER_59"},
        EnumeratorName{physics::LayerMask::LAYER_60, "LAYER_60"},
        EnumeratorName{physics::LayerMask::LAYER_61, "LAYER_61"},
        EnumeratorName{physics::LayerMask::LAYER_62, "LAYER_62"},
        EnumeratorName{physics::LayerMask::LAYER_63, "LAYER_63"},
    };
};

template<>
struct EnumerationTraits<camera::CameraBackendKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::camera"; ///< Errors name these "gfx::camera::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{camera::CameraBackendKind::STANDARD, "STANDARD"},
        EnumeratorName{camera::CameraBackendKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<camera::CameraProjectionKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{camera::CameraProjectionKind::PERSPECTIVE, "PERSPECTIVE"},
        EnumeratorName{camera::CameraProjectionKind::ORTHOGRAPHIC, "ORTHOGRAPHIC"},
    };
};

template<>
struct EnumerationTraits<camera::CameraControllerKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{camera::CameraControllerKind::FREE_FLY, "FREE_FLY"},
        EnumeratorName{camera::CameraControllerKind::FIRST_PERSON, "FIRST_PERSON"},
        EnumeratorName{camera::CameraControllerKind::ORBIT, "ORBIT"},
    };
};

template<>
struct EnumerationTraits<camera::NormalizedDeviceYAxis>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{camera::NormalizedDeviceYAxis::UP, "UP"},
        EnumeratorName{camera::NormalizedDeviceYAxis::DOWN, "DOWN"},
    };
};

template<>
struct EnumerationTraits<camera::ReferenceFrame>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{camera::ReferenceFrame::WORLD, "WORLD"},
        EnumeratorName{camera::ReferenceFrame::LOCAL, "LOCAL"},
    };
};

} // namespace gfx

namespace gfx::enumeration_verification
{

static_assert(verify<physics::PhysicsBackendKind>(), "EnumerationTraits<physics::PhysicsBackendKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::PhysicsSubsystem>(), "EnumerationTraits<physics::PhysicsSubsystem>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::PhysicsCapability>(), "EnumerationTraits<physics::PhysicsCapability>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::BodyKind>(), "EnumerationTraits<physics::BodyKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::ShapeKind>(), "EnumerationTraits<physics::ShapeKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::JointKind>(), "EnumerationTraits<physics::JointKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::DistanceMode>(), "EnumerationTraits<physics::DistanceMode>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::ConstraintKind>(), "EnumerationTraits<physics::ConstraintKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::ConstraintRowKind>(), "EnumerationTraits<physics::ConstraintRowKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::SolverKind>(), "EnumerationTraits<physics::SolverKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::IntegratorKind>(), "EnumerationTraits<physics::IntegratorKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::SpatialStructureKind>(), "EnumerationTraits<physics::SpatialStructureKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::BroadPhaseKind>(), "EnumerationTraits<physics::BroadPhaseKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::NarrowPhaseKind>(), "EnumerationTraits<physics::NarrowPhaseKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::MotionQuality>(), "EnumerationTraits<physics::MotionQuality>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::TriggerEventKind>(), "EnumerationTraits<physics::TriggerEventKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::QueryFailureKind>(), "EnumerationTraits<physics::QueryFailureKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::FrustumIntersection>(), "EnumerationTraits<physics::FrustumIntersection>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::FrustumPlane>(), "EnumerationTraits<physics::FrustumPlane>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::PlaneSide>(), "EnumerationTraits<physics::PlaneSide>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::DepthRange>(), "EnumerationTraits<physics::DepthRange>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::GjkStatus>(), "EnumerationTraits<physics::GjkStatus>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::EpaStatus>(), "EnumerationTraits<physics::EpaStatus>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::ContactAlgorithm>(), "EnumerationTraits<physics::ContactAlgorithm>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::JointAxis>(), "EnumerationTraits<physics::JointAxis>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::AxisMotion>(), "EnumerationTraits<physics::AxisMotion>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::QueryEngine>(), "EnumerationTraits<physics::QueryEngine>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::ContactEventKind>(), "EnumerationTraits<physics::ContactEventKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::JointEventKind>(), "EnumerationTraits<physics::JointEventKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::SleepEventKind>(), "EnumerationTraits<physics::SleepEventKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::StepPhase>(), "EnumerationTraits<physics::StepPhase>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::SoftBodyConstraintKind>(), "EnumerationTraits<physics::SoftBodyConstraintKind>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::AxisPlane>(), "EnumerationTraits<physics::AxisPlane>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::MotionLock>(), "EnumerationTraits<physics::MotionLock>::NAMES is out of sync with the enumeration");
static_assert(verify<physics::LayerMask>(), "EnumerationTraits<physics::LayerMask>::NAMES is out of sync with the enumeration");
static_assert(verify<camera::CameraBackendKind>(), "EnumerationTraits<camera::CameraBackendKind>::NAMES is out of sync with the enumeration");
static_assert(verify<camera::CameraProjectionKind>(), "EnumerationTraits<camera::CameraProjectionKind>::NAMES is out of sync with the enumeration");
static_assert(verify<camera::CameraControllerKind>(), "EnumerationTraits<camera::CameraControllerKind>::NAMES is out of sync with the enumeration");
static_assert(verify<camera::NormalizedDeviceYAxis>(), "EnumerationTraits<camera::NormalizedDeviceYAxis>::NAMES is out of sync with the enumeration");
static_assert(verify<camera::ReferenceFrame>(), "EnumerationTraits<camera::ReferenceFrame>::NAMES is out of sync with the enumeration");

} // namespace gfx::enumeration_verification
