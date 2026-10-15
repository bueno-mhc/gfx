/// @file handles.cppm
/// @brief Strongly typed numbers shared by the gfx libraries: handles (objects a backend created) and
///        identifiers (names the application declared).
///
/// A handle is a 32-bit value: bits [23:0] hold a slot index and bits [31:24] a generation that starts
/// at 1, so the value 0 is always the null handle. Handles are trivially copyable, so they can be
/// stored in render graph nodes, sorted, hashed and passed between threads by value. User code can only
/// test, compare, hash and format a handle: its value is private and supports no arithmetic. Encoding
/// and decoding require a HandleKey, which only HandleIssuer descendants (the backends) can obtain.
///
/// An identifier is the 32-bit FNV-1a hash of a name, computed at compile time (`"jump"_action`): the
/// cheap comparison of an interned string without a string table at run time.
export module gfx.core.handles;

import std;
import gfx.core.enums;
import gfx.core.errors;

export namespace gfx
{

class HandleIssuer;

/// @brief Token proving that the caller may encode or decode handle values.
///
/// It can only be created by HandleIssuer, so it is out of reach of code that merely uses handles.
class HandleKey final
{
private:
    friend class HandleIssuer;
    constexpr HandleKey() noexcept = default;
};

/// @brief Base class of the objects allowed to create handles and read their slot (the backends).
class HandleIssuer
{
protected:
    constexpr HandleIssuer() noexcept = default;
    constexpr HandleIssuer(const HandleIssuer&) noexcept = default;
    constexpr HandleIssuer(HandleIssuer&&) noexcept = default;
    constexpr auto operator=(const HandleIssuer&) noexcept -> HandleIssuer& = default;
    constexpr auto operator=(HandleIssuer&&) noexcept -> HandleIssuer& = default;
    ~HandleIssuer() = default;

    /// @brief Key required by Handle::create, Handle::index and Handle::generation.
    [[nodiscard]] static constexpr auto handle_key() noexcept -> HandleKey
    {
        return HandleKey{};
    }
};

/// @brief Strongly typed, trivially copyable numeric name of an object a backend created.
/// @tparam Tag Empty type identifying the kind of object; it provides `NAME` (used in messages). Each
///             library may require more (the graphics tags provide `USABLE_ON`).
template<class Tag>
class Handle
{
public:
    /// @brief Kind of object this handle names.
    using TagType = Tag;

    /// @brief Creates the null handle.
    constexpr Handle() noexcept = default;

    /// @brief The null handle.
    [[nodiscard]] static constexpr auto null() noexcept -> Handle
    {
        return Handle{};
    }

    /// @brief Encodes a slot index and generation. Reserved for backends.
    /// @param key        Proof that the caller is a HandleIssuer.
    /// @param index      Slot index, at most MAXIMUM_HANDLE_INDEX.
    /// @param generation Slot generation, in [1, MAXIMUM_HANDLE_GENERATION].
    /// @throws InvalidHandleError if @p index or @p generation is out of range.
    [[nodiscard]] static constexpr auto create(HandleKey key,
                                               std::uint32_t index,
                                               std::uint32_t generation) -> Handle;

    /// @brief Slot index. Reserved for backends.
    [[nodiscard]] constexpr auto index([[maybe_unused]] HandleKey key) const noexcept -> std::uint32_t
    {
        return m_value & MAXIMUM_HANDLE_INDEX;
    }

    /// @brief Slot generation (0 for the null handle). Reserved for backends.
    [[nodiscard]] constexpr auto generation([[maybe_unused]] HandleKey key) const noexcept -> std::uint32_t
    {
        return m_value >> HANDLE_INDEX_BITS;
    }

    /// @brief Whether the handle names an object (it may still be destroyed; the backend checks that).
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return m_value != 0;
    }

    /// @brief Same as is_valid().
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return is_valid();
    }

    /// @brief Handles of the same kind compare by value; the null handle orders first.
    [[nodiscard]] constexpr auto operator<=>(const Handle&) const noexcept = default;

private:
    friend struct std::hash<Handle>;
    friend struct std::formatter<Handle, char>;

private:
    std::uint32_t m_value = 0;
};

template<class Tag>
constexpr auto
Handle<Tag>::create([[maybe_unused]] HandleKey key,
                    std::uint32_t index,
                    std::uint32_t generation) -> Handle
{
    // The slot index must fit in its bits.
    if (index > MAXIMUM_HANDLE_INDEX)
    {
        throw InvalidHandleError({
            .backend = std::nullopt,
            .operation = "Handle::create",
            .handle_type = std::string{Tag::NAME},
            .problem = std::format("slot index {} exceeds MAXIMUM_HANDLE_INDEX ({}); a backend holds at most {} live "
                                   "objects of one kind",
                                   index, MAXIMUM_HANDLE_INDEX, MAXIMUM_HANDLE_INDEX + 1U),
        });
    }

    // The generation must be in [1, MAXIMUM_HANDLE_GENERATION]; generation 0 is reserved for the null handle.
    if (generation == 0 || generation > MAXIMUM_HANDLE_GENERATION)
    {
        throw InvalidHandleError({
            .backend = std::nullopt,
            .operation = "Handle::create",
            .handle_type = std::string{Tag::NAME},
            .problem = std::format("generation {} is outside [1, {}]; generation 0 is reserved for the null handle",
                                   generation, MAXIMUM_HANDLE_GENERATION),
        });
    }

    // Pack the generation and the index into the 32-bit value.
    Handle handle;
    handle.m_value = (generation << HANDLE_INDEX_BITS) | index;
    return handle;
}

/// @brief A handle of any library: a 4-byte, trivially copyable Handle whose tag provides NAME.
template<class Type>
concept TaggedHandle = requires {
                           typename Type::TagType;
                       }
                       && std::same_as<Type, Handle<typename Type::TagType>>
                       && std::is_trivially_copyable_v<Type>
                       && sizeof(Type) == sizeof(std::uint32_t)
                       && requires {
                              { Type::TagType::NAME } -> std::convertible_to<std::string_view>;
                          };

/// @brief 32-bit FNV-1a hash of @p name: the value of identifiers. Never 0 for a non-empty name (a
///        hash of 0 becomes 1), since 0 is the null identifier.
[[nodiscard]] constexpr auto
name_hash(std::string_view name) noexcept -> std::uint32_t
{
    // FNV-1a: xor each byte in, then multiply by the prime.
    std::uint32_t hash = NAME_HASH_OFFSET_BASIS;
    for (const char character : name)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= NAME_HASH_PRIME;
    }

    // Keep 0 for the null identifier.
    return name.empty() ? 0U : hash == 0U ? 1U : hash;
}

/// @brief Strongly typed name the application declares (an action, a binding context, ...): the hash
///        of the name, computed at compile time. Identifiers of different tags do not mix.
/// @tparam Tag Empty type identifying the kind of name; it provides `NAME` (used in messages).
template<class Tag>
class Identifier
{
public:
    /// @brief Kind of name this identifier stands for.
    using TagType = Tag;

    /// @brief The null identifier (names nothing).
    constexpr Identifier() noexcept = default;

    /// @brief The identifier of @p name, at compile time. An empty name does not compile.
    consteval explicit Identifier(std::string_view name)
        : m_value{name_hash(name)}
    {
        // A constant expression cannot throw: an empty name is a compilation error.
        if (name.empty())
        {
            throw "an identifier needs a non-empty name";
        }
    }

    /// @brief The identifier of a name known only at run time (data-driven schemas, saved bindings).
    [[nodiscard]] static constexpr auto from_name(std::string_view name) noexcept -> Identifier
    {
        Identifier identifier;
        identifier.m_value = name_hash(name);
        return identifier;
    }

    /// @brief The hash (stable across runs and builds: it depends on the name only).
    [[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t
    {
        return m_value;
    }

    /// @brief Whether the identifier names something.
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return m_value != 0;
    }

    /// @brief Identifiers of the same kind compare by value.
    [[nodiscard]] constexpr auto operator<=>(const Identifier&) const noexcept = default;

private:
    std::uint32_t m_value = 0;
};

/// @brief An identifier of any kind: a 4-byte Identifier whose tag provides NAME.
template<class Type>
concept TaggedIdentifier = requires {
                               typename Type::TagType;
                           }
                           && std::same_as<Type, Identifier<typename Type::TagType>>
                           && std::is_trivially_copyable_v<Type>
                           && sizeof(Type) == sizeof(std::uint32_t)
                           && requires {
                                  { Type::TagType::NAME } -> std::convertible_to<std::string_view>;
                              };

} // namespace gfx

/// @brief Hashes a handle, so handles can key unordered containers.
template<class Tag>
struct std::hash<gfx::Handle<Tag>>
{
    [[nodiscard]] auto operator()(const gfx::Handle<Tag>& handle) const noexcept -> std::size_t
    {
        return std::hash<std::uint32_t>{}(handle.m_value);
    }
};

/// @brief Formats a handle as "Buffer(index 5, generation 3)" or "Buffer(null)".
template<class Tag>
struct std::formatter<gfx::Handle<Tag>, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(const gfx::Handle<Tag>& handle, FormatContext& context) const -> FormatContext::iterator
    {
        // Format the handle as "Buffer(index 5, generation 3)" or "Buffer(null)".
        const std::string text = !handle.m_value
            ? std::format("{}(null)", Tag::NAME)
            : std::format("{}(index {}, generation {})", Tag::NAME, handle.m_value & gfx::MAXIMUM_HANDLE_INDEX,
                          handle.m_value >> gfx::HANDLE_INDEX_BITS);

        // Delegate to the string_view formatter to write the text into the context.
        return std::formatter<std::string_view, char>::format(text, context);
    }
};

/// @brief Hashes an identifier, so identifiers can key unordered containers.
template<class Tag>
struct std::hash<gfx::Identifier<Tag>>
{
    [[nodiscard]] auto operator()(const gfx::Identifier<Tag>& identifier) const noexcept -> std::size_t
    {
        return std::hash<std::uint32_t>{}(identifier.value());
    }
};

/// @brief Formats an identifier as "Action(0x1A2B3C4D)" or "Action(null)" (names are not kept at run time).
template<class Tag>
struct std::formatter<gfx::Identifier<Tag>, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(const gfx::Identifier<Tag>& identifier, FormatContext& context) const -> FormatContext::iterator
    {
        const std::string text = identifier.is_valid() ? std::format("{}(0x{:08X})", Tag::NAME, identifier.value())
                                                       : std::format("{}(null)", Tag::NAME);
        return std::formatter<std::string_view, char>::format(text, context);
    }
};
