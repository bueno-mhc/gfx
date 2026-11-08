// Recording: the current profiler, the categories it records, each thread's context (found through a thread-local
// cache), and the record functions the PROFILE_* macros call. A record costs a relaxed load and a branch when nothing
// records its category; otherwise an acquire load, a thread-local read, a clock reading and a ring write.
module gfx.profiler;

import std;
import gfx.profiler.enums;
import :state;

namespace gfx::profiler
{

namespace
{

std::atomic<Profiler*> g_current_profiler{nullptr};
std::atomic<Profiler::Implementation*> g_current{nullptr};
std::atomic<std::uint32_t> g_categories{0};

/// The context of the calling thread in the profiler it last recorded into.
struct ThreadCache
{
    std::uint64_t profiler{0};
    ThreadContext* context{nullptr};
};

thread_local ThreadCache t_cache{};
thread_local std::string t_thread_name{};

/// Registers the calling thread with @p profiler (once per thread and profiler: the only lock and allocation of
/// recording). Null when it cannot (memory exhausted): the record is lost.
auto
attach(Profiler::Implementation& profiler) noexcept -> ThreadContext*
{
    try
    {
        const std::scoped_lock lock{profiler.mutex};
        const auto index = static_cast<std::uint32_t>(profiler.contexts.size());
        profiler.contexts.push_back(std::make_unique<ThreadContext>(index, profiler.description.ring_capacity, t_thread_name));
        t_cache = {.profiler = profiler.id, .context = profiler.contexts.back().get()};
        return t_cache.context;
    }
    catch (...)
    {
        return nullptr;
    }
}

/// Adds a record of @p site to the calling thread's ring, if the current profiler records its category.
void
push(const ProfileSite& site,
     RecordKind kind,
     std::uint64_t value) noexcept
{
    if constexpr (!PROFILER_ENABLED)
    {
        return;
    }
    // Nothing records this category: one load and a branch.
    const std::uint32_t categories = g_categories.load(std::memory_order_relaxed);
    if ((categories & std::to_underlying(site.category())) == 0U)
    {
        return;
    }
    Profiler::Implementation* profiler = g_current.load(std::memory_order_acquire);
    if (profiler == nullptr)
    {
        return;
    }
    // The thread's ring in this profiler (registered at its first record).
    ThreadContext* context = t_cache.profiler == profiler->id ? t_cache.context : attach(*profiler);
    if (context != nullptr)
    {
        (void)context->ring.push({.time = now_nanoseconds(), .site = &site, .value = value, .kind = kind});
    }
}

} // namespace

RecordRing::RecordRing(std::uint32_t capacity)
    : m_records{std::make_unique<ProfileRecord[]>(capacity)}
    , m_capacity{capacity}
    , m_mask{capacity - 1U}
{
}

ThreadContext::ThreadContext(std::uint32_t context_index,
                             std::uint32_t capacity,
                             std::string thread_name)
    : index{context_index}
    , name{std::move(thread_name)}
    , ring{capacity}
{
}

auto
now_nanoseconds() noexcept -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void
publish_recording(Profiler::Implementation& profiler) noexcept
{
    if (g_current.load(std::memory_order_acquire) == &profiler)
    {
        g_categories.store(profiler.recording_categories.load(std::memory_order_relaxed), std::memory_order_release);
    }
}

void
forget_current(Profiler::Implementation& profiler) noexcept
{
    Profiler::Implementation* expected = &profiler;
    if (g_current.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel))
    {
        g_categories.store(0, std::memory_order_release);
        g_current_profiler.store(nullptr, std::memory_order_release);
    }
}

auto
ProfilerAccess::implementation(Profiler& profiler) noexcept -> Profiler::Implementation&
{
    return *profiler.m_implementation;
}

void
set_current_profiler(Profiler* profiler) noexcept
{
    Profiler::Implementation* implementation = profiler != nullptr ? &ProfilerAccess::implementation(*profiler) : nullptr;
    g_current_profiler.store(profiler, std::memory_order_release);
    g_current.store(implementation, std::memory_order_release);
    g_categories.store(implementation != nullptr ? implementation->recording_categories.load(std::memory_order_relaxed) : 0U,
                       std::memory_order_release);
}

auto
current_profiler() noexcept -> Profiler*
{
    return g_current_profiler.load(std::memory_order_acquire);
}

void
record_scope_begin(const ProfileSite& site) noexcept
{
    push(site, RecordKind::SCOPE_BEGIN, 0);
}

void
record_scope_end(const ProfileSite& site) noexcept
{
    push(site, RecordKind::SCOPE_END, 0);
}

void
record_counter(const ProfileSite& site,
               std::int64_t value) noexcept
{
    push(site, RecordKind::COUNTER, std::bit_cast<std::uint64_t>(value));
}

void
record_counter_float(const ProfileSite& site,
                     double value) noexcept
{
    push(site, RecordKind::COUNTER_FLOAT, std::bit_cast<std::uint64_t>(value));
}

void
record_memory(const ProfileSite& site,
              std::uint64_t bytes) noexcept
{
    push(site, RecordKind::MEMORY, bytes);
}

void
record_marker(const ProfileSite& site) noexcept
{
    push(site, RecordKind::MARKER, 0);
}

void
record_async_begin(const ProfileSite& site,
                   std::uint64_t token) noexcept
{
    push(site, RecordKind::ASYNC_BEGIN, token);
}

void
record_async_end(const ProfileSite& site,
                 std::uint64_t token) noexcept
{
    push(site, RecordKind::ASYNC_END, token);
}

void
set_thread_name(std::string_view name)
{
    // Kept for the profilers the thread registers with later; applied now to the current one.
    t_thread_name = name;
    Profiler::Implementation* profiler = g_current.load(std::memory_order_acquire);
    if (profiler == nullptr)
    {
        return;
    }
    if (t_cache.profiler != profiler->id)
    {
        (void)attach(*profiler);
        return;
    }
    const std::scoped_lock lock{profiler->mutex};
    t_cache.context->name = name;
}

auto
is_recording(ProfilerCategory category) noexcept -> bool
{
    return PROFILER_ENABLED && (g_categories.load(std::memory_order_relaxed) & std::to_underlying(category)) != 0U
           && g_current.load(std::memory_order_acquire) != nullptr;
}

} // namespace gfx::profiler
