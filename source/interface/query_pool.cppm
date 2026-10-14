/// @file query_pool.cppm
/// @brief QueryPool: GPU timestamps, occlusion counts and pipeline statistics (per-pass profiling).
export module gfx.interface:query_pool;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a QueryPool.
struct QueryPoolDescription
{
    QueryType type = QueryType::TIMESTAMP;                    ///< Kind of query.
    std::uint32_t count = 0;                                  ///< Number of queries.
    PipelineStatistic statistics = PipelineStatistic::NONE;   ///< Counters (QueryType::PIPELINE_STATISTICS).
    std::string_view debug_name{};                            ///< Name shown by debugging tools.
};

/// @brief Storage for query results written by commands.
class QueryPool : public BackendObject
{
public:
    /// @brief Handle naming this pool in commands.
    [[nodiscard]] virtual auto handle() const noexcept -> QueryPoolHandle = 0;

    /// @brief Kind of query.
    [[nodiscard]] virtual auto type() const noexcept -> QueryType = 0;

    /// @brief Number of queries.
    [[nodiscard]] virtual auto count() const noexcept -> std::uint32_t = 0;

    /// @brief Copies results, starting at query @p first, into @p destination (one value per query,
    ///        or one per selected statistic for pipeline statistics).
    /// @param wait Block until every requested result is available.
    /// @return Whether every requested result was available.
    [[nodiscard]] virtual auto results(std::uint32_t first,
                                       std::span<std::uint64_t> destination,
                                       bool wait = true) -> bool = 0;
};

auto
QueryPool::results(std::uint32_t /*first*/,
                   std::span<std::uint64_t> /*destination*/,
                   bool /*wait*/) -> bool
{
    throw_not_implemented("QueryPool::results");
}

} // namespace gfx
