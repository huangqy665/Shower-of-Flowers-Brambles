#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "algorithm_runtime.hpp"
#include "runtime_persistence.hpp"

namespace dillen::persistence {

struct ReplayCommandEntry
{
    std::uint64_t submitTick = 0;
    std::uint64_t notBeforeTick = 0;
    std::int32_t priority = 0;
    kernel::WorldTransaction transaction;
};

struct ReplayCommandLog
{
    std::uint64_t finalTick = 0;
    std::vector<ReplayCommandEntry> entries;
};

enum class DeterministicReplayStatus
{
    Completed,
    InitialStateRejected,
    InvalidCommandLog,
    TickFailed,
    CaptureFailed,
    FactEncodingFailed
};

struct DeterministicReplayResult
{
    DeterministicReplayStatus status = DeterministicReplayStatus::Completed;
    std::string message;
    std::vector<std::uint8_t> finalSave;
    std::vector<std::uint8_t> factStream;
    std::uint64_t finalStateChecksum = 0;
    std::uint64_t factStreamChecksum = 0;

    explicit operator bool() const noexcept;
};

class DeterministicReplayService
{
public:
    DeterministicReplayResult Replay(
        const RuntimeSaveImage& initial,
        const ReplayCommandLog& commandLog,
        const kernel::FrozenRuntimeCatalog& catalog,
        const runtime::AlgorithmExecutorRegistry& executors,
        const RuntimeMigrationRegistry* migrations = nullptr
    ) const;
};

}
