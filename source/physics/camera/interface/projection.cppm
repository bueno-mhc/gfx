/// @file projection.cppm
/// @brief What a camera is made of, as values: projections, clip conventions, the camera description, screen
///        viewports, and every matrix and geometric function as a constexpr free function (usable without a camera
///        object, at compile time too).
///
/// Conventions: right-handed world and view space; the camera looks along -Z with +Y up and +X right in view space;
/// matrices are column-major and multiply column vectors (`clip = projection * view * world`); orientations are unit
/// quaternions (Hamilton). Screen coordinates are pixels of a viewport, origin at its top-left corner, +Y down (pixel
/// (i, j) covers [i, i + 1) × [j, j + 1); its center is (i + 0.5, j + 0.5)). No unit is assumed: near and far planes,
/// positions and sizes are in the caller's unit.
export module gfx.camera.interface:projection;

import std;
import gfx.core.math;
import gfx.core.types;
import gfx.physics.enums;
import gfx.physics.primitives;

export namespace gfx::camera
{

// The primitives cameras produce and test, named in the camera's namespace too.
using physics::Aabb;
using physics::DepthRange;
using physics::Frustum;
using physics::FrustumIntersection;
using physics::FrustumPlane;
using physics::Obb;
using physics::Plane;
using physics::Ray;
using physics::Sphere;

// ===================================================================================================== projections

/// @brief A symmetric perspective projection.
struct PerspectiveProjection
{
    float vertical_field_of_view = 1.0F; ///< The angle between the bottom and top planes, in radians, in (0, π).
    float aspect_ratio = 1.0F;           ///< Width over height of the image, greater than 0.
    float near_plane = 0.1F;             ///< Distance of the near plane along the view direction, greater than 0.
    float far_plane = 1000.0F;           ///< Distance of the far plane, greater than near_plane, or INFINITE_FAR_PLANE.

    /// @brief Equal members.
    [[nodiscard]] constexpr auto operator==(const PerspectiveProjection&) const noexcept -> bool = default;
};

/// @brief An orthographic projection: the box [left, right] × [bottom, top] × [-far_plane, -near_plane] of view space.
struct OrthographicProjection
{
    float left = -1.0F;        ///< Smallest view-space x seen.
    float right = 1.0F;        ///< Largest view-space x seen, greater than left.
    float bottom = -1.0F;      ///< Smallest view-space y seen.
    float top = 1.0F;          ///< Largest view-space y seen, greater than bottom.
    float near_plane = 0.0F;   ///< Distance of the near plane along the view direction (may be negative: behind the camera).
    float far_plane = 100.0F;  ///< Distance of the far plane, greater than near_plane and finite.

    /// @brief Equal members.
    [[nodiscard]] constexpr auto operator==(const OrthographicProjection&) const noexcept -> bool = default;
};

/// @brief The size of a centered orthographic projection (orthographic() builds it).
struct OrthographicSize
{
    float width = 2.0F;       ///< Width of the box seen.
    float height = 2.0F;      ///< Height of the box seen.
    float near_plane = 0.0F;  ///< Distance of the near plane.
    float far_plane = 100.0F; ///< Distance of the far plane.
};

/// @brief The orthographic projection of @p size, centered on the view direction.
[[nodiscard]] constexpr auto
orthographic(const OrthographicSize& size) noexcept -> OrthographicProjection
{
    return {.left = -size.width * 0.5F,
            .right = size.width * 0.5F,
            .bottom = -size.height * 0.5F,
            .top = size.height * 0.5F,
            .near_plane = size.near_plane,
            .far_plane = size.far_plane};
}

/// @brief A camera's projection: perspective or orthographic.
using CameraProjection = std::variant<PerspectiveProjection, OrthographicProjection>;

/// @brief The kind of @p projection.
[[nodiscard]] constexpr auto
projection_kind(const CameraProjection& projection) noexcept -> CameraProjectionKind
{
    return std::holds_alternative<PerspectiveProjection>(projection) ? CameraProjectionKind::PERSPECTIVE : CameraProjectionKind::ORTHOGRAPHIC;
}

/// @brief What the renderer expects of a projection matrix: the depth range of clip space, whether depth is reversed
///        (near plane at the far end of the range: better precision with floating-point depth buffers), and where
///        +Y points in normalized device coordinates.
struct ClipConventions
{
    DepthRange depth_range = DepthRange::ZERO_TO_ONE;      ///< ZERO_TO_ONE (Vulkan, Direct3D, Metal, WebGPU) or NEGATIVE_ONE_TO_ONE (OpenGL).
    bool reversed_depth = false;                           ///< The near plane at the range's high end, the far plane at its low end.
    NormalizedDeviceYAxis y_axis = NormalizedDeviceYAxis::UP; ///< UP, or DOWN for Vulkan with a positive-height viewport.

    /// @brief Equal members.
    [[nodiscard]] constexpr auto operator==(const ClipConventions&) const noexcept -> bool = default;
};

/// @brief Everything a camera is: its projection, its clip conventions and its pose.
struct CameraDescription
{
    CameraProjection projection{PerspectiveProjection{}}; ///< The projection.
    ClipConventions clip{};                               ///< The clip conventions of the projection matrix.
    Vector3 position{};                                   ///< Where the camera is.
    Quaternion orientation{};                             ///< Its rotation from looking along -Z with +Y up (a unit quaternion).
};

/// @brief A rectangle of the screen, in pixels: origin at the top-left corner, +Y down.
struct ScreenViewport
{
    float x = 0.0F;      ///< Left edge.
    float y = 0.0F;      ///< Top edge.
    float width = 1.0F;  ///< Width, greater than 0.
    float height = 1.0F; ///< Height, greater than 0.
};

// ===================================================================================================== matrices

namespace projection_detail
{

/// Whether @p value is neither infinite nor NaN (usable in constant expressions, where inf - inf is not).
[[nodiscard]] constexpr auto
is_finite(float value) noexcept -> bool
{
    constexpr float LARGEST = std::numeric_limits<float>::max();
    return value >= -LARGEST && value <= LARGEST;
}

/// The depths the near and far planes map to.
struct DepthTargets
{
    float near_depth = 0.0F;
    float far_depth = 1.0F;
};

[[nodiscard]] constexpr auto
depth_targets(const ClipConventions& clip) noexcept -> DepthTargets
{
    const float low = clip.depth_range == DepthRange::ZERO_TO_ONE ? 0.0F : -1.0F;
    return clip.reversed_depth ? DepthTargets{.near_depth = 1.0F, .far_depth = low} : DepthTargets{.near_depth = low, .far_depth = 1.0F};
}

[[nodiscard]] constexpr auto
y_sign(const ClipConventions& clip) noexcept -> float
{
    return clip.y_axis == NormalizedDeviceYAxis::UP ? 1.0F : -1.0F;
}

} // namespace projection_detail

/// @brief The matrix of @p projection under @p clip: view space to clip space.
///
/// Clip depth is `(scale z + offset) / -z`, with scale and offset chosen so that z = -near_plane gives the near depth
/// and z = -far_plane the far depth of @p clip (0 and 1, 1 and 0 when reversed, -1 and 1 for OpenGL); with an infinite
/// far plane, the limit of both as far_plane grows.
[[nodiscard]] constexpr auto
perspective_matrix(const PerspectiveProjection& projection,
                   const ClipConventions& clip = {}) noexcept -> Matrix4
{
    const float focal = 1.0F / tangent(projection.vertical_field_of_view * 0.5F);
    const auto [near_depth, far_depth] = projection_detail::depth_targets(clip);
    const float near_distance = projection.near_plane;
    float scale = -far_depth;
    float offset = near_distance * (near_depth - far_depth);
    if (projection.far_plane != INFINITE_FAR_PLANE)
    {
        const float far_distance = projection.far_plane;
        scale = (near_depth * near_distance - far_depth * far_distance) / (far_distance - near_distance);
        offset = near_depth * near_distance + scale * near_distance;
    }
    return {.columns = {Vector4{.x = focal / projection.aspect_ratio}, Vector4{.y = projection_detail::y_sign(clip) * focal},
                        Vector4{.z = scale, .w = -1.0F}, Vector4{.z = offset}}};
}

/// @brief The matrix of @p projection under @p clip: view space to clip space (w stays 1).
[[nodiscard]] constexpr auto
orthographic_matrix(const OrthographicProjection& projection,
                    const ClipConventions& clip = {}) noexcept -> Matrix4
{
    const auto [near_depth, far_depth] = projection_detail::depth_targets(clip);
    const float width = projection.right - projection.left;
    const float height = projection.top - projection.bottom;
    const float scale = (near_depth - far_depth) / (projection.far_plane - projection.near_plane);
    const float offset = near_depth + scale * projection.near_plane;
    const float y_sign = projection_detail::y_sign(clip);
    return {.columns = {Vector4{.x = 2.0F / width}, Vector4{.y = y_sign * 2.0F / height}, Vector4{.z = scale},
                        Vector4{.x = -(projection.right + projection.left) / width, .y = -y_sign * (projection.top + projection.bottom) / height,
                                .z = offset, .w = 1.0F}}};
}

/// @brief The matrix of @p projection under @p clip.
[[nodiscard]] constexpr auto
projection_matrix(const CameraProjection& projection,
                  const ClipConventions& clip = {}) noexcept -> Matrix4
{
    if (const auto* perspective = std::get_if<PerspectiveProjection>(&projection))
    {
        return perspective_matrix(*perspective, clip);
    }
    return orthographic_matrix(std::get<OrthographicProjection>(projection), clip);
}

/// @brief The view matrix of a camera at @p position turned by @p orientation: world space to view space.
[[nodiscard]] constexpr auto
view_matrix(Vector3 position,
            const Quaternion& orientation) noexcept -> Matrix4
{
    return Matrix4::from_transform(inverse(Transform{.position = position, .orientation = orientation}));
}

/// @brief The camera's transform: view space to world space (the inverse of the view matrix).
[[nodiscard]] constexpr auto
camera_transform(const CameraDescription& description) noexcept -> Transform
{
    return {.position = description.position, .orientation = description.orientation};
}

/// @brief The view matrix of @p description.
[[nodiscard]] constexpr auto
view_matrix(const CameraDescription& description) noexcept -> Matrix4
{
    return view_matrix(description.position, description.orientation);
}

/// @brief The projection matrix of @p description.
[[nodiscard]] constexpr auto
projection_matrix(const CameraDescription& description) noexcept -> Matrix4
{
    return projection_matrix(description.projection, description.clip);
}

/// @brief The view-projection matrix of @p description: world space to clip space.
[[nodiscard]] constexpr auto
view_projection_matrix(const CameraDescription& description) noexcept -> Matrix4
{
    return projection_matrix(description) * view_matrix(description);
}

// ===================================================================================================== orientation

/// @brief The orientation that looks along the unit vector @p forward with its up as close to @p up as possible;
///        when @p forward is parallel to @p up, the right axis is any perpendicular (Vector3's any_perpendicular()).
///        std::nullopt when @p up has no direction.
[[nodiscard]] constexpr auto
orientation_toward(Vector3 forward,
                   Vector3 up) noexcept -> std::optional<Quaternion>
{
    // The columns of the rotation: right, up and back (+Z of view space points backward).
    if (length_squared(up) <= 0.0F)
    {
        return std::nullopt;
    }
    const Vector3 back = -forward;
    Vector3 right = cross(up, back);
    right = length_squared(right) <= 1.0e-12F * length_squared(up) ? any_perpendicular(back) : normalized(right);
    const Vector3 camera_up = cross(back, right);
    return Quaternion::from_rotation_matrix(Matrix3{.columns = {right, camera_up, back}});
}

/// @brief The orientation of a camera at @p eye looking at @p target with its up as close to @p up as possible;
///        std::nullopt when @p target is @p eye or @p up has no direction.
[[nodiscard]] constexpr auto
look_at_orientation(Vector3 eye,
                    Vector3 target,
                    Vector3 up = {.y = 1.0F}) noexcept -> std::optional<Quaternion>
{
    const Vector3 toward = target - eye;
    if (length_squared(toward) <= 0.0F)
    {
        return std::nullopt;
    }
    return orientation_toward(normalized(toward), up);
}

// ===================================================================================================== frustums

/// @brief The world-space frustum of the view-projection matrix @p view_projection made under @p clip: six planes
///        facing in (Frustum::planes order), the far plane without direction when it is at infinity.
[[nodiscard]] constexpr auto
camera_frustum(const Matrix4& view_projection,
               const ClipConventions& clip = {}) noexcept -> Frustum
{
    // Extraction labels planes for +Y up; with +Y down, the plane it calls bottom is the top one.
    Frustum frustum = Frustum::from_matrix(view_projection, clip.depth_range, clip.reversed_depth);
    if (clip.y_axis == NormalizedDeviceYAxis::DOWN)
    {
        std::swap(frustum.planes[std::to_underlying(FrustumPlane::BOTTOM)], frustum.planes[std::to_underlying(FrustumPlane::TOP)]);
    }
    return frustum;
}

/// @brief The world-space frustum of @p description.
[[nodiscard]] constexpr auto
camera_frustum(const CameraDescription& description) noexcept -> Frustum
{
    return camera_frustum(view_projection_matrix(description), description.clip);
}

// ===================================================================================================== screen

/// @brief The normalized device coordinates (x, y) of the screen point @p point of @p viewport under @p clip.
[[nodiscard]] constexpr auto
screen_to_normalized_device(Vector2 point,
                            const ScreenViewport& viewport,
                            const ClipConventions& clip = {}) noexcept -> Vector2
{
    const float across = (point.x - viewport.x) / viewport.width;
    const float down = (point.y - viewport.y) / viewport.height;
    return {.x = across * 2.0F - 1.0F, .y = clip.y_axis == NormalizedDeviceYAxis::UP ? 1.0F - down * 2.0F : down * 2.0F - 1.0F};
}

/// @brief The world-space ray through the screen point @p point of @p viewport, from the near plane, for the inverse
///        view-projection matrix @p inverse_view_projection made under @p clip. Perspective rays spread from the
///        camera; orthographic rays are parallel to the view direction; reversed and infinite depth work alike (the
///        direction comes from the near point and a point halfway through the depth range, never the far end).
[[nodiscard]] constexpr auto
screen_to_world_ray(const Matrix4& inverse_view_projection,
                    Vector2 point,
                    const ScreenViewport& viewport,
                    const ClipConventions& clip = {}) noexcept -> Ray
{
    const Vector2 device = screen_to_normalized_device(point, viewport, clip);
    const auto [near_depth, far_depth] = projection_detail::depth_targets(clip);
    const Vector3 near_point = project_point(inverse_view_projection, {.x = device.x, .y = device.y, .z = near_depth});
    const Vector3 middle_point = project_point(inverse_view_projection, {.x = device.x, .y = device.y, .z = (near_depth + far_depth) * 0.5F});
    return {.origin = near_point, .direction = normalized(middle_point - near_point)};
}

/// @brief Where the world-space point @p point appears in @p viewport for the view-projection matrix
///        @p view_projection made under @p clip: (x, y) in pixels and z the clip depth (in the depth range of @p clip
///        when the point is between the near and far planes). std::nullopt for a point on or behind the camera's
///        plane (perspective projections).
[[nodiscard]] constexpr auto
world_to_screen(const Matrix4& view_projection,
                Vector3 point,
                const ScreenViewport& viewport,
                const ClipConventions& clip = {}) noexcept -> std::optional<Vector3>
{
    const Vector4 clipped = view_projection * to_vector4(point, 1.0F);
    if (clipped.w <= 0.0F)
    {
        return std::nullopt;
    }
    const Vector3 device = to_vector3(clipped) / clipped.w;
    const float down = clip.y_axis == NormalizedDeviceYAxis::UP ? (1.0F - device.y) * 0.5F : (device.y + 1.0F) * 0.5F;
    return Vector3{.x = viewport.x + (device.x + 1.0F) * 0.5F * viewport.width, .y = viewport.y + down * viewport.height, .z = device.z};
}

// ===================================================================================================== validity

/// @brief What is wrong with a projection or a pose: static text, the value at fault and what to do
///        (camera_problem()).
struct CameraProblem
{
    std::string_view problem{};    ///< What is wrong, e.g. "the vertical field of view must be in (0, π) radians".
    float value = 0.0F;            ///< The value at fault.
    std::string_view suggestion{}; ///< What to do instead.
};

/// @brief The first problem of @p description (std::nullopt when it is valid): every value finite where it must be,
///        a field of view in (0, π), positive aspect ratio and near plane, a far plane beyond the near one (infinite
///        only for perspective projections), bounds in increasing order, an orientation of non-zero length.
[[nodiscard]] constexpr auto
camera_problem(const CameraDescription& description) noexcept -> std::optional<CameraProblem>
{
    using projection_detail::is_finite;
    if (const auto* perspective = std::get_if<PerspectiveProjection>(&description.projection))
    {
        if (!(perspective->vertical_field_of_view > 0.0F && perspective->vertical_field_of_view < std::numbers::pi_v<float>))
        {
            return CameraProblem{.problem = "the vertical field of view must be in (0, π) radians",
                                 .value = perspective->vertical_field_of_view,
                                 .suggestion = "give the angle in radians (degrees × π / 180; 1.0 is about 57°)"};
        }
        if (!(perspective->aspect_ratio > 0.0F) || !is_finite(perspective->aspect_ratio))
        {
            return CameraProblem{.problem = "the aspect ratio must be finite and greater than 0",
                                 .value = perspective->aspect_ratio,
                                 .suggestion = "give the image width over its height (16 / 9 for 1920 × 1080)"};
        }
        if (!(perspective->near_plane > 0.0F) || !is_finite(perspective->near_plane))
        {
            return CameraProblem{.problem = "the near plane of a perspective projection must be finite and greater than 0",
                                 .value = perspective->near_plane,
                                 .suggestion = "give a small positive distance (a perspective divides by it); reversed depth keeps precision with a tiny one"};
        }
        if (!(perspective->far_plane > perspective->near_plane))
        {
            return CameraProblem{.problem = "the far plane must be beyond the near plane (or INFINITE_FAR_PLANE)",
                                 .value = perspective->far_plane,
                                 .suggestion = "give a far distance greater than the near one, or INFINITE_FAR_PLANE"};
        }
    }
    else
    {
        const auto& box = std::get<OrthographicProjection>(description.projection);
        if (!is_finite(box.left) || !is_finite(box.right) || !(box.right > box.left))
        {
            return CameraProblem{.problem = "the right bound must be finite and greater than the left one",
                                 .value = box.right,
                                 .suggestion = "give finite bounds with right > left (orthographic() centers a width)"};
        }
        if (!is_finite(box.bottom) || !is_finite(box.top) || !(box.top > box.bottom))
        {
            return CameraProblem{.problem = "the top bound must be finite and greater than the bottom one",
                                 .value = box.top,
                                 .suggestion = "give finite bounds with top > bottom (orthographic() centers a height)"};
        }
        if (!is_finite(box.near_plane) || !is_finite(box.far_plane) || !(box.far_plane > box.near_plane))
        {
            return CameraProblem{.problem = "the far plane of an orthographic projection must be finite and beyond the near plane",
                                 .value = box.far_plane,
                                 .suggestion = "give finite near and far distances with far > near (orthographic depth is linear: no infinite far plane)"};
        }
    }
    if (!is_finite(description.position.x) || !is_finite(description.position.y) || !is_finite(description.position.z))
    {
        return CameraProblem{.problem = "the position must be finite",
                                 .value = description.position.x + description.position.y + description.position.z,
                                 .suggestion = "give finite coordinates"};
    }
    const float size = length(description.orientation);
    if (!(size > 0.0F) || !is_finite(size))
    {
        return CameraProblem{.problem = "the orientation must be a quaternion of finite, non-zero length",
                                 .value = size,
                                 .suggestion = "give a rotation (Quaternion::from_axis_angle(), look_at_orientation()); the identity looks along -Z"};
    }
    return std::nullopt;
}

} // namespace gfx::camera
