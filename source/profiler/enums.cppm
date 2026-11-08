/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx profiler. No other file of the library defines either; the
///        enumeration machinery (traits, verification, bitmask operators, spelling and parsing) comes from
///        gfx.core.enums, which this module re-exports.
///
/// Conventions: enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform headers
/// define as macros are avoided, which a CTest test checks.
module;

// Set to 1 by CMake when the option GFX_PROFILER is on (source/profiler/CMakeLists.txt); the profiler then records.
#ifndef GFX_PROFILER_ENABLED
#define GFX_PROFILER_ENABLED 0
#endif

export module gfx.profiler.enums;

import std;
export import gfx.core.enums;

export namespace gfx::profiler
{

// The bitmask operators and helpers of gfx, found by argument-dependent lookup for the bitmask enumerations of this
// namespace too.
using gfx::operator|;
using gfx::operator&;
using gfx::operator^;
using gfx::operator~;
using gfx::operator|=;
using gfx::operator&=;
using gfx::operator^=;
using gfx::has_all;
using gfx::has_any;
using gfx::is_empty;
using gfx::to_string;

// =================================================================================================
// Enumerations
// =================================================================================================

/// @brief A part of the profiler, named by its errors ("[gfx::profiler::EXPORT] ...").
enum class ProfilerSubsystem : std::uint8_t
{
    RECORDING, ///< Thread contexts and the records they keep.
    SESSIONS,  ///< Sessions, frames and the traces they make.
    EXPORT,    ///< Trace files: writing and reading them.
    REPORT,    ///< Reports made from traces.
};

/// @brief What a recorded site is about; sessions record the categories they are given. Bitmask.
enum class ProfilerCategory : std::uint32_t
{
    NONE        = 0,       ///< No category.
    GENERAL     = 1U << 0, ///< Anything not filed elsewhere (the macros' default).
    PHYSICS     = 1U << 1, ///< Physics worlds and their backends.
    GRAPHICS    = 1U << 2, ///< Command recording, submission, presentation.
    GPU         = 1U << 3, ///< Work timed on a GPU.
    UI          = 1U << 4, ///< The UI library.
    PLATFORM    = 1U << 5, ///< Windows, events and input.
    APPLICATION = 1U << 6, ///< The application's own code.
};

/// @brief What a record says.
enum class RecordKind : std::uint8_t
{
    SCOPE_BEGIN,   ///< A scope opened.
    SCOPE_END,     ///< A scope closed.
    COUNTER,       ///< An integer sample.
    COUNTER_FLOAT, ///< A floating-point sample.
    MEMORY,        ///< A size in bytes.
    MARKER,        ///< An instant.
    ASYNC_BEGIN,   ///< A span that may end on another thread or in another frame began (its token names it).
    ASYNC_END,     ///< That span ended.
};

/// @brief The formats a trace is written in.
enum class TraceFormat : std::uint8_t
{
    CHROME_JSON,       ///< Chrome Tracing JSON (chrome://tracing, Perfetto).
    BINARY,            ///< The profiler's own versioned format, read back by read_binary_trace().
    BINARY_COMPRESSED, ///< The same, compressed with zstd.
};

/// @brief Whether a GPU scope was timed, and why not.
enum class GpuScopeStatus : std::uint8_t
{
    TIMED,       ///< Both timestamps were written and read.
    DROPPED,     ///< Its frame's timestamp queries were used up: recorded, not timed (its times are when it was recorded).
    UNAVAILABLE, ///< Its queue writes no timestamps.
    INCOMPLETE,  ///< Its timestamps were not written in time (its commands were not submitted, or did not finish).
};

/// @brief How a GPU track's times were placed on the CPU's timeline.
enum class GpuClockAlignment : std::uint8_t
{
    CALIBRATED,  ///< From calibrated timestamps (the device paired its clock with the host's).
    ESTIMATED,   ///< From a submission writing a timestamp, timed on the CPU: off by up to half its round trip.
    UNAVAILABLE, ///< Not placed: the queue writes no timestamps.
};

/// @brief Where a session is.
enum class SessionState : std::uint8_t
{
    RECORDING, ///< Records go into it.
    STOPPED,   ///< Its frame count is reached: nothing is recorded, it waits to be finalized.
    FINALIZED, ///< Its trace was made; it holds nothing more.
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief Whether this build records (option GFX_PROFILER): without it, the PROFILE_* macros are empty and the
///        recording functions return at once.
inline constexpr bool PROFILER_ENABLED = GFX_PROFILER_ENABLED != 0;

/// @brief Every category (the seven bits of ProfilerCategory).
inline constexpr ProfilerCategory ALL_PROFILER_CATEGORIES = static_cast<ProfilerCategory>((1U << 7U) - 1U);

/// @brief Bytes of one record in a thread's ring (half a cache line).
inline constexpr std::size_t PROFILER_RECORD_SIZE = 32;

/// @brief Records a thread's ring holds unless the profiler's description says otherwise.
inline constexpr std::uint32_t DEFAULT_PROFILER_RING_CAPACITY = 1U << 16U;

/// @brief The largest ring a thread may have (records).
inline constexpr std::uint32_t MAXIMUM_PROFILER_RING_CAPACITY = 1U << 26U;

/// @brief Bytes the cache lines of the rings' counters are padded to.
inline constexpr std::size_t PROFILER_CACHE_LINE_SIZE = 64;

/// @brief The first bytes of a binary trace.
inline constexpr std::string_view PROFILER_TRACE_MAGIC = "GFXTRACE";

/// @brief The version of the binary trace format this library writes and reads (2: GPU tracks and scopes).
inline constexpr std::uint32_t PROFILER_TRACE_VERSION = 2;

/// @brief Bytes of a binary trace's header.
inline constexpr std::size_t PROFILER_TRACE_HEADER_SIZE = 32;

} // namespace gfx::profiler

// =================================================================================================
// Enumeration traits (names, bitmask or not), verified below
// =================================================================================================

export namespace gfx
{

template<>
struct EnumerationTraits<profiler::ProfilerSubsystem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::profiler"; ///< Errors name these "gfx::profiler::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{profiler::ProfilerSubsystem::RECORDING, "RECORDING"},
        EnumeratorName{profiler::ProfilerSubsystem::SESSIONS, "SESSIONS"},
        EnumeratorName{profiler::ProfilerSubsystem::EXPORT, "EXPORT"},
        EnumeratorName{profiler::ProfilerSubsystem::REPORT, "REPORT"},
    };
};

template<>
struct EnumerationTraits<profiler::ProfilerCategory>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{profiler::ProfilerCategory::GENERAL, "GENERAL"},
        EnumeratorName{profiler::ProfilerCategory::PHYSICS, "PHYSICS"},
        EnumeratorName{profiler::ProfilerCategory::GRAPHICS, "GRAPHICS"},
        EnumeratorName{profiler::ProfilerCategory::GPU, "GPU"},
        EnumeratorName{profiler::ProfilerCategory::UI, "UI"},
        EnumeratorName{profiler::ProfilerCategory::PLATFORM, "PLATFORM"},
        EnumeratorName{profiler::ProfilerCategory::APPLICATION, "APPLICATION"},
    };
};

template<>
struct EnumerationTraits<profiler::RecordKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{profiler::RecordKind::SCOPE_BEGIN, "SCOPE_BEGIN"},
        EnumeratorName{profiler::RecordKind::SCOPE_END, "SCOPE_END"},
        EnumeratorName{profiler::RecordKind::COUNTER, "COUNTER"},
        EnumeratorName{profiler::RecordKind::COUNTER_FLOAT, "COUNTER_FLOAT"},
        EnumeratorName{profiler::RecordKind::MEMORY, "MEMORY"},
        EnumeratorName{profiler::RecordKind::MARKER, "MARKER"},
        EnumeratorName{profiler::RecordKind::ASYNC_BEGIN, "ASYNC_BEGIN"},
        EnumeratorName{profiler::RecordKind::ASYNC_END, "ASYNC_END"},
    };
};

template<>
struct EnumerationTraits<profiler::TraceFormat>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{profiler::TraceFormat::CHROME_JSON, "CHROME_JSON"},
        EnumeratorName{profiler::TraceFormat::BINARY, "BINARY"},
        EnumeratorName{profiler::TraceFormat::BINARY_COMPRESSED, "BINARY_COMPRESSED"},
    };
};

template<>
struct EnumerationTraits<profiler::GpuScopeStatus>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{profiler::GpuScopeStatus::TIMED, "TIMED"},
        EnumeratorName{profiler::GpuScopeStatus::DROPPED, "DROPPED"},
        EnumeratorName{profiler::GpuScopeStatus::UNAVAILABLE, "UNAVAILABLE"},
        EnumeratorName{profiler::GpuScopeStatus::INCOMPLETE, "INCOMPLETE"},
    };
};

template<>
struct EnumerationTraits<profiler::GpuClockAlignment>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{profiler::GpuClockAlignment::CALIBRATED, "CALIBRATED"},
        EnumeratorName{profiler::GpuClockAlignment::ESTIMATED, "ESTIMATED"},
        EnumeratorName{profiler::GpuClockAlignment::UNAVAILABLE, "UNAVAILABLE"},
    };
};

template<>
struct EnumerationTraits<profiler::SessionState>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{profiler::SessionState::RECORDING, "RECORDING"},
        EnumeratorName{profiler::SessionState::STOPPED, "STOPPED"},
        EnumeratorName{profiler::SessionState::FINALIZED, "FINALIZED"},
    };
};

} // namespace gfx

namespace gfx::enumeration_verification
{

static_assert(verify<profiler::ProfilerSubsystem>(), "EnumerationTraits<profiler::ProfilerSubsystem>::NAMES is out of sync with the enumeration");
static_assert(verify<profiler::ProfilerCategory>(), "EnumerationTraits<profiler::ProfilerCategory>::NAMES is out of sync with the enumeration");
static_assert(verify<profiler::RecordKind>(), "EnumerationTraits<profiler::RecordKind>::NAMES is out of sync with the enumeration");
static_assert(verify<profiler::TraceFormat>(), "EnumerationTraits<profiler::TraceFormat>::NAMES is out of sync with the enumeration");
static_assert(verify<profiler::GpuScopeStatus>(), "EnumerationTraits<profiler::GpuScopeStatus>::NAMES is out of sync with the enumeration");
static_assert(verify<profiler::GpuClockAlignment>(), "EnumerationTraits<profiler::GpuClockAlignment>::NAMES is out of sync with the enumeration");
static_assert(verify<profiler::SessionState>(), "EnumerationTraits<profiler::SessionState>::NAMES is out of sync with the enumeration");

} // namespace gfx::enumeration_verification
