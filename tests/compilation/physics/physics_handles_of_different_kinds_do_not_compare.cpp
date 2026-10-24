// A rigid body handle and a collider handle are different kinds: they cannot be compared or mixed.
import gfx.physics.handles;

auto main() -> int
{
    const gfx::physics::RigidBodyHandle body{};
    const gfx::physics::ColliderHandle collider{};
    return body == collider ? 0 : 1;
}
