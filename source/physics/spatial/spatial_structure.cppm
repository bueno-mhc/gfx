/// @file spatial_structure.cppm
/// @brief SpatialStructure: any of the spatial structures behind a virtual interface, for a structure chosen at run
///        time (SpatialStructureAdapter wraps one; make_spatial_structure() makes one of each kind). Visitors are
///        std::function: a lambda capturing a reference or two, or std::ref(visitor), is stored without allocating.
export module gfx.physics.spatial:spatial_structure;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import :structure;

export namespace gfx::physics
{

/// @brief A spatial structure chosen at run time.
class SpatialStructure
{
public:
    /// @brief Gets each key found; returns false to stop.
    using Visitor = std::function<bool(ColliderHandle)>;

    /// @brief Gets each key a ray finds and the distance at which the ray enters its box; returns false to stop.
    using RayVisitor = std::function<bool(ColliderHandle, float)>;

    /// @brief Gets each pair of keys whose boxes overlap.
    using PairVisitor = std::function<void(ColliderHandle, ColliderHandle)>;

    /// @brief Gets each key with its box.
    using ItemVisitor = std::function<void(ColliderHandle, const Aabb&)>;

    /// @brief Gets each node or cell.
    using NodeVisitor = std::function<void(const SpatialNode&)>;

    virtual ~SpatialStructure() = default;

    /// @brief Its kind.
    [[nodiscard]] virtual auto kind() const noexcept -> SpatialStructureKind = 0;

    /// @brief Stores @p key with @p box.
    virtual auto insert(ColliderHandle key,
                        const Aabb& box) -> SpatialProxyHandle = 0;

    /// @brief Forgets @p proxy.
    /// @throws PhysicsError if @p proxy names nothing stored.
    virtual void remove(SpatialProxyHandle proxy) = 0;

    /// @brief Gives @p proxy the box @p box.
    /// @return Whether it moved in the structure.
    virtual auto update(SpatialProxyHandle proxy,
                        const Aabb& box) -> bool = 0;

    /// @brief The key stored with @p proxy.
    [[nodiscard]] virtual auto key(SpatialProxyHandle proxy) const -> ColliderHandle = 0;

    /// @brief The box stored with @p proxy.
    [[nodiscard]] virtual auto box(SpatialProxyHandle proxy) const -> Aabb = 0;

    /// @brief Proxies stored.
    [[nodiscard]] virtual auto size() const noexcept -> std::uint32_t = 0;

    /// @brief Forgets every proxy.
    virtual void clear() = 0;

    /// @brief Visits the keys whose boxes overlap @p box.
    virtual void query(const Aabb& box,
                       const Visitor& visitor) const = 0;

    /// @brief Visits the keys whose boxes @p ray enters within @p maximum.
    virtual void query(const Ray& ray,
                       float maximum,
                       const RayVisitor& visitor) const = 0;

    /// @brief Visits the keys whose boxes @p segment touches.
    virtual void query(const Segment& segment,
                       const Visitor& visitor) const = 0;

    /// @brief Visits the keys whose boxes overlap @p sphere.
    virtual void query(const Sphere& sphere,
                       const Visitor& visitor) const = 0;

    /// @brief Visits the keys whose boxes are not outside @p frustum.
    virtual void query(const Frustum& frustum,
                       const Visitor& visitor) const = 0;

    /// @brief Visits each pair of keys whose boxes overlap, once.
    virtual void pairs(const PairVisitor& visitor) const = 0;

    /// @brief Visits every key with its box.
    virtual void iterate(const ItemVisitor& visitor) const = 0;

    /// @brief Visits its nodes or cells (SpatialNode: what a debug view draws).
    virtual void nodes(const NodeVisitor& visitor) const = 0;

protected:
    SpatialStructure() = default;
    SpatialStructure(const SpatialStructure&) = default;
    auto operator=(const SpatialStructure&) -> SpatialStructure& = default;
};

/// @brief @p Structure behind the SpatialStructure interface.
template<SpatialStructureConcept Structure>
class SpatialStructureAdapter final : public SpatialStructure
{
public:
    /// @param structure The structure (empty, or with proxies already).
    explicit SpatialStructureAdapter(Structure structure = Structure{})
        : m_structure{std::move(structure)}
    {
    }

    /// @brief The structure itself.
    [[nodiscard]] auto structure() noexcept -> Structure&
    {
        return m_structure;
    }

    /// @brief The structure itself.
    [[nodiscard]] auto structure() const noexcept -> const Structure&
    {
        return m_structure;
    }

    [[nodiscard]] auto kind() const noexcept -> SpatialStructureKind override
    {
        return Structure::KIND;
    }

    auto insert(ColliderHandle key,
                const Aabb& box) -> SpatialProxyHandle override
    {
        return m_structure.insert(key, box);
    }

    void remove(SpatialProxyHandle proxy) override
    {
        m_structure.remove(proxy);
    }

    auto update(SpatialProxyHandle proxy,
                const Aabb& box) -> bool override
    {
        return m_structure.update(proxy, box);
    }

    [[nodiscard]] auto key(SpatialProxyHandle proxy) const -> ColliderHandle override
    {
        return m_structure.key(proxy);
    }

    [[nodiscard]] auto box(SpatialProxyHandle proxy) const -> Aabb override
    {
        return m_structure.box(proxy);
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t override
    {
        return m_structure.size();
    }

    void clear() override
    {
        m_structure.clear();
    }

    void query(const Aabb& box,
               const Visitor& visitor) const override
    {
        m_structure.query(box, visitor);
    }

    void query(const Ray& ray,
               float maximum,
               const RayVisitor& visitor) const override
    {
        m_structure.query(ray, maximum, visitor);
    }

    void query(const Segment& segment,
               const Visitor& visitor) const override
    {
        m_structure.query(segment, visitor);
    }

    void query(const Sphere& sphere,
               const Visitor& visitor) const override
    {
        m_structure.query(sphere, visitor);
    }

    void query(const Frustum& frustum,
               const Visitor& visitor) const override
    {
        m_structure.query(frustum, visitor);
    }

    void pairs(const PairVisitor& visitor) const override
    {
        m_structure.pairs(visitor);
    }

    void iterate(const ItemVisitor& visitor) const override
    {
        m_structure.iterate(visitor);
    }

    void nodes(const NodeVisitor& visitor) const override
    {
        m_structure.nodes(visitor);
    }

private:
    Structure m_structure;
};

/// @brief A structure of @p kind with its default parameters (octrees and quadtrees: a region of ±1024, depth 8,
///        quadtrees over X and Z; grids: cells of 1; sweep and prune: one axis).
[[nodiscard]] auto make_spatial_structure(SpatialStructureKind kind) -> std::unique_ptr<SpatialStructure>;

} // namespace gfx::physics
