// A consteval function of a module interface with a block-scope using-declaration of another module's
// enumeration. GCC 16 crashes while it writes this module's file ("internal compiler error: in
// import_entity_module, at cp/module.cc:4361"). Without the using-declaration (the enumeration named in full), or
// with the function inline rather than consteval, or with a using-declaration of a function, it compiles.
export module gfx.defects.using_declaration;

import gfx.defects.using_declaration.enumerations;

export namespace defects
{

/// Two bindings.
struct Bindings
{
    platform::Binding first{};
    platform::Binding second{};
};

/// Made at compile time, the enumeration named by a using-declaration.
consteval auto
bindings() -> Bindings
{
    using platform::Button;
    return Bindings{.first = platform::bind(Button::SOUTH), .second = platform::bind(Button::EAST)};
}

} // namespace defects
