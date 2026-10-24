// Helpers of the physics and camera tests: handles as a world would issue them, projection matrices written out by hand
// (references independent of the camera library), and a deterministic generator of floats.
export module gfx.tests.physics_support;

import std;
import gfx.physics.handles;
import gfx.physics.primitives;

export namespace gfx::tests
{

/// Issues physics handles as a world does (only HandleIssuer descendants can).
class PhysicsHandles : public HandleIssuer
{
public:
    /// The handle of @p Tag at slot @p index, generation @p generation.
    template<class Tag>
    [[nodiscard]] static auto make(std::uint32_t index,
                                   std::uint32_t generation = 1) -> Handle<Tag>
    {
        return Handle<Tag>::create(handle_key(), index, generation);
    }
};

/// A right-handed perspective projection looking along -Z (vertical field of view @p field_of_view, radians), depth
/// mapped to [0, 1] or [-1, 1].
[[nodiscard]] constexpr auto
perspective(float field_of_view,
            float aspect_ratio,
            float near_plane,
            float far_plane,
            physics::DepthRange depth = physics::DepthRange::ZERO_TO_ONE) -> Matrix4
{
    const float focal = 1.0F / tangent(field_of_view * 0.5F);
    const bool zero_to_one = depth == physics::DepthRange::ZERO_TO_ONE;
    const float depth_scale = zero_to_one ? far_plane / (near_plane - far_plane) : (far_plane + near_plane) / (near_plane - far_plane);
    const float depth_offset = zero_to_one ? near_plane * far_plane / (near_plane - far_plane) : 2.0F * near_plane * far_plane / (near_plane - far_plane);
    return {.columns = {Vector4{.x = focal / aspect_ratio}, Vector4{.y = focal}, Vector4{.z = depth_scale, .w = -1}, Vector4{.z = depth_offset}}};
}

/// The same projection with reversed depth (near at 1, far at 0) and the far plane at infinity.
[[nodiscard]] constexpr auto
reversed_infinite_perspective(float field_of_view,
                              float aspect_ratio,
                              float near_plane) -> Matrix4
{
    const float focal = 1.0F / tangent(field_of_view * 0.5F);
    return {.columns = {Vector4{.x = focal / aspect_ratio}, Vector4{.y = focal}, Vector4{.z = 0, .w = -1}, Vector4{.z = near_plane}}};
}

/// "(x, y, z)", for assertion messages.
[[nodiscard]] inline auto
describe(Vector3 vector) -> std::string
{
    return std::format("({}, {}, {})", vector.x, vector.y, vector.z);
}

/// A frustum shaped like a box: the six planes of [-@p half_size, @p half_size] on each axis (inside = within), so
/// tests can place volumes exactly on its planes.
[[nodiscard]] constexpr auto
box_frustum(float half_size) -> physics::Frustum
{
    return {.planes = {physics::Plane{.normal = {.x = 1}, .distance = -half_size}, physics::Plane{.normal = {.x = -1}, .distance = -half_size},
                       physics::Plane{.normal = {.y = 1}, .distance = -half_size}, physics::Plane{.normal = {.y = -1}, .distance = -half_size},
                       physics::Plane{.normal = {.z = 1}, .distance = -half_size}, physics::Plane{.normal = {.z = -1}, .distance = -half_size}}};
}

/// A deterministic generator of floats (xorshift), usable in constant expressions.
class Generator
{
public:
    constexpr explicit Generator(std::uint32_t seed)
        : m_state{seed}
    {
    }

    /// A float in [@p low, @p high].
    constexpr auto between(float low,
                           float high) -> float
    {
        m_state ^= m_state << 13U;
        m_state ^= m_state >> 17U;
        m_state ^= m_state << 5U;
        return low + (high - low) * static_cast<float>(m_state % 1000001U) / 1000000.0F;
    }

    /// A vector with components in [@p low, @p high].
    constexpr auto vector(float low,
                          float high) -> Vector3
    {
        return {.x = between(low, high), .y = between(low, high), .z = between(low, high)};
    }

    /// A unit vector.
    constexpr auto direction() -> Vector3
    {
        Vector3 candidate{};
        while (length_squared(candidate) < 0.01F)
        {
            candidate = vector(-1, 1);
        }
        return normalized(candidate);
    }

    /// A unit quaternion.
    constexpr auto rotation() -> Quaternion
    {
        return normalized(Quaternion{.x = between(-1, 1), .y = between(-1, 1), .z = between(-1, 1), .w = between(-1, 1) + 1.5F});
    }

private:
    std::uint32_t m_state;
};

} // namespace gfx::tests
