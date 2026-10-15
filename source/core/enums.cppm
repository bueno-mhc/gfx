/// @file enums.cppm
/// @brief The enumeration machinery shared by the gfx libraries (traits, verification, bitmask
///        operators, spelling and parsing, std::format support), the enumerations and the constants
///        both the graphics and the platform libraries use. Each library's own enumerations and
///        constants live only in its own enums.cppm.
///
/// Conventions:
/// - Enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform headers
///   define as macros are avoided (checked by a CTest test).
/// - Every enumeration has an EnumerationTraits specialization listing the spelling of each enumerator,
///   verified at compile time (order, uniqueness, single bits for bitmasks) and, on GCC and Clang,
///   against the compiler's own spelling of each enumerator.
export module gfx.core.enums;

import std;

// Set to 1 by CMake for Debug builds (source/core/CMakeLists.txt); everything else is a Release build.
#ifndef GFX_DEBUG_BUILD
#define GFX_DEBUG_BUILD 0
#endif

export namespace gfx
{

// =================================================================================================
// Enumerations shared by the libraries
// =================================================================================================

/// @brief How much the library checks. Levels are ordered: each includes the checks of the previous one.
enum class ValidationLevel : std::uint8_t
{
    NONE,     ///< No checks: invalid usage is undefined behavior (the native API's own rules apply).
    CREATION, ///< Checks when objects are created and recordings begin; nothing per command (Release default).
    COMMANDS, ///< Also checks every command, descriptor write and push-constant write (Debug default).
};

/// @brief Memory heap an allocation failure refers to.
enum class MemoryDomain : std::uint8_t
{
    HOST,   ///< System memory.
    DEVICE, ///< GPU memory.
};

/// @brief Windowing system of a NativeWindowHandle: which alternative it holds.
enum class NativeWindowKind : std::uint8_t
{
    NONE,       ///< No window.
    WINDOWS,    ///< Win32 window (HINSTANCE, HWND).
    XLIB,       ///< X11 window through Xlib (Display*, Window).
    XCB,        ///< X11 window through XCB (xcb_connection_t*, xcb_window_t).
    WAYLAND,    ///< Wayland surface (wl_display*, wl_surface*).
    COCOA,      ///< macOS window (NSWindow*, CAMetalLayer*).
    WEB_CANVAS, ///< HTML canvas (CSS selector).
};

// =================================================================================================
// Constants shared by the libraries
// =================================================================================================

/// @brief Number of bits of a handle value holding the slot index.
inline constexpr std::uint32_t HANDLE_INDEX_BITS = 24;

/// @brief Number of bits of a handle value holding the slot generation.
inline constexpr std::uint32_t HANDLE_GENERATION_BITS = 8;

/// @brief Largest slot index a handle can address.
inline constexpr std::uint32_t MAXIMUM_HANDLE_INDEX = (1U << HANDLE_INDEX_BITS) - 1U;

/// @brief Largest slot generation; generations count from 1 to this value and then wrap to 1.
inline constexpr std::uint32_t MAXIMUM_HANDLE_GENERATION = (1U << HANDLE_GENERATION_BITS) - 1U;

/// @brief Values of a sparse enumeration below this limit are all checked at compile time for an
///        enumerator missing from its NAMES table (larger values only through their entries).
inline constexpr std::size_t SPARSE_VERIFICATION_LIMIT = 1024;

/// @brief FNV-1a offset basis: the hash of the empty string (32 bits).
inline constexpr std::uint32_t NAME_HASH_OFFSET_BASIS = 2166136261U;

/// @brief FNV-1a prime (32 bits).
inline constexpr std::uint32_t NAME_HASH_PRIME = 16777619U;

/// @brief Whether this build of the library is a Debug build.
inline constexpr bool DEBUG_BUILD = GFX_DEBUG_BUILD != 0;

/// @brief Validation level of instances that do not choose one: every check in Debug builds, only the
///        checks that cost nothing per command in Release builds.
inline constexpr ValidationLevel DEFAULT_VALIDATION_LEVEL = DEBUG_BUILD ? ValidationLevel::COMMANDS : ValidationLevel::CREATION;

/// @brief Timeout that never expires.
inline constexpr std::chrono::nanoseconds WAIT_INDEFINITELY = std::chrono::nanoseconds::max();

// =================================================================================================
// Enumeration descriptions
// =================================================================================================

/// @brief Associates an enumerator with its spelling, for diagnostics and formatting.
template<class Enumeration>
struct EnumeratorName
{
    Enumeration value;     ///< Enumerator.
    std::string_view name; ///< Its spelling, without the enumeration name.
};

/// @brief Compile-time description of an enumeration. Specialized for every enumeration of the
///        library with `IS_BITMASK` and `NAMES` (the single-bit enumerators of a bitmask, or every
///        enumerator of a sequential enumeration in declaration order). Enumerations whose values have
///        gaps (e.g. keys numbered by USB HID usage) also set `IS_SPARSE = true` and list their
///        enumerators in ascending value order.
template<class Enumeration>
struct EnumerationTraits;

/// @brief An enumeration with an EnumerationTraits specialization.
template<class Enumeration>
concept DescribedEnumeration = std::is_enum_v<Enumeration> // Must be an enumeration type
                                    && requires {
                                        { EnumerationTraits<Enumeration>::IS_BITMASK } -> std::convertible_to<bool>; // Must have a specialization with IS_BITMASK
                                        EnumerationTraits<Enumeration>::NAMES.size(); // Must have a specialization with NAMES
                                    };

/// @brief An enumeration whose enumerators are single bits combined with |, & and ^.
template<class Enumeration>
concept BitmaskEnumeration = DescribedEnumeration<Enumeration>
                                && EnumerationTraits<Enumeration>::IS_BITMASK;

/// @brief An enumeration whose values have gaps (traits with `IS_SPARSE = true`), e.g. keys numbered
///        by USB HID usage. NAMES lists its enumerators in ascending value order.
template<class Enumeration>
concept SparseEnumeration = DescribedEnumeration<Enumeration>
                                && !EnumerationTraits<Enumeration>::IS_BITMASK
                                && requires {
                                       requires EnumerationTraits<Enumeration>::IS_SPARSE;
                                   };

/// @brief An enumeration whose enumerators are numbered 0, 1, 2, ... without gaps.
template<class Enumeration>
concept SequentialEnumeration = DescribedEnumeration<Enumeration>
                                    && !EnumerationTraits<Enumeration>::IS_BITMASK
                                    && !SparseEnumeration<Enumeration>;

/// @brief Number of enumerators of a sequential enumeration (e.g. to size translation tables).
template<SequentialEnumeration Enumeration>
inline constexpr std::size_t ENUMERATOR_COUNT = EnumerationTraits<Enumeration>::NAMES.size();

/// @brief An enumeration naming the backends of one library: its traits also give BACKEND_LIBRARY,
///        the library's name in error messages ("gfx", "gfx::platform").
template<class Enumeration>
concept BackendEnumeration = SequentialEnumeration<Enumeration>
                                 && requires {
                                        { EnumerationTraits<Enumeration>::BACKEND_LIBRARY } -> std::convertible_to<std::string_view>;
                                    };

template<>
struct EnumerationTraits<ValidationLevel>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ValidationLevel::NONE, "NONE"},
        EnumeratorName{ValidationLevel::CREATION, "CREATION"},
        EnumeratorName{ValidationLevel::COMMANDS, "COMMANDS"},
    };
};

template<>
struct EnumerationTraits<MemoryDomain>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{MemoryDomain::HOST, "HOST"},
        EnumeratorName{MemoryDomain::DEVICE, "DEVICE"},
    };
};

template<>
struct EnumerationTraits<NativeWindowKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{NativeWindowKind::NONE, "NONE"},
        EnumeratorName{NativeWindowKind::WINDOWS, "WINDOWS"},
        EnumeratorName{NativeWindowKind::XLIB, "XLIB"},
        EnumeratorName{NativeWindowKind::XCB, "XCB"},
        EnumeratorName{NativeWindowKind::WAYLAND, "WAYLAND"},
        EnumeratorName{NativeWindowKind::COCOA, "COCOA"},
        EnumeratorName{NativeWindowKind::WEB_CANVAS, "WEB_CANVAS"},
    };
};

// =================================================================================================
// Bitmask operators
// =================================================================================================

/// @brief Union of two sets of bits.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator|(Enumeration left, Enumeration right) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) | std::to_underlying(right)));
}

/// @brief Intersection of two sets of bits.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator&(Enumeration left, Enumeration right) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) & std::to_underlying(right)));
}

/// @brief Bits set in exactly one of the operands.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator^(Enumeration left, Enumeration right) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    return static_cast<Enumeration>(static_cast<Underlying>(std::to_underlying(left) ^ std::to_underlying(right)));
}

/// @brief Complement of a set of bits, restricted to the bits named by the enumeration.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
operator~(Enumeration value) noexcept -> Enumeration
{
    using Underlying = std::underlying_type_t<Enumeration>;
    Underlying named_bits = 0;
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
    {
        named_bits = static_cast<Underlying>(named_bits | std::to_underlying(entry.value));
    }
    return static_cast<Enumeration>(static_cast<Underlying>(named_bits & static_cast<Underlying>(~std::to_underlying(value))));
}

/// @brief Adds the bits of @p right to @p left.
template<BitmaskEnumeration Enumeration>
constexpr auto
operator|=(Enumeration& left, Enumeration right) noexcept -> Enumeration&
{
    return left = left | right;
}

/// @brief Keeps in @p left only the bits also set in @p right.
template<BitmaskEnumeration Enumeration>
constexpr auto
operator&=(Enumeration& left, Enumeration right) noexcept -> Enumeration&
{
    return left = left & right;
}

/// @brief Toggles in @p left the bits set in @p right.
template<BitmaskEnumeration Enumeration>
constexpr auto
operator^=(Enumeration& left, Enumeration right) noexcept -> Enumeration&
{
    return left = left ^ right;
}

/// @brief Whether every bit of @p bits is set in @p value (true when @p bits is empty).
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
has_all(Enumeration value, Enumeration bits) noexcept -> bool
{
    return (value & bits) == bits;
}

/// @brief Whether at least one bit of @p bits is set in @p value.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
has_any(Enumeration value, Enumeration bits) noexcept -> bool
{
    return std::to_underlying(value & bits) != 0;
}

/// @brief Whether no bit is set.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
is_empty(Enumeration value) noexcept -> bool
{
    return std::to_underlying(value) == 0;
}

// =================================================================================================
// Spelling
// =================================================================================================

/// @brief Spelling of an enumerator of a sequential enumeration, e.g. "R8G8B8A8_UNORM".
/// @return The spelling, or "<invalid>" for a value that names no enumerator.
template<SequentialEnumeration Enumeration>
[[nodiscard]] constexpr auto
to_string(Enumeration value) noexcept -> std::string_view
{
    const auto index = static_cast<std::size_t>(std::to_underlying(value));
    const auto& names = EnumerationTraits<Enumeration>::NAMES;
    return index < names.size() ? names[index].name : std::string_view{"<invalid>"};
}

/// @brief Spelling of an enumerator of a sparse enumeration, e.g. "SPACE".
/// @return The spelling, or "<invalid>" for a value that names no enumerator.
template<SparseEnumeration Enumeration>
[[nodiscard]] constexpr auto
to_string(Enumeration value) noexcept -> std::string_view
{
    // NAMES is sorted by value: binary search.
    const auto& names = EnumerationTraits<Enumeration>::NAMES;
    const auto found = std::ranges::lower_bound(names, std::to_underlying(value), {},
                                                [](const auto& entry) { return std::to_underlying(entry.value); });
    return found != names.end() && found->value == value ? found->name : std::string_view{"<invalid>"};
}

/// @brief Spelling of a set of bits, e.g. "VERTEX | FRAGMENT"; "NONE" when empty. Bits that no
///        enumerator names are appended in hexadecimal.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
to_string(Enumeration value) -> std::string
{
    using Underlying = std::underlying_type_t<Enumeration>;
    auto remaining = std::to_underlying(value);

    // Check for the empty set first, so that we don't print "NONE | 0x..." for a value with no named bits.
    if (remaining == 0)
    {
        return "NONE";
    }

    // Iterate over the enumerators in order, appending their names to the text if their bit is set.
    std::string text;
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
    {
        // Skip enumerators that are not single bits, so that we don't print "ALL_GRAPHICS" when the value is "VERTEX | FRAGMENT | COMPUTE".
        const auto bit = std::to_underlying(entry.value);
        if ((remaining & bit) != 0)
        {
            // Append a separator if this is not the first enumerator name.
            if (!text.empty())
            {
                text += " | ";
            }

            // Append the enumerator name and remove its bit from the remaining bits.
            text += entry.name;

            // Remove the bit from the remaining bits, so that we don't print it again if another enumerator has the same bit set.
            remaining = static_cast<Underlying>(remaining & static_cast<Underlying>(~bit));
        }
    }

    // If any bits remain that no enumerator names, append them in hexadecimal.
    if (remaining != 0)
    {
        // Append the remaining bits in hexadecimal, with the most significant digit first. The digits are appended to the front of the string, so that the final string is in big-endian order.
        constexpr std::string_view digits = "0123456789ABCDEF";
        std::string hexadecimal;

        // Shift the remaining bits right by 4 each iteration, appending the least significant hex digit to the front of the string.
        for (auto rest = static_cast<std::uint64_t>(remaining); rest != 0; rest >>= 4U)
         {
            hexadecimal.insert(hexadecimal.begin(), digits[static_cast<std::size_t>(rest & 0xFU)]);
        }

        // Append a separator if this is not the first enumerator name, then append the hexadecimal representation of the remaining bits.
        if (!text.empty())
        {
            text += " | ";
        }

        // Append the hexadecimal representation of the remaining bits, prefixed with "0x".
        text += "0x" + hexadecimal;
    }
    return text;
}

// =================================================================================================
// Parsing
// =================================================================================================

/// @brief The enumerator spelled @p name, as to_string() spells it (e.g. "R8G8B8A8_UNORM").
/// @return The enumerator, or std::nullopt when no enumerator has that spelling.
template<class Enumeration>
    requires SequentialEnumeration<Enumeration> || SparseEnumeration<Enumeration>
[[nodiscard]] constexpr auto
from_string(std::string_view name) noexcept -> std::optional<Enumeration>
{
    // Look the spelling up in the enumeration's table.
    for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
    {
        if (entry.name == name)
        {
            return entry.value;
        }
    }
    return std::nullopt;
}

/// @brief The set of bits spelled @p text, as to_string() spells it: enumerator names separated by
///        "|" (spaces around them optional), "NONE" for the empty set, unnamed bits in hexadecimal
///        ("0x40").
/// @return The bits, or std::nullopt when a part names no enumerator.
template<BitmaskEnumeration Enumeration>
[[nodiscard]] constexpr auto
from_string(std::string_view text) noexcept -> std::optional<Enumeration>
{
    using Underlying = std::underlying_type_t<Enumeration>;
    std::uint64_t bits = 0;

    // Every part between separators names an enumerator, NONE, or a hexadecimal value.
    std::size_t start = 0;
    while (start <= text.size())
    {
        // The next part, without surrounding spaces.
        const std::size_t separator = std::min(text.find('|', start), text.size());
        std::string_view part = text.substr(start, separator - start);
        while (!part.empty() && part.front() == ' ')
        {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ')
        {
            part.remove_suffix(1);
        }

        // An enumerator, the empty set, or bits in hexadecimal.
        bool known = false;
        for (const auto& entry : EnumerationTraits<Enumeration>::NAMES)
        {
            if (entry.name == part)
            {
                bits |= static_cast<std::uint64_t>(std::to_underlying(entry.value));
                known = true;
            }
        }
        if (!known && part == "NONE")
        {
            known = true;
        }
        if (!known && part.size() > 2 && part.starts_with("0x"))
        {
            std::uint64_t value = 0;
            known = true;
            for (const char digit : part.substr(2))
            {
                const int nibble = digit >= '0' && digit <= '9'   ? digit - '0'
                                   : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                   : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                                                                  : -1;
                if (nibble < 0)
                {
                    known = false;
                    break;
                }
                value = (value << 4U) | static_cast<std::uint64_t>(nibble);
            }
            bits |= value;
        }
        if (!known)
        {
            return std::nullopt;
        }
        start = separator + 1;
    }
    return static_cast<Enumeration>(static_cast<Underlying>(bits));
}

} // namespace gfx

// =================================================================================================
// Compile-time verification of enumeration tables, used by every library's enums.cppm
// =================================================================================================
export namespace gfx::enumeration_verification
{

/// Checks order (sequential: value == index; bitmask: single ascending bits; sparse: ascending values)
/// and unique spellings.
template<class Enumeration, std::size_t Count>
consteval auto
names_are_consistent(const std::array<EnumeratorName<Enumeration>, Count>& names,
                     bool is_bitmask,
                     bool is_sparse) -> bool
{
    // The underlying type of the enumeration, converted to unsigned so that we can compare values without worrying about signedness.
    using Unsigned = std::make_unsigned_t<std::underlying_type_t<Enumeration>>;

    // Check that every enumerator has a non-empty name, that the enumerators are in the right order, and that no two enumerators have the same name.
    for (std::size_t index = 0; index < Count; ++index)
    {
        // The enumerator's value, converted to unsigned so that we can compare values without worrying about signedness.
        const auto value = static_cast<Unsigned>(std::to_underlying(names[index].value));

        // Check that the enumerator has a non-empty name.
        if (names[index].name.empty())
        {
            return false;
        }

        // Check that the enumerators are in the right order, and that no two enumerators have the same name.
        if (is_bitmask)
        {
            // Check that the enumerators are in ascending order, and that each enumerator is a single bit (1, 2, 4, 8, ...).
            if (!std::has_single_bit(value))
            {
                return false;
            }

            // Check that the enumerators are in ascending order, and that each enumerator is a single bit (1, 2, 4, 8, ...).
            if (index > 0 && value <= static_cast<Unsigned>(std::to_underlying(names[index - 1].value)))
            {
                return false;
            }
        }
        else if (is_sparse) // Sparse enumerations list strictly ascending values.
        {
            if (index > 0 && value <= static_cast<Unsigned>(std::to_underlying(names[index - 1].value)))
            {
                return false;
            }
        }
        else if (value != index) // Sequential enumerations must have values equal to their index.
        {
            return false;
        }

        // Iterate over all previous enumerators to check that no two enumerators have the same name.
        for (std::size_t other = 0; other < index; ++other)
        {
            // Check that no two enumerators have the same name.
            if (names[other].name == names[index].name)
            {
                return false;
            }
        }
    }
    return true;
}

#if defined(__GNUC__)
/// The compiler's own spelling of the enumerator Value: its last `::` component, or an empty view
/// when Value names no enumerator (GCC and Clang then print a cast such as "(gfx::Format)72").
template<auto Value>
consteval auto
compiler_spelling() -> std::string_view
{
    // The compiler's own spelling of the enumerator Value, as printed by __PRETTY_FUNCTION__.
    const std::string_view signature = __PRETTY_FUNCTION__;

    // The signature is of the form:
    const auto start = signature.find("Value = ");

    // If not found, the signature is not in the expected format, so we return an empty view.
    if (start == std::string_view::npos)
    {
        return {};
    }

    // The signature is of the form:
    const auto end = signature.find_first_of(";]", start);
    const auto value = signature.substr(start + 8, end - start - 8);

    // If not found, the signature is not in the expected format, so we return an empty view.
    if (value.find(')') != std::string_view::npos)
    {
        return {};
    }

    // The signature is of the form:
    const auto separator = value.rfind("::");

    // If not found, the signature is not in the expected format, so we return the whole value.
    return separator == std::string_view::npos ? value
                                               : value.substr(separator + 2);
}

/// Every NAMES entry is spelled exactly like its enumerator, and the enumeration declares no
/// enumerator missing from NAMES (sequential: right after the last entry; bitmask: any other bit).
template<DescribedEnumeration Enumeration>
consteval auto
names_match_compiler() -> bool
{
    using Traits = EnumerationTraits<Enumeration>;
    using Underlying = std::underlying_type_t<Enumeration>;

    // The number of entries in NAMES, so that we can check every entry for a spelling match.
    constexpr auto count = Traits::NAMES.size();

    // Check that every NAMES entry is spelled exactly like its enumerator.
    const bool entries_match = []<std::size_t... Index>(std::index_sequence<Index...>) {
        return ((compiler_spelling<Traits::NAMES[Index].value>() == Traits::NAMES[Index].name) && ...);
    }(std::make_index_sequence<count>{});

    // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
    if constexpr (Traits::IS_BITMASK)
    {
        // The number of bits in the underlying type of the enumeration, so that we can check every bit for a missing enumerator.
        constexpr auto bit_count = std::numeric_limits<std::make_unsigned_t<Underlying>>::digits;

        // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
        const bool no_unnamed_bit = []<std::size_t... Bit>(std::index_sequence<Bit...>)
        {
            constexpr auto named = [] {
                // The union of all bits named by the enumerators in NAMES, so that we can check every bit for a missing enumerator.
                std::make_unsigned_t<Underlying> bits = 0;

                // Compute the union of all bits named by the enumerators in NAMES.
                for (const auto& entry : Traits::NAMES)
                {
                    bits = static_cast<std::make_unsigned_t<Underlying>>(bits | static_cast<std::make_unsigned_t<Underlying>>(std::to_underlying(entry.value)));
                }

                // Return the union of all bits named by the enumerators in NAMES.
                return bits;
            }();

            // Check that every bit that is not named by an enumerator is not named by the compiler either. This ensures that the enumeration declares no enumerator missing from NAMES.
            return ((((named >> Bit) & 1U) != 0 ||
                     compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(std::uint64_t{1} << Bit))>().empty()) && ...);
        }(std::make_index_sequence<bit_count>{});

        // The entries in NAMES match the compiler's spelling, and the enumeration declares no enumerator missing from NAMES.
        return entries_match && no_unnamed_bit;
    }
    else if constexpr (SparseEnumeration<Enumeration>)
    {
        // Every value below SPARSE_VERIFICATION_LIMIT (and up to just past the largest named one) that NAMES
        // does not list must name no enumerator; larger values are only checked through their NAMES entries.
        constexpr auto largest = static_cast<std::size_t>(std::to_underlying(Traits::NAMES[count - 1].value));
        constexpr auto checked = std::min<std::size_t>(largest + 2, SPARSE_VERIFICATION_LIMIT);
        const bool no_unnamed_value = []<std::size_t... Value>(std::index_sequence<Value...>)
        {
            constexpr auto is_named = [](std::size_t value)
            {
                for (const auto& entry : Traits::NAMES)
                {
                    if (static_cast<std::size_t>(std::to_underlying(entry.value)) == value)
                    {
                        return true;
                    }
                }
                return false;
            };
            return ((is_named(Value) || compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(Value))>().empty()) && ...);
        }(std::make_index_sequence<checked>{});
        return entries_match && no_unnamed_value;
    }
    else
    {
        // The entries in NAMES match the compiler's spelling, and the enumeration declares no enumerator missing from NAMES (the last enumerator is right after the last entry in NAMES).
        return entries_match && compiler_spelling<static_cast<Enumeration>(static_cast<Underlying>(count))>().empty();
    }
}
#else
template<DescribedEnumeration Enumeration>
consteval auto names_match_compiler() -> bool {
    return true;
}
#endif

template<DescribedEnumeration Enumeration>
consteval auto
verify() -> bool
{
    // Check that the entries in NAMES are consistent with the enumeration, and that they match the compiler's spelling of the enumerators.
    return names_are_consistent(EnumerationTraits<Enumeration>::NAMES, EnumerationTraits<Enumeration>::IS_BITMASK,
                                SparseEnumeration<Enumeration>)
        && names_match_compiler<Enumeration>();
}

static_assert(verify<ValidationLevel>(), "EnumerationTraits<ValidationLevel>::NAMES is out of sync with the enumeration");
static_assert(verify<MemoryDomain>(), "EnumerationTraits<MemoryDomain>::NAMES is out of sync with the enumeration");
static_assert(verify<NativeWindowKind>(), "EnumerationTraits<NativeWindowKind>::NAMES is out of sync with the enumeration");

} // namespace gfx::enumeration_verification

// =================================================================================================
// std::format support: std::format("{}", gfx::Format::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM"
// =================================================================================================

/// @brief Formats any described gfx enumeration by its spelling (bitmasks as "A | B").
template<gfx::DescribedEnumeration Enumeration>
struct std::formatter<Enumeration, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(Enumeration value, FormatContext& context) const -> FormatContext::iterator
    {
        // Check if the enumeration is a bitmask enumeration, and format it accordingly.
        if constexpr (gfx::BitmaskEnumeration<Enumeration>)
        {
            // For bitmask enumerations, we can format the value as a string by using the to_string function, which will return a string with the names of
            // the enumerators that are set in the bitmask, separated by " | ". If no enumerators are set, it will return "0x0". We can then use the
            // std::formatter for std::string_view to format this string.
            const std::string text = gfx::to_string(value);
            return std::formatter<std::string_view, char>::format(text, context);
        }
        else
        {
            // For non-bitmask enumerations, we can use the underlying type to format the value as a string.
            return std::formatter<std::string_view, char>::format(gfx::to_string(value), context);
        }
    }
};
