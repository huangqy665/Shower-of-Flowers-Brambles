#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "map_command.hpp"
#include "map_entity_index.hpp"
#include "mechanism_panel.hpp"
#include "fixed_point.hpp"
#include "runtime_persistence.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P4a -- the command loop.
//
// The claim: A UI PRODUCES NOTHING THE HOST API CANNOT ALREADY EXPRESS, and
// everything it produces goes through the same KernelRuntime::Enqueue the CLI
// Inspector uses. If a panel ever needs a route the CLI does not have, that is
// a hole in the Host API and should be visible as one rather than quietly
// patched with a presentation-only entry point.
//
// Falsifying it needs two worlds, driven identically and compared byte for
// byte: one where the transactions come from the translator, one where they
// are built by hand the way a CLI command does. If the saves diverge, the UI
// path is doing something of its own.
//
// The other half is that presentation still never writes. The translator takes
// a const view and returns an inert kernel::WorldTransaction; the only mutable
// reference in this file belongs to the probe, standing in for the application
// that owns the runtime. No module links both sides, which is what keeps the
// architecture guard's direction intact.

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
        std::cerr << "map command: " << what << '\n';
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

// The site Definition's level slot, resolved once in main() from the frozen
// catalog. Held here because the intents below need it and a probe should
// spend a name exactly where the Compiler would.
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

runtime::WorldQuerySnapshotHandle Publish(host::StandaloneSession& session)
{
    return std::make_shared<const runtime::WorldQuerySnapshot>(
        session.Runtime().Query()
    );
}

}

int main()
{
    if (!fs::exists(kMapWorldRoot))
    {
        std::cerr << "map command: " << kMapWorldRoot << " is missing\n";
        return 1;
    }

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(Config(), report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "map command: the world did not load\n";
        return 2;
    }

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
            std::cerr << "map command: the Ruleset has no level field\n";
            return 2;
        }
        gLevelSlot = *slot;
    }
    presentation::MapCommandTranslator translator;
    std::string message;
    if (translator.Bind(session.Catalog(), CommandSpec(), message)
        != presentation::MapCommandStatus::Ok)
    {
        std::cerr << "map command: bind failed: " << message << '\n';
        return 3;
    }
    presentation::PresentationView view;
    view.Advance(Publish(session));
    Check(translator.Resolve(view) == presentation::MapCommandStatus::Ok,
        "resolve failed");

    // Every province has exactly one mechanism, and the translator found it by
    // reading the role bindings rather than by assuming instance N is province
    // N -- which it is not, because instances are numbered in Spawn order and
    // Spawns are ordered by hashed id.
    Check(translator.Resolved() == kProvinces,
        "resolved " + std::to_string(translator.Resolved())
            + " provinces to mechanisms, expected "
            + std::to_string(kProvinces));

    // The raster index -> Entity table, built from the id table the raster
    // ships and the source_id the world carries. Nothing below reconstructs an
    // Entity from a name.
    presentation::MapEntityIndex entities;
    if (!BindEntities(session, view, entities, message))
    {
        std::cerr << "map command: the entity index failed: " << message
                  << '\n';
        return 4;
    }
    Check(entities.Count() == kProvinces
            && entities.Resolved() == kProvinces,
        "only " + std::to_string(entities.Resolved()) + " of "
            + std::to_string(entities.Count())
            + " raster indices resolved to an Entity");
    Check(entities.EntityFor(0) == kernel::EntityId{},
        "the ocean resolved to an Entity");
    Check(entities.EntityFor(kProvinces + 1) == kernel::EntityId{},
        "an index past the map resolved to an Entity");
    for (const std::uint32_t index : {1u, 4096u, 9000u, kProvinces})
    {
        const kernel::EntityId entity = entities.EntityFor(index);
        Check(static_cast<bool>(entity)
                && entities.IndexFor(entity) == index,
            "raster index " + std::to_string(index)
                + " does not round-trip through its Entity");
    }

    // --- refusals happen before anything is produced ---
    kernel::WorldTransaction rejected;
    Check(translator.Translate(Adjust(kernel::EntityId{}, 1), rejected)
            == presentation::MapCommandStatus::IntentEmpty
            && rejected.commands.empty(),
        "a click on the ocean produced a command");
    {
        // An Entity that exists in the world but owns no instance of this
        // Definition. Its own id, mangled, is not an Entity at all -- so the
        // honest case is an Entity from the id table that the translator
        // never bound, and there is none in this world; the refusal that IS
        // reachable is a well-formed intent whose capability the Definition
        // does not provide.
        presentation::MapIntent forged =
            Adjust(entities.EntityFor(1), 1);
        forged.capability =
            kernel::StableCapabilityId("dillen.map.not_provided");
        Check(translator.Translate(forged, rejected)
                == presentation::MapCommandStatus::CapabilityNotProvided
                && rejected.commands.empty(),
            "a capability the Definition does not provide was commanded");
    }
    {
        presentation::MapIntent empty;
        Check(translator.Translate(empty, rejected)
                == presentation::MapCommandStatus::IntentEmpty
                && rejected.commands.empty(),
            "an empty intent produced a command");
    }

    // Resolved once, up front: the hand-built path needs it and the lambda
    // must not reach back into the translator for anything but the instance.
    const auto levelSlot = session.Catalog().ResolveDefinitionFieldSlot(
        kernel::StableMechanismDefinitionId(
            kernel::StableMechanismTypeId("dillen.map.production_site"),
            "dillen.map.site"
        ),
        "level"
    );
    Check(static_cast<bool>(levelSlot), "the level field is missing");

    // --- the two paths ---
    //
    // Both worlds are driven by the same intents over the same ticks. One
    // takes its transactions from the translator; the other builds the same
    // command the way a CLI `set` does, straight from kernel types. They must
    // end up byte-identical.
    const auto drive = [&levelSlot, &entities](
        host::StandaloneSession& target,
        const presentation::MapCommandTranslator& source,
        bool useTranslator,
        std::vector<std::uint8_t>& save)
    {
        presentation::PresentationView local;
        local.Advance(
            std::make_shared<const runtime::WorldQuerySnapshot>(
                target.Runtime().Query()
            )
        );
        presentation::MapCommandTranslator resolved = source;
        resolved.Resolve(local);

        for (std::uint64_t tick = 1; tick <= 6; ++tick)
        {
            for (const presentation::MapIntent& intent : Intents(entities))
            {
                kernel::WorldTransaction transaction;
                if (useTranslator)
                {
                    if (resolved.Translate(intent, transaction)
                        != presentation::MapCommandStatus::Ok)
                    {
                        continue;
                    }
                }
                else
                {
                    // The hand-built equivalent, constructed from kernel types
                    // the way a CLI `set` does. It uses the translator only to
                    // look the instance up, which is data rather than the code
                    // under test -- calling Translate here would compare the
                    // translator with itself and pass no matter what it did.
                    const kernel::MechanismInstanceId instance =
                        resolved.InstanceFor(intent.entity);
                    if (!instance || !levelSlot)
                    {
                        continue;
                    }
                    transaction.commands.push_back(
                        kernel::WorldCommand::Mechanism(
                            kernel::MechanismCommand::AddField(
                                instance,
                                *levelSlot,
                                kernel::MechanismValue(intent.delta)
                            )
                        )
                    );
                }
                target.Runtime().Enqueue(std::move(transaction), tick, 0);
            }
            if (!target.Runtime().RunTick(tick))
            {
                return false;
            }
        }
        persistence::RuntimePersistenceService persistence;
        return static_cast<bool>(persistence.Save(target.Runtime(), save));
    };

    std::vector<std::uint8_t> viaUi;
    Check(drive(session, translator, true, viaUi), "the UI run failed");

    host::StandaloneSession second;
    host::StandaloneSessionReport secondReport;
    if (!second.Start(Config(), secondReport))
    {
        std::cerr << "map command: the second world did not load\n";
        return 4;
    }
    presentation::MapCommandTranslator secondTranslator;
    if (secondTranslator.Bind(second.Catalog(), CommandSpec(), message)
        != presentation::MapCommandStatus::Ok)
    {
        return 5;
    }
    std::vector<std::uint8_t> viaHand;
    Check(drive(second, secondTranslator, false, viaHand),
        "the hand-built run failed");

    Check(!viaUi.empty() && viaUi == viaHand,
        "the UI path and the hand-built path produced different worlds ("
            + std::to_string(viaUi.size()) + " vs "
            + std::to_string(viaHand.size()) + " bytes)");

    // --- the commands actually did something ---
    //
    // Two runs agreeing proves they agree; it does not prove either did
    // anything. Province 1 was adjusted by +3 and +2 on each of six ticks, so
    // its level must have moved by 30 from its default of 1.
    const kernel::MechanismQuerySnapshot& mechanisms =
        session.Runtime().Query().Mechanisms();

    presentation::PresentationView finalView;
    finalView.Advance(Publish(session));
    presentation::MapCommandTranslator finalTranslator;
    finalTranslator.Bind(session.Catalog(), CommandSpec(), message);
    finalTranslator.Resolve(finalView);

    kernel::WorldTransaction probeOne;
    Check(finalTranslator.Translate(
              Adjust(entities.EntityFor(1), 0),
              probeOne)
            == presentation::MapCommandStatus::Ok
            && probeOne.commands.size() == 1,
        "the translator stopped producing a command for province 1");

    if (levelSlot && probeOne.commands.size() == 1)
    {
        const auto* command = std::get_if<kernel::MechanismCommand>(
            &probeOne.commands.front().payload
        );
        const kernel::MechanismValue* level = command == nullptr
            ? nullptr
            : mechanisms.FindField(command->target, *levelSlot);
        Check(level != nullptr
                && *level == kernel::MechanismValue(std::int64_t{31}),
            "province 1's level is not 1 + 6 ticks x (3 + 2)");
    }

    // --- the panel reads what the commands wrote ---
    //
    // A panel that computed its own numbers would agree with itself forever.
    // What has to hold is that it reports the AUTHORITATIVE state: the level a
    // command moved, and the output the algorithm derived from it on the
    // following tick.
    // The panel binds to a Definition and a set of resolved slots, not to a
    // list of names. A compiled view supplies both; this probe has no view, so
    // it resolves them the same way the Compiler would and asserts they exist.
    const kernel::MechanismDefinitionId siteDefinition =
        kernel::StableMechanismDefinitionId(
            kernel::StableMechanismTypeId("dillen.map.production_site"),
            "dillen.map.site"
        );
    const std::vector<std::string> panelFields{"level", "output"};
    std::vector<kernel::MechanismFieldSlotId> panelSlots;
    for (const std::string& field : panelFields)
    {
        const auto slot = session.Catalog().ResolveDefinitionFieldSlot(
            siteDefinition,
            field
        );
        Check(static_cast<bool>(slot),
            "the Ruleset has no field '" + field + "' on the site Definition");
        panelSlots.push_back(slot ? *slot : kernel::MechanismFieldSlotId{});
    }

    presentation::MechanismPanel panel;
    Check(panel.Bind(
              session.Catalog(),
              siteDefinition,
              panelFields,
              panelSlots,
              message)
            == presentation::MechanismPanelStatus::Ok,
        "the panel failed to bind: " + message);

    const presentation::MechanismPanelReadout readout =
        panel.Read(finalTranslator, finalView, entities.EntityFor(1));
    Check(readout.valid && readout.fields.size() == 2,
        "the panel produced no readout for province 1");
    if (readout.valid && readout.fields.size() == 2)
    {
        Check(readout.fields[0].label == "level"
                && readout.fields[0].value == 31,
            "the panel does not show the level the commands produced");
        // output = source_id * level, computed by the algorithm. Province 1's
        // source_id is 1, so output tracks level exactly -- and it is carried
        // at the fixed-point internal scale, which is 10^4.
        Check(readout.fields[1].label == "output"
                && readout.fields[1].isDecimal
                && readout.fields[1].value == 31 * kernel::kDecimalInternalScale,
            "the panel does not show the output the algorithm derived");
    }

    // A province with no mechanism, and one outside the map, are different
    // answers from "a mechanism with no values" -- neither is a blank panel.
    Check(!panel.Read(finalTranslator, finalView, kernel::EntityId{}).valid,
        "the ocean produced a valid readout");
    // An Entity that is real but owns no instance of this Definition. The map
    // is total here, so the honest stand-in is an id no province has.
    Check(!panel.Read(
              finalTranslator,
              finalView,
              kernel::EntityId{0xD111ull}).valid,
        "an Entity with no mechanism produced a valid readout");

    if (failures != 0)
    {
        std::cerr << "map command: " << failures << " failure(s)\n";
        return 6;
    }
    std::cout << "Map command: passed (" << translator.Resolved()
              << " provinces resolved to mechanisms, " << Intents(entities).size()
              << " intents x 6 ticks, both paths " << viaUi.size()
              << " identical save bytes)\n";
    return 0;
}
