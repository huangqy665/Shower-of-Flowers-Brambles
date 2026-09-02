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
    kernel::MechanismValue treasuryMoney;
    kernel::MechanismValue treasurySeen;
    persistence::RuntimeSaveImage saveImage;
    std::vector<std::uint8_t> saveBytes;
    std::uint64_t replayStateChecksum = 0;
    std::uint64_t replayFactChecksum = 0;
};

host::StandaloneSessionConfig DemoConfig(
    const fs::path& economySource = "Dillen-Game/economy/demo_0_5",
    bool includeTechnology = true,
    const fs::path& contractSource = "Dillen-Game/demo_0_5/contracts"
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
            "Dillen-Game/technology/demo_0_5",
            20,
            {},
            {},
            {}
        });
    }
    config.sources.push_back({
        "demo05_production",
        "Dillen-Game/production/demo_0_5",
        30,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "demo05_content",
        "Dillen-Game/demo_0_5/content",
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

// Reads one field from EVERY instance of a Definition and requires them to
// agree.
//
// The probe used to read ordinal 0 and stop, so eight production sites and
// four research projects were represented by one of each. Identical Spawn
// counts with divergent state -- one instance faulted, one never created, one
// left on a stale value -- would have gone unseen.
// Reads one field of one Entity's Component.
//
// Mechanism state and Entity state are different stores, and until the computed
// set_component_field landed a Mechanism could only ever stamp a literal into
// the second one. Asserting a Component field is therefore the only way to see
// whether a derived value really crossed that boundary.
bool ReadComponentField(
    const host::StandaloneSession& session,
    const std::string& entityTypeName,
    const std::string& entityDefinitionName,
    const std::string& componentName,
    const std::string& fieldName,
    kernel::MechanismValue& output
)
{
    const kernel::ComponentTypeId component =
        kernel::StableComponentTypeId(componentName);
    const auto slot = session.Catalog().ResolveComponentFieldSlot(
        component,
        1,
        fieldName
    );
    if (!slot)
    {
        return false;
    }
    const kernel::EntityId owner = kernel::StableEntityId(
        kernel::StableEntityDefinitionId(
            kernel::StableEntityTypeId(entityTypeName),
            entityDefinitionName
        )
    );
    const kernel::MechanismValue* value =
        session.Runtime().Query().Components().FindField(
            owner,
            component,
            *slot
        );
    if (value == nullptr)
    {
        return false;
    }
    output = *value;
    return true;
}

// Renders a MechanismValue for diagnostics without asserting its kind.
std::string Describe(const kernel::MechanismValue& value)
{
    if (const auto* number = std::get_if<std::int64_t>(&value.data))
    {
        return std::to_string(*number);
    }
    if (const auto* decimal = std::get_if<double>(&value.data))
    {
        return std::to_string(*decimal);
    }
    if (const auto* flag = std::get_if<bool>(&value.data))
    {
        return *flag ? "true" : "false";
    }
    return "?";
}

bool UniformField(
    const host::StandaloneSession& session,
    const std::string& mechanismName,
    const std::string& definitionName,
    const std::string& fieldName,
    std::uint32_t expectedCount,
    kernel::MechanismValue& output
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
        return false;
    }
    const kernel::MechanismQuerySnapshot& mechanisms =
        session.Runtime().Query().Mechanisms();
    const std::vector<kernel::MechanismInstanceId>& instances =
        mechanisms.FindByDefinition(definition);
    if (instances.size() != expectedCount)
    {
        std::cerr << "Demo 0.5: " << definitionName << " has "
                  << instances.size() << " instances, expected "
                  << expectedCount << '\n';
        return false;
    }
    bool first = true;
    for (const kernel::MechanismInstanceId instance : instances)
    {
        const kernel::MechanismValue* value =
            mechanisms.FindField(instance, *slot);
        if (value == nullptr)
        {
            return false;
        }
        if (first)
        {
            output = *value;
            first = false;
        }
        else if (*value != output)
        {
            std::cerr << "Demo 0.5: " << definitionName << '.' << fieldName
                      << " differs between instances\n";
            return false;
        }
    }
    return true;
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
        && UniformField(
            session,
            "dillen.demo05.research_project",
            "dillen.demo05.metallurgy_program",
            "progress",
            4,
            output.progress
        )
        && UniformField(
            session,
            "dillen.demo05.research_project",
            "dillen.demo05.metallurgy_program",
            "completed",
            4,
            output.completed
        )
        && UniformField(
            session,
            "dillen.demo05.research_project",
            "dillen.demo05.metallurgy_program",
            "unlock_sent",
            4,
            output.unlockSent
        )
        && UniformField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "unlocked",
            8,
            output.unlocked
        )
        && UniformField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "goods_output",
            8,
            output.output
        )
        && UniformField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "reports_sent",
            8,
            output.reportsSent
        )
        && UniformField(
            session,
            "dillen.demo05.production_site",
            "dillen.demo05.north_reach_industry",
            "treasury_seen",
            8,
            output.treasurySeen
        )
        && ReadComponentField(
            session,
            "dillen.demo05.country",
            "dillen.demo05.alvara",
            "dillen.demo05.treasury",
            "money",
            output.treasuryMoney
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
            "Dillen-Game/economy/demo_0_5",
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
        "Dillen-Game/demo_0_5/contracts",
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
        "Dillen-Game/economy/demo_0_5",
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

// A Mechanism Package that names a Content Entity Definition must be rejected
// at load time.
//
// This is a standing gate, not a one-off injection, because the violation it
// catches was written into this repository by hand and passed every test that
// existed at the time. A Mechanism Package declares dependencies only on
// Contract Packages; an Entity Definition name inside its algorithms is
// therefore a dependency it never declared and cannot declare. Without this
// check the "swap the economy Package" promise is unverifiable -- the swap
// keeps working only as long as the replacement happens to target the same
// Content.
//
// The copy below rewrites the balance write-back from the role-addressed form
// back to the owner-named form: the exact edit that would undo the decoupling.
bool RejectHardCodedContentEntity()
{
    const fs::path temporary = fs::temp_directory_path()
        / "dillen_demo_0_5_hardcoded_entity";
    std::error_code error;
    fs::remove_all(temporary, error);
    error.clear();
    fs::copy(
        "Dillen-Game/economy/demo_0_5",
        temporary,
        fs::copy_options::recursive,
        error
    );
    if (error)
    {
        return false;
    }

    const fs::path algorithm = temporary / "algorithms/budget.dalgorithm";
    std::string text;
    {
        std::ifstream input(algorithm, std::ios::binary);
        if (!input)
        {
            return false;
        }
        text.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
    }
    // Anchored on the instruction, not on the role name. `role = capital`
    // also appears in the create block's READ path, and rewriting that one
    // produces a parse error instead of the boundary violation under test --
    // a false pass dressed as a failure.
    const std::string roleForm = "role = capital";
    const std::string ownerForm =
        "owner_entity_type = dillen.demo05.country"
        "  owner_definition = dillen.demo05.alvara";
    const std::size_t instruction = text.find("set_component_field");
    const std::size_t at = instruction == std::string::npos
        ? std::string::npos
        : text.find(roleForm, instruction);
    if (at == std::string::npos)
    {
        std::cerr << "Demo 0.5: the role-addressed write-back is gone, so the "
                     "hard-coded-Entity gate has nothing to rewrite" << '\n';
        return false;
    }
    text.replace(at, roleForm.size(), ownerForm);
    {
        std::ofstream output(algorithm, std::ios::binary | std::ios::trunc);
        output << text;
    }

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    const bool started = session.Start(DemoConfig(temporary), report);
    // The content_digest no longer matches either, and that check would fire
    // on its own. The gate is only satisfied by the boundary diagnostic.
    const bool rejected = !started && HasDiagnostic(
        report,
        "dillen.authoring.package_entity_reference_violation"
    );
    fs::remove_all(temporary, error);
    return rejected;
}

bool ValidateClosedLoop(const DemoResult& result)
{
    const bool valid = result.packageCount == 5
        && result.sourceCount == 29
        && result.mechanismCount == 13
        && result.balance == kernel::MechanismValue(1165.0)
        && result.reportCount == kernel::MechanismValue(std::int64_t{56})
        && result.progress == kernel::MechanismValue(75.0)
        && result.completed == kernel::MechanismValue(true)
        && result.unlockSent == kernel::MechanismValue(true)
        && result.unlocked == kernel::MechanismValue(true)
        && result.output == kernel::MechanismValue(18.0)
        && result.reportsSent == kernel::MechanismValue(std::int64_t{8})
        // The country's treasury must track the budget, not the 25.0 it was
        // authored with. This is the assertion that fails if computed
        // set_component_field silently stops writing: every Mechanism-side
        // number above stays correct without it, because the budget reads the
        // treasury only once at create.
        && result.treasuryMoney == result.balance
        && result.treasuryMoney != kernel::MechanismValue(25.0)
        // A Mechanism Instance role slot, read through
        // AlgorithmReadTerminal::MechanismField. Every production site holds a
        // `treasury` role bound to the budget instance and copies its balance,
        // so this number is only reachable if the Spawn actually wrote the
        // slot -- and it is the same for all eight sites, which UniformField
        // above already required.
        //
        // It lags result.balance by design: the production Tick reads the
        // dispatch snapshot, so it sees the balance as of the start of the
        // final Tick, not the end.
        && result.treasurySeen == kernel::MechanismValue(1154.0);
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
                  << " reports_sent=" << scalar(result.reportsSent)
                  << " treasury_seen=" << Describe(result.treasurySeen)
                  << " treasury_money=" << Describe(result.treasuryMoney)
                  << '\n';
    }
    return valid;
}

}

// Running on from a restored save must be indistinguishable from never having
// saved at all.
//
// The probe already proved that loading a save and immediately re-saving gives
// back the same bytes. That is a statement about the image, not about the
// simulation: it says the codec round trips, and says nothing about whether the
// restored runtime still *behaves* the same. Anything the save does not carry
// -- a scheduled event that was in flight, an RNG stream position, a cached
// index -- survives the round trip untested and only shows up as divergence
// several ticks later, which is precisely when a player would meet it.
//
// So run the same eight ticks twice. Once straight through from tick 1, and
// once across a save at tick 12, and require the tick-20 saves to be equal
// byte for byte.
bool CheckSaveResumeEquivalence(const fs::path& economySource)
{
    constexpr std::uint64_t kResumeTick = 20;

    const auto runTo = [](host::StandaloneSession& session,
                          std::uint64_t from,
                          std::uint64_t to)
    {
        for (std::uint64_t tick = from; tick <= to; ++tick)
        {
            if (!session.Runtime().RunTick(tick)
                || session.Runtime().LastCreateAlgorithms().FailedCount() != 0
                || session.Runtime().LastTickAlgorithms().FailedCount() != 0
                || session.Runtime().LastEventAlgorithms().FailedCount() != 0)
            {
                std::cerr << "Demo 0.5 resume: runtime failed at tick "
                          << tick << "\n";
                return false;
            }
        }
        return true;
    };

    persistence::RuntimePersistenceService persistence;

    // --- continuous: ticks 1 through 20, no save in the middle ---
    host::StandaloneSession continuous;
    host::StandaloneSessionReport continuousReport;
    std::vector<std::uint8_t> continuousBytes;
    if (!continuous.Start(DemoConfig(economySource), continuousReport)
        || !runTo(continuous, 1, kResumeTick)
        || !persistence.Save(continuous.Runtime(), continuousBytes))
    {
        PrintReport(continuousReport);
        std::cerr << "Demo 0.5 resume: continuous run failed\n";
        return false;
    }

    // --- interrupted: ticks 1 through 12, save, load into a fresh session,
    //     then ticks 13 through 20 ---
    host::StandaloneSession before;
    host::StandaloneSessionReport beforeReport;
    std::vector<std::uint8_t> midpoint;
    if (!before.Start(DemoConfig(economySource), beforeReport)
        || !runTo(before, 1, kFinalTick)
        || !persistence.Save(before.Runtime(), midpoint))
    {
        PrintReport(beforeReport);
        std::cerr << "Demo 0.5 resume: pre-save run failed\n";
        return false;
    }

    // A fresh session, not the one that produced the save. Reloading into the
    // same process-warm runtime would let leftover state stand in for state the
    // save was supposed to carry.
    host::StandaloneSession after;
    host::StandaloneSessionReport afterReport;
    if (!after.Start(DemoConfig(economySource), afterReport))
    {
        PrintReport(afterReport);
        std::cerr << "Demo 0.5 resume: restore session failed to start\n";
        return false;
    }
    const persistence::RuntimePersistenceReport loaded =
        persistence.Load(after.Runtime(), midpoint);
    if (!loaded || after.World().Tick() != kFinalTick)
    {
        std::cerr << "Demo 0.5 resume: load failed\n";
        return false;
    }

    std::vector<std::uint8_t> resumedBytes;
    if (!runTo(after, kFinalTick + 1, kResumeTick)
        || !persistence.Save(after.Runtime(), resumedBytes))
    {
        std::cerr << "Demo 0.5 resume: post-load run failed\n";
        return false;
    }

    if (continuousBytes != resumedBytes)
    {
        std::cerr << "Demo 0.5 resume: tick " << kResumeTick
                  << " diverged across a save"
                  << " (continuous " << continuousBytes.size()
                  << " bytes / checksum "
                  << persistence::StableRuntimeChecksum(continuousBytes)
                  << ", resumed " << resumedBytes.size()
                  << " bytes / checksum "
                  << persistence::StableRuntimeChecksum(resumedBytes)
                  << ")\n";
        return false;
    }
    return true;
}

int main()
{
    DemoResult first;
    DemoResult second;
    DemoResult replacement;
    if (!RunDemo("Dillen-Game/economy/demo_0_5", first)
        || !RunDemo("Dillen-Game/economy/demo_0_5", second)
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
            "Dillen-Game/economy/demo_0_5_rebalanced",
            replacement,
            &first.saveBytes)
        || replacement.fingerprint == first.fingerprint
        || replacement.saveBytes == first.saveBytes
        || replacement.packageCount != 5
        || replacement.sourceCount != 29
        || replacement.mechanismCount != 13
        || replacement.balance != kernel::MechanismValue(1057.0)
        || replacement.reportCount
            != kernel::MechanismValue(std::int64_t{48})
        || replacement.progress != kernel::MechanismValue(45.0)
        || replacement.completed != kernel::MechanismValue(true)
        || replacement.output != kernel::MechanismValue(18.0))
    {
        // Print what the replacement world actually settled on. A bare
        // "gate failed" makes every rebalance -- intended or not -- look
        // identical from the outside.
        std::cerr << "Demo 0.5 Package replacement gate failed:"
                  << " packages=" << replacement.packageCount
                  << " sources=" << replacement.sourceCount
                  << " mechanisms=" << replacement.mechanismCount
                  << " balance=" << Describe(replacement.balance)
                  << " reports=" << Describe(replacement.reportCount)
                  << " progress=" << Describe(replacement.progress)
                  << " completed=" << Describe(replacement.completed)
                  << " output=" << Describe(replacement.output)
                  << '\n';
        return 2;
    }

    if (!CheckSaveResumeEquivalence("Dillen-Game/economy/demo_0_5"))
    {
        std::cerr << "Demo 0.5 save-resume equivalence gate failed" << '\n';
        return 8;
    }

    if (!RejectMissingPackage())
    {
        std::cerr << "Demo 0.5 missing Package was not rejected\n";
        return 3;
    }
    if (!RejectHardCodedContentEntity())
    {
        std::cerr << "Demo 0.5 hard-coded Content Entity was not rejected"
                  << '\n';
        return 9;
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
