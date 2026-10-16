/// @file input.cppm
/// @brief `gfx.platform.input`: action mapping above the platform's events. Games declare actions and
///        binding contexts (a compile-time InputSchema), activate contexts per player, call
///        InputSystem::update() once per frame and read actions from the InputFrame it returns.
///        Players rebind at run time; bindings are saved as JSON naming every input by name.
export module gfx.platform.input;

export import :binding;
export import :schema;
export import :frame;
export import :event_channel;
export import :input_system;
