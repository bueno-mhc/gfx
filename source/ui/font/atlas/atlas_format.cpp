/// @file atlas_format.cpp
/// @brief describe_atlas_error(): the explanations of AtlasProblem.
module gfx.ui.atlas_format;

import std;

namespace gfx::ui
{

auto
describe_atlas_error(const AtlasReadError& error) -> std::string
{
    // Where: the section, when one is concerned, and the byte.
    const std::string where = error.section.has_value() ? std::format("section {} (byte {})", to_string(*error.section), error.offset)
                                                        : std::format("byte {}", error.offset);
    switch (error.problem)
    {
        case AtlasProblem::TRUNCATED:
            return std::format("the blob is truncated: it has {} bytes, {} are needed", error.actual, error.expected);
        case AtlasProblem::BAD_MAGIC:
            return std::format("the blob is not a .uiatlas atlas: byte {} differs from the magic \"GFXUIATL\"", error.offset);
        case AtlasProblem::UNSUPPORTED_VERSION:
            return std::format("the atlas has format version {}, this reader reads version {} (rebuild the atlas with this version's tool)",
                               error.actual, error.expected);
        case AtlasProblem::WRONG_ENDIANNESS:
            return std::format("the atlas's endianness tag reads 0x{:08X} instead of 0x{:08X}: it was written big-endian", error.actual,
                               error.expected);
        case AtlasProblem::SIZE_MISMATCH:
            return std::format("the header gives a size of {} bytes, the blob has {} (sizes are multiples of 8)", error.actual, error.expected);
        case AtlasProblem::CHECKSUM_MISMATCH:
            return std::format("the checksum of the atlas is 0x{:016X} but its bytes hash to 0x{:016X}: the blob is corrupted", error.actual,
                               error.expected);
        case AtlasProblem::SECTION_OUT_OF_BOUNDS:
            return std::format("{} ends at byte {}, past the end of the blob ({} bytes)", where, error.actual, error.expected);
        case AtlasProblem::MISALIGNED_SECTION:
            return std::format("{} starts at byte {}, not a multiple of 8", where, error.actual);
        case AtlasProblem::SECTION_CHECKSUM_MISMATCH:
            return std::format("the checksum of {} is 0x{:016X} but its bytes hash to 0x{:016X}: the blob is corrupted", where, error.actual,
                               error.expected);
        case AtlasProblem::UNKNOWN_COMPRESSION:
            return std::format("{} uses compression {}, which this reader does not know", where, error.actual);
        case AtlasProblem::MISSING_SECTION:
            return std::format("the atlas has no {} section", error.section.has_value() ? to_string(*error.section) : std::string_view{"required"});
        case AtlasProblem::DUPLICATE_SECTION:
            return std::format("{} appears more than once", where);
        case AtlasProblem::MALFORMED_SECTION:
            return std::format("{} is malformed (expected {}, found {}): sizes, ordering or references are inconsistent", where, error.expected,
                               error.actual);
    }
    return "the atlas is invalid";
}

} // namespace gfx::ui
