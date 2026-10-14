// Must not compile: queue types are alternatives, not bits; only bitmask enumerations combine with |.
import gfx;

auto main() -> int {
    const auto queues = gfx::QueueType::GRAPHICS | gfx::QueueType::COMPUTE;
    return static_cast<int>(queues);
}
