#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "client_state.hpp"
#include "deterministic_replay.hpp"
#include "map_command.hpp"
#include "map_entity_index.hpp"
#include "runtime_persistence.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P4b -- client state cannot reach the world.
//
// Selection, hover, camera and viewport belong to one viewer. Two players
// watching the same simulation disagree about all four and must still compute
// the same save and the same Replay Checksum. Memo section 4.4.2 says so; up to
// now nothing checked it.
//
// The trap in writing this gate is that it is trivially true of a struct
// nobody reads. So it is written to fail in both directions:
//
//   * the two runs must genuinely SEE different things -- asserted through
//     ClientState::Digest, so a probe that accidentally drove both sessions
//     identically cannot pass by not moving;
//   * the two runs must genuinely DO the same thing -- the same intents, in the
//     same order, on the same ticks.
//
// What it guards against is not a struct leaking into a save. It is the
// convenience a UI reaches for first: "act on the selected province", wired by
// letting the command path read the selection. The moment that happens, what
// the world becomes depends on where someone was pointing.

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

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "client state: " << what << '\n';
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
    // The Presentation Package, for the raster's id table. A probe that
    // addresses Entities has to read the same table the viewer does; building
    // the correspondence any other way would be the naming convention again,
    // in a test.
    config.sources.push_back({
        "world_map_presentation",
        kMapPresentationRoot,
        200,
        {},
        {},
        {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}

kernel::MechanismFieldSlotId gLevelSlot;

kernel::MechanismDefinitionId SiteDefinition()
{
    return kernel::StableMechanismDefinitionId(
        kernel::StableMechanismTypeId("dillen.map.production_site"),
        "dillen.map.site"
    );
}

presentation::MapCommandSpec CommandSpec()
{
    // Two fields, and the host asserts nothing about the Package beyond them.
    // The entity type, the province count, the field names and the naming
    // convention have all gone.
    presentation::MapCommandSpec spec;
    spec.definition = SiteDefinition();
    spec.roleName = "province";
    return spec;
}

// Builds raster index -> Entity from data: the id table the raster ships and
// the source_id the world carries. No naming convention anywhere.
bool BindEntities(
    host::StandaloneSession& session,
    const presentation::PresentationView& view,
    presentation::MapEntityIndex& entities,
    std::string& message
)
{
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind != "map_province_ids")
        {
            continue;
        }
        return entities.Bind(session.Catalog(), asset, message)
                == presentation::MapEntityIndexStatus::Ok
            && entities.Resolve(view)
                == presentation::MapEntityIndexStatus::Ok;
    }
    message = "the Package ships no province id table";
    return false;
}

// One intent, addressed by Entity and carrying the public contract the
// Package's buttons name.
presentation::MapIntent Adjust(kernel::EntityId entity, std::int64_t delta)
{
    presentation::MapIntent intent;
    intent.entity = entity;
    intent.capability =
        kernel::StableCapabilityId("dillen.map.site_development");
    intent.capabilityVersion = 1;
    intent.field = gLevelSlot;
    intent.delta = delta;
    return intent;
}

// The intents a player might produce in one session. Deliberately includes
// repeats on one province: several inputs in one tick have to accumulate.
std::vector<presentation::MapIntent> Intents(
    const presentation::MapEntityIndex& entities
)
{
    std::vector<presentation::MapIntent> intents;
    const std::pair<std::uint32_t, std::int64_t> plan[] = {
        {1u, 3}, {4096u, 7}, {1u, 2}, {14187u, -1}, {9000u, 5}
    };
    for (const auto& entry : plan)
    {
        intents.push_back(
            Adjust(entities.EntityFor(entry.first), entry.second)
        );
    }
    return intents;
}


struct RunResult
{
    std::vector<std::uint8_t> save;
    std::uint64_t replayState = 0;
    std::uint64_t replayFacts = 0;
    std::uint64_t clientDigest = 0;
    bool ok = false;
};

// `drift` decides how the viewer behaves: 0 leaves the client state alone,
// anything else moves the selection, the hover and the camera every tick.
RunResult Run(std::uint32_t drift)
{
    RunResult result;
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(Config(), report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        return result;
    }

    presentation::MapCommandTranslator translator;
    std::string message;
    // The level slot and the raster index -> Entity table, both resolved from
    // data before anything is commanded. This is where the naming convention
    // used to be.
    {
        const auto slot = session.Catalog().ResolveDefinitionFieldSlot(
            SiteDefinition(),
            "level"
        );
        if (!slot)
        {
            std::cerr << "client state: the Ruleset has no level field\n";
            return result;
        }
        gLevelSlot = *slot;
    }
    if (translator.Bind(session.Catalog(), CommandSpec(), message)
        != presentation::MapCommandStatus::Ok)
    {
        std::cerr << "client state: bind failed: " << message << '\n';
        return result;
    }
    presentation::PresentationView view;
    view.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );
    translator.Resolve(view);

    presentation::MapEntityIndex entities;
    if (!BindEntities(session, view, entities, message))
    {
        std::cerr << "client state: the entity index failed: " << message
                  << '\n';
        return result;
    }

    presentation::ClientState client;
    persistence::ReplayCommandLog log;
    for (std::uint64_t tick = 1; tick <= 8; ++tick)
    {
        if (drift != 0)
        {
            // A viewer clicking around: a different province every tick, the
            // camera panning and bending. None of it may reach the world.
            client.selected = entities.EntityFor(
                static_cast<std::uint32_t>((tick * drift * 97u) % kProvinces)
                + 1u
            );
            client.hovered = entities.EntityFor(
                static_cast<std::uint32_t>((tick * drift * 31u) % kProvinces)
                + 1u
            );
            client.camera.lookAtU =
                static_cast<double>((tick * drift) % 100u) / 100.0;
            client.camera.lookAtV =
                static_cast<double>((tick * drift * 7u) % 100u) / 100.0;
            client.camera.bend =
                static_cast<double>((tick * drift * 13u) % 100u) / 100.0;
            client.viewportWidth = 640u + static_cast<std::uint32_t>(tick);
        }

        for (const presentation::MapIntent& intent : Intents(entities))
        {
            kernel::WorldTransaction transaction;
            if (translator.Translate(intent, transaction)
                != presentation::MapCommandStatus::Ok)
            {
                continue;
            }
            const std::uint64_t sequence = session.Runtime().Enqueue(
                kernel::WorldTransaction(transaction),
                tick,
                0
            );
            log.entries.push_back({sequence, tick, 0, transaction});
        }
        if (!session.Runtime().RunTick(tick))
        {
            return result;
        }
        // Read the world through the client's eyes as a real viewer would.
        // If any of this fed back, it would do so here.
        view.Advance(
            std::make_shared<const runtime::WorldQuerySnapshot>(
                session.Runtime().Query()
            )
        );
    }

    persistence::RuntimePersistenceService persistence;
    if (!persistence.Save(session.Runtime(), result.save))
    {
        return result;
    }
    result.clientDigest = client.Digest();
    result.ok = true;
    return result;
}

}

int main()
{
    if (!fs::exists(kMapWorldRoot))
    {
        std::cerr << "client state: " << kMapWorldRoot << " is missing\n";
        return 1;
    }

    const RunResult still = Run(0);
    const RunResult busy = Run(3);
    const RunResult busier = Run(11);
    if (!still.ok || !busy.ok || !busier.ok)
    {
        std::cerr << "client state: a run failed\n";
        return 2;
    }

    // --- the runs really did see different things ---
    //
    // Without this the whole probe could pass by driving three identical
    // sessions, which is exactly the shape of a boundary test that proves
    // nothing.
    Check(still.clientDigest != busy.clientDigest,
        "the idle run and the busy run had the same client state");
    Check(busy.clientDigest != busier.clientDigest,
        "the two busy runs had the same client state");

    // --- and still computed the same world ---
    Check(!still.save.empty(), "the idle run saved nothing");
    Check(still.save == busy.save,
        "clicking around changed the save ("
            + std::to_string(still.save.size()) + " vs "
            + std::to_string(busy.save.size()) + " bytes)");
    Check(still.save == busier.save,
        "a differently-behaved viewer changed the save");

    if (failures != 0)
    {
        std::cerr << "client state: " << failures << " failure(s)\n";
        return 3;
    }
    std::cout << "Client state: passed (3 viewers, "
              << 5 << " intents x 8 ticks, "
              << still.save.size()
              << " identical save bytes across three different client "
                 "digests)\n";
    return 0;
}
