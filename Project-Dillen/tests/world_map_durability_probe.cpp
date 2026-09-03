#include <cstdint>
#include <memory>
#include <utility>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "algorithm_runtime.hpp"
#include "deterministic_replay.hpp"
#include "map_command.hpp"
#include "map_entity_index.hpp"
#include "presentation_view.hpp"
#include "runtime_migration.hpp"
#include "runtime_persistence.hpp"
#include "standalone_session.hpp"

// Durability, on the 14187 entity world rather than on the demo slice.
//
// Save / Load, Migration and Replay were all proved on the Demo 0.5 world --
// three entities, three mechanisms -- and that is a real proof of the CODE and
// no proof at all of the scale. The failures that only appear at scale are the
// ones nobody writes a small test for: an index that overflows, a container
// that reorders above some size, a per-instance cost that turns a checkpoint
// into a minute. Section 4.4.4 asks for these gates on the actual map, and
// this is them.
//
// The shape of every assertion is the same and worth stating once: a run that
// was interrupted and resumed must produce THE SAME BYTES as a run that was
// not. Not the same summary, not the same checksum of a summary -- the same
// save image, compared whole. Anything weaker passes for a world that quietly
// lost a mechanism's state on the way through the file.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

constexpr std::uint32_t kProvinces = 14187;
constexpr std::uint64_t kFinalTick = 20;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "world map durability: " << what << '\n';
        ++failures;
    }
}

host::StandaloneSessionConfig Config()
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "world_map_contracts", kMapContractsRoot, 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_mechanisms", kMapMechanismRoot, 50, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kMapWorldRoot, 100, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_presentation", kMapPresentationRoot, 200, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}

kernel::MechanismDefinitionId SiteDefinition()
{
    return kernel::StableMechanismDefinitionId(
        kernel::StableMechanismTypeId("dillen.map.production_site"),
        "dillen.map.site"
    );
}

}

int main()
{
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(Config(), report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "world map durability: the world did not load\n";
        return 1;
    }

    std::string message;
    const auto levelSlot = session.Catalog().ResolveDefinitionFieldSlot(
        SiteDefinition(),
        "level"
    );
    if (!levelSlot)
    {
        std::cerr << "world map durability: the Ruleset has no level field\n";
        return 2;
    }

    // The Entities to command, resolved from data exactly as a viewer would.
    presentation::PresentationView view;
    view.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );
    presentation::MapEntityIndex entities;
    const kernel::PresentationAsset* idAsset = nullptr;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "map_province_ids")
        {
            idAsset = &asset;
            break;
        }
    }
    if (idAsset == nullptr
        || entities.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entities.Resolve(view) != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "world map durability: the entity index failed: "
                  << message << '\n';
        return 3;
    }

    presentation::MapCommandSpec commandSpec;
    commandSpec.definition = SiteDefinition();
    commandSpec.roleName = "province";
    presentation::MapCommandTranslator translator;
    if (translator.Bind(session.Catalog(), commandSpec, message)
        != presentation::MapCommandStatus::Ok)
    {
        std::cerr << "world map durability: the translator failed: " << message
                  << '\n';
        return 4;
    }
    translator.Resolve(view);

    // --- one fixed Command Log ------------------------------------------
    //
    // Written once and replayed everywhere below. Spread across the map and
    // across the run, with repeats on one province so a tick has to accumulate
    // rather than overwrite, and one command scheduled ahead of the tick that
    // submits it so the queue is exercised rather than bypassed.
    persistence::ReplayCommandLog commandLog;
    commandLog.finalTick = kFinalTick;
    {
        const std::pair<std::uint32_t, std::int64_t> plan[] = {
            {1u, 3}, {4096u, 7}, {1u, 2}, {kProvinces, -1}, {9000u, 5},
            {250u, 11}, {13000u, -4}, {777u, 6}
        };
        // `submitTick` is the world's tick BEFORE the one being run, which
        // is the convention DeterministicReplayService uses; a log written
        // one-based would replay a tick out of step with the live run and the
        // two would disagree by exactly one command's worth of state.
        std::uint64_t tick = 0;
        for (const auto& entry : plan)
        {
            presentation::MapIntent intent;
            intent.entity = entities.EntityFor(entry.first);
            intent.capability =
                kernel::StableCapabilityId("dillen.map.site_development");
            intent.capabilityVersion = 1;
            intent.field = *levelSlot;
            intent.delta = entry.second;

            kernel::WorldTransaction transaction;
            Check(translator.Translate(intent, transaction)
                    == presentation::MapCommandStatus::Ok,
                "an intent in the command log did not translate");
            persistence::ReplayCommandEntry logged;
            logged.submitTick = tick;
            // Most run on the next tick; every third waits one more, so the
            // Inbox carries state across a checkpoint rather than being empty
            // at every save.
            logged.notBeforeTick = tick + 1 + (tick % 3 == 0 ? 1 : 0);
            logged.priority = 0;
            logged.transaction = std::move(transaction);
            commandLog.entries.push_back(std::move(logged));
            tick += 2;
        }
    }

    // --- the reference run ------------------------------------------------
    const persistence::RuntimePersistenceService persistence;
    const auto drive = [&](
        runtime::KernelRuntime& runtime,
        std::uint64_t from,
        std::uint64_t to)
    {
        for (std::uint64_t tick = from; tick <= to; ++tick)
        {
            for (const persistence::ReplayCommandEntry& entry
                : commandLog.entries)
            {
                // Submitted against the world as it stands before this tick.
                if (entry.submitTick + 1 != tick)
                {
                    continue;
                }
                kernel::WorldTransaction copy = entry.transaction;
                runtime.Enqueue(
                    std::move(copy),
                    entry.notBeforeTick,
                    entry.priority
                );
            }
            if (!runtime.RunTick(tick))
            {
                return false;
            }
        }
        return true;
    };

    persistence::RuntimeSaveImage initial;
    Check(static_cast<bool>(
            persistence.Capture(session.Runtime(), initial)),
        "the initial state could not be captured");

    Check(drive(session.Runtime(), 1, kFinalTick),
        "the reference run failed");
    std::vector<std::uint8_t> reference;
    Check(static_cast<bool>(
            persistence.Save(session.Runtime(), reference)),
        "the reference run could not be saved");
    Check(!reference.empty(), "the reference save is empty");

    // --- Save, Load, and carry on ----------------------------------------
    //
    // Four checkpoints rather than one. A single checkpoint proves the file
    // round-trips at one moment; four spread across the run also catch state
    // that only exists between certain ticks -- a scheduled command still in
    // the Inbox, an algorithm mid-continuation.
    for (const std::uint64_t checkpoint : {4ull, 8ull, 12ull, 16ull})
    {
        host::StandaloneSession first;
        host::StandaloneSessionReport firstReport;
        if (!first.Start(Config(), firstReport))
        {
            Check(false, "a checkpoint session did not load");
            continue;
        }
        if (!drive(first.Runtime(), 1, checkpoint))
        {
            Check(false, "the run to a checkpoint failed");
            continue;
        }
        std::vector<std::uint8_t> saved;
        Check(static_cast<bool>(persistence.Save(first.Runtime(), saved)),
            "the checkpoint at tick " + std::to_string(checkpoint)
                + " could not be saved");

        host::StandaloneSession second;
        host::StandaloneSessionReport secondReport;
        if (!second.Start(Config(), secondReport))
        {
            Check(false, "a resume session did not load");
            continue;
        }
        Check(static_cast<bool>(persistence.Load(second.Runtime(), saved)),
            "the checkpoint at tick " + std::to_string(checkpoint)
                + " could not be loaded");
        Check(drive(second.Runtime(), checkpoint + 1, kFinalTick),
            "the resumed run failed from tick "
                + std::to_string(checkpoint));

        std::vector<std::uint8_t> resumed;
        Check(static_cast<bool>(persistence.Save(second.Runtime(), resumed)),
            "the resumed run could not be saved");
        Check(resumed == reference,
            "resuming from tick " + std::to_string(checkpoint)
                + " produced " + std::to_string(resumed.size())
                + " bytes that differ from the " + std::to_string(
                    reference.size()) + " byte straight-through run");
    }

    // --- Replay from the initial state ------------------------------------
    //
    // The same command log through DeterministicReplayService rather than
    // through a live session: a different code path to the same bytes, which
    // is what makes the replay service worth having.
    {
        // The declarative worlds this project ships need no C++ executors;
        // the registry is frozen empty so the replay service has the same
        // shape of dependency a host would give it.
        runtime::AlgorithmExecutorRegistry executors;
        executors.Freeze();
        const persistence::DeterministicReplayService replay;
        const persistence::DeterministicReplayResult first = replay.Replay(
            initial,
            commandLog,
            session.Catalog(),
            executors
        );
        Check(static_cast<bool>(first),
            "replaying the command log failed: " + first.message);
        Check(first.finalSave == reference,
            "replay produced a different final save than the live run");

        // Twice, because a replay that is not reproducible is not a replay.
        const persistence::DeterministicReplayResult again = replay.Replay(
            initial,
            commandLog,
            session.Catalog(),
            executors
        );
        Check(again.finalSave == first.finalSave
                && again.factStreamChecksum == first.factStreamChecksum
                && again.finalStateChecksum == first.finalStateChecksum,
            "two replays of one command log disagreed");
    }

    // --- Migration, then carry on -----------------------------------------
    //
    // The gate that matters is not "the migrated bytes can be read". It is
    // that a world which arrived through a migration KEEPS RUNNING and lands
    // on the same state as one that never needed migrating -- which is the
    // only thing a player cares about and the thing a format-only check misses.
    {
        persistence::RuntimeSaveImage aged;
        Check(static_cast<bool>(persistence.Capture(session.Runtime(), aged)),
            "the state to age could not be captured");

        host::StandaloneSession fresh;
        host::StandaloneSessionReport freshReport;
        if (!fresh.Start(Config(), freshReport))
        {
            std::cerr << "world map durability: the migration session did not "
                         "load\n";
            return 5;
        }
        if (!drive(fresh.Runtime(), 1, 8))
        {
            std::cerr << "world map durability: the pre-migration run failed\n";
            return 6;
        }
        persistence::RuntimeSaveImage midway;
        Check(static_cast<bool>(persistence.Capture(fresh.Runtime(), midway)),
            "the midway state could not be captured");

        const persistence::RuntimeSaveIdentity current =
            persistence::RuntimePersistenceService::IdentityFor(
                session.Catalog()
            );
        const std::size_t worldMechanisms =
            session.Runtime().Query().Mechanisms().Size();
        persistence::RuntimeSaveImage legacy = midway;
        // An older fingerprint stands in for an older Ruleset. Every mechanism
        // in the world carries the old version, so the migration has to touch
        // 14187 of them rather than one.
        legacy.identity.rulesetFingerprint = {0x5EEDULL, 0xD11E4ULL};

        persistence::RuntimeMigrationRegistry migrations;
        persistence::RuntimeMigrationStep step;
        step.canonicalName = "dillen.map.world.v0_to_v1";
        step.source = legacy.identity;
        step.target = current;
        // The registry writes candidate.identity = step.target itself, so a
        // step that only rewrote the fingerprint would be doing nothing. This
        // one walks every mechanism the image carries and reports how many it
        // saw, so "the migration ran over the whole world" is a number checked
        // below rather than a sentence in a comment.
        std::size_t steppedMechanisms = 0;
        step.migrate = [&steppedMechanisms](
            persistence::RuntimeSaveImage& image,
            std::string& reason)
        {
            if (image.mechanisms.empty())
            {
                reason = "nothing to migrate";
                return false;
            }
            steppedMechanisms = image.mechanisms.size();
            return true;
        };
        Check(migrations.Register(std::move(step))
                == persistence::RuntimeMigrationRegisterResult::Added,
            "the migration step was refused");
        migrations.Freeze();

        host::StandaloneSession migrated;
        host::StandaloneSessionReport migratedReport;
        if (!migrated.Start(Config(), migratedReport))
        {
            std::cerr << "world map durability: the migrated session did not "
                         "load\n";
            return 7;
        }
        // The stale fingerprint goes in AS IS.
        //
        // An earlier version of this block copied the current fingerprint onto
        // the image first. That made Restore's identity check pass, so
        // Migrate() was never called at all -- and every assertion below still
        // held, because an image that needs no migration restores fine and
        // carries on fine. The gate was green and it was measuring nothing.
        // Hence the three assertions after it: a migration that did not
        // happen must not be able to report that it did.
        const persistence::RuntimePersistenceReport restored =
            persistence.Restore(
                migrated.Runtime(),
                legacy,
                &migrations
            );
        Check(static_cast<bool>(restored),
            "the migrated image could not be restored");
        Check(restored.migration.status
                == persistence::RuntimeMigrationStatus::Migrated,
            "Restore did not migrate: the image was accepted as already "
            "current, so nothing under test ran");
        Check(restored.migration.appliedSteps.size() == 1
                && restored.migration.appliedSteps.front()
                    == "dillen.map.world.v0_to_v1",
            "the migration did not apply exactly the one registered step");
        Check(steppedMechanisms == worldMechanisms && worldMechanisms != 0,
            "the migration step saw " + std::to_string(steppedMechanisms)
                + " mechanisms, the world holds "
                + std::to_string(worldMechanisms));
        Check(drive(migrated.Runtime(), 9, kFinalTick),
            "the migrated world would not keep running");

        std::vector<std::uint8_t> after;
        Check(static_cast<bool>(persistence.Save(migrated.Runtime(), after)),
            "the migrated world could not be saved");
        Check(after == reference,
            "a world that came through a migration landed on different bytes "
            "than one that did not");
    }

    if (failures != 0)
    {
        std::cerr << "world map durability: " << failures << " failure(s)\n";
        return 8;
    }
    std::cout << "World map durability: passed (" << kProvinces
              << " provinces, " << commandLog.entries.size()
              << " logged commands over " << kFinalTick
              << " ticks; four checkpoints, a replay and a migration all land "
                 "on the same " << reference.size() << " save bytes)\n";
    return 0;
}
