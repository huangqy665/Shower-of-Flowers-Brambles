#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_value.hpp"
#include "deterministic_replay.hpp"
#include "runtime_persistence.hpp"
#include "standalone_session.hpp"

namespace {

struct DemoVariantResult
{
    std::string fingerprint;
    std::size_t mechanismCount = 0;
    std::size_t settlementCount = 0;
    dillen::persistence::RuntimeSaveImage saveImage;
    std::vector<std::uint8_t> saveBytes;
    std::uint64_t replayChecksum = 0;
};

const dillen::kernel::MechanismValue* FindField(
    const dillen::host::StandaloneSession& session,
    dillen::kernel::MechanismInstanceId instance,
    dillen::kernel::MechanismDefinitionId definition,
    const std::string& field
)
{
    const auto slot = session.Catalog().ResolveDefinitionFieldSlot(
        definition,
        field
    );
    return slot
        ? session.Runtime().Query().Mechanisms().FindField(instance, *slot)
        : nullptr;
}

bool RejectTamperedPackageSource()
{
    namespace fs = std::filesystem;
    const fs::path temporary = fs::temp_directory_path()
        / "dillen_demo_1_0_digest_tamper";
    std::error_code error;
    fs::remove_all(temporary, error);
    error.clear();
    fs::copy(
        "Project-Dillen/demo/dillen_demo_1_0/packages/settlement_growth",
        temporary,
        fs::copy_options::recursive,
        error
    );
    if (error)
    {
        std::cerr << "Demo 1.0 tamper fixture copy failed\n";
        return false;
    }
    {
        std::ofstream changed(
            temporary / "definitions/default_settlement.ddefinition",
            std::ios::app | std::ios::binary
        );
        changed << "\n# tampered source\n";
    }

    dillen::host::StandaloneSessionConfig config;
    config.sources.push_back({
        "demo1_contracts",
        "Project-Dillen/demo/dillen_demo_1_0/packages/contracts",
        0,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo1_settlement",
        temporary,
        10,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo1_trade",
        "Project-Dillen/demo/dillen_demo_1_0/packages/trade_cycle",
        20,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo1_ruleset",
        "Project-Dillen/demo/dillen_demo_1_0/rulesets/balanced",
        100,
        {},
        {},
        {}
    });
    config.rulesets.root = {
        dillen::kernel::StableRulesetId("dillen.demo1.balanced_root"),
        "dillen.demo1.balanced_root",
        1
    };
    dillen::host::StandaloneSession session;
    dillen::host::StandaloneSessionReport report;
    const bool started = session.Start(config, report);
    const bool diagnosed = std::any_of(
        report.diagnostics.begin(),
        report.diagnostics.end(),
        [](const std::string& diagnostic)
        {
            return diagnostic.find(
                "dillen.authoring.package_content_digest_mismatch"
            ) != std::string::npos;
        }
    );
    fs::remove_all(temporary, error);
    if (started || !diagnosed)
    {
        std::cerr << "Demo 1.0 tampered Package source was accepted\n";
        return false;
    }
    return true;
}

bool RunVariant(
    const std::string& rulesetSource,
    const std::string& rootName,
    std::size_t expectedSettlementCount,
    std::int64_t initialPopulation,
    std::int64_t initialFood,
    std::int64_t initialMarket,
    DemoVariantResult& output,
    const std::vector<std::uint8_t>* foreignRootSave = nullptr
)
{
    using namespace dillen;
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "demo1_contracts",
        "Project-Dillen/demo/dillen_demo_1_0/packages/contracts",
        0,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo1_settlement",
        "Project-Dillen/demo/dillen_demo_1_0/packages/settlement_growth",
        10,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo1_trade",
        "Project-Dillen/demo/dillen_demo_1_0/packages/trade_cycle",
        20,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo1_ruleset",
        rulesetSource,
        100,
        {},
        {},
        {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId(rootName), rootName, 1
    };

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(config, report)
        || !session.IsReady()
        || session.Catalog().ActiveRuleset()
            != kernel::StableRulesetId(rootName)
        || session.Catalog().LockedPackages().Size() != 4
        || session.Catalog().LockedSources().Size() != 18
        || session.Catalog().LayoutCount() != 2
        || session.Catalog().AlgorithmCount() != 2
        || session.Catalog().DefinitionCount() != 2
        || session.Catalog().SpawnDefinitionCount() != 2)
    {
        std::cerr << "Demo 1.0 variant bootstrap failed: "
                  << report.message << '\n';
        return false;
    }
    for (const kernel::SourceLockEntry& source
        : session.Catalog().LockedSources().Entries())
    {
        if (!source.package
            || source.packageVersion != kernel::PackageVersion{1, 0, 0})
        {
            std::cerr << "Demo 1.0 Package/Source binding failed\n";
            return false;
        }
    }
    const kernel::MechanismTypeId unselectedType =
        kernel::StableMechanismTypeId("dillen.demo1.unselected_probe");
    const kernel::AlgorithmId unselectedAlgorithm =
        kernel::StableAlgorithmId(
            "dillen.demo1.unselected_probe_algorithm"
        );
    const kernel::MechanismDefinitionId unselectedDefinition =
        kernel::StableMechanismDefinitionId(
            unselectedType,
            "dillen.demo1.unselected_probe_definition"
        );
    const kernel::MechanismSpawnDefinitionId unselectedSpawn =
        kernel::StableMechanismSpawnDefinitionId(
            unselectedDefinition,
            "dillen.demo1.unselected_probe_spawn"
        );
    if (session.Catalog().FindLayout(unselectedType, 1) != nullptr
        || session.Catalog().FindAlgorithm(unselectedAlgorithm, 1) != nullptr
        || session.Catalog().FindDefinition(unselectedDefinition) != nullptr
        || session.Catalog().FindSpawnDefinition(unselectedSpawn) != nullptr)
    {
        std::cerr << "Demo 1.0 Ruleset closure pruning failed\n";
        return false;
    }

    const kernel::MechanismTypeId settlementType =
        kernel::StableMechanismTypeId("dillen.demo1.settlement_growth");
    const kernel::MechanismDefinitionId settlementDefinition =
        kernel::StableMechanismDefinitionId(
            settlementType,
            "dillen.demo1.default_settlement"
        );
    const kernel::MechanismTypeId tradeType =
        kernel::StableMechanismTypeId("dillen.demo1.trade_cycle");
    const kernel::MechanismDefinitionId tradeDefinition =
        kernel::StableMechanismDefinitionId(
            tradeType,
            "dillen.demo1.default_trade_cycle"
        );
    const std::vector<kernel::MechanismInstanceId> settlements =
        session.Runtime().Query().Mechanisms()
        .FindByType(settlementType);
    const std::vector<kernel::MechanismInstanceId> trades =
        session.Runtime().Query().Mechanisms()
        .FindByType(tradeType);
    if (settlements.size() != expectedSettlementCount
        || trades.size() != 1
        || session.Runtime().Query().Mechanisms().Size()
            != expectedSettlementCount + 1)
    {
        std::cerr << "Demo 1.0 Root spawn composition failed\n";
        return false;
    }
    for (const kernel::MechanismInstanceId settlement : settlements)
    {
        const kernel::MechanismValue* population = FindField(
            session,
            settlement,
            settlementDefinition,
            "population"
        );
        const kernel::MechanismValue* food = FindField(
            session,
            settlement,
            settlementDefinition,
            "food_stock"
        );
        if (population == nullptr || food == nullptr
            || *population != kernel::MechanismValue(initialPopulation)
            || *food != kernel::MechanismValue(initialFood))
        {
            std::cerr << "Demo 1.0 Root initial values failed\n";
            return false;
        }
    }

    if (!session.Runtime().RunTick(1)
        || session.Runtime().LastCreateAlgorithms().FailedCount() != 0
        || session.Runtime().LastTickAlgorithms().FailedCount() != 0
        || !session.Runtime().RunTick(2)
        || session.Runtime().LastEventAlgorithms().CompletedCount() == 0
        || !session.Runtime().RunTick(3)
        || session.Runtime().LastTickAlgorithms().FailedCount() != 0)
    {
        std::cerr << "Demo 1.0 Algorithm execution failed\n";
        return false;
    }

    for (const kernel::MechanismInstanceId settlement : settlements)
    {
        const kernel::MechanismValue* population = FindField(
            session,
            settlement,
            settlementDefinition,
            "population"
        );
        const kernel::MechanismValue* food = FindField(
            session,
            settlement,
            settlementDefinition,
            "food_stock"
        );
        const kernel::MechanismInstance* instance =
            session.Runtime().Query().Mechanisms().Find(settlement);
        if (instance == nullptr
            || instance->lifecycle
                != kernel::MechanismLifecycleState::Active
            || population == nullptr || food == nullptr
            || *population
                != kernel::MechanismValue(initialPopulation + 6)
            || *food != kernel::MechanismValue(initialFood + 6))
        {
            std::cerr << "Demo 1.0 settlement growth failed\n";
            return false;
        }
    }

    const kernel::MechanismInstanceId trade = trades.front();
    const kernel::MechanismValue* marketIndex = FindField(
        session,
        trade,
        tradeDefinition,
        "market_index"
    );
    const kernel::MechanismValue* completedCycles = FindField(
        session,
        trade,
        tradeDefinition,
        "completed_cycles"
    );
    const kernel::DeterministicRngStream* rng =
        session.Runtime().RngSnapshot().Find(
            kernel::StableRngStreamId("dillen.demo1.trade_rng")
        );
    if (marketIndex == nullptr || completedCycles == nullptr || rng == nullptr
        || *marketIndex
            != kernel::MechanismValue(std::int64_t{initialMarket + 12})
        || *completedCycles != kernel::MechanismValue(std::int64_t{3})
        || rng->seed != 20260829
        || rng->drawCount != 3)
    {
        std::cerr << "Demo 1.0 Event/RNG trade cycle failed\n";
        return false;
    }

    persistence::RuntimePersistenceService persistence;
    if (!persistence.Capture(session.Runtime(), output.saveImage)
        || !persistence.Save(session.Runtime(), output.saveBytes))
    {
        std::cerr << "Demo 1.0 Save capture failed\n";
        return false;
    }
    const kernel::MechanismTransactionResult mutationApplied =
        session.Runtime().ApplyMechanismImmediate({
            kernel::MechanismCommand::SetField(
                settlements.front(),
                *session.Catalog().ResolveDefinitionFieldSlot(
                    settlementDefinition,
                    "population"
                ),
                kernel::MechanismValue(std::int64_t{777})
            )
        }, 3);
    const persistence::RuntimePersistenceReport loadReport = mutationApplied
        ? persistence.Load(session.Runtime(), output.saveBytes)
        : persistence::RuntimePersistenceReport{};
    if (!mutationApplied || !loadReport)
    {
        std::cerr << "Demo 1.0 Save restoration failed: "
                  << loadReport.message << " / "
                  << loadReport.codec.message << '\n';
        return false;
    }
    const kernel::MechanismValue* restoredPopulation = FindField(
        session,
        settlements.front(),
        settlementDefinition,
        "population"
    );
    std::vector<std::uint8_t> restoredBytes;
    if (restoredPopulation == nullptr
        || *restoredPopulation
            != kernel::MechanismValue(initialPopulation + 6)
        || !persistence.Save(session.Runtime(), restoredBytes)
        || restoredBytes != output.saveBytes)
    {
        std::cerr << "Demo 1.0 Save state mismatch after restoration\n";
        return false;
    }

    persistence::RuntimeSaveImage tampered = output.saveImage;
    tampered.identity.sourceLock.front().fingerprint ^= 1;
    const persistence::RuntimePersistenceReport tamperReport =
        persistence.Restore(session.Runtime(), std::move(tampered));
    if (tamperReport
        || tamperReport.status
            != persistence::RuntimePersistenceStatus::IdentityMismatch
        || session.World().Tick() != 3)
    {
        std::cerr << "Demo 1.0 Source Lock tamper was accepted\n";
        return false;
    }

    runtime::AlgorithmExecutorRegistry executors;
    executors.Freeze();
    persistence::ReplayCommandLog replayLog;
    replayLog.finalTick = 6;
    const persistence::DeterministicReplayResult firstReplay =
        persistence::DeterministicReplayService{}.Replay(
            output.saveImage,
            replayLog,
            session.Catalog(),
            executors
        );
    const persistence::DeterministicReplayResult secondReplay =
        persistence::DeterministicReplayService{}.Replay(
            output.saveImage,
            replayLog,
            session.Catalog(),
            executors
        );
    if (!firstReplay || !secondReplay
        || firstReplay.finalSave != secondReplay.finalSave
        || firstReplay.factStream != secondReplay.factStream
        || firstReplay.finalStateChecksum
            != secondReplay.finalStateChecksum
        || firstReplay.factStreamChecksum
            != secondReplay.factStreamChecksum)
    {
        std::cerr << "Demo 1.0 deterministic Replay failed\n";
        return false;
    }
    output.replayChecksum = firstReplay.finalStateChecksum;

    if (foreignRootSave != nullptr)
    {
        const persistence::RuntimePersistenceReport crossRoot =
            persistence.Load(session.Runtime(), *foreignRootSave);
        if (crossRoot
            || crossRoot.status
                != persistence::RuntimePersistenceStatus::IdentityMismatch
            || session.World().Tick() != 3)
        {
            std::cerr << "Demo 1.0 cross-Root Save was accepted\n";
            return false;
        }
    }

    output.fingerprint = session.Catalog().Fingerprint().ToHex();
    output.mechanismCount = session.Runtime().Query().Mechanisms().Size();
    output.settlementCount = settlements.size();
    return true;
}

}

int main()
{
    DemoVariantResult balanced;
    DemoVariantResult accelerated;
    if (!RejectTamperedPackageSource()
        || !RunVariant(
            "Project-Dillen/demo/dillen_demo_1_0/rulesets/balanced",
            "dillen.demo1.balanced_root",
            2,
            100,
            40,
            10,
            balanced)
        || !RunVariant(
            "Project-Dillen/demo/dillen_demo_1_0/rulesets/accelerated",
            "dillen.demo1.accelerated_root",
            3,
            250,
            90,
            30,
            accelerated,
            &balanced.saveBytes)
        || balanced.fingerprint == accelerated.fingerprint
        || balanced.mechanismCount != 3
        || accelerated.mechanismCount != 4)
    {
        std::cerr << "Demo 1.0 replaceable Root acceptance failed\n";
        return 1;
    }

    std::cout
        << "Pure Dillen Demo 1.0: passed (balanced="
        << balanced.fingerprint
        << ", accelerated=" << accelerated.fingerprint
        << ", replay=" << balanced.replayChecksum << ")\n";
    return 0;
}
