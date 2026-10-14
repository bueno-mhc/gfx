// Must not compile: this "backend" buffer does not override mapped_memory, flush_mapped_range,
// invalidate_mapped_range and device_address, so it stays abstract and cannot be created.
import std;
import gfx;

class IncompleteBuffer final : public gfx::Buffer
{
public:
    auto backend() const noexcept -> gfx::BackendKind override { return gfx::BackendKind::NULL_BACKEND; }
    auto debug_name() const noexcept -> std::string_view override { return "incomplete"; }
    auto handle() const noexcept -> gfx::BufferHandle override { return {}; }
    auto size() const noexcept -> std::uint64_t override { return 0; }
    auto usage() const noexcept -> gfx::BufferUsage override { return gfx::BufferUsage::NONE; }
    auto location() const noexcept -> gfx::MemoryLocation override { return gfx::MemoryLocation::DEVICE_LOCAL; }
};

auto main() -> int
{
    IncompleteBuffer buffer;
    return static_cast<int>(buffer.size());
}
