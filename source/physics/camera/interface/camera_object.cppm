/// @file camera_object.cppm
/// @brief The abstract camera: a pose, a projection and clip conventions, the matrices and the frustum they make, and
///        what is built on them (axes, turning, moving, looking at a point, culling, screen rays).
///
/// The library's camera comes from create_camera() (gfx.camera.factory); an application's camera derives from Camera
/// and is constructed directly. Each operation that can fail is pure virtual with a definition that throws a
/// descriptive BackendCapabilityError, so a derived camera overrides what it supports and may forward the rest to the
/// base class. Everything below "Built on the operations above" is library code that works for any derived camera.
///
/// A camera is not synchronized: one thread changes it, or several read it while none changes it.
export module gfx.camera.interface:camera;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.core.types;
import gfx.physics.enums;
import gfx.physics.primitives;
import :projection;

export namespace gfx::camera
{

/// @brief A camera: where it is, where it looks, how it projects.
class Camera
{
public:
    virtual ~Camera() = default;

    Camera(const Camera&) = delete;
    auto operator=(const Camera&) -> Camera& = delete;

    /// @brief The implementation (errors name it: "[gfx::camera::STANDARD] ...").
    [[nodiscard]] auto backend() const noexcept -> BackendIdentity
    {
        return m_backend;
    }

    // ------------------------------------------------------------------------------------------------ state

    /// @brief The projection, clip conventions and pose, as one value.
    [[nodiscard]] virtual auto description() const noexcept -> CameraDescription = 0;

    /// @brief Where the camera is.
    [[nodiscard]] virtual auto position() const noexcept -> Vector3 = 0;

    /// @brief Its rotation from looking along -Z with +Y up (a unit quaternion).
    [[nodiscard]] virtual auto orientation() const noexcept -> Quaternion = 0;

    /// @brief Its projection.
    [[nodiscard]] virtual auto projection() const noexcept -> CameraProjection = 0;

    /// @brief The clip conventions of its projection matrix.
    [[nodiscard]] virtual auto clip_conventions() const noexcept -> ClipConventions = 0;

    /// @brief Moves the camera to @p position.
    /// @throws InvalidUsageError if @p position is not finite.
    virtual void set_position(Vector3 position) = 0;

    /// @brief Turns the camera to @p orientation (normalized: any non-zero quaternion is accepted).
    /// @throws InvalidUsageError if @p orientation has no finite, non-zero length.
    virtual void set_orientation(const Quaternion& orientation) = 0;

    /// @brief Moves and turns the camera at once (the matrices are computed once).
    /// @throws InvalidUsageError as set_position() and set_orientation().
    virtual void set_pose(Vector3 position,
                          const Quaternion& orientation) = 0;

    /// @brief Changes the projection.
    /// @throws InvalidUsageError if @p projection is invalid (camera_problem() names the problem).
    virtual void set_projection(const CameraProjection& projection) = 0;

    /// @brief Changes the clip conventions of the projection matrix.
    virtual void set_clip_conventions(const ClipConventions& conventions) = 0;

    /// @brief Fits the projection to an image of aspect ratio @p aspect_ratio (width over height), as after a window
    ///        resize: a perspective projection keeps its vertical field of view, an orthographic one its height and
    ///        center (its width becomes height × @p aspect_ratio).
    /// @throws InvalidUsageError if @p aspect_ratio is not finite and greater than 0.
    virtual void set_aspect_ratio(float aspect_ratio) = 0;

    // ------------------------------------------------------------------------------------------------ matrices

    /// @brief World space to view space.
    [[nodiscard]] virtual auto view() const noexcept -> Matrix4 = 0;

    /// @brief View space to clip space.
    [[nodiscard]] virtual auto projection_matrix() const noexcept -> Matrix4 = 0;

    /// @brief World space to clip space: projection_matrix() × view().
    [[nodiscard]] virtual auto view_projection() const noexcept -> Matrix4 = 0;

    /// @brief View space to world space.
    [[nodiscard]] virtual auto inverse_view() const noexcept -> Matrix4 = 0;

    /// @brief Clip space to view space.
    [[nodiscard]] virtual auto inverse_projection() const noexcept -> Matrix4 = 0;

    /// @brief Clip space to world space.
    [[nodiscard]] virtual auto inverse_view_projection() const noexcept -> Matrix4 = 0;

    /// @brief The six world-space planes of what the camera sees, facing in (camera_frustum()).
    [[nodiscard]] virtual auto frustum() const noexcept -> Frustum = 0;

    // ------------------------------------------------------------------------------------------------ built on the operations above

    /// @brief View space to world space, as a transform.
    [[nodiscard]] auto transform() const noexcept -> Transform;

    /// @brief The world-space direction the camera looks along (its -Z).
    [[nodiscard]] auto forward() const noexcept -> Vector3;

    /// @brief The world-space direction of its +X.
    [[nodiscard]] auto right() const noexcept -> Vector3;

    /// @brief The world-space direction of its +Y.
    [[nodiscard]] auto up() const noexcept -> Vector3;

    /// @brief Its orientation as yaw (about +Y), pitch (about +X) and roll (about +Z), applied roll first
    ///        (to_yaw_pitch_roll(): at a pitch of ±π/2 the roll is 0).
    [[nodiscard]] auto yaw_pitch_roll() const noexcept -> YawPitchRoll;

    /// @brief Turns the camera to the orientation of @p angles (Quaternion::from_yaw_pitch_roll()).
    void set_orientation_from(YawPitchRoll angles);

    /// @brief Turns the camera by @p angle radians about @p axis (counter-clockwise looking against the axis), an axis
    ///        of the world or of the camera (+X right, +Y up, -Z forward).
    /// @throws InvalidUsageError if @p axis has no direction.
    void rotate(Vector3 axis,
                float angle,
                ReferenceFrame frame);

    /// @brief Moves the camera by @p offset, in world space or along its own axes.
    void translate(Vector3 offset,
                   ReferenceFrame frame);

    /// @brief Turns the camera toward @p target, its up as close to @p up as possible (when it looks along @p up, its
    ///        right axis is any perpendicular).
    /// @throws InvalidUsageError if @p target is the camera's position or @p up has no direction.
    void look_at(Vector3 target,
                 Vector3 up = {.y = 1.0F});

    /// @brief Where @p box is with respect to the frustum.
    [[nodiscard]] auto classify(const Aabb& box) const noexcept -> FrustumIntersection;

    /// @brief Where @p sphere is with respect to the frustum.
    [[nodiscard]] auto classify(const Sphere& sphere) const noexcept -> FrustumIntersection;

    /// @brief Where @p box is with respect to the frustum.
    [[nodiscard]] auto classify(const Obb& box) const noexcept -> FrustumIntersection;

    /// @brief Where each of @p boxes is with respect to the frustum, into @p output (SIMD: physics::classify()).
    /// @throws PhysicsError if @p output is shorter than @p boxes.
    void classify(std::span<const Aabb> boxes,
                  std::span<FrustumIntersection> output) const;

    /// @brief Where each of @p spheres is with respect to the frustum, into @p output (SIMD).
    /// @throws PhysicsError if @p output is shorter than @p spheres.
    void classify(std::span<const Sphere> spheres,
                  std::span<FrustumIntersection> output) const;

    /// @brief The world-space ray through the pixel position @p point of @p viewport, from the near plane
    ///        (screen_to_world_ray()).
    [[nodiscard]] auto screen_to_world_ray(Vector2 point,
                                           const ScreenViewport& viewport) const noexcept -> Ray;

    /// @brief Where @p point appears in @p viewport: pixels and clip depth; std::nullopt behind a perspective camera
    ///        (world_to_screen()).
    [[nodiscard]] auto world_to_screen(Vector3 point,
                                       const ScreenViewport& viewport) const noexcept -> std::optional<Vector3>;

protected:
    /// @param backend The implementation, as errors name it.
    explicit Camera(BackendIdentity backend) noexcept
        : m_backend{backend}
    {
    }

private:
    BackendIdentity m_backend;
};

/// @brief Throws InvalidUsageError if @p description is invalid (camera_problem()), naming @p operation and
///        @p backend. Cameras call it before accepting a description; derived cameras can too.
void validate_camera_description(const CameraDescription& description,
                                 std::string_view operation,
                                 BackendIdentity backend);

} // namespace gfx::camera
