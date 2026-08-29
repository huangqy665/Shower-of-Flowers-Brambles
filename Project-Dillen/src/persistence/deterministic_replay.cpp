#include "deterministic_replay.hpp"

#include <iterator>
#include <utility>

namespace dillen::persistence {

DeterministicReplayResult::operator bool() const noexcept
{
    return status == DeterministicReplayStatus::Completed;
}

DeterministicReplayResult DeterministicReplayService::Replay(
    const RuntimeSaveImage& initial,
    const ReplayCommandLog& commandLog,
    const kernel::FrozenRuntimeCatalog& catalog,
    const runtime::AlgorithmExecutorRegistry& executors,
    const RuntimeMigrationRegistry* migrations
) const
{
    DeterministicReplayResult result;
    if (commandLog.finalTick < initial.worldTick)
    {
        result.status = DeterministicReplayStatus::InvalidCommandLog;
        result.message = "Replay final Tick precedes the initial Save Image";
        return result;
    }
    std::uint64_t previousSubmitTick = initial.worldTick;
    bool first = true;
    for (const ReplayCommandEntry& entry : commandLog.entries)
    {
        if (entry.submitTick < initial.worldTick
            || entry.submitTick > commandLog.finalTick
            || (!first && entry.submitTick < previousSubmitTick))
        {
            result.status = DeterministicReplayStatus::InvalidCommandLog;
            result.message =
                "Replay commands must use nondecreasing in-range submit Ticks";
            return result;
        }
        first = false;
        previousSubmitTick = entry.submitTick;
    }

    world::AuthoritativeWorld world;
    runtime::KernelRuntime runtime(world, catalog, executors);
    const RuntimePersistenceReport restored =
        RuntimePersistenceService{}.Restore(runtime, initial, migrations);
    if (!restored)
    {
        result.status = DeterministicReplayStatus::InitialStateRejected;
        result.message = restored.message;
        return result;
    }

    std::vector<kernel::WorldEvent> facts;
    std::size_t nextEntry = 0;
    while (world.Tick() <= commandLog.finalTick)
    {
        while (nextEntry < commandLog.entries.size()
            && commandLog.entries[nextEntry].submitTick == world.Tick())
        {
            const ReplayCommandEntry& entry = commandLog.entries[nextEntry++];
            runtime.Enqueue(
                entry.transaction,
                entry.notBeforeTick,
                entry.priority
            );
        }
        if (world.Tick() == commandLog.finalTick)
        {
            break;
        }
        if (!runtime.RunTick(world.Tick() + 1))
        {
            result.status = DeterministicReplayStatus::TickFailed;
            result.message = "Kernel Runtime rejected a Replay Tick";
            return result;
        }
        std::vector<kernel::WorldEvent> tickFacts = runtime.DrainEvents();
        facts.insert(
            facts.end(),
            std::make_move_iterator(tickFacts.begin()),
            std::make_move_iterator(tickFacts.end())
        );
    }
    if (nextEntry != commandLog.entries.size())
    {
        result.status = DeterministicReplayStatus::InvalidCommandLog;
        result.message = "Replay command submission did not reach every entry";
        return result;
    }

    const RuntimePersistenceReport captured =
        RuntimePersistenceService{}.Save(runtime, result.finalSave);
    if (!captured)
    {
        result.status = DeterministicReplayStatus::CaptureFailed;
        result.message = captured.message;
        return result;
    }
    const RuntimeSaveCodecReport factsEncoded =
        RuntimeSaveCodec{}.EncodeFactStream(facts, result.factStream);
    if (!factsEncoded)
    {
        result.status = DeterministicReplayStatus::FactEncodingFailed;
        result.message = factsEncoded.message;
        return result;
    }
    result.finalStateChecksum = StableRuntimeChecksum(result.finalSave);
    result.factStreamChecksum = StableRuntimeChecksum(result.factStream);
    return result;
}

}
