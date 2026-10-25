/// @file debug_draw.cppm
/// @brief What a renderer needs to show a physics world, without depending on one (as Box3D's b3DebugDraw or Jolt's
///        DebugRenderer): every shape kind as a triangle mesh, and colored lines for what has no surface — bounding
///        boxes, axes, contact points and normals, joint anchors, soft-body constraints.
///
/// tessellate() turns a shape into a DebugMesh in the shape's own frame, once: shapes are immutable and shared, so a
/// renderer caches the mesh of each shape and draws it at the pose of every collider that uses it. Round shapes
/// (spheres, capsules, cylinders, cones) are revolved profiles; boxes and hulls are their faces and, when rounded, a strip
/// along each edge and a spherical patch at each corner (the exact rounded surface at the vertices); triangle meshes and height fields
/// are their triangles; compounds their children at their poses. Every vertex lies on the shape's surface, every normal
/// points out, triangles wind counter-clockwise seen from outside, and closed shapes give closed meshes.
///
/// DebugLines collects colored line segments in world space; clear() keeps its storage, so refilling it every frame
/// allocates nothing once it has held the largest frame.
export module gfx.physics.debug_draw;

import std;
export import gfx.core.math;
export import gfx.physics.primitives;
export import gfx.physics.shapes;
export import gfx.physics.narrow_phase;

export namespace gfx::physics
{

// ===================================================================================================== meshes

/// @brief A vertex of a shape's mesh.
struct DebugVertex
{
    Vector3 position{}; ///< On the surface, in the shape's frame.
    Vector3 normal{};   ///< The surface's outward unit normal there.

    [[nodiscard]] constexpr auto operator==(const DebugVertex&) const noexcept -> bool = default;
};

/// @brief A shape as triangles: three indices per triangle, counter-clockwise seen from outside.
struct DebugMesh
{
    std::vector<DebugVertex> vertices{};  ///< Positions and normals (a vertex is repeated where the normal changes).
    std::vector<std::uint32_t> indices{}; ///< Three per triangle, into vertices.
    bool two_sided = false;               ///< Seen from both sides (a two-sided triangle mesh): draw it without culling back faces.

    /// @brief How many triangles.
    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t
    {
        return indices.size() / 3;
    }

    /// @brief Empties it, keeping its storage.
    void clear() noexcept
    {
        vertices.clear();
        indices.clear();
        two_sided = false;
    }
};

/// @brief How finely round surfaces are cut into triangles.
struct TessellationDescription
{
    std::uint32_t segments = 32; ///< Around a full circle (spheres, capsules, cylinders, cones, rounded edges and corners), at least 3.
};

/// @brief Appends @p shape's triangles to @p mesh, in the shape's frame (a compound's children at their poses).
/// @throws PhysicsError if @p description has fewer than 3 segments.
void tessellate(const Shape& shape,
                DebugMesh& mesh,
                const TessellationDescription& description = {});

/// @brief @p shape's triangles, in the shape's frame.
/// @throws PhysicsError if @p description has fewer than 3 segments.
[[nodiscard]] auto tessellate(const Shape& shape,
                              const TessellationDescription& description = {}) -> DebugMesh;

/// @brief The volume @p mesh encloses (the divergence theorem over its triangles): positive when its triangles face out.
///        Meaningful for closed meshes only.
[[nodiscard]] auto enclosed_volume(const DebugMesh& mesh) noexcept -> float;

// ===================================================================================================== lines

/// @brief A color: 8-bit red, green, blue and opacity.
struct DebugColor
{
    std::uint8_t red = 255;   ///< Red.
    std::uint8_t green = 255; ///< Green.
    std::uint8_t blue = 255;  ///< Blue.
    std::uint8_t alpha = 255; ///< Opacity (255: opaque).

    [[nodiscard]] constexpr auto operator==(const DebugColor&) const noexcept -> bool = default;

    /// @brief The four bytes in one word: red in the lowest byte, then green, blue and opacity (little-endian RGBA8).
    [[nodiscard]] constexpr auto packed() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(red) | static_cast<std::uint32_t>(green) << 8U | static_cast<std::uint32_t>(blue) << 16U
             | static_cast<std::uint32_t>(alpha) << 24U;
    }
};

/// @brief An end of a line segment.
struct DebugLineVertex
{
    Vector3 position{}; ///< World space.
    DebugColor color{}; ///< Its color.
};

/// @brief Colored line segments in world space: two vertices per segment.
class DebugLines
{
public:
    /// @brief The segments' ends, two per segment.
    [[nodiscard]] auto vertices() const noexcept -> std::span<const DebugLineVertex>
    {
        return m_vertices;
    }

    /// @brief How many segments.
    [[nodiscard]] auto line_count() const noexcept -> std::size_t
    {
        return m_vertices.size() / 2;
    }

    /// @brief Removes every segment, keeping the storage.
    void clear() noexcept
    {
        m_vertices.clear();
    }

    /// @brief A segment from @p from to @p to.
    void add_line(Vector3 from,
                  Vector3 to,
                  DebugColor color);

    /// @brief The 12 edges of @p box.
    void add_box(const Aabb& box,
                 DebugColor color);

    /// @brief The 12 edges of @p box.
    void add_box(const Obb& box,
                 DebugColor color);

    /// @brief @p pose's axes, @p length long: x red, y green, z blue.
    void add_axes(const Transform& pose,
                  float length);

    /// @brief A segment from @p from to @p to with a head of four strokes @p head_size long at @p to (none when the
    ///        segment has no length).
    void add_arrow(Vector3 from,
                   Vector3 to,
                   DebugColor color,
                   float head_size);

    /// @brief Three segments through @p point along the axes, @p size long.
    void add_cross(Vector3 point,
                   float size,
                   DebugColor color);

    /// @brief A circle of @p radius around @p center in the plane perpendicular to @p normal (any length but 0), in
    ///        @p segments segments (at least 3).
    void add_circle(Vector3 center,
                    Vector3 normal,
                    float radius,
                    DebugColor color,
                    std::uint32_t segments = 32);

    /// @brief Each of @p manifold's points as a cross @p point_size long, with an arrow @p normal_length long along the
    ///        manifold's normal (from the first shape to the second).
    void add_contacts(const ContactManifold& manifold,
                      DebugColor color,
                      float point_size,
                      float normal_length);

private:
    std::vector<DebugLineVertex> m_vertices{};
};

} // namespace gfx::physics
