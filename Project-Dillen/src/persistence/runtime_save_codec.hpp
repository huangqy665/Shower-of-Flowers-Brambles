#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime_save_image.hpp"
#include "world_event.hpp"

namespace dillen::persistence {

enum class RuntimeSaveCodecStatus
{
    Completed,
    InvalidImage,
    InvalidMagic,
    Truncated,
    LimitExceeded,
    InvalidValue,
    ChecksumMismatch,
    TrailingBytes
};

struct RuntimeSaveCodecReport
{
    RuntimeSaveCodecStatus status = RuntimeSaveCodecStatus::Completed;
    std::string message;

    explicit operator bool() const noexcept;
};

class RuntimeSaveCodec
{
public:
    RuntimeSaveCodecReport Encode(
        const RuntimeSaveImage& image,
        std::vector<std::uint8_t>& output
    ) const;
    RuntimeSaveCodecReport Decode(
        const std::vector<std::uint8_t>& bytes,
        RuntimeSaveImage& output
    ) const;
    RuntimeSaveCodecReport EncodeFactStream(
        const std::vector<kernel::WorldEvent>& events,
        std::vector<std::uint8_t>& output
    ) const;
};

std::uint64_t StableRuntimeChecksum(
    const std::vector<std::uint8_t>& bytes
) noexcept;

}
