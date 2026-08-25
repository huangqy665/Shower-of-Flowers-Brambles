#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class Hoi3LifecycleProbeStatus
{
    UnsupportedExecutable,
    Unavailable,
    Frontend,
    Gameplay
};

struct Hoi3LifecycleProbeResult
{
    Hoi3LifecycleProbeStatus status =
        Hoi3LifecycleProbeStatus::Unavailable;
    std::string playerTag;
    std::uintptr_t gameStateAddress = 0;
    uint64_t worldFingerprint = 0;
    bool hasTotalDays = false;
    int32_t totalDays = 0;
};

bool DecodeHoi3PlayerTag(
    const uint8_t* bytes,
    std::size_t size,
    std::string& playerTag
);

Hoi3LifecycleProbeResult ProbeHoi3Lifecycle();
