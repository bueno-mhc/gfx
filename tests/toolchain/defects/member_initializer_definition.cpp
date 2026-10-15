// Implementation unit defining the constructor of the class the internal partition declares. GCC 16
// crashes (internal compiler error: Segmentation fault) at the end of the mem-initializer list, where
// it default-initializes the member of the imported class template.
module gfx.defects.member_initializer;

import :declaration;

namespace defects
{

Thing::Thing(int value)
    : m_value{value}
{
}

auto
Thing::value() const noexcept -> int
{
    return m_value;
}

auto
thing_value(int value) -> int
{
    return Thing{value}.value();
}

} // namespace defects
