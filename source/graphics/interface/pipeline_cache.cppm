/// @file pipeline_cache.cppm
/// @brief PipelineCache: compiled pipelines kept across runs, so shipping builds do not compile them again
///        (VkPipelineCache, ID3D12PipelineLibrary).
///
/// Pipeline compilation is the main source of hitches in shipping games. Save the cache when the
/// application exits (serialize()), load it at the next start (PipelineCacheDescription::initial_data),
/// and create pipelines during loading screens, in the background (PipelineCreationMode::ASYNCHRONOUS).
export module gfx.interface:pipeline_cache;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a PipelineCache.
struct PipelineCacheDescription
{
    std::span<const std::byte> initial_data{}; ///< Data from serialize() of an earlier run. Data from another
                                               ///< driver, device or backend is ignored (the cache starts empty).
    std::string_view debug_name{};             ///< Name shown by debugging tools.
};

/// @brief Compiled pipelines, shared by pipeline creations that name it (thread-safe).
class PipelineCache : public BackendObject
{
public:
    /// @brief Handle naming this cache in pipeline descriptions.
    [[nodiscard]] virtual auto handle() const noexcept -> PipelineCacheHandle = 0;

    /// @brief Whether initial_data was accepted (false when it came from another driver or device).
    [[nodiscard]] virtual auto loaded_initial_data() const noexcept -> bool = 0;

    /// @brief Everything the cache holds, to store and pass as initial_data at the next start.
    [[nodiscard]] virtual auto serialize() const -> std::vector<std::byte> = 0;
};

auto
PipelineCache::serialize() const -> std::vector<std::byte>
{
    throw_not_implemented("PipelineCache::serialize");
}

} // namespace gfx
