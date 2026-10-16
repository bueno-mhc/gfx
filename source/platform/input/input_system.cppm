/// @file input_system.cppm
/// @brief InputSystem: turns events into actions, once per game frame. It owns the bindings of every
///        player (defaults from the schema, rebound at run time and saved as JSON), the active binding
///        contexts of every player (pushed and popped as RAII ContextActivation objects), and routes
///        devices to players (keyboard and mouse to one player, one gamepad per player).
///
/// Threading (the model of commercial engines): the platform thread pumps events; the game thread
/// calls update() once per frame (directly, or through an EventChannel when the game runs on its own
/// thread) and every other member function; the InputFrame update() returns is immutable, so the jobs
/// of the frame read it from any thread. A frame stays valid and unchanged until the second update()
/// after it (frames are double-buffered), so jobs of frame N may still run while frame N + 1 updates.
///
/// @code
/// InputSystem input{SCHEMA, {.players = 2}};
/// ContextActivation gameplay = input.activate_context("gameplay"_context);
/// while (running)
/// {
///     const InputFrame& frame = input.update(platform.poll_events());
///     if (frame.was_action_just_pressed("jump"_action))
///     {
///         ...
///     }
/// }
/// @endcode
export module gfx.platform.input:input_system;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :binding;
import :schema;
import :frame;

namespace gfx::platform
{

/// A player's bindings of one action in one context, by name, as saved bindings hold them.
struct StoredBindingList
{
    std::uint32_t player = 0;
    std::string context{};
    std::string action{};
    std::vector<Binding> bindings{};
};

/// The saved-bindings document holding @p lists (binding_storage.cpp).
[[nodiscard]] auto write_binding_storage(std::span<const StoredBindingList> lists) -> std::string;

/// The lists of the saved-bindings document @p json; what is wrong with it goes to @p problems.
[[nodiscard]] auto read_binding_storage(std::string_view json,
                                        std::vector<std::string>& problems) -> std::vector<StoredBindingList>;

} // namespace gfx::platform

export namespace gfx::platform
{

class InputSystem;

/// @brief How an InputSystem routes devices and evaluates actions.
struct InputSystemDescription
{
    std::uint32_t players = 1;                                           ///< Local players, 1 to MAXIMUM_PLAYERS.
    GamepadAssignment gamepad_assignment = GamepadAssignment::AUTOMATIC; ///< How gamepads find their player.
    float actuation_threshold = DEFAULT_ACTUATION_THRESHOLD;             ///< Strength at which an analog source presses a BUTTON action, in (0, 1].
};

/// @brief One binding of a player: the context, the action, and its position among the action's
///        bindings in that context.
struct BindingSlot
{
    BindingContextIdentifier context{}; ///< Context.
    ActionIdentifier action{};          ///< Action.
    std::size_t slot = 0;               ///< Position among the action's bindings in the context.

    constexpr bool operator==(const BindingSlot&) const = default;
};

/// @brief A binding context active for a player while this object lives (a layer pushed on the
///        player's context stack; destroying it pops the layer). Move-only; harmless when it outlives
///        the InputSystem. Use it on the thread that calls InputSystem::update().
class ContextActivation
{
public:
    /// @brief No activation.
    ContextActivation() noexcept = default;

    /// @brief Deactivates the context.
    ~ContextActivation();

    ContextActivation(ContextActivation&& other) noexcept;
    auto operator=(ContextActivation&& other) noexcept -> ContextActivation&;
    ContextActivation(const ContextActivation&) = delete;
    auto operator=(const ContextActivation&) -> ContextActivation& = delete;

    /// @brief Whether the context is still active (activated, not reset, InputSystem alive).
    [[nodiscard]] auto is_active() const noexcept -> bool;

    /// @brief Deactivates the context now.
    void reset() noexcept;

    /// @brief The context.
    [[nodiscard]] auto context() const noexcept -> BindingContextIdentifier
    {
        return m_context;
    }

    /// @brief The player it is active for.
    [[nodiscard]] auto player() const noexcept -> PlayerIdentifier
    {
        return m_player;
    }

private:
    friend class InputSystem;

    ContextActivation(std::weak_ptr<InputSystem* const> system,
                      std::uint64_t identifier,
                      BindingContextIdentifier context,
                      PlayerIdentifier player) noexcept;

private:
    std::weak_ptr<InputSystem* const> m_system{};
    std::uint64_t m_identifier = 0;
    BindingContextIdentifier m_context{};
    PlayerIdentifier m_player{};
};

/// @brief Turns events into actions for every player (see the file description for threading).
class InputSystem
{
public:
    /// @brief An input system for @p schema (copied: the schema need not outlive it).
    /// @throws InvalidUsageError when the schema or the description is invalid (validate_input_schema()).
    explicit InputSystem(InputSchemaView schema,
                         const InputSystemDescription& description = {},
                         std::source_location location = std::source_location::current());

    ~InputSystem();

    InputSystem(const InputSystem&) = delete;
    auto operator=(const InputSystem&) -> InputSystem& = delete;

    // ----------------------------------------------------------------------------------------- frames

    /// @brief Applies @p events (one or more pumps, in order) and evaluates every action at the
    ///        current steady-clock time.
    /// @return The new frame, unchanged until the second update() after this one.
    auto update(const EventSpan& events) -> const InputFrame&;

    /// @brief Same as update(events), at time @p now on the steady clock (tests, replays).
    auto update(const EventSpan& events,
                std::chrono::nanoseconds now) -> const InputFrame&;

    /// @brief The latest frame (an empty frame before the first update()).
    [[nodiscard]] auto frame() const noexcept -> const InputFrame&;

    // ----------------------------------------------------------------------------------------- contexts

    /// @brief Activates @p context for @p player until the returned object is destroyed.
    /// @throws InvalidUsageError for an unknown context or player.
    [[nodiscard]] auto activate_context(BindingContextIdentifier context,
                                        PlayerIdentifier player = {},
                                        std::source_location location = std::source_location::current()) -> ContextActivation;

    /// @brief Whether @p context is active for @p player.
    [[nodiscard]] auto is_context_active(BindingContextIdentifier context,
                                         PlayerIdentifier player = {}) const noexcept -> bool;

    /// @brief The contexts active for @p player, in evaluation order (highest priority first).
    [[nodiscard]] auto active_contexts(PlayerIdentifier player = {},
                                       std::source_location location = std::source_location::current()) const
        -> std::vector<BindingContextIdentifier>;

    /// @brief Adds a context the schema did not declare (mods, tools); its bindings use declared actions.
    /// @throws InvalidUsageError when the context's name is taken or a binding is invalid.
    void add_context(const ContextDefinition& context,
                     std::span<const ContextBinding> bindings,
                     std::source_location location = std::source_location::current());

    // ----------------------------------------------------------------------------------------- players and devices

    /// @brief Number of players.
    [[nodiscard]] auto player_count() const noexcept -> std::uint32_t;

    /// @brief Routes the keyboard and the mouse to @p player (player 0 by default).
    void assign_keyboard_and_mouse(PlayerIdentifier player,
                                   std::source_location location = std::source_location::current());

    /// @brief Player the keyboard and the mouse are routed to.
    [[nodiscard]] auto keyboard_and_mouse_player() const noexcept -> PlayerIdentifier;

    /// @brief Routes @p gamepad to @p player (taking it from another player if needed); the player's
    ///        previous gamepad, if any, is unassigned. Works for gamepads not connected yet.
    void assign_gamepad(GamepadHandle gamepad,
                        PlayerIdentifier player,
                        std::source_location location = std::source_location::current());

    /// @brief Stops routing @p gamepad to any player.
    void unassign_gamepad(GamepadHandle gamepad) noexcept;

    /// @brief The gamepad routed to @p player (null when none).
    [[nodiscard]] auto gamepad_of(PlayerIdentifier player,
                                  std::source_location location = std::source_location::current()) const -> GamepadHandle;

    /// @brief While active, keyboard bindings of contexts without reads_keyboard_during_text_input read
    ///        nothing (typing in a chat box does not move the character). Call it with the platform's
    ///        Window::start_text_input() / stop_text_input().
    void set_text_input_active(bool active) noexcept;

    /// @brief Whether text input is active.
    [[nodiscard]] auto is_text_input_active() const noexcept -> bool;

    // ----------------------------------------------------------------------------------------- rebinding

    /// @brief The bindings of @p action in @p context for @p player (slot order).
    [[nodiscard]] auto bindings(BindingContextIdentifier context,
                                ActionIdentifier action,
                                PlayerIdentifier player = {},
                                std::source_location location = std::source_location::current()) const -> std::vector<Binding>;

    /// @brief Replaces the binding in @p slot for @p player.
    /// @throws InvalidUsageError for an unknown context, action, slot or player, or a binding that does
    ///         not suit the action (check_binding()).
    void rebind(const BindingSlot& slot,
                const Binding& binding,
                PlayerIdentifier player = {},
                std::source_location location = std::source_location::current());

    /// @brief Adds a binding of @p action in @p context for @p player.
    /// @return Its slot.
    auto add_binding(BindingContextIdentifier context,
                     ActionIdentifier action,
                     const Binding& binding,
                     PlayerIdentifier player = {},
                     std::source_location location = std::source_location::current()) -> BindingSlot;

    /// @brief Removes the binding in @p slot for @p player (later slots move down by one).
    void remove_binding(const BindingSlot& slot,
                        PlayerIdentifier player = {},
                        std::source_location location = std::source_location::current());

    /// @brief Restores every default binding of @p player.
    void reset_bindings(PlayerIdentifier player = {},
                        std::source_location location = std::source_location::current());

    /// @brief Restores the default bindings of @p action in @p context for @p player.
    void reset_bindings(BindingContextIdentifier context,
                        ActionIdentifier action,
                        PlayerIdentifier player = {},
                        std::source_location location = std::source_location::current());

    /// @brief The bindings of @p player that read @p source (conflicts for a rebinding menu).
    [[nodiscard]] auto find_bindings_using(InputSource source,
                                           PlayerIdentifier player = {},
                                           std::source_location location = std::source_location::current()) const
        -> std::vector<BindingSlot>;

    /// @brief Captures the next input of @p player from @p sources as a binding, reported once in
    ///        InputFrame::captured_binding() (canceled when @p cancel_key is pressed). The captured
    ///        press does not reach the actions.
    void capture_next_binding(PlayerIdentifier player = {},
                              BindingCaptureSource sources = BindingCaptureSource::KEYBOARD | BindingCaptureSource::MOUSE_BUTTON
                                                             | BindingCaptureSource::GAMEPAD_BUTTON | BindingCaptureSource::GAMEPAD_AXIS,
                              Key cancel_key = Key::ESCAPE,
                              std::source_location location = std::source_location::current());

    /// @brief Stops a capture without reporting it.
    void cancel_binding_capture() noexcept;

    /// @brief Whether a capture is in progress.
    [[nodiscard]] auto is_capturing_binding() const noexcept -> bool;

    // ----------------------------------------------------------------------------------------- storage

    /// @brief The bindings that differ from the defaults, for every player, as JSON naming contexts,
    ///        actions and inputs by name (`{"format": "gfx.input.bindings", "version": 1, ...}`).
    [[nodiscard]] auto save_bindings() const -> std::string;

    /// @brief Restores bindings saved by save_bindings(): all or nothing.
    /// @throws InvalidUsageError listing every problem (malformed JSON, unknown names, invalid
    ///         bindings); no binding changes then.
    void load_bindings(std::string_view json,
                       std::source_location location = std::source_location::current());

    // ----------------------------------------------------------------------------------------- names

    /// @brief Name of @p action (empty when it is not declared).
    [[nodiscard]] auto action_name(ActionIdentifier action) const noexcept -> std::string_view;

    /// @brief Name of @p context (empty when it is not declared).
    [[nodiscard]] auto context_name(BindingContextIdentifier context) const noexcept -> std::string_view;

private:
    friend class ContextActivation;

    void deactivate(std::uint64_t identifier) noexcept;

private:
    std::unique_ptr<InputSystemState> m_state{};
    std::shared_ptr<InputSystem* const> m_lifetime{};
};

} // namespace gfx::platform
