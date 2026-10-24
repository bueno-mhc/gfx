// The errors of the physics library: PhysicsError (in the core hierarchy, with the objects concerned), capability gaps
// thrown as the core BackendCapabilityError, and query failures returned in std::expected and raised on request.
#include <gtest/gtest.h>

import std;
import gfx.core.errors;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.tests.physics_support;

namespace
{

using namespace gfx::physics;
using gfx::tests::PhysicsHandles;

TEST(PhysicsErrorsTest, ErrorsNameTheirOriginAndTheObjectsConcerned)
{
    const PhysicsErrorContext context{.world = PhysicsHandles::make<WorldTag>(0), .rigid_body = PhysicsHandles::make<RigidBodyTag>(7, 2)};
    try
    {
        throw PhysicsError({.backend = PhysicsBackendKind::BOX3D,
                            .operation = "RigidBody::set_mass",
                            .problem = "the mass is negative (-1)",
                            .suggestion = "give a positive mass, or make the body static",
                            .context = context});
    }
    catch (const PhysicsError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::physics::BOX3D] RigidBody::set_mass: the mass is negative (-1)")) << message;
        EXPECT_NE(message.find("\n  World: World(index 0, generation 1)"), std::string_view::npos) << message;
        EXPECT_NE(message.find("\n  Body: RigidBody(index 7, generation 2)"), std::string_view::npos) << message;
        EXPECT_NE(message.find("\n  Suggestion: give a positive mass, or make the body static"), std::string_view::npos) << message;
        EXPECT_EQ(message.find("Collider"), std::string_view::npos) << "only the objects concerned are listed: " << message;
        EXPECT_EQ(error.problem(), "the mass is negative (-1)");
        EXPECT_EQ(error.suggestion(), "give a positive mass, or make the body static");
        EXPECT_EQ(error.context().rigid_body, context.rigid_body);
        ASSERT_TRUE(error.backend().has_value());
        EXPECT_TRUE(*error.backend() == PhysicsBackendKind::BOX3D);
    }

    // One catch for every library of gfx.
    EXPECT_THROW(throw PhysicsError({.backend = PhysicsSubsystem::SPATIAL, .operation = "Bvh::remove", .problem = "stale proxy", .suggestion = {}, .context = {}}),
                 gfx::Error);
    EXPECT_EQ(context_lines({}), "");
}

TEST(PhysicsErrorsTest, CapabilityGapsAreCoreCapabilityErrors)
{
    try
    {
        throw_unsupported(PhysicsBackendKind::BOX3D, "PhysicsWorld::create_joint", PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS,
                          "Box3D has no generic six-axis joint", "combine a spherical and a prismatic joint, or use the REFERENCE backend",
                          {.joint = PhysicsHandles::make<JointTag>(3)});
        FAIL() << "throw_unsupported must throw";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx::physics::BOX3D] PhysicsWorld::create_joint")) << message;
        EXPECT_NE(message.find("Box3D has no generic six-axis joint"), std::string_view::npos) << message;
        EXPECT_NE(message.find("Joint: Joint(index 3, generation 1)"), std::string_view::npos) << message;
        EXPECT_NE(message.find("use the REFERENCE backend"), std::string_view::npos) << message;
        EXPECT_EQ(error.capability(), "SIX_DEGREES_OF_FREEDOM_JOINTS");
    }
}

/// A query that finds nothing past a distance, or that a backend cannot answer.
auto
nearest_hit(float distance,
            bool supported) -> QueryResult<float>
{
    if (!supported)
    {
        return std::unexpected(QueryFailure{.kind = QueryFailureKind::UNSUPPORTED,
                                            .operation = "PhysicsWorld::shape_cast",
                                            .detail = "hull casts are not implemented",
                                            .backend = PhysicsBackendKind::NULL_BACKEND,
                                            .capability = PhysicsCapability::WORLD_OBJECTS,
                                            .context = {}});
    }
    if (distance > 10)
    {
        return std::unexpected(QueryFailure{.kind = QueryFailureKind::NO_HIT, .operation = "PhysicsWorld::ray_cast"});
    }
    return distance;
}

TEST(PhysicsErrorsTest, QueryFailuresAreValuesUntilRaised)
{
    EXPECT_EQ(nearest_hit(4, true), 4.0F);

    // Nothing hit: a value that describes itself, raised as a PhysicsError on request.
    const QueryResult<float> missed = nearest_hit(20, true);
    ASSERT_FALSE(missed.has_value());
    EXPECT_EQ(missed.error().message(), "[gfx::physics] PhysicsWorld::ray_cast: NO_HIT");
    EXPECT_THROW(missed.error().raise(), PhysicsError);

    // A backend that cannot answer: the capability is named, and raising it throws the capability error.
    const QueryResult<float> unsupported = nearest_hit(1, false);
    ASSERT_FALSE(unsupported.has_value());
    EXPECT_EQ(unsupported.error().message(),
              "[gfx::physics::NULL_BACKEND] PhysicsWorld::shape_cast: UNSUPPORTED (WORLD_OBJECTS): hull casts are not implemented");
    try
    {
        unsupported.error().raise();
        FAIL() << "raise() must throw";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_EQ(error.capability(), "WORLD_OBJECTS");
        EXPECT_NE(std::string_view{error.what()}.find("hull casts are not implemented"), std::string_view::npos) << error.what();
    }
}

} // namespace
