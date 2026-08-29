#include <filesystem>
#include <iostream>
#include <string>

#include "engine_registry.h"
#include "reverse_probe_framework.h"

int main()
{
    core::engine::EngineRegistry registry;
    const core::engine::VersionProfile& profile =
        core::engine::Hoi3Tfh402D328Profile();
    std::string error;
    if (!registry.SelectVersion(
            profile.version.executable,
            0x10000000,
            error
        ))
    {
        std::cerr << "Registry setup failed: " << error << "\n";
        return 1;
    }

    core::ReverseProbeFramework framework;
    if (!core::RegisterCoreReverseProbes(framework, error))
    {
        std::cerr << "Core probe registration failed: " << error << "\n";
        return 2;
    }
    if (!framework.Contains("engine.registry.candidate_symbols"))
    {
        std::cerr << "Candidate symbol probe missing\n";
        return 3;
    }

    bool mutationRan = false;
    core::ReverseProbeDefinition mutation;
    mutation.id = "probe.write_guard";
    mutation.category = "self_test";
    mutation.access = core::ReverseProbeAccess::WriteMemory;
    mutation.execute = [&mutationRan](const core::ReverseProbeContext&)
    {
        mutationRan = true;
        core::ReverseProbeResult result;
        result.status = core::ReverseProbeStatus::Passed;
        result.evidence = core::ReverseProbeEvidence::VerifiedWrite;
        return result;
    };
    if (!framework.Register(std::move(mutation), error))
    {
        std::cerr << "Mutation probe registration failed\n";
        return 4;
    }

    bool stableReadRan = false;
    bool stableLeaseObserved = false;
    core::ReverseProbeDefinition stableRead;
    stableRead.id = "probe.stable_read";
    stableRead.category = "self_test";
    stableRead.access = core::ReverseProbeAccess::ReadMemory;
    stableRead.requiresGameplay = true;
    stableRead.requiresStableBarrier = true;
    stableRead.execute = [
        &stableReadRan,
        &stableLeaseObserved
    ](const core::ReverseProbeContext& probeContext)
    {
        stableReadRan = true;
        stableLeaseObserved = probeContext.safetyLease != nullptr;
        core::ReverseProbeResult result;
        result.status = stableLeaseObserved
            ? core::ReverseProbeStatus::Passed
            : core::ReverseProbeStatus::Failed;
        result.evidence = core::ReverseProbeEvidence::Confirmed;
        result.message = stableLeaseObserved
            ? "stable_lease_observed"
            : "stable_lease_missing";
        return result;
    };
    if (!framework.Register(std::move(stableRead), error))
    {
        std::cerr << "Stable read probe registration failed\n";
        return 5;
    }

    core::ReverseProbeContext context;
    context.engine = &registry;
    const core::ReverseProbeResult structure = framework.Run(
        "engine.registry.structure",
        context
    );
    if (!structure.Succeeded())
    {
        std::cerr << "Registry structure probe failed: "
                  << structure.message << "\n";
        return 6;
    }

    const core::ReverseProbeResult rejected = framework.Run(
        "probe.write_guard",
        context
    );
    if (rejected.status != core::ReverseProbeStatus::Rejected
        || mutationRan)
    {
        std::cerr << "Unsafe mutation probe was not rejected\n";
        return 7;
    }

    core::ReverseProbeDefinition duplicate;
    duplicate.id = "probe.write_guard";
    duplicate.execute = [](const core::ReverseProbeContext&)
    {
        return core::ReverseProbeResult{};
    };
    if (framework.Register(std::move(duplicate), error))
    {
        std::cerr << "Duplicate reverse probe was accepted\n";
        return 8;
    }

    core::NativeSaveLoadBarrier barrier(3);
    barrier.Start();
    context.saveLoadBarrier = &barrier;
    context.lifecycle.runtimeActive = true;
    context.lifecycle.phase = core::GamePhase::Gameplay;
    context.lifecycle.generation = 9;
    context.lifecycle.playerTag = "CHI";
    context.timestampMilliseconds = 1234;
    const core::ReverseProbeResult closed = framework.Run(
        "probe.stable_read",
        context
    );
    if (closed.status != core::ReverseProbeStatus::Skipped
        || stableReadRan)
    {
        std::cerr << "Closed barrier allowed stable probe\n";
        return 9;
    }
    core::NativeLifecycleSample sample;
    sample.available = true;
    sample.gameplay = true;
    sample.playerTag = "CHI";
    sample.gameStateAddress = 0x1000;
    sample.worldFingerprint = 77;
    sample.hasTotalDays = true;
    sample.totalDays = 100;
    barrier.Observe(sample);
    barrier.Observe(sample);
    barrier.Observe(sample);
    const core::ReverseProbeReport selected = framework.RunSelected(
        {"probe.stable_read", "probe.missing"},
        context
    );
    if (selected.results.size() != 2
        || !selected.results[0].Succeeded()
        || selected.results[1].status
            != core::ReverseProbeStatus::Skipped
        || selected.results[0].runId != selected.runId
        || selected.results[1].runId != selected.runId
        || selected.lifecycleGeneration != 9
        || selected.playerTag != "CHI"
        || !stableReadRan
        || !stableLeaseObserved)
    {
        std::cerr << "Selected stable probe execution failed\n";
        return 10;
    }

    const core::ReverseProbeReport report = framework.RunAll(context);
    const std::filesystem::path reportPath =
        std::filesystem::temp_directory_path()
        / "new_core_reverse_probe_test.jsonl";
    std::filesystem::remove(reportPath);
    if (!framework.AppendReport(reportPath, report, error)
        || !std::filesystem::exists(reportPath))
    {
        std::cerr << "Reverse probe report failed: " << error << "\n";
        return 11;
    }
    std::filesystem::remove(reportPath);

    std::cout << "Reverse probe framework: passed\n";
    return 0;
}
