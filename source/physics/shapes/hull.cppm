/// @file hull.cppm
/// @brief Convex hulls as shapes: the vertices, the planes of the faces (facing out) and each face's polygon
///        (counter-clockwise around its plane's normal), with an optional rounding radius.
///
/// convex_hull<V, F, I>(points) builds a hull with quickhull in a constant expression: a hull known at compile time is
/// built by the compiler, its capacities checked (a call to `convex_hull_<problem>()` in the diagnostic names what is
/// wrong). HullShape::from_points() runs the same algorithm at run time. Quickhull computes in double precision; points
/// within a tolerance of a face (3 · FLT_EPSILON · the sum of the largest coordinates) do not make new vertices, and
/// coplanar triangles merge into one polygon.
export module gfx.physics.shapes:hull;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import :convex;

export namespace gfx::physics
{

/// @brief A face of a hull: its vertices are `face_vertices[first, first + count)`.
struct HullFace
{
    std::uint16_t first = 0; ///< The first of its vertex indices.
    std::uint16_t count = 0; ///< How many (at least 3).

    [[nodiscard]] constexpr auto operator==(const HullFace&) const noexcept -> bool = default;
};

/// @brief A hull seen through spans: what every hull type gives and the narrow phase reads.
struct HullView
{
    std::span<const Vector3> vertices{};            ///< The corners.
    std::span<const Plane> planes{};                ///< One plane per face, facing out.
    std::span<const HullFace> faces{};              ///< One polygon per face.
    std::span<const std::uint16_t> face_vertices{}; ///< The polygons' vertex indices, counter-clockwise around their planes' normals.
    float radius = 0.0F;                            ///< The rounding (the margin).

    /// @brief The first vertex farthest along @p direction (scalar: HullShape::support() uses SIMD for many vertices).
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        if (vertices.empty())
        {
            return {};
        }
        std::size_t best = 0;
        float largest = dot(vertices[0], direction);
        for (std::size_t index = 1; index < vertices.size(); ++index)
        {
            const float along = dot(vertices[index], direction);
            if (along > largest)
            {
                largest = along;
                best = index;
            }
        }
        return vertices[best];
    }

    /// @brief The rounding.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return radius;
    }

    /// @brief The box around the rounded hull.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_points(vertices).expanded(radius);
    }

    /// @brief The face whose normal is closest to @p direction (its index is the feature); a polygon of more than
    ///        MAXIMUM_FACE_VERTICES vertices gives that many of them, evenly spaced.
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        if (faces.empty())
        {
            SupportingFace point{.normal = direction};
            point.vertices[0] = support(direction);
            point.count = vertices.empty() ? 0U : 1U;
            return point;
        }
        std::size_t best = 0;
        float largest = dot(planes[0].normal, direction);
        for (std::size_t index = 1; index < faces.size(); ++index)
        {
            const float along = dot(planes[index].normal, direction);
            if (along > largest)
            {
                largest = along;
                best = index;
            }
        }
        const HullFace face = faces[best];
        SupportingFace result{.normal = planes[best].normal, .face = static_cast<std::uint32_t>(best)};
        result.count = std::min<std::uint32_t>(face.count, MAXIMUM_FACE_VERTICES);
        for (std::uint32_t index = 0; index < result.count; ++index)
        {
            const std::size_t taken = static_cast<std::size_t>(index) * face.count / result.count;
            result.vertices[index] = vertices[face_vertices[face.first + taken]];
        }
        return result;
    }

    /// @brief The hull as a primitive (containment, ray distances).
    [[nodiscard]] constexpr auto primitive() const noexcept -> ConvexHullView
    {
        return {.vertices = vertices, .faces = planes};
    }
};

/// @brief A hull in fixed storage (constexpr: convex_hull() builds one at compile time).
/// @tparam VertexCapacity Vertices at most.
/// @tparam FaceCapacity   Faces at most.
/// @tparam IndexCapacity  Vertex indices of all the faces' polygons at most.
template<std::size_t VertexCapacity, std::size_t FaceCapacity, std::size_t IndexCapacity>
struct FixedHull
{
    std::array<Vector3, VertexCapacity> vertices{};           ///< The corners (vertex_count of them).
    std::array<Plane, FaceCapacity> planes{};                  ///< The faces' planes (face_count of them).
    std::array<HullFace, FaceCapacity> faces{};                ///< The faces' polygons.
    std::array<std::uint16_t, IndexCapacity> face_vertices{};  ///< The polygons' vertex indices (index_count of them).
    std::uint32_t vertex_count = 0;                            ///< Vertices in use.
    std::uint32_t face_count = 0;                              ///< Faces in use.
    std::uint32_t index_count = 0;                             ///< Indices in use.
    float radius = 0.0F;                                       ///< The rounding.

    /// @brief The hull through spans.
    [[nodiscard]] constexpr auto view() const noexcept -> HullView
    {
        return {.vertices = std::span{vertices}.first(vertex_count),
                .planes = std::span{planes}.first(face_count),
                .faces = std::span{faces}.first(face_count),
                .face_vertices = std::span{face_vertices}.first(index_count),
                .radius = radius};
    }

    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        return view().support(direction);
    }

    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return radius;
    }

    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return view().bounds();
    }

    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        return view().supporting_face(direction);
    }
};

namespace hull_detail
{

/// A point in double precision.
struct Point
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] constexpr auto
operator-(Point first,
          Point second) noexcept -> Point
{
    return {.x = first.x - second.x, .y = first.y - second.y, .z = first.z - second.z};
}

[[nodiscard]] constexpr auto
operator+(Point first,
          Point second) noexcept -> Point
{
    return {.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z};
}

[[nodiscard]] constexpr auto
operator*(Point point,
          double factor) noexcept -> Point
{
    return {.x = point.x * factor, .y = point.y * factor, .z = point.z * factor};
}

[[nodiscard]] constexpr auto
dot(Point first,
    Point second) noexcept -> double
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] constexpr auto
cross(Point first,
      Point second) noexcept -> Point
{
    return {.x = first.y * second.z - first.z * second.y, .y = first.z * second.x - first.x * second.z, .z = first.x * second.y - first.y * second.x};
}

/// The square root in double precision (Newton's method in constant expressions).
[[nodiscard]] constexpr auto
root(double value) noexcept -> double
{
    if consteval
    {
        if (!(value > 0.0))
        {
            return 0.0;
        }
        double estimate = value > 1.0 ? value : 1.0;
        for (int iteration = 0; iteration < 64; ++iteration)
        {
            const double next = 0.5 * (estimate + value / estimate);
            if (next >= estimate)
            {
                break;
            }
            estimate = next;
        }
        return estimate;
    }
    else
    {
        return std::sqrt(value);
    }
}

[[nodiscard]] constexpr auto
magnitude(Point point) noexcept -> double
{
    return root(dot(point, point));
}

/// No face, no point.
inline constexpr std::uint32_t NONE = 0xFFFFFFFFU;

/// A triangle of the hull being built: counter-clockwise vertices seen from outside, the faces across its edges
/// (edge e runs from vertex e to vertex e + 1), its plane, and the points above it.
struct Face
{
    std::array<std::uint32_t, 3> vertex{};
    std::array<std::uint32_t, 3> neighbor{NONE, NONE, NONE};
    Point normal{};
    double offset = 0.0;
    std::vector<std::uint32_t> outside{};
    bool alive = true;
    bool visible = false;
};

/// An edge of the horizon: edge `edge` of face `face`.
struct HorizonEdge
{
    std::uint32_t face = 0;
    std::uint32_t edge = 0;
};

/// What quickhull built (or the problem that stopped it).
struct HullBuild
{
    std::vector<Vector3> vertices{};
    std::vector<Plane> planes{};
    std::vector<HullFace> faces{};
    std::vector<std::uint16_t> face_vertices{};
    std::string_view problem{};
};

/// Sets the plane of @p face from its vertices (or, when degenerate, from @p fallback).
constexpr void
set_plane(Face& face,
          std::span<const Point> points,
          const Face* fallback)
{
    const Point a = points[face.vertex[0]];
    const Point normal = cross(points[face.vertex[1]] - a, points[face.vertex[2]] - a);
    const double size = magnitude(normal);
    face.normal = size > 1e-300 ? normal * (1.0 / size) : (fallback != nullptr ? fallback->normal : Point{.y = 1.0});
    face.offset = dot(face.normal, a);
}

[[nodiscard]] constexpr auto
distance_above(const Face& face,
               Point point) noexcept -> double
{
    return dot(face.normal, point) - face.offset;
}

/// Marks the faces @p eye sees, from @p face entered across its edge @p entered (NONE for the first), and records the
/// horizon edges in counter-clockwise order.
constexpr void
find_horizon(std::vector<Face>& faces,
             std::span<const Point> points,
             Point eye,
             std::uint32_t face,
             std::uint32_t entered,
             double tolerance,
             std::vector<HorizonEdge>& horizon)
{
    faces[face].visible = true;
    for (std::uint32_t step = entered == NONE ? 0U : 1U; step < 3; ++step)
    {
        const std::uint32_t edge = entered == NONE ? step : (entered + step) % 3;
        const std::uint32_t neighbor = faces[face].neighbor[edge];
        if (faces[neighbor].visible)
        {
            continue;
        }
        if (distance_above(faces[neighbor], eye) > tolerance)
        {
            // The neighbor's edge back to this face.
            std::uint32_t back = 0;
            while (faces[neighbor].neighbor[back] != face)
            {
                ++back;
            }
            find_horizon(faces, points, eye, neighbor, back, tolerance, horizon);
        }
        else
        {
            horizon.push_back({.face = face, .edge = edge});
        }
    }
}

/// Quickhull over @p input: the hull's vertices (in input order), face planes and polygons, or a problem.
[[nodiscard]] constexpr auto
quickhull(std::span<const Vector3> input) -> HullBuild
{
    HullBuild build;
    if (input.size() < 4)
    {
        build.problem = "a hull needs at least 4 points";
        return build;
    }
    if (input.size() >= NONE)
    {
        build.problem = "too many points";
        return build;
    }

    // The points in double precision, and the tolerance below which a point is on a face.
    std::vector<Point> points;
    points.reserve(input.size());
    double largest_x = 0.0;
    double largest_y = 0.0;
    double largest_z = 0.0;
    for (const Vector3 point : input)
    {
        constexpr float LARGEST = std::numeric_limits<float>::max();
        if (!(point.x >= -LARGEST && point.x <= LARGEST && point.y >= -LARGEST && point.y <= LARGEST && point.z >= -LARGEST && point.z <= LARGEST))
        {
            build.problem = "the points must be finite";
            return build;
        }
        points.push_back({.x = point.x, .y = point.y, .z = point.z});
        largest_x = std::max(largest_x, point.x < 0 ? -static_cast<double>(point.x) : static_cast<double>(point.x));
        largest_y = std::max(largest_y, point.y < 0 ? -static_cast<double>(point.y) : static_cast<double>(point.y));
        largest_z = std::max(largest_z, point.z < 0 ? -static_cast<double>(point.z) : static_cast<double>(point.z));
    }
    const double tolerance = 3.0 * static_cast<double>(std::numeric_limits<float>::epsilon()) * (largest_x + largest_y + largest_z);
    const auto count = static_cast<std::uint32_t>(points.size());

    // The initial tetrahedron: the farthest pair of the extreme points, the point farthest from their line, the point
    // farthest from their plane.
    std::array<std::uint32_t, 6> extremes{};
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const Point p = points[index];
        extremes[0] = p.x < points[extremes[0]].x ? index : extremes[0];
        extremes[1] = p.x > points[extremes[1]].x ? index : extremes[1];
        extremes[2] = p.y < points[extremes[2]].y ? index : extremes[2];
        extremes[3] = p.y > points[extremes[3]].y ? index : extremes[3];
        extremes[4] = p.z < points[extremes[4]].z ? index : extremes[4];
        extremes[5] = p.z > points[extremes[5]].z ? index : extremes[5];
    }
    std::uint32_t first = extremes[0];
    std::uint32_t second = extremes[1];
    double widest = -1.0;
    for (std::size_t a = 0; a < 6; ++a)
    {
        for (std::size_t b = a + 1; b < 6; ++b)
        {
            const Point gap = points[extremes[b]] - points[extremes[a]];
            if (dot(gap, gap) > widest)
            {
                widest = dot(gap, gap);
                first = extremes[a];
                second = extremes[b];
            }
        }
    }
    if (!(root(widest) > tolerance))
    {
        build.problem = "the points coincide";
        return build;
    }
    const Point line = points[second] - points[first];
    std::uint32_t third = NONE;
    double farthest = tolerance;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const double away = magnitude(cross(line, points[index] - points[first])) / magnitude(line);
        if (away > farthest)
        {
            farthest = away;
            third = index;
        }
    }
    if (third == NONE)
    {
        build.problem = "the points are collinear: a hull needs volume";
        return build;
    }
    const Point base_normal = cross(line, points[third] - points[first]);
    const double base_size = magnitude(base_normal);
    std::uint32_t fourth = NONE;
    farthest = tolerance;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const double away = dot(base_normal, points[index] - points[first]) / base_size;
        if ((away < 0 ? -away : away) > farthest)
        {
            farthest = away < 0 ? -away : away;
            fourth = index;
        }
    }
    if (fourth == NONE)
    {
        build.problem = "the points are coplanar: a hull needs volume";
        return build;
    }
    if (dot(base_normal, points[fourth] - points[first]) > 0.0)
    {
        std::swap(second, third);
    }

    // Four faces facing away from the opposite vertex, linked across their edges.
    std::vector<Face> faces;
    faces.push_back({.vertex = {first, second, third}});
    faces.push_back({.vertex = {first, fourth, second}});
    faces.push_back({.vertex = {second, fourth, third}});
    faces.push_back({.vertex = {third, fourth, first}});
    for (std::uint32_t face = 0; face < 4; ++face)
    {
        set_plane(faces[face], points, nullptr);
        for (std::uint32_t edge = 0; edge < 3; ++edge)
        {
            const std::uint32_t from = faces[face].vertex[edge];
            const std::uint32_t to = faces[face].vertex[(edge + 1) % 3];
            for (std::uint32_t other = 0; other < 4; ++other)
            {
                for (std::uint32_t other_edge = 0; other_edge < 3; ++other_edge)
                {
                    if (faces[other].vertex[other_edge] == to && faces[other].vertex[(other_edge + 1) % 3] == from)
                    {
                        faces[face].neighbor[edge] = other;
                    }
                }
            }
        }
    }

    // Every other point goes to the face it is farthest above (or is inside and dropped).
    const auto assign = [&faces, &points, tolerance](std::uint32_t point, std::uint32_t from_face) {
        std::uint32_t best = NONE;
        double highest = tolerance;
        for (std::uint32_t face = from_face; face < faces.size(); ++face)
        {
            if (faces[face].alive)
            {
                const double above = distance_above(faces[face], points[point]);
                if (above > highest)
                {
                    highest = above;
                    best = face;
                }
            }
        }
        if (best != NONE)
        {
            faces[best].outside.push_back(point);
        }
    };
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (index != first && index != second && index != third && index != fourth)
        {
            assign(index, 0);
        }
    }

    // Grow: the farthest point above the first face that has any, its horizon, a fan of new faces to it.
    std::vector<HorizonEdge> horizon;
    std::vector<std::uint32_t> orphans;
    while (true)
    {
        std::uint32_t face = NONE;
        for (std::uint32_t candidate = 0; candidate < faces.size(); ++candidate)
        {
            if (faces[candidate].alive && !faces[candidate].outside.empty())
            {
                face = candidate;
                break;
            }
        }
        if (face == NONE)
        {
            break;
        }
        std::uint32_t eye = faces[face].outside[0];
        for (const std::uint32_t point : faces[face].outside)
        {
            eye = distance_above(faces[face], points[point]) > distance_above(faces[face], points[eye]) ? point : eye;
        }

        horizon.clear();
        find_horizon(faces, points, points[eye], face, NONE, tolerance, horizon);

        // The points above the visible faces become orphans; the visible faces die.
        orphans.clear();
        for (Face& visible : faces)
        {
            if (visible.alive && visible.visible)
            {
                for (const std::uint32_t point : visible.outside)
                {
                    if (point != eye)
                    {
                        orphans.push_back(point);
                    }
                }
                visible.outside.clear();
                visible.alive = false;
            }
        }

        // One new face per horizon edge, linked to the face beyond the edge and to its neighbors in the fan.
        const auto first_new = static_cast<std::uint32_t>(faces.size());
        const auto fan = static_cast<std::uint32_t>(horizon.size());
        for (std::uint32_t index = 0; index < fan; ++index)
        {
            const HorizonEdge edge = horizon[index];
            const std::uint32_t from = faces[edge.face].vertex[edge.edge];
            const std::uint32_t to = faces[edge.face].vertex[(edge.edge + 1) % 3];
            const std::uint32_t beyond = faces[edge.face].neighbor[edge.edge];
            Face created{.vertex = {from, to, eye}, .neighbor = {beyond, first_new + (index + 1) % fan, first_new + (index + fan - 1) % fan}};
            set_plane(created, points, &faces[beyond]);
            for (std::uint32_t& back : faces[beyond].neighbor)
            {
                back = back == edge.face ? first_new + index : back;
            }
            faces.push_back(std::move(created));
        }
        for (const std::uint32_t orphan : orphans)
        {
            assign(orphan, first_new);
        }
    }

    // Coplanar triangles merge into polygons: a face joins a neighbor's group when it faces the group's first face's
    // way and its far vertex is on that face's plane.
    const double merge_tolerance = 8.0 * tolerance;
    std::vector<std::uint32_t> group(faces.size(), NONE);
    std::vector<std::uint32_t> group_leaders;
    std::vector<std::uint32_t> pending;
    for (std::uint32_t leader = 0; leader < faces.size(); ++leader)
    {
        if (!faces[leader].alive || group[leader] != NONE)
        {
            continue;
        }
        const auto group_index = static_cast<std::uint32_t>(group_leaders.size());
        group_leaders.push_back(leader);
        group[leader] = group_index;
        pending.assign(1, leader);
        while (!pending.empty())
        {
            const std::uint32_t member = pending.back();
            pending.pop_back();
            for (std::uint32_t edge = 0; edge < 3; ++edge)
            {
                const std::uint32_t neighbor = faces[member].neighbor[edge];
                if (group[neighbor] != NONE || !faces[neighbor].alive || dot(faces[neighbor].normal, faces[leader].normal) < 0.99999)
                {
                    continue;
                }
                bool coplanar = true;
                for (const std::uint32_t vertex : faces[neighbor].vertex)
                {
                    const double off = distance_above(faces[leader], points[vertex]);
                    coplanar = coplanar && (off < 0 ? -off : off) <= merge_tolerance;
                }
                if (coplanar)
                {
                    group[neighbor] = group_index;
                    pending.push_back(neighbor);
                }
            }
        }
    }

    // Each group's boundary, walked from edge to edge, is its polygon; its plane is the area-weighted normal with
    // the offset of its farthest vertex (every vertex behind it).
    std::vector<std::uint32_t> remap(count, NONE);
    std::vector<std::uint32_t> next(count, NONE);
    std::vector<std::uint32_t> loop;
    std::vector<std::vector<std::uint32_t>> polygons;
    std::vector<Point> normals;
    for (std::uint32_t group_index = 0; group_index < group_leaders.size(); ++group_index)
    {
        Point normal{};
        std::uint32_t start = NONE;
        for (std::uint32_t member = 0; member < faces.size(); ++member)
        {
            if (group[member] != group_index)
            {
                continue;
            }
            const Point a = points[faces[member].vertex[0]];
            normal = normal + cross(points[faces[member].vertex[1]] - a, points[faces[member].vertex[2]] - a);
            for (std::uint32_t edge = 0; edge < 3; ++edge)
            {
                if (group[faces[member].neighbor[edge]] != group_index)
                {
                    next[faces[member].vertex[edge]] = faces[member].vertex[(edge + 1) % 3];
                    start = start == NONE ? faces[member].vertex[edge] : start;
                }
            }
        }
        loop.clear();
        std::uint32_t vertex = start;
        do
        {
            loop.push_back(vertex);
            const std::uint32_t following = next[vertex];
            next[vertex] = NONE;
            vertex = following;
        } while (vertex != start && vertex != NONE && loop.size() <= count);
        if (vertex != start || loop.size() < 3)
        {
            build.problem = "the hull's faces did not close (degenerate input)";
            return build;
        }
        polygons.push_back(loop);
        const double size = magnitude(normal);
        normals.push_back(size > 0.0 ? normal * (1.0 / size) : faces[group_leaders[group_index]].normal);
    }

    // The vertices the polygons use, in input order; then the planes and the polygons by the new indices.
    for (const std::vector<std::uint32_t>& polygon : polygons)
    {
        for (const std::uint32_t vertex : polygon)
        {
            remap[vertex] = 0;
        }
    }
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (remap[index] != NONE)
        {
            remap[index] = static_cast<std::uint32_t>(build.vertices.size());
            build.vertices.push_back(input[index]);
        }
    }
    if (build.vertices.size() > 0xFFFFU)
    {
        build.problem = "a hull holds at most 65535 vertices";
        return build;
    }
    for (std::size_t face = 0; face < polygons.size(); ++face)
    {
        double offset = -std::numeric_limits<double>::max();
        for (const std::uint32_t vertex : polygons[face])
        {
            offset = std::max(offset, dot(normals[face], points[vertex]));
        }
        if (build.face_vertices.size() + polygons[face].size() > 0xFFFFU)
        {
            build.problem = "a hull's polygons hold at most 65535 vertex indices";
            return build;
        }
        build.faces.push_back({.first = static_cast<std::uint16_t>(build.face_vertices.size()), .count = static_cast<std::uint16_t>(polygons[face].size())});
        for (const std::uint32_t vertex : polygons[face])
        {
            build.face_vertices.push_back(static_cast<std::uint16_t>(remap[vertex]));
        }
        build.planes.push_back({.normal = {.x = static_cast<float>(normals[face].x), .y = static_cast<float>(normals[face].y), .z = static_cast<float>(normals[face].z)},
                                .distance = static_cast<float>(offset)});
    }
    return build;
}

/// Named in the diagnostic when convex_hull() is given points that make no hull.
void convex_hull_rejected_the_points();

/// Named in the diagnostic when convex_hull()'s capacities are too small for the hull.
void convex_hull_capacity_is_too_small();

} // namespace hull_detail

/// @brief The convex hull of @p points, built at compile time (quickhull), rounded by @p radius.
/// @tparam VertexCapacity Vertices the hull may have (a cube's 8; at most the points).
/// @tparam FaceCapacity   Faces it may have (a cube's 6 after coplanar triangles merge).
/// @tparam IndexCapacity  Vertex indices of all its polygons (a cube's 24).
template<std::size_t VertexCapacity, std::size_t FaceCapacity, std::size_t IndexCapacity, std::size_t PointCount>
[[nodiscard]] consteval auto
convex_hull(const std::array<Vector3, PointCount>& points,
            float radius = 0.0F) -> FixedHull<VertexCapacity, FaceCapacity, IndexCapacity>
{
    const hull_detail::HullBuild build = hull_detail::quickhull(points);
    if (!build.problem.empty())
    {
        hull_detail::convex_hull_rejected_the_points();
    }
    if (build.vertices.size() > VertexCapacity || build.faces.size() > FaceCapacity || build.face_vertices.size() > IndexCapacity)
    {
        hull_detail::convex_hull_capacity_is_too_small();
    }
    FixedHull<VertexCapacity, FaceCapacity, IndexCapacity> hull{.radius = radius};
    std::ranges::copy(build.vertices, hull.vertices.begin());
    std::ranges::copy(build.planes, hull.planes.begin());
    std::ranges::copy(build.faces, hull.faces.begin());
    std::ranges::copy(build.face_vertices, hull.face_vertices.begin());
    hull.vertex_count = static_cast<std::uint32_t>(build.vertices.size());
    hull.face_count = static_cast<std::uint32_t>(build.faces.size());
    hull.index_count = static_cast<std::uint32_t>(build.face_vertices.size());
    return hull;
}

/// @brief A hull that owns its arrays (built at run time, or copied from a FixedHull). Its support function uses SIMD
///        from 16 vertices on (the same point as the scalar loop: the first of the farthest).
class HullShape
{
public:
    /// @brief The convex hull of @p points (quickhull), rounded by @p radius.
    /// @throws PhysicsError if the points make no hull (fewer than 4, coincident, collinear, coplanar, not finite) or
    ///         @p radius is negative.
    [[nodiscard]] static auto from_points(std::span<const Vector3> points,
                                          float radius = 0.0F) -> HullShape;

    /// @brief A copy of @p hull.
    template<std::size_t VertexCapacity, std::size_t FaceCapacity, std::size_t IndexCapacity>
    explicit HullShape(const FixedHull<VertexCapacity, FaceCapacity, IndexCapacity>& hull)
        : HullShape(hull.view())
    {
    }

    /// @brief A copy of @p hull.
    explicit HullShape(const HullView& hull);

    /// @brief The hull through spans.
    [[nodiscard]] auto view() const noexcept -> HullView
    {
        return {.vertices = m_vertices, .planes = m_planes, .faces = m_faces, .face_vertices = m_face_vertices, .radius = m_radius};
    }

    /// @brief The first vertex farthest along @p direction.
    [[nodiscard]] auto support(Vector3 direction) const noexcept -> Vector3;

    [[nodiscard]] auto margin() const noexcept -> float
    {
        return m_radius;
    }

    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_bounds;
    }

    [[nodiscard]] auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        return view().supporting_face(direction);
    }

    [[nodiscard]] auto operator==(const HullShape& other) const noexcept -> bool
    {
        return m_vertices == other.m_vertices && m_planes == other.m_planes && m_faces == other.m_faces && m_face_vertices == other.m_face_vertices
               && m_radius == other.m_radius;
    }

private:
    HullShape(std::vector<Vector3> vertices,
              std::vector<Plane> planes,
              std::vector<HullFace> faces,
              std::vector<std::uint16_t> face_vertices,
              float radius);

private:
    std::vector<Vector3> m_vertices{};
    std::vector<Plane> m_planes{};
    std::vector<HullFace> m_faces{};
    std::vector<std::uint16_t> m_face_vertices{};
    float m_radius = 0.0F;
    Aabb m_bounds{};
};

} // namespace gfx::physics
