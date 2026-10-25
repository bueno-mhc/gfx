/// @file collide.cppm
/// @brief Collision of any two shapes at their poses (run time): compounds recurse into the children their partner's
///        box overlaps; convex shapes collide through the convex dispatch; a convex shape against a mesh or a height
///        field collides with each triangle under its box, one-sided unless the mesh is two-sided, contacts on inactive
///        edges taking the triangle's normal; two meshes or height fields collide triangle by triangle (separating
///        axes). The manifolds of one pair whose normals agree (CollisionSettings::merge_cosine) merge into one of at
///        most four points; at most CollisionSettings::maximum_manifolds are kept, the deepest.
///
/// collide() is pure: two threads collide different pairs at once, each with its own ContactCollector (which keeps its
/// storage between calls: no allocation once it has grown).
export module gfx.physics.narrow_phase:collide;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.shapes;
import :contact;
import :convex;

export namespace gfx::physics
{

/// @brief A shape at a pose.
struct ShapeInstance
{
    const Shape* shape = nullptr; ///< Not null.
    Transform pose{};             ///< Its frame in the world.
};

class ContactCollector;

/// @brief The contact manifolds of @p first and @p second into @p contacts (cleared first).
/// @throws PhysicsError if an instance has no shape or @p settings are invalid.
void collide(const ShapeInstance& first,
             const ShapeInstance& second,
             ContactCollector& contacts,
             const CollisionSettings& settings = {});

/// @brief The manifolds collide() found, and the storage it reuses.
class ContactCollector
{
public:
    /// @brief Forgets the manifolds (the storage stays).
    void clear() noexcept
    {
        m_manifolds.clear();
    }

    /// @brief The manifolds of the last collide().
    [[nodiscard]] auto manifolds() const noexcept -> std::span<const ContactManifold>
    {
        return m_manifolds;
    }

    /// @brief Their points, all together.
    [[nodiscard]] auto point_count() const noexcept -> std::size_t
    {
        std::size_t count = 0;
        for (const ContactManifold& manifold : m_manifolds)
        {
            count += manifold.point_count;
        }
        return count;
    }

private:
    friend void collide(const ShapeInstance& first,
                        const ShapeInstance& second,
                        ContactCollector& contacts,
                        const CollisionSettings& settings);

private:
    std::vector<ContactManifold> m_manifolds{};
    std::vector<ContactManifold> m_found{};
    std::vector<ContactPoint> m_points{};
};

/// @brief The distance between two convex shapes at their poses (convex_distance()).
/// @throws PhysicsError if a shape is missing or not convex.
[[nodiscard]] auto shape_distance(const ShapeInstance& first,
                                  const ShapeInstance& second) -> ShapeDistance;

} // namespace gfx::physics
