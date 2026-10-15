// Another module providing a class template (as gfx.handles provides Handle<Tag>).
export module gfx.defects.member_initializer_base;

export namespace defects
{

// Any class template: neither a constructor nor a default member initializer is needed for the defect.
template<class Tag>
class Handle
{
    unsigned m_value = 0;
};

struct FenceTag
{
};

using FenceHandle = Handle<FenceTag>;

} // namespace defects
