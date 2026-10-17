// The reference XXH64: zstd's copy of xxHash, inlined in this plain translation unit.
#include "xxhash_reference.hpp"

#define XXH_INLINE_ALL
#include <common/xxhash.h>

namespace gfx::tests
{

unsigned long long
reference_xxhash64(const unsigned char* data,
                   unsigned long long size,
                   unsigned long long seed)
{
    return XXH64(data, static_cast<size_t>(size), seed);
}

} // namespace gfx::tests
