// Shapes as triangles (revolved profiles for round shapes; faces, edge strips and corner fans for boxes and hulls; the
// triangles of meshes and height fields; compounds through their children) and colored line segments.
module gfx.physics.debug_draw;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;

namespace gfx::physics
{

namespace
{

constexpr float PI = std::numbers::pi_v<float>;

/// Throws the PhysicsError of an invalid drawing request.
[[noreturn]] void
reject(std::string_view operation,
       std::string problem,
       std::string_view suggestion)
{
    throw PhysicsError({.backend = PhysicsSubsystem::GEOMETRY, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::string{suggestion}, .context = {}});
}

// ------------------------------------------------------------------------------------------------- building meshes

/// Appends vertices and triangles to a mesh, moved by a pose (a compound child's in its compound).
class MeshBuilder
{
public:
    MeshBuilder(DebugMesh& mesh,
                const Transform& pose) noexcept
        : m_mesh{mesh}
        , m_pose{pose}
    {
    }

    /// A vertex at @p position with @p normal (in the frame the pose moves); its index.
    auto vertex(Vector3 position,
                Vector3 normal) -> std::uint32_t
    {
        m_mesh.vertices.push_back({.position = transform_point(m_pose, position), .normal = rotate(m_pose.orientation, normal)});
        return static_cast<std::uint32_t>(m_mesh.vertices.size() - 1);
    }

    /// The triangle of three vertices, wound counter-clockwise seen from where their normals point; none when it has no
    /// area (the poles of revolved profiles, edges between coplanar faces).
    void triangle(std::uint32_t first,
                  std::uint32_t second,
                  std::uint32_t third)
    {
        const DebugVertex& a = m_mesh.vertices[first];
        const DebugVertex& b = m_mesh.vertices[second];
        const DebugVertex& c = m_mesh.vertices[third];
        const Vector3 face = cross(b.position - a.position, c.position - a.position);
        if (!(length_squared(face) > std::numeric_limits<float>::min()))
        {
            return;
        }
        const bool facing = dot(face, a.normal + b.normal + c.normal) >= 0.0F;
        m_mesh.indices.insert(m_mesh.indices.end(), {first, facing ? second : third, facing ? third : second});
    }

    /// The vertices added from now on start here.
    [[nodiscard]] auto next_vertex() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_mesh.vertices.size());
    }

private:
    DebugMesh& m_mesh;
    Transform m_pose;
};

// ------------------------------------------------------------------------------------------------- round shapes

/// A point of a profile revolved around +Y: its distance from the axis, its height, and its outward normal in the
/// (radial, height) plane.
struct ProfilePoint
{
    float radius = 0.0F;
    float height = 0.0F;
    float normal_radial = 0.0F;
    float normal_height = 0.0F;
};

/// Revolves @p strip (consecutive points joined into bands) around +Y in @p segments steps.
void
revolve(MeshBuilder& builder,
        std::span<const ProfilePoint> strip,
        std::uint32_t segments)
{
    // One ring of vertices per profile point, the seam repeated so each ring closes on its own vertex.
    const std::uint32_t first = builder.next_vertex();
    const std::uint32_t ring = segments + 1;
    for (const ProfilePoint& point : strip)
    {
        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float angle = 2.0F * PI * static_cast<float>(segment % segments) / static_cast<float>(segments);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            static_cast<void>(builder.vertex({.x = point.radius * cosine, .y = point.height, .z = point.radius * sine},
                                             {.x = point.normal_radial * cosine, .y = point.normal_height, .z = point.normal_radial * sine}));
        }
    }

    // Two triangles per quad between consecutive rings (those of a ring on the axis have no area and are dropped).
    for (std::uint32_t row = 0; row + 1 < strip.size(); ++row)
    {
        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t upper = first + row * ring + segment;
            const std::uint32_t lower = upper + ring;
            builder.triangle(upper, upper + 1, lower + 1);
            builder.triangle(upper, lower + 1, lower);
        }
    }
}

/// The steps a quarter circle takes at @p segments around a full one (at least 1).
[[nodiscard]] auto
quarter_steps(std::uint32_t segments) noexcept -> std::uint32_t
{
    return std::max(1U, segments / 4);
}

void
add_sphere(MeshBuilder& builder,
           const SphereShape& sphere,
           std::uint32_t segments)
{
    // From the top pole to the bottom one.
    const std::uint32_t rows = 2 * quarter_steps(segments);
    std::vector<ProfilePoint> profile;
    profile.reserve(rows + 1);
    for (std::uint32_t row = 0; row <= rows; ++row)
    {
        const float polar = PI * static_cast<float>(row) / static_cast<float>(rows);
        const float sine = row == rows ? 0.0F : std::sin(polar);
        const float cosine = std::cos(polar);
        profile.push_back({.radius = sphere.radius * sine, .height = sphere.radius * cosine, .normal_radial = sine, .normal_height = cosine});
    }
    revolve(builder, profile, segments);
}

void
add_capsule(MeshBuilder& builder,
            const CapsuleShape& capsule,
            std::uint32_t segments)
{
    // The top hemisphere around (0, h, 0), the side, the bottom hemisphere around (0, -h, 0): one smooth strip.
    const std::uint32_t quarter = quarter_steps(segments);
    std::vector<ProfilePoint> profile;
    profile.reserve(2 * quarter + 2);
    for (std::uint32_t row = 0; row <= 2 * quarter; ++row)
    {
        const float polar = PI * 0.5F * static_cast<float>(row) / static_cast<float>(quarter);
        const float sine = row == 2 * quarter ? 0.0F : std::sin(polar);
        const float cosine = row == quarter ? 0.0F : std::cos(polar);
        const ProfilePoint point{.radius = capsule.radius * sine, .height = capsule.radius * cosine, .normal_radial = sine, .normal_height = cosine};
        if (row <= quarter)
        {
            profile.push_back({.radius = point.radius, .height = point.height + capsule.half_height, .normal_radial = sine, .normal_height = cosine});
        }
        if (row >= quarter)
        {
            profile.push_back({.radius = point.radius, .height = point.height - capsule.half_height, .normal_radial = sine, .normal_height = cosine});
        }
    }
    revolve(builder, profile, segments);
}

void
add_cylinder(MeshBuilder& builder,
             const CylinderShape& cylinder,
             std::uint32_t segments)
{
    // The top cap, the side and the bottom cap, each with its own normals.
    const float r = cylinder.radius;
    const float h = cylinder.half_height;
    const std::array top{ProfilePoint{.radius = 0.0F, .height = h, .normal_height = 1.0F}, ProfilePoint{.radius = r, .height = h, .normal_height = 1.0F}};
    const std::array side{ProfilePoint{.radius = r, .height = h, .normal_radial = 1.0F}, ProfilePoint{.radius = r, .height = -h, .normal_radial = 1.0F}};
    const std::array bottom{ProfilePoint{.radius = r, .height = -h, .normal_height = -1.0F}, ProfilePoint{.radius = 0.0F, .height = -h, .normal_height = -1.0F}};
    revolve(builder, top, segments);
    revolve(builder, side, segments);
    revolve(builder, bottom, segments);
}

void
add_cone(MeshBuilder& builder,
         const ConeShape& cone,
         std::uint32_t segments)
{
    // The slant from the apex at (0, h, 0) to the base's rim, with the slant's normal at the apex too (shading stays
    // smooth around it), then the base.
    const float r = cone.radius;
    const float h = cone.half_height;
    const float slant = std::sqrt(4.0F * h * h + r * r);
    const float radial = 2.0F * h / slant;
    const float upward = r / slant;
    const std::array side{ProfilePoint{.radius = 0.0F, .height = h, .normal_radial = radial, .normal_height = upward},
                          ProfilePoint{.radius = r, .height = -h, .normal_radial = radial, .normal_height = upward}};
    const std::array base{ProfilePoint{.radius = r, .height = -h, .normal_height = -1.0F}, ProfilePoint{.radius = 0.0F, .height = -h, .normal_height = -1.0F}};
    revolve(builder, side, segments);
    revolve(builder, base, segments);
}

// ------------------------------------------------------------------------------------------------- polytopes

/// The direction @p step of @p steps along the great arc from @p from to @p to (unit vectors), exactly the ends at 0 and
/// @p steps.
[[nodiscard]] auto
arc_direction(Vector3 from,
              Vector3 to,
              std::uint32_t step,
              std::uint32_t steps) noexcept -> Vector3
{
    if (step == 0)
    {
        return from;
    }
    if (step == steps)
    {
        return to;
    }
    const float angle = std::acos(std::clamp(dot(from, to), -1.0F, 1.0F));
    const float sine = std::sin(angle);
    if (!(sine > 1.0e-6F))
    {
        return normalized(from + (to - from) * (static_cast<float>(step) / static_cast<float>(steps)));
    }
    const float fraction = static_cast<float>(step) / static_cast<float>(steps);
    return normalized(from * (std::sin((1.0F - fraction) * angle) / sine) + to * (std::sin(fraction * angle) / sine));
}

/// The steps the arc between two face normals takes at @p segments around a full circle (at least 1).
[[nodiscard]] auto
arc_steps(Vector3 first,
          Vector3 second,
          std::uint32_t segments) noexcept -> std::uint32_t
{
    const float angle = std::acos(std::clamp(dot(first, second), -1.0F, 1.0F));
    return std::max(1U, static_cast<std::uint32_t>(std::ceil(angle / (2.0F * PI) * static_cast<float>(segments) - 1.0e-3F)));
}

/// The arc's direction @p step from face @p from toward face @p to, computed from the face of the smaller index so an
/// edge's strip and its corners' fans share their vertices exactly.
[[nodiscard]] auto
face_arc(const HullView& hull,
         std::uint32_t from,
         std::uint32_t to,
         std::uint32_t step,
         std::uint32_t steps) noexcept -> Vector3
{
    return from < to ? arc_direction(hull.planes[from].normal, hull.planes[to].normal, step, steps)
                     : arc_direction(hull.planes[to].normal, hull.planes[from].normal, steps - step, steps);
}

/// A convex polytope's faces moved out by @p radius along their normals, and when @p radius > 0, a strip around each
/// edge (the arc between its faces' normals) and a spherical patch at each corner (rings out to the arcs between its
/// faces' normals): the surface of the polytope rounded by a sphere of @p radius.
void
add_polytope(MeshBuilder& builder,
             const HullView& hull,
             float radius,
             std::uint32_t segments)
{
    // The faces, each a fan from its first vertex.
    for (std::size_t face_index = 0; face_index < hull.faces.size(); ++face_index)
    {
        const HullFace face = hull.faces[face_index];
        const Vector3 normal = hull.planes[face_index].normal;
        const std::uint32_t first = builder.next_vertex();
        for (std::uint32_t corner = 0; corner < face.count; ++corner)
        {
            static_cast<void>(builder.vertex(hull.vertices[hull.face_vertices[face.first + corner]] + normal * radius, normal));
        }
        for (std::uint32_t corner = 1; corner + 1 < face.count; ++corner)
        {
            builder.triangle(first, first + corner, first + corner + 1);
        }
    }
    if (!(radius > 0.0F))
    {
        return;
    }

    // Each edge's two faces: edges sorted by their vertices (the smaller first).
    struct EdgeFace
    {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        std::uint32_t face = 0;
    };
    std::vector<EdgeFace> edges;
    for (std::uint32_t face_index = 0; face_index < hull.faces.size(); ++face_index)
    {
        const HullFace face = hull.faces[face_index];
        for (std::uint32_t corner = 0; corner < face.count; ++corner)
        {
            const std::uint32_t a = hull.face_vertices[face.first + corner];
            const std::uint32_t b = hull.face_vertices[face.first + (corner + 1) % face.count];
            edges.push_back({.low = std::min(a, b), .high = std::max(a, b), .face = face_index});
        }
    }
    std::ranges::sort(edges, {}, [](const EdgeFace& edge) { return std::tuple{edge.low, edge.high, edge.face}; });

    // A strip of quads along each edge shared by two faces.
    for (std::size_t index = 0; index + 1 < edges.size(); ++index)
    {
        const EdgeFace& first = edges[index];
        const EdgeFace& second = edges[index + 1];
        if (first.low != second.low || first.high != second.high)
        {
            continue;
        }
        const std::uint32_t steps = arc_steps(hull.planes[first.face].normal, hull.planes[second.face].normal, segments);
        const Vector3 low = hull.vertices[first.low];
        const Vector3 high = hull.vertices[first.high];
        const std::uint32_t start = builder.next_vertex();
        for (std::uint32_t step = 0; step <= steps; ++step)
        {
            const Vector3 direction = face_arc(hull, first.face, second.face, step, steps);
            static_cast<void>(builder.vertex(low + direction * radius, direction));
            static_cast<void>(builder.vertex(high + direction * radius, direction));
        }
        for (std::uint32_t step = 0; step < steps; ++step)
        {
            const std::uint32_t at = start + 2 * step;
            builder.triangle(at, at + 2, at + 3);
            builder.triangle(at, at + 3, at + 1);
        }
        ++index;
    }

    // A fan at each corner: its faces ordered around their mean normal, the arcs between neighbors.
    std::vector<std::uint32_t> faces;
    std::vector<float> angles;
    std::vector<Vector3> boundary;
    for (std::uint32_t vertex = 0; vertex < hull.vertices.size(); ++vertex)
    {
        faces.clear();
        for (std::uint32_t face_index = 0; face_index < hull.faces.size(); ++face_index)
        {
            const HullFace face = hull.faces[face_index];
            const auto corners = hull.face_vertices.subspan(face.first, face.count);
            if (std::ranges::find(corners, static_cast<std::uint16_t>(vertex)) != corners.end())
            {
                faces.push_back(face_index);
            }
        }
        if (faces.size() < 3)
        {
            continue;
        }
        Vector3 mean{};
        for (const std::uint32_t face : faces)
        {
            mean += hull.planes[face].normal;
        }
        mean = normalized(mean);
        const Vector3 across = any_perpendicular(mean);
        const Vector3 along = cross(mean, across);
        angles.clear();
        for (const std::uint32_t face : faces)
        {
            const Vector3 normal = hull.planes[face].normal;
            angles.push_back(std::atan2(dot(normal, along), dot(normal, across)));
        }
        std::vector<std::uint32_t> order(faces.size());
        std::iota(order.begin(), order.end(), 0U);
        std::ranges::sort(order, {}, [&angles](std::uint32_t index) { return angles[index]; });

        // The patch's boundary: the arcs between neighboring faces' normals, each arc's last direction the next one's first.
        boundary.clear();
        for (std::size_t index = 0; index < order.size(); ++index)
        {
            const std::uint32_t from = faces[order[index]];
            const std::uint32_t to = faces[order[(index + 1) % order.size()]];
            const std::uint32_t steps = arc_steps(hull.planes[from].normal, hull.planes[to].normal, segments);
            for (std::uint32_t step = 0; step < steps; ++step)
            {
                boundary.push_back(face_arc(hull, from, to, step, steps));
            }
        }

        // Rings from the mean normal out to the boundary (the outermost ring is the boundary itself, shared exactly with
        // the faces and the edge strips), then a fan around the center and quads between rings.
        std::uint32_t rings = 1;
        for (const Vector3 direction : boundary)
        {
            rings = std::max(rings, arc_steps(mean, direction, segments));
        }
        const Vector3 corner = hull.vertices[vertex];
        const std::uint32_t center = builder.vertex(corner + mean * radius, mean);
        const std::uint32_t start = builder.next_vertex();
        for (const Vector3 direction : boundary)
        {
            for (std::uint32_t ring = 1; ring <= rings; ++ring)
            {
                const Vector3 point = arc_direction(mean, direction, ring, rings);
                static_cast<void>(builder.vertex(corner + point * radius, point));
            }
        }
        const auto at = [start, rings](std::size_t spoke, std::uint32_t ring) { return start + static_cast<std::uint32_t>(spoke) * rings + ring - 1; };
        for (std::size_t spoke = 0; spoke < boundary.size(); ++spoke)
        {
            const std::size_t next = (spoke + 1) % boundary.size();
            builder.triangle(center, at(spoke, 1), at(next, 1));
            for (std::uint32_t ring = 1; ring < rings; ++ring)
            {
                builder.triangle(at(spoke, ring), at(spoke, ring + 1), at(next, ring + 1));
                builder.triangle(at(spoke, ring), at(next, ring + 1), at(next, ring));
            }
        }
    }
}

void
add_box(MeshBuilder& builder,
        const BoxShape& box,
        std::uint32_t segments)
{
    // The box as a polytope: corner bit 0 x, bit 1 y, bit 2 z (set: positive); each face's corners in a cycle.
    const Vector3 half = box.half_extents;
    std::array<Vector3, 8> corners{};
    for (std::uint32_t index = 0; index < corners.size(); ++index)
    {
        corners[index] = {.x = (index & 1U) != 0 ? half.x : -half.x, .y = (index & 2U) != 0 ? half.y : -half.y, .z = (index & 4U) != 0 ? half.z : -half.z};
    }
    constexpr std::array<std::uint16_t, 24> FACE_CORNERS{0, 2, 6, 4, 1, 3, 7, 5, 0, 1, 5, 4, 2, 3, 7, 6, 0, 1, 3, 2, 4, 5, 7, 6};
    const std::array planes{Plane{.normal = {.x = -1}, .distance = half.x}, Plane{.normal = {.x = 1}, .distance = half.x},
                            Plane{.normal = {.y = -1}, .distance = half.y}, Plane{.normal = {.y = 1}, .distance = half.y},
                            Plane{.normal = {.z = -1}, .distance = half.z}, Plane{.normal = {.z = 1}, .distance = half.z}};
    constexpr std::array<HullFace, 6> FACES{HullFace{.first = 0, .count = 4},  HullFace{.first = 4, .count = 4},  HullFace{.first = 8, .count = 4},
                                            HullFace{.first = 12, .count = 4}, HullFace{.first = 16, .count = 4}, HullFace{.first = 20, .count = 4}};
    add_polytope(builder, {.vertices = corners, .planes = planes, .faces = FACES, .face_vertices = FACE_CORNERS, .radius = box.radius}, box.radius, segments);
}

// ------------------------------------------------------------------------------------------------- triangles

/// A triangle with its face's normal (none when it has no area).
void
add_triangle(MeshBuilder& builder,
             const TriangleShape& triangle)
{
    const Vector3 normal = triangle.normal();
    if (length_squared(normal) == 0.0F)
    {
        return;
    }
    const std::uint32_t first = builder.vertex(triangle.first, normal);
    static_cast<void>(builder.vertex(triangle.second, normal));
    static_cast<void>(builder.vertex(triangle.third, normal));
    builder.triangle(first, first + 1, first + 2);
}

/// Any shape at @p pose (in the mesh's frame).
void
add_shape(DebugMesh& mesh,
          const Shape& shape,
          const Transform& pose,
          std::uint32_t segments)
{
    MeshBuilder builder{mesh, pose};
    std::visit(
        [&](const auto& geometry)
        {
            using Geometry = std::remove_cvref_t<decltype(geometry)>;
            if constexpr (std::same_as<Geometry, SphereShape>)
            {
                add_sphere(builder, geometry, segments);
            }
            else if constexpr (std::same_as<Geometry, CapsuleShape>)
            {
                add_capsule(builder, geometry, segments);
            }
            else if constexpr (std::same_as<Geometry, CylinderShape>)
            {
                add_cylinder(builder, geometry, segments);
            }
            else if constexpr (std::same_as<Geometry, ConeShape>)
            {
                add_cone(builder, geometry, segments);
            }
            else if constexpr (std::same_as<Geometry, BoxShape>)
            {
                add_box(builder, geometry, segments);
            }
            else if constexpr (std::same_as<Geometry, HullShape>)
            {
                add_polytope(builder, geometry.view(), geometry.margin(), segments);
            }
            else if constexpr (std::same_as<Geometry, TriangleMeshShape>)
            {
                for (std::uint32_t index = 0; index < geometry.triangle_count(); ++index)
                {
                    add_triangle(builder, geometry.triangle(index));
                }
                mesh.two_sided = mesh.two_sided || geometry.two_sided();
            }
            else if constexpr (std::same_as<Geometry, HeightFieldShape>)
            {
                for (std::uint32_t index = 0; index < geometry.triangle_count(); ++index)
                {
                    add_triangle(builder, geometry.triangle(index));
                }
            }
            else
            {
                static_assert(std::same_as<Geometry, CompoundShape>);
                for (const CompoundChild& child : geometry.children())
                {
                    add_shape(mesh, *child.shape, pose * child.pose, segments);
                }
            }
        },
        shape.geometry());
}

} // namespace

// ===================================================================================================== meshes

void
tessellate(const Shape& shape,
           DebugMesh& mesh,
           const TessellationDescription& description)
{
    if (description.segments < 3)
    {
        reject("tessellate", std::format("{} segments cannot make a circle", description.segments), "ask at least 3 segments");
    }
    add_shape(mesh, shape, {}, description.segments);
}

auto
tessellate(const Shape& shape,
           const TessellationDescription& description) -> DebugMesh
{
    DebugMesh mesh;
    tessellate(shape, mesh, description);
    return mesh;
}

auto
enclosed_volume(const DebugMesh& mesh) noexcept -> float
{
    // Each triangle and the origin make a tetrahedron of signed volume a · (b × c) / 6.
    double volume = 0.0;
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
    {
        const Vector3 a = mesh.vertices[mesh.indices[index]].position;
        const Vector3 b = mesh.vertices[mesh.indices[index + 1]].position;
        const Vector3 c = mesh.vertices[mesh.indices[index + 2]].position;
        volume += static_cast<double>(dot(a, cross(b, c)));
    }
    return static_cast<float>(volume / 6.0);
}

// ===================================================================================================== lines

void
DebugLines::add_line(Vector3 from,
                     Vector3 to,
                     DebugColor color)
{
    m_vertices.push_back({.position = from, .color = color});
    m_vertices.push_back({.position = to, .color = color});
}

void
DebugLines::add_box(const Aabb& box,
                    DebugColor color)
{
    add_box(Obb{.center = box.center(), .half_extents = box.half_extents(), .orientation = {}}, color);
}

void
DebugLines::add_box(const Obb& box,
                    DebugColor color)
{
    // The corners (bit 0 x, bit 1 y, bit 2 z positive), then every pair that differs in one bit.
    std::array<Vector3, 8> corners{};
    for (std::uint32_t index = 0; index < corners.size(); ++index)
    {
        const Vector3 local{.x = (index & 1U) != 0 ? box.half_extents.x : -box.half_extents.x,
                            .y = (index & 2U) != 0 ? box.half_extents.y : -box.half_extents.y,
                            .z = (index & 4U) != 0 ? box.half_extents.z : -box.half_extents.z};
        corners[index] = box.center + rotate(box.orientation, local);
    }
    for (std::uint32_t index = 0; index < corners.size(); ++index)
    {
        for (const std::uint32_t bit : {1U, 2U, 4U})
        {
            if ((index & bit) == 0)
            {
                add_line(corners[index], corners[index | bit], color);
            }
        }
    }
}

void
DebugLines::add_axes(const Transform& pose,
                     float length)
{
    add_line(pose.position, transform_point(pose, Vector3{.x = length}), {.red = 255, .green = 0, .blue = 0});
    add_line(pose.position, transform_point(pose, Vector3{.y = length}), {.red = 0, .green = 255, .blue = 0});
    add_line(pose.position, transform_point(pose, Vector3{.z = length}), {.red = 0, .green = 0, .blue = 255});
}

void
DebugLines::add_arrow(Vector3 from,
                      Vector3 to,
                      DebugColor color,
                      float head_size)
{
    add_line(from, to, color);
    const Vector3 direction = to - from;
    if (!(length_squared(direction) > 0.0F))
    {
        return;
    }

    // Four strokes back from the tip, splayed around the shaft.
    const Vector3 back = normalized(direction) * -head_size;
    const Vector3 across = any_perpendicular(direction) * (head_size * 0.5F);
    const Vector3 along = cross(normalized(direction), across);
    for (const Vector3 side : {across, -across, along, -along})
    {
        add_line(to, to + back + side, color);
    }
}

void
DebugLines::add_cross(Vector3 point,
                      float size,
                      DebugColor color)
{
    const float half = size * 0.5F;
    add_line(point - Vector3{.x = half}, point + Vector3{.x = half}, color);
    add_line(point - Vector3{.y = half}, point + Vector3{.y = half}, color);
    add_line(point - Vector3{.z = half}, point + Vector3{.z = half}, color);
}

void
DebugLines::add_circle(Vector3 center,
                       Vector3 normal,
                       float radius,
                       DebugColor color,
                       std::uint32_t segments)
{
    if (segments < 3)
    {
        reject("DebugLines::add_circle", std::format("{} segments cannot make a circle", segments), "ask at least 3 segments");
    }
    if (!(length_squared(normal) > 0.0F))
    {
        reject("DebugLines::add_circle", "the normal has no direction", "give the circle's plane a normal of any length but 0");
    }
    const Vector3 across = any_perpendicular(normal) * radius;
    const Vector3 along = cross(normalized(normal), across);
    Vector3 previous = center + across;
    for (std::uint32_t segment = 1; segment <= segments; ++segment)
    {
        const float angle = 2.0F * PI * static_cast<float>(segment) / static_cast<float>(segments);
        const Vector3 point = segment == segments ? center + across : center + across * std::cos(angle) + along * std::sin(angle);
        add_line(previous, point, color);
        previous = point;
    }
}

void
DebugLines::add_contacts(const ContactManifold& manifold,
                         DebugColor color,
                         float point_size,
                         float normal_length)
{
    for (const ContactPoint& point : manifold.contacts())
    {
        add_cross(point.position, point_size, color);
        add_arrow(point.position, point.position + manifold.normal * normal_length, color, normal_length * 0.2F);
    }
}

} // namespace gfx::physics
