#pragma once

#include <cstdint>
#include <vector>

#include "algorithm_runtime.hpp"
#include "authoritative_world.hpp"
#include "frozen_runtime_catalog.hpp"
#include "mechanism_scheduler.hpp"
#include "world_query_snapshot.hpp"
#include "world_command_queue.hpp"
#include "world_event.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::runtime {

class KernelRuntime
{
public:
    KernelRuntime(
        world::AuthoritativeWorld& world,
        const kernel::FrozenRuntimeCatalog& catalog
    );
    KernelRuntime(
        world::AuthoritativeWorld& world,
        const kernel::FrozenRuntimeCatalog& catalog,
        const AlgorithmExecutorRegistry& executors
    );

    const kernel::WorldCommandQueue& Commands() const noexcept;
    const kernel::WorldEventQueue& Events() const noexcept;
    const WorldQuerySnapshot& Query() const noexcept;
    WorldQuerySnapshotHandle AcquireQuerySnapshot() const noexcept;
    const kernel::MechanismQuerySnapshot& Snapshot() const noexcept;
    const kernel::DeterministicRngSnapshot& RngSnapshot() const noexcept;
    const AlgorithmStageReport& LastCreateAlgorithms() const noexcept;
    const AlgorithmStageReport& LastTickAlgorithms() const noexcept;
    const AlgorithmStageReport& LastEventAlgorithms() const noexcept;
    const AlgorithmStageReport& LastCommandAlgorithms() const noexcept;
    const AlgorithmStageReport& LastDestroyAlgorithms() const noexcept;

    std::uint64_t Enqueue(
        kernel::WorldTransaction transaction,
        std::uint64_t notBeforeTick,
        std::int32_t priority = 0
    );
    kernel::WorldTransactionResult ApplyImmediate(
        const kernel::WorldTransaction& transaction,
        std::uint64_t currentTick
    );
    kernel::MechanismTransactionResult ApplyMechanismImmediate(
        const std::vector<kernel::MechanismCommand>& commands,
        std::uint64_t currentTick
    );
    MechanismSchedulerTickResult RunTick(
        std::uint64_t nextTick
    );
    std::vector<kernel::WorldEvent> DrainEvents();

private:
    friend class persistence::RuntimePersistenceService;

    enum class AlgorithmCommitMode
    {
        Standard,
        CompleteCreate,
        DestroyInstance
    };

    kernel::WorldTransactionResult ApplyImmediateCore(
        const kernel::WorldTransaction& transaction,
        std::uint64_t currentTick
    );
    void ApplyAlgorithmReport(
        AlgorithmStageReport& report,
        std::uint64_t currentTick,
        AlgorithmCommitMode mode
    );
    void ApplyAlgorithmFault(
        AlgorithmInvocationResult& invocation,
        std::uint64_t currentTick
    );
    void CaptureAlgorithmEvents();
    void PublishSnapshots();

    world::AuthoritativeWorld& world_;
    const kernel::FrozenRuntimeCatalog& catalog_;
    AlgorithmRuntime algorithmRuntime_;
    MechanismScheduler scheduler_;
    kernel::WorldCommandQueue commands_;
    kernel::WorldEventQueue events_;
    WorldQueryService queryService_;
    WorldQuerySnapshotHandle querySnapshot_;
    kernel::DeterministicRngSnapshot rngSnapshot_;
    AlgorithmStageReport lastCreateAlgorithms_;
    AlgorithmStageReport lastTickAlgorithms_;
    AlgorithmStageReport lastEventAlgorithms_;
    AlgorithmStageReport lastCommandAlgorithms_;
    AlgorithmStageReport lastDestroyAlgorithms_;
    std::vector<kernel::WorldEvent> pendingAlgorithmEvents_;
    std::uint64_t lastAlgorithmEventSequence_ = 0;
};

}
