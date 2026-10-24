// A shape with a support function but no margin is not a ConvexShape: support_point() refuses it.
import gfx.physics.primitives;

struct Pointy
{
    [[nodiscard]] auto support(gfx::Vector3 /*direction*/) const -> gfx::Vector3
    {
        return {};
    }

    [[nodiscard]] auto bounds() const -> gfx::physics::Aabb
    {
        return {};
    }
};

auto main() -> int
{
    return static_cast<int>(gfx::physics::support_point(Pointy{}, {.x = 1}).x);
}
