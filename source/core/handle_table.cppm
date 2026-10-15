/// @file handle_table.cppm
/// @brief Module gfx.core.handle_table: the table that issues the typed handles of one kind of
///        object and resolves them to backend records (a SlotTable that speaks gfx handles and errors).
export module gfx.core.handle_table;

import std;
import gfx.core.enums;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.slot_table;

export namespace gfx::backends
{

/// @brief Issues the handles of one kind of object (Tag) and resolves them to backend records.
///        Thread-safe: resolving is lock-free, issuing and releasing take a mutex (see SlotTable).
template<class Tag, class Record>
class HandleTable : private HandleIssuer
{
public:
    /// @brief The handles this table issues.
    using HandleType = Handle<Tag>;

    /// @param backend Backend named in error messages.
    explicit HandleTable(BackendIdentity backend) noexcept
        : m_backend{backend}
    {}

    /// @brief Stores @p record and returns a new handle naming it.
    /// @throws InvalidHandleError when the device already holds MAXIMUM_HANDLE_INDEX + 1 objects of this kind.
    [[nodiscard]] auto insert(Record record) -> HandleType
    {
        const std::optional<SlotName> name = m_slots.insert(std::move(record));
        if (!name) [[unlikely]]
        {
            throw InvalidHandleError({
                .backend = m_backend,
                .operation = std::format("create {}", Tag::NAME),
                .handle_type = std::string{Tag::NAME},
                .problem = std::format("the device already holds {} live {} objects, the most a handle can name",
                                       MAXIMUM_HANDLE_INDEX + 1U, Tag::NAME),
            });
        }
        return HandleType::create(handle_key(), name->index, name->generation);
    }

    /// @brief The record of @p handle, or nullptr when the handle is null or its object was destroyed.
    [[nodiscard]] auto find(HandleType handle) const noexcept -> const Record*
    {
        return m_slots.find(name_of(handle));
    }

    /// @brief Same as find(), for records the caller mutates under its own synchronization.
    [[nodiscard]] auto find(HandleType handle) noexcept -> Record*
    {
        return m_slots.find(name_of(handle));
    }

    /// @brief The record of @p handle.
    /// @param operation Operation that received the handle, named in the error.
    /// @throws InvalidHandleError when the handle is null or its object was destroyed.
    [[nodiscard]] auto get(HandleType handle,
                           std::string_view operation,
                           std::source_location location = std::source_location::current()) const -> const Record&
    {
        const Record* record = m_slots.find(name_of(handle));
        if (record == nullptr) [[unlikely]]
        {
            throw_invalid(handle, operation, location);
        }
        return *record;
    }

    /// @brief Removes the record of @p handle and returns it (std::nullopt when the handle is not live).
    auto remove(HandleType handle) -> std::optional<Record>
    {
        return m_slots.remove(name_of(handle));
    }

    /// @brief Number of live objects.
    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return m_slots.size();
    }

    /// @brief Calls @p function(handle, record) for every live object (under the table's mutex).
    template<class Function>
    void for_each(Function&& function) const
    {
        m_slots.for_each([&function](SlotName name, const Record& record)
        {
            function(HandleType::create(handle_key(), name.index, name.generation), record);
        });
    }

private:
    [[nodiscard]] static auto name_of(HandleType handle) noexcept -> SlotName
    {
        return {.index = handle.index(handle_key()), .generation = handle.generation(handle_key())};
    }

    [[noreturn]] void throw_invalid(HandleType handle, std::string_view operation, std::source_location location) const
    {
        throw InvalidHandleError(
            {
                .backend = m_backend,
                .operation = std::string{operation},
                .handle_type = std::string{Tag::NAME},
                .problem = std::format("{} does not name a live object: {}", handle, m_slots.describe_missing(name_of(handle))),
            },
            location);
    }

private:
    BackendIdentity m_backend;
    SlotTable<Record, HANDLE_INDEX_BITS, HANDLE_GENERATION_BITS> m_slots;
};

} // namespace gfx::backends
