/// @file site.cppm
/// @brief What code records: a ProfileSite (a place in the code, named and categorized once, made at compile time),
///        the recording functions, and ProfileScope (a scope timed from its construction to its destruction). The
///        PROFILE_* macros of <gfx/profiler.hpp> declare a site and call these; code may also use them directly:
/// @code
/// void load_level()
/// {
///     static constinit const gfx::profiler::ProfileSite site{"load level", gfx::profiler::ProfilerCategory::APPLICATION};
///     const gfx::profiler::ProfileScope scope{site};
///     // ...
/// }
/// @endcode
///        Recording never allocates (after a thread's first record), never locks (after it), never throws: a record
///        goes into the calling thread's ring, or is counted as dropped when the ring is full. Without a current
///        profiler or a session recording the site's category, a record costs one atomic load and a branch; without
///        PROFILER_ENABLED, nothing.
export module gfx.profiler:site;

import std;
import gfx.profiler.enums;

export namespace gfx::profiler
{

/// @brief 64-bit FNV-1a of @p text, continuing from @p hash (sites' identifiers; usable at compile time).
[[nodiscard]] constexpr auto
profile_hash(std::string_view text,
             std::uint64_t hash = 14'695'981'039'346'656'037ULL) noexcept -> std::uint64_t
{
    for (const char character : text)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

/// @brief A place in the code that records: a name, a category, the file and line it is at. Made once per place
///        (`static constinit`), it lives for the whole program, so records refer to it without copying its strings.
class ProfileSite
{
public:
    /// @param name     What is measured or counted (a string literal: it must outlive the program's recordings).
    /// @param category Which sessions record it.
    /// @param location Where it is (defaults to where the site is declared).
    constexpr explicit ProfileSite(std::string_view name,
                                   ProfilerCategory category = ProfilerCategory::GENERAL,
                                   std::source_location location = std::source_location::current()) noexcept
        : m_name{name}
        , m_category{category}
        , m_file{location.file_name()}
        , m_line{location.line()}
        , m_name_identifier{profile_hash(name)}
        , m_identifier{identify(name, m_file, m_line)}
    {
    }

    /// @brief A site named after the function it is in (what PROFILE_FUNCTION() declares).
    [[nodiscard]] static constexpr auto function(ProfilerCategory category = ProfilerCategory::GENERAL,
                                                 std::source_location location = std::source_location::current()) noexcept -> ProfileSite
    {
        return ProfileSite{location.function_name(), category, location};
    }

    /// @brief What is measured or counted.
    [[nodiscard]] constexpr auto name() const noexcept -> std::string_view
    {
        return m_name;
    }

    /// @brief Which sessions record it.
    [[nodiscard]] constexpr auto category() const noexcept -> ProfilerCategory
    {
        return m_category;
    }

    /// @brief The source file it is in.
    [[nodiscard]] constexpr auto file() const noexcept -> std::string_view
    {
        return m_file;
    }

    /// @brief The line it is at.
    [[nodiscard]] constexpr auto line() const noexcept -> std::uint32_t
    {
        return m_line;
    }

    /// @brief A hash of the name alone (asynchronous spans pair their begin and end by it and their token).
    [[nodiscard]] constexpr auto name_identifier() const noexcept -> std::uint64_t
    {
        return m_name_identifier;
    }

    /// @brief A hash of the name, file and line: the same in every run of the same program.
    [[nodiscard]] constexpr auto identifier() const noexcept -> std::uint64_t
    {
        return m_identifier;
    }

private:
    [[nodiscard]] static constexpr auto identify(std::string_view name,
                                                 std::string_view file,
                                                 std::uint32_t line) noexcept -> std::uint64_t
    {
        // The name, a separator, the file, a separator, the line's digits.
        std::uint64_t hash = profile_hash(file, profile_hash("|", profile_hash(name)));
        hash = profile_hash("|", hash);
        do
        {
            const char digit = static_cast<char>('0' + line % 10);
            hash = profile_hash(std::string_view{&digit, 1}, hash);
            line /= 10;
        } while (line != 0);
        return hash;
    }

    std::string_view m_name;
    ProfilerCategory m_category;
    std::string_view m_file;
    std::uint32_t m_line;
    std::uint64_t m_name_identifier;
    std::uint64_t m_identifier;
};

/// @brief Records that the scope of @p site begins on the calling thread.
void record_scope_begin(const ProfileSite& site) noexcept;

/// @brief Records that the scope of @p site ends on the calling thread (pairs with the last unmatched begin of
///        @p site).
void record_scope_end(const ProfileSite& site) noexcept;

/// @brief Records the integer @p value of the counter @p site.
void record_counter(const ProfileSite& site,
                    std::int64_t value) noexcept;

/// @brief Records the floating-point @p value of the counter @p site.
void record_counter_float(const ProfileSite& site,
                          double value) noexcept;

/// @brief Records a size in bytes (memory in use, bytes moved).
void record_memory(const ProfileSite& site,
                   std::uint64_t bytes) noexcept;

/// @brief Records an instant.
void record_marker(const ProfileSite& site) noexcept;

/// @brief Records that the span @p token of @p site's name begins; it ends with record_async_end() of the same name
///        and token, on any thread, in any later frame.
void record_async_begin(const ProfileSite& site,
                        std::uint64_t token) noexcept;

/// @brief Records that the span @p token of @p site's name ends.
void record_async_end(const ProfileSite& site,
                      std::uint64_t token) noexcept;

/// @brief Names the calling thread in traces (registers it with the current profiler, or with the next one it records
///        into). Allocates; not for hot paths.
void set_thread_name(std::string_view name);

/// @brief Whether a record of @p category made now would be kept: a current profiler with a session recording it.
[[nodiscard]] auto is_recording(ProfilerCategory category = ALL_PROFILER_CATEGORIES) noexcept -> bool;

/// @brief Times the scope of a site on the calling thread, from construction to destruction (or end()).
class ProfileScope
{
public:
    /// @brief Begins the scope of @p site.
    explicit ProfileScope(const ProfileSite& site) noexcept
        : m_site{&site}
    {
        if constexpr (PROFILER_ENABLED)
        {
            record_scope_begin(site);
        }
    }

    /// @brief Ends the scope unless end() did.
    ~ProfileScope()
    {
        end();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    auto operator=(const ProfileScope&) -> ProfileScope& = delete;
    auto operator=(ProfileScope&&) -> ProfileScope& = delete;

    /// @brief Ends the scope now (the destructor then records nothing).
    void end() noexcept
    {
        if constexpr (PROFILER_ENABLED)
        {
            if (m_site != nullptr)
            {
                record_scope_end(*m_site);
                m_site = nullptr;
            }
        }
    }

private:
    const ProfileSite* m_site;
};

} // namespace gfx::profiler
