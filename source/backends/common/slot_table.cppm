/// @file slot_table.cppm
/// @brief Module gfx.backends.slot_table: a thread-safe table of records named by (index, generation)
///        pairs - the storage behind the handles a backend issues. It depends on the standard library
///        only, so it is a library of its own (gfx_slot_table), unit-tested in isolation; the backends
///        wrap it in a HandleTable that speaks gfx handles and errors.
export module gfx.backends.slot_table;

import std;

export namespace gfx::backends
{

/// @brief Name of a slot: its index and the generation of the record it holds (never 0 when issued).
struct SlotName
{
    std::uint32_t index = 0;      ///< Slot index.
    std::uint32_t generation = 0; ///< Generation of the record; 0 names nothing.

    bool operator==(const SlotName&) const = default;
};

/// @brief Thread-safe table of records named by SlotName.
///
/// - **Lookups are lock-free**: one acquire load of the slot's generation, compared with the name's.
/// - **Insertions and removals** take a mutex.
/// - Slots live in chunks of SLOTS_PER_CHUNK allocated on demand and never moved or freed before the
///   table, so a lookup never races a reallocation.
/// - A removed slot's generation is retired: names of removed records stop resolving. Slots are reused
///   only once more than the reuse threshold of them are free, oldest first, which delays the reuse of a
///   generation (generations wrap around after MAXIMUM_GENERATION uses of a slot).
///
/// A live record may be looked up from any thread while other records are inserted or removed. Looking a
/// record up while removing *that* record is a race of the caller (an object destroyed while another
/// thread still uses it); the generation check catches such uses only once the removal is visible.
///
/// @tparam Record         Data stored per slot (small, cheap to move: native handles and a few fields).
/// @tparam IndexBits      Bits of a slot index (the table holds at most 2^IndexBits records).
/// @tparam GenerationBits Bits of a generation.
template<class Record, std::uint32_t IndexBits, std::uint32_t GenerationBits>
    requires std::is_nothrow_default_constructible_v<Record> && std::is_nothrow_move_constructible_v<Record> &&
             std::is_nothrow_move_assignable_v<Record> && (IndexBits >= 1 && IndexBits <= 24) &&
             (GenerationBits >= 1 && GenerationBits <= 8)
class SlotTable
{
public:
    /// @brief Largest slot index.
    static constexpr std::uint32_t MAXIMUM_INDEX = (1U << IndexBits) - 1U;

    /// @brief Largest generation (generations run from 1 to MAXIMUM_GENERATION, then wrap around to 1).
    static constexpr std::uint32_t MAXIMUM_GENERATION = (1U << GenerationBits) - 1U;

    /// @brief Slots allocated at once.
    static constexpr std::uint32_t SLOTS_PER_CHUNK = std::min(4096U, MAXIMUM_INDEX + 1U);

    /// @brief Number of chunks that cover every index.
    static constexpr std::uint32_t CHUNK_COUNT = (MAXIMUM_INDEX + 1U) / SLOTS_PER_CHUNK;

    /// @param reuse_threshold Free slots kept before the oldest one is reused (0 reuses immediately).
    explicit SlotTable(std::size_t reuse_threshold = 1024)
        : m_reuse_threshold{reuse_threshold}
    {}

    ~SlotTable()
    {
        // Chunks are owned by the table and freed with it.
        for (std::atomic<Chunk*>& chunk : m_chunks)
        {
            delete chunk.load(std::memory_order_relaxed);
        }
    }

    SlotTable(const SlotTable&) = delete;
    SlotTable(SlotTable&&) = delete;
    auto operator=(const SlotTable&) -> SlotTable& = delete;
    auto operator=(SlotTable&&) -> SlotTable& = delete;

    /// @brief Stores @p record in a slot and names it. Thread-safe.
    /// @return The name, or std::nullopt when every slot holds a record.
    [[nodiscard]] auto insert(Record record) -> std::optional<SlotName>
    {
        const std::scoped_lock lock{m_mutex};

        // Reuse the oldest free slot once enough are free (or when no slot was ever left unused).
        std::uint32_t index = 0;
        if (!m_free.empty() && (m_free.size() > m_reuse_threshold || m_next_unused > MAXIMUM_INDEX))
        {
            index = m_free.front();
            m_free.pop_front();
        }
        else if (m_next_unused <= MAXIMUM_INDEX)
        {
            index = m_next_unused++;
            allocate_chunk_of(index);
        }
        else
        {
            return std::nullopt;
        }

        // Write the record, then publish its generation: a lookup that sees the generation sees the record.
        Slot& slot = slot_at(index);
        slot.last_generation = slot.last_generation == MAXIMUM_GENERATION ? 1U : slot.last_generation + 1U;
        slot.record = std::move(record);
        slot.live_generation.store(slot.last_generation, std::memory_order_release);
        m_size.fetch_add(1, std::memory_order_relaxed);
        return SlotName{.index = index, .generation = slot.last_generation};
    }

    /// @brief The record named @p name, or nullptr when the name is not live (null, removed, or never
    ///        issued). Lock-free; the pointer stays valid until the record is removed.
    [[nodiscard]] auto find(SlotName name) const noexcept -> const Record*
    {
        const Slot* slot = live_slot(name);
        return slot == nullptr ? nullptr : &slot->record;
    }

    /// @brief Same as find(), for records the caller mutates under its own synchronization.
    [[nodiscard]] auto find(SlotName name) noexcept -> Record*
    {
        const Slot* slot = live_slot(name);
        return slot == nullptr ? nullptr : &const_cast<Slot*>(slot)->record;
    }

    /// @brief Removes the record named @p name and returns it. Thread-safe.
    /// @return The record, or std::nullopt when the name is not live.
    auto remove(SlotName name) -> std::optional<Record>
    {
        const std::scoped_lock lock{m_mutex};
        if (live_slot(name) == nullptr)
        {
            return std::nullopt;
        }

        // Retire the generation first: lookups stop resolving the name, then the record leaves.
        Slot& slot = slot_at(name.index);
        slot.live_generation.store(0, std::memory_order_release);
        std::optional<Record> record{std::move(slot.record)};
        slot.record = Record{};
        m_free.push_back(name.index);
        m_size.fetch_sub(1, std::memory_order_relaxed);
        return record;
    }

    /// @brief Number of live records.
    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return m_size.load(std::memory_order_relaxed);
    }

    /// @brief Why @p name does not resolve, for error messages ("" when it does).
    [[nodiscard]] auto describe_missing(SlotName name) const -> std::string
    {
        if (name.generation == 0)
        {
            return "it is the null handle";
        }
        if (name.index > MAXIMUM_INDEX || name.generation > MAXIMUM_GENERATION)
        {
            return std::format("its slot {} or generation {} is out of range", name.index, name.generation);
        }
        const Chunk* chunk = m_chunks[name.index / SLOTS_PER_CHUNK].load(std::memory_order_acquire);
        if (chunk == nullptr)
        {
            return std::format("slot {} was never issued (it does not belong to this device)", name.index);
        }
        const std::uint32_t live = chunk->slots[name.index % SLOTS_PER_CHUNK].live_generation.load(std::memory_order_acquire);
        if (live == name.generation)
        {
            return {};
        }
        if (live == 0)
        {
            return std::format("its object was destroyed (slot {} generation {} is free)", name.index, name.generation);
        }
        return std::format("its object was destroyed (slot {} now holds generation {}, the handle has generation {})",
                           name.index, live, name.generation);
    }

    /// @brief Calls @p function(name, record) for every live record, under the table's mutex (for
    ///        leak reports and teardown; do not insert or remove from @p function).
    template<class Function>
    void for_each(Function&& function) const
    {
        const std::scoped_lock lock{m_mutex};
        for (std::uint32_t index = 0; index < m_next_unused; ++index)
        {
            const Slot& slot = slot_at(index);
            const std::uint32_t generation = slot.live_generation.load(std::memory_order_relaxed);
            if (generation != 0)
            {
                function(SlotName{.index = index, .generation = generation}, slot.record);
            }
        }
    }

private:
    struct Slot
    {
        std::atomic<std::uint32_t> live_generation{0}; ///< Generation of the live record, 0 when free.
        std::uint32_t last_generation = 0;             ///< Last generation issued (guarded by the mutex).
        Record record{};
    };

    struct Chunk
    {
        std::array<Slot, SLOTS_PER_CHUNK> slots{};
    };

    // The slot holding a live record named @p name, or nullptr.
    [[nodiscard]] auto live_slot(SlotName name) const noexcept -> const Slot*
    {
        if (name.generation == 0 || name.index > MAXIMUM_INDEX) [[unlikely]]
        {
            return nullptr;
        }
        const Chunk* chunk = m_chunks[name.index / SLOTS_PER_CHUNK].load(std::memory_order_acquire);
        if (chunk == nullptr) [[unlikely]]
        {
            return nullptr;
        }
        const Slot& slot = chunk->slots[name.index % SLOTS_PER_CHUNK];
        return slot.live_generation.load(std::memory_order_acquire) == name.generation ? &slot : nullptr;
    }

    // Allocates the chunk of slot @p index when it does not exist yet (called under the mutex).
    void allocate_chunk_of(std::uint32_t index)
    {
        std::atomic<Chunk*>& chunk = m_chunks[index / SLOTS_PER_CHUNK];
        if (chunk.load(std::memory_order_relaxed) == nullptr)
        {
            chunk.store(new Chunk{}, std::memory_order_release);
        }
    }

    // Slot @p index of an allocated chunk (called under the mutex).
    [[nodiscard]] auto slot_at(std::uint32_t index) const noexcept -> Slot&
    {
        return m_chunks[index / SLOTS_PER_CHUNK].load(std::memory_order_relaxed)->slots[index % SLOTS_PER_CHUNK];
    }

private:
    std::array<std::atomic<Chunk*>, CHUNK_COUNT> m_chunks{};
    mutable std::mutex m_mutex;
    std::deque<std::uint32_t> m_free;
    std::uint32_t m_next_unused = 0;
    std::size_t m_reuse_threshold;
    std::atomic<std::size_t> m_size{0};
};

} // namespace gfx::backends
