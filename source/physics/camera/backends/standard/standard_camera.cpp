// The standard camera: a validated CameraDescription and the matrices and frustum computed from it whenever it
// changes, so that every getter is a copy (readers on several threads need no lock while no thread changes it).
module gfx.camera.backends.standard;

import std;
import gfx.core.errors;
import gfx.camera.interface;

namespace gfx::camera::standard
{

namespace
{

/// The standard camera.
class StandardCamera final : public Camera
{
public:
    explicit StandardCamera(const CameraDescription& description)
        : Camera(CameraBackendKind::STANDARD)
    {
        apply(description, "create_camera");
    }

    [[nodiscard]] auto description() const noexcept -> CameraDescription override
    {
        return m_description;
    }

    [[nodiscard]] auto position() const noexcept -> Vector3 override
    {
        return m_description.position;
    }

    [[nodiscard]] auto orientation() const noexcept -> Quaternion override
    {
        return m_description.orientation;
    }

    [[nodiscard]] auto projection() const noexcept -> CameraProjection override
    {
        return m_description.projection;
    }

    [[nodiscard]] auto clip_conventions() const noexcept -> ClipConventions override
    {
        return m_description.clip;
    }

    void set_position(Vector3 position) override
    {
        CameraDescription changed = m_description;
        changed.position = position;
        apply(changed, "Camera::set_position");
    }

    void set_orientation(const Quaternion& orientation) override
    {
        CameraDescription changed = m_description;
        changed.orientation = orientation;
        apply(changed, "Camera::set_orientation");
    }

    void set_pose(Vector3 position,
                  const Quaternion& orientation) override
    {
        CameraDescription changed = m_description;
        changed.position = position;
        changed.orientation = orientation;
        apply(changed, "Camera::set_pose");
    }

    void set_projection(const CameraProjection& projection) override
    {
        CameraDescription changed = m_description;
        changed.projection = projection;
        apply(changed, "Camera::set_projection");
    }

    void set_clip_conventions(const ClipConventions& conventions) override
    {
        CameraDescription changed = m_description;
        changed.clip = conventions;
        apply(changed, "Camera::set_clip_conventions");
    }

    void set_aspect_ratio(float aspect_ratio) override
    {
        // A perspective keeps its vertical field of view; an orthographic box its height and center.
        if (!std::isfinite(aspect_ratio) || !(aspect_ratio > 0.0F))
        {
            throw InvalidUsageError({.backend = backend(),
                                     .operation = "Camera::set_aspect_ratio",
                                     .problem = std::format("the aspect ratio must be finite and greater than 0 (it is {})", aspect_ratio),
                                     .suggestion = "give the image width over its height; skip the call while a window is minimized (0 × 0)"});
        }
        CameraDescription changed = m_description;
        if (auto* perspective = std::get_if<PerspectiveProjection>(&changed.projection))
        {
            perspective->aspect_ratio = aspect_ratio;
        }
        else
        {
            auto& box = std::get<OrthographicProjection>(changed.projection);
            const float middle = (box.left + box.right) * 0.5F;
            const float half_width = (box.top - box.bottom) * aspect_ratio * 0.5F;
            box.left = middle - half_width;
            box.right = middle + half_width;
        }
        apply(changed, "Camera::set_aspect_ratio");
    }

    [[nodiscard]] auto view() const noexcept -> Matrix4 override
    {
        return m_view;
    }

    [[nodiscard]] auto projection_matrix() const noexcept -> Matrix4 override
    {
        return m_projection;
    }

    [[nodiscard]] auto view_projection() const noexcept -> Matrix4 override
    {
        return m_view_projection;
    }

    [[nodiscard]] auto inverse_view() const noexcept -> Matrix4 override
    {
        return m_inverse_view;
    }

    [[nodiscard]] auto inverse_projection() const noexcept -> Matrix4 override
    {
        return m_inverse_projection;
    }

    [[nodiscard]] auto inverse_view_projection() const noexcept -> Matrix4 override
    {
        return m_inverse_view_projection;
    }

    [[nodiscard]] auto frustum() const noexcept -> Frustum override
    {
        return m_frustum;
    }

private:
    /// Validates @p candidate, keeps it with its orientation normalized and recomputes what depends on it; an invalid
    /// candidate leaves the camera as it was.
    void apply(const CameraDescription& candidate,
               std::string_view operation)
    {
        validate_camera_description(candidate, operation, backend());
        m_description = candidate;
        m_description.orientation = normalized(candidate.orientation);

        // The inverses are composed from the parts' inverses (exact for the view, one 4×4 inverse for the projection).
        m_view = view_matrix(m_description);
        m_inverse_view = Matrix4::from_transform(camera_transform(m_description));
        m_projection = camera::projection_matrix(m_description);
        m_inverse_projection = inverse(m_projection);
        m_view_projection = m_projection * m_view;
        m_inverse_view_projection = m_inverse_view * m_inverse_projection;
        m_frustum = camera_frustum(m_view_projection, m_description.clip);
    }

private:
    CameraDescription m_description{};
    Matrix4 m_view{};
    Matrix4 m_inverse_view{};
    Matrix4 m_projection{};
    Matrix4 m_inverse_projection{};
    Matrix4 m_view_projection{};
    Matrix4 m_inverse_view_projection{};
    Frustum m_frustum{};
};

} // namespace

auto
unavailable_reason() -> std::string
{
    return {};
}

auto
create_camera(const CameraDescription& description) -> std::unique_ptr<Camera>
{
    return std::make_unique<StandardCamera>(description);
}

} // namespace gfx::camera::standard
