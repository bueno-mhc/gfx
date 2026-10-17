/// @file xxhash_reference.hpp
/// @brief The reference XXH64 (the xxHash implementation zstd ships), to check gfx::ui::xxhash64 against.
///        No standard header: test files that import std include it.
#pragma once

namespace gfx::tests
{

/// XXH64 of @p size bytes at @p data with @p seed.
unsigned long long reference_xxhash64(const unsigned char* data,
                                      unsigned long long size,
                                      unsigned long long seed);

} // namespace gfx::tests
