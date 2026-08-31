#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "deterministic_replay.hpp"
#include "runtime_persistence.hpp"
#include "standalone_session.hpp"

namespace {

namespace fs = std::filesystem;
using namespace dillen;

constexpr std::uint64_t kFinalTick = 12;
constexpr std::chrono::seconds kLoadBudget{30};

struct DemoResult
{
    std::size_t packageCount = 0;
    std::size_t sourceCount = 0;
    std::size_t mechanismCount = 0;
    std::uint64_t loadMicroseconds = 0;
    std::string fingerprint;
    kernel::MechanismValue balance;
    kernel::MechanismValue reportCount;
    kernel::MechanismValue progress;
    kernel::MechanismValue completed;
    kernel::MechanismValue unlockSent;
    kernel::MechanismValue unlocked;
    kernel::MechanismValue output;
    kernel::MechanismValue reportsSent;
    persistence::RuntimeSaveImage saveImage;
    std::vector<std::uint8_t> saveBytes;
    std::uint64_t replayStateChecksum = 0;
    std::uint64_t replayFactChecksum = 0;
};

host::StandaloneSessionConfig DemoConfig(
    const fs::path& economySource = "Dillen-Game/packages/economy",
    bool includeTechnology = true,
    const fs::path& contractSource = "Dillen-Game/contracts/demo_0_5"
)
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "demo05_contracts", contractSource, 0, {}, {}, {}
    });
    config.sources.push_back({
        "demo05_economy", economySource, 10, {}, {}, {}
    });
    if (includeTechnology)
    {
        config.sources.push_back({
            "demo05_technology",
            "Dillen-Game/packages/technology",
            20,
            {},
            {},
            {}
        });
    }
    config.sources.push_back({
        "demo05_production",
        "Dillen-Game/packages/production",
        30,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo05_content",
        "Dillen-Game/content/demo_0_5",
        100,
        {},
        {},
        {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.demo05.root"),
        "dillen.demo05.root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}

bool HasDiagnostic(
    const host::StandaloneSessionReport& report,
    const std::string& code
)
{
    return std::any_of(
        report.diagnostics.begin(),
        report.diagnostics.end(),
        [&code](const std::string& diagnostic)
        {
            return diagnostic.find(code) != std::string::npos;
        }
    );
}

void PrintReport(const host::StandaloneSessionReport& report)
{
    std::cerr << report.message << '\n';
    for (const std::string& diagnostic : report.diagnostics)
    {
        std::cerr << "  " << diagnostic << '\n';
    }
}

const kernel::MechanismValue* FindField(
    const host::StandaloneSession& session,
    const std::string& mechanismName,
    const std::string& definitionName,
    const std::string& fieldName
)
{
    const kernel::MechanismDefinitionId definition =
        kernel::StableMechanismDefinitionId(
            kernel::StableMechanismTypeId(mechanismName),
            definitionName
        );
    const auto slot = session.Catalog().ResolveDefinitionFieldSlot(
        definition,
        fieldName
    );
    if (!slot)
    {
        return nullptr;
    }
    return session.Runtime().Query().Mechanisms().FindField(
        kernel::StableMechanismInstanceId(definition, 0),
        *slot
    );
}

bool ReadField(
    const host::StandaloneSession& session,
    const std::string& mechanismName,
    const std::string& definitionName,
    const std::string& fieldName,
    kernel::MechanismValue& output
)
{
    const kernel::MechanismValue* value = FindField(
        session,
        mechanismName,
        definitionName,
        fieldName
    );
    if (value == nullptr)
    {
        return false;
    }
    output = *value;
    return true;
}

bool RunDemo(
    const fs::path& economySource,
    DemoResult& output,
    const std::vector<std::uint8_t>* foreignSave = nullptr
)
{
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    const auto loadStart = std::chrono::steady_clock::now();
    if (!session.Start(DemoConfig(economySource), report))
    {
        PrintReport(report);
        return false;
    }
    const auto loadEnd = std::chrono::steady_clock::now();
    output.loadMicroseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            loadEnd - loadStart
        ).count()
    );
    if (loadEnd - loadStart > kLoadBudget)
    {
        std::cerr << "Demo 0.5 exceeded the 30 second loading gate\n";
        return false;
    }

    for (std::uint64_t tick = 1; tick <= kFinalTick; ++tick)
    {
        if (!session.Runtime().RunTick(tick)
            || session.Runtime().LastCreateAlgorithms().FailedCount() != 0
            || session.Runtime().LastTickAlgorithms().FailedCount() != 0
            || session.Runtime().LastEventAlgorithms().FailedCount() != 0)
        {
            std::cerr << "Demo 0.5 runtime failed at tick " << tick << '\n';
            const auto printFaults = [](const auto& batch)
            {
                for (const runtime::AlgorithmInvocationResult& invocation
                    : batch.invocations)
                {
                    if (!invocation)
                    {
                        std::cerr << "  " << invocation.message << '\n';
                    }
                }
            };
            printFaults(session.Runtime().LastCreateAlgorithms());
            printFaults(session.Runtime().LastTickAlgorithms());
            printFaults(session.Runtime().LastEventAlgorithms());
            return false;
        }
    }

    output.packageCount = session.Catalog().LockedPackages().Size();
    output.sourceCount = session.Catalog().LockedSources().Size();
    output.mechanismCount = session.Runtime().Query().Mechanisms().Size();
    output.fingerprint = session.Catalog().Fingerprint().ToHex();
    const bool read =
        ReadField(
            session,
            "dillen.demo05.national_budget",
            "dillen.demo05.alvara_budget",
            "balance",
            output.balance
        )
        && ReadField(
            session,
            "dillen.demo05.national_budget",
            "dillen.demo05.alvara_budget",
            "report_count",
            output.reportCount
        )
        && ReadField(
            session,
            "dillen.demo05.research_project",
            "dillen.demo05.metallurgy_program",
            "progress",
            output.progress
        )
        && ReadField(
            session,
            "dillen.demo05.research_project",
            "dillen.demo05.metallurgy_program",
            "completed",
            output.completed
        )
        && ReadField(
            session,
            "dillen.demo05.research_project",
            "dillen.demo05.metallurgy_program",
            "unlock_sent",
            output.unlockSent
        )
        && ReadField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "unlocked",
            output.unlocked
        )
        && ReadField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "goods_output",
            output.output
        )
        && ReadField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "reports_sent",
            output.reportsSent
        );
    if (!read)
    {
        std::cerr << "Demo 0.5 result fields are unavailable\n";
        return false;
    }

    persistence::RuntimePersistenceService persistence;
    if (!persistence.Capture(session.Runtime(), output.saveImage)
        || !persistence.Save(session.Runtime(), output.saveBytes))
    {
        std::cerr << "Demo 0.5 save capture failed\n";
        return false;
    }

    if (!session.Runtime().RunTick(kFinalTick + 1))
    {
        return false;
    }
    const persistence::RuntimePersistenceReport restored =
        persistence.Load(session.Runtime(), output.saveBytes);
    std::vector<std::uint8_t> restoredBytes;
    if (!restored || session.World().Tick() != kFinalTick
        || !persistence.Save(session.Runtime(), restoredBytes)
        || restoredBytes != output.saveBytes)
    {
        std::cerr << "Demo 0.5 save restoration was not byte-stable\n";
        return false;
    }

    persistence::RuntimeSaveImage tampered = output.saveImage;
    if (tampered.identity.sourceLock.empty())
    {
        return false;
    }
    tampered.identity.sourceLock.front().fingerprint ^= 1;
    const persistence::RuntimePersistenceReport tamperReport =
        persistence.Restore(session.Runtime(), std::move(tampered));
    if (tamperReport
        || tamperReport.status
            != persistence::RuntimePersistenceStatus::IdentityMismatch
        || session.World().Tick() != kFinalTick)
    {
        std::cerr << "Demo 0.5 accepted a tampered Source Lock\n";
        return false;
    }

    if (foreignSave != nullptr)
    {
        const persistence::RuntimePersistenceReport crossPackage =
            persistence.Load(session.Runtime(), *foreignSave);
        if (crossPackage
            || crossPackage.status
                != persistence::RuntimePersistenceStatus::IdentityMismatch
            || session.World().Tick() != kFinalTick)
        {
            std::cerr << "Demo 0.5 accepted a save from another Package set\n";
            return false;
        }
    }

    runtime::AlgorithmExecutorRegistry executors;
    executors.Freeze();
    persistence::ReplayCommandLog replayLog;
    replayLog.finalTick = kFinalTick + 4;
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
        std::cerr << "Demo 0.5 deterministic Replay diverged\n";
        return false;
    }
    output.replayStateChecksum = firstReplay.finalStateChecksum;
    output.replayFactChecksum = firstReplay.factStreamChecksum;
    return true;
}

bool RejectMissingPackage()
{
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    const bool started = session.Start(DemoConfig(
            "Dillen-Game/packages/economy",
            false
        ), report);
    const bool rejected = !started
        && (HasDiagnostic(report, "dillen.authoring.package_lock_failed")
            || HasDiagnostic(report, "dillen.authoring.definition_rejected"));
    if (!rejected)
    {
        PrintReport(report);
    }
    return rejected;
}

bool RejectIllegalPackageRole()
{
    const fs::path temporary = fs::temp_directory_path()
        / "dillen_demo_0_5_illegal_contract";
    std::error_code error;
    fs::remove_all(temporary, error);
    error.clear();
    fs::copy(
        "Dillen-Game/contracts/demo_0_5",
        temporary,
        fs::copy_options::recursive,
        error
    );
    if (error)
    {
        return false;
    }
    fs::create_directories(temporary / "algorithms", error);
    std::ofstream illegal(
        temporary / "algorithms/illegal.dalgorithm",
        std::ios::binary
    );
    illegal
        << "algorithm_descriptor = {\n"
        << " name = dillen.demo05.illegal_contract_algorithm\n"
        << " version = 1\n"
        << " backend = declarative\n"
        << " entry_points = { tick }\n"
        << " deterministic = yes\n"
        << " execution_policy = { instruction_budget = 4 failure_policy = fail_instance }\n"
        << " program = { tick = { transition_lifecycle = active } }\n"
        << "}\n";
    illegal.close();

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    const bool started = session.Start(DemoConfig(
        "Dillen-Game/packages/economy",
        true,
        temporary
    ), report);
    const bool rejected = !started && HasDiagnostic(
        report,
        "dillen.authoring.package_role_violation"
    );
    fs::remove_all(temporary, error);
    return rejected;
}

bool ValidateClosedLoop(const DemoResult& result)
{
    const bool valid = result.packageCount == 5
        && result.sourceCount == 29
        && result.mechanismCount == 13
        && result.balance == kernel::MechanismValue(240.0)
        && result.reportCount == kernel::MechanismValue(std::int64_t{6})
        && result.progress == kernel::MechanismValue(66.0)
        && result.completed == kernel::MechanismValue(true)
        && result.unlockSent == kernel::MechanismValue(true)
        && result.unlocked == kernel::MechanismValue(true)
        && result.output == kernel::MechanismValue(18.0)
        && result.reportsSent == kernel::MechanismValue(std::int64_t{7});
    if (!valid)
    {
        const auto scalar = [](const kernel::MechanismValue& value)
        {
            if (const auto* item = std::get_if<std::int64_t>(&value.data))
            {
                return std::to_string(*item);
            }
            if (const auto* item = std::get_if<double>(&value.data))
            {
                return std::to_string(*item);
            }
            if (const auto* item = std::get_if<bool>(&value.data))
            {
                return std::string(*item ? "true" : "false");
            }
            return std::string("non-scalar");
        };
        std::cerr << "Demo 0.5 values: packages=" << result.packageCount
                  << " sources=" << result.sourceCount
                  << " mechanisms=" << result.mechanismCount
                  << " balance=" << scalar(result.balance)
                  << " reports=" << scalar(result.reportCount)
                  << " progress=" << scalar(result.progress)
                  << " completed=" << scalar(result.completed)
                  << " unlock_sent=" << scalar(result.unlockSent)
                  << " unlocked=" << scalar(result.unlocked)
                  << " output=" << scalar(result.output)
                  << " reports_sent=" << scalar(result.reportsSent) << '\n';
    }
    return valid;
}

}

int main()
{
    DemoResult first;
    DemoResult second;
    DemoResult replacement;
    if (!RunDemo("Dillen-Game/packages/economy", first)
        || !RunDemo("Dillen-Game/packages/economy", second)
        || !ValidateClosedLoop(first)
        || !ValidateClosedLoop(second)
        || first.fingerprint != second.fingerprint
        || first.saveBytes != second.saveBytes
        || first.replayStateChecksum != second.replayStateChecksum
        || first.replayFactChecksum != second.replayFactChecksum)
    {
        std::cerr << "Demo 0.5 baseline/checksum gate failed\n";
        return 1;
    }

    if (!RunDemo(
            "Dillen-Game/packages/economy_rebalanced",
            replacement,
            &first.saveBytes)
        || replacement.fingerprint == first.fingerprint
        || replacement.saveBytes == first.saveBytes
        || replacement.packageCount != 5
        || replacement.sourceCount != 29
        || replacement.mechanismCount != 13
        || replacement.balance != kernel::MechanismValue(204.0)
        || replacement.reportCount
            != kernel::MechanismValue(std::int64_t{2})
        || replacement.progress != kernel::MechanismValue(33.0)
        || replacement.completed != kernel::MechanismValue(true)
        || replacement.output != kernel::MechanismValue(18.0))
    {
        std::cerr << "Demo 0.5 Package replacement gate failed\n";
        return 2;
    }

    if (!RejectMissingPackage())
    {
        std::cerr << "Demo 0.5 missing Package was not rejected\n";
        return 3;
    }
    if (!RejectIllegalPackageRole())
    {
        std::cerr << "Demo 0.5 illegal Package role was not rejected\n";
        return 4;
    }

    std::cout
        << "Demo 0.5: passed (formal Dillen-Game content, 5 Packages, "
        << first.sourceCount << " locked sources, "
        << first.mechanismCount << " mechanism instances x "
        << kFinalTick << " ticks, three Capability feedback edges, "
        << "replace/delete/role/persistence/replay gates; load "
        << first.loadMicroseconds << " us)\n";
    return 0;
}
