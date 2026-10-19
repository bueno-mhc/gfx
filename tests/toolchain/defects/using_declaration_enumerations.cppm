// An enumeration, and a function taking it, of a module another module imports (see
// using_declaration_function.cppm).
export module gfx.defects.using_declaration.enumerations;

export namespace defects::platform
{

/// Any enumeration.
enum class Button : unsigned char
{
    SOUTH,
    EAST,
};

/// Any value holding it.
struct Binding
{
    Button button = Button::SOUTH;
};

/// Any function taking it.
constexpr auto
bind(Button button) -> Binding
{
    return Binding{.button = button};
}

} // namespace defects::platform
