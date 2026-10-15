// Primary interface unit of the module whose implementation unit GCC 16 cannot compile (see
// member_initializer_definition.cpp).
export module gfx.defects.member_initializer;

export namespace defects
{

/// @brief The value a Thing was created with.
auto
thing_value(int value) -> int;

} // namespace defects
