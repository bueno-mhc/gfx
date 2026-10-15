// Must not compile: a texture index is not a storage image index, although both are 32-bit values.
import gfx;

auto main() -> int
{
    const gfx::SampledImageIndex texture{3};
    const gfx::StorageImageIndex image = texture;
    return static_cast<int>(image.value());
}
