#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "control_tree.hpp"
#include "map_command.hpp"
#include "map_entity_index.hpp"
#include "mechanism_panel.hpp"
#include "presentation_compiler.hpp"
#include "presentation_schema.hpp"
#include "presentation_view.hpp"
#include "province_projection.hpp"
#include "runtime_persistence.hpp"
#include "standalone_session.hpp"

// Closing the interface, and opening it again.
//
// Section 4.4.4 asks for two things that sound like one: that the world keeps
// running with the GUI gone, and that reopening it shows the world as it is
// NOW rather than as it was when the GUI last looked. They are different
// failures. The first is about the authoritative side depending on the
// presentation side; the second is about the presentation side keeping state
// of its own that outlives the snapshot it came from.
//
// Both are checked the same way, and the way is the point: two sessions, the
// same commands, and one of them has its entire presentation stack built,
// destroyed and rebuilt underneath it. If closing a window can change a world
// -- or if reopening one can show a stale one -- the two runs part company.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

constexpr std::uint64_t kFinalTick = 12;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "presentation lifecycle: " << what << '\n';
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

// Everything a viewer holds. Built from a session and a snapshot, and nothing
// else -- which is what makes "rebuilt from the current Snapshot" a property
// rather than a hope.
struct Interface
{
    presentation::PresentationSchemaRegistry schema;
    presentation::FrozenPresentationCatalog catalog;
    presentation::ControlTree tree;
    presentation::MapCommandTranslator translator;
    presentation::MechanismPanel panel;
    presentation::MapEntityIndex entities;
    presentation::ProvinceProjection projection;
    presentation::PresentationView view;

    bool Open(host::StandaloneSession& session, std::string& message)
    {
        if (!presentation::RegisterBuiltinControls(schema))
        {
            message = "the control vocabulary is inconsistent";
            return false;
        }
        view.Advance(
            std::make_shared<const runtime::WorldQuerySnapshot>(
                session.Runtime().Query()
            )
        );
        const kernel::PresentationAsset* binding = nullptr;
        const kernel::PresentationAsset* ids = nullptr;
        for (const kernel::PresentationAsset& asset
            : session.PresentationAssets())
        {
            if (asset.kind == "ui_binding")
            {
                binding = &asset;
            }
            else if (asset.kind == "map_province_ids")
            {
                ids = &asset;
            }
        }
        if (binding == nullptr || ids == nullptr)
        {
            message = "the Presentation Package is incomplete";
            return false;
        }
        if (presentation::PresentationCompiler{}.Compile(
                schema,
                session.PresentationAssets(),
                session.Catalog(),
                catalog,
                message)
            != presentation::PresentationCompileStatus::Ok)
        {
            return false;
        }
        if (tree.Bind(
                catalog,
                presentation::StablePresentationViewId(
                    binding->canonicalName),
                session.Catalog(),
                message)
            != presentation::ControlTreeStatus::Ok)
        {
            return false;
        }
        if (entities.Bind(session.Catalog(), *ids, message)
                != presentation::MapEntityIndexStatus::Ok
            || entities.Resolve(view)
                != presentation::MapEntityIndexStatus::Ok)
        {
            return false;
        }
        presentation::MapCommandSpec spec;
        spec.definition = tree.Definition();
        spec.roleName = tree.SubjectRole();
        if (translator.Bind(session.Catalog(), spec, message)
            != presentation::MapCommandStatus::Ok)
        {
            return false;
        }
        translator.Resolve(view);
        if (panel.Bind(
                session.Catalog(),
                tree.Definition(),
                tree.BoundFields(),
                tree.View().boundFieldSlots,
                message)
            != presentation::MechanismPanelStatus::Ok)
        {
            return false;
        }
        presentation::ProvinceProjectionSpec projectionSpec;
        projectionSpec.entityTypeName = entities.Spec().entityTypeName;
        projectionSpec.count = entities.Count();
        projectionSpec.columns.push_back({
            entities.Spec().componentName,
            entities.Spec().componentVersion,
            entities.Spec().sourceIdFieldName
        });
        if (projection.Bind(
                session.Catalog(), projectionSpec, entities, message)
            != presentation::ProvinceProjectionStatus::Ok)
        {
            return false;
        }
        projection.Refresh(view);
        tree.Layout(260, 720);
        return true;
    }

    // Everything the interface would put on screen, as text. Comparing this is
    // comparing the display without needing pixels.
    std::vector<std::string> Display(const std::vector<std::uint32_t>& indices)
    {
        std::vector<std::string> lines;
        for (const std::uint32_t index : indices)
        {
            const kernel::EntityId entity = entities.EntityFor(index);
            const presentation::MechanismPanelReadout readout =
                panel.Read(translator, view, entity);
            lines.push_back("province " + std::to_string(index));
            for (const presentation::ControlDraw& draw : tree.Draw(readout))
            {
                if (!draw.text.empty())
                {
                    lines.push_back("  " + draw.text);
                }
            }
            lines.push_back(
                "  source_id " + std::to_string(projection.Value(index, 0))
            );
        }
        return lines;
    }
};

}

int main()
{
    const std::vector<std::uint32_t> watched{1u, 250u, 4096u, 9000u, 14187u};

    // Two sessions, one command log, and one of them loses its interface
    // halfway through.
    host::StandaloneSession quiet;
    host::StandaloneSession watchedSession;
    host::StandaloneSessionReport quietReport;
    host::StandaloneSessionReport watchedReport;
    if (!quiet.Start(Config(), quietReport)
        || !watchedSession.Start(Config(), watchedReport))
    {
        std::cerr << "presentation lifecycle: a world did not load\n";
        return 1;
    }

    std::string message;
    const auto levelSlot = watchedSession.Catalog().ResolveDefinitionFieldSlot(
        SiteDefinition(),
        "level"
    );
    if (!levelSlot)
    {
        std::cerr << "presentation lifecycle: no level field\n";
        return 2;
    }

    // Built once to author the commands, then thrown away and rebuilt below.
    auto face = std::make_unique<Interface>();
    if (!face->Open(watchedSession, message))
    {
        std::cerr << "presentation lifecycle: the interface did not open: "
                  << message << '\n';
        return 3;
    }

    std::vector<kernel::WorldTransaction> commands;
    for (const std::uint32_t index : watched)
    {
        presentation::MapIntent intent;
        intent.entity = face->entities.EntityFor(index);
        intent.capability =
            kernel::StableCapabilityId("dillen.map.site_development");
        intent.capabilityVersion = 1;
        intent.field = *levelSlot;
        intent.delta = 2 + static_cast<std::int64_t>(index % 5);
        kernel::WorldTransaction transaction;
        Check(face->translator.Translate(intent, transaction)
                == presentation::MapCommandStatus::Ok,
            "an intent did not translate");
        commands.push_back(std::move(transaction));
    }

    const auto submit = [&](runtime::KernelRuntime& runtime,
                            std::uint64_t tick)
    {
        if (tick == 2 || tick == 7)
        {
            for (const kernel::WorldTransaction& transaction : commands)
            {
                kernel::WorldTransaction copy = transaction;
                runtime.Enqueue(std::move(copy), tick, 0);
            }
        }
    };

    // --- the world does not notice ---------------------------------------
    std::vector<std::string> displayAfterRebuild;
    std::vector<std::string> displayNeverClosed;
    {
        // A second interface on the same session that is never closed, so the
        // rebuilt one has something to be compared against.
        Interface constant;
        Check(constant.Open(watchedSession, message),
            "the constant interface did not open: " + message);

        for (std::uint64_t tick = 1; tick <= kFinalTick; ++tick)
        {
            submit(quiet.Runtime(), tick);
            submit(watchedSession.Runtime(), tick);
            Check(static_cast<bool>(quiet.Runtime().RunTick(tick)),
                "a tick failed in the world with no interface");
            Check(static_cast<bool>(watchedSession.Runtime().RunTick(tick)),
                "a tick failed in the watched world");

            if (tick == 4)
            {
                // Closed. Everything the viewer held is destroyed while the
                // world carries on -- which is the half of the property that
                // is about the authoritative side not depending on this one.
                face.reset();
            }
            if (tick == 9)
            {
                face = std::make_unique<Interface>();
                Check(face->Open(watchedSession, message),
                    "the interface did not reopen: " + message);
            }
            if (face != nullptr)
            {
                face->view.Advance(
                    std::make_shared<const runtime::WorldQuerySnapshot>(
                        watchedSession.Runtime().Query()
                    )
                );
                face->projection.Refresh(face->view);
            }
            constant.view.Advance(
                std::make_shared<const runtime::WorldQuerySnapshot>(
                    watchedSession.Runtime().Query()
                )
            );
            constant.projection.Refresh(constant.view);
        }
        Check(face != nullptr, "the interface was never reopened");
        if (face != nullptr)
        {
            displayAfterRebuild = face->Display(watched);
        }
        displayNeverClosed = constant.Display(watched);
    }

    // The world that was watched, closed, reopened and watched again must be
    // byte for byte the world that nobody ever looked at.
    const persistence::RuntimePersistenceService persistence;
    std::vector<std::uint8_t> quietSave;
    std::vector<std::uint8_t> watchedSave;
    Check(static_cast<bool>(persistence.Save(quiet.Runtime(), quietSave)),
        "the unwatched world could not be saved");
    Check(static_cast<bool>(
            persistence.Save(watchedSession.Runtime(), watchedSave)),
        "the watched world could not be saved");
    Check(!quietSave.empty() && quietSave == watchedSave,
        "opening, closing and reopening an interface changed the world: "
            + std::to_string(watchedSave.size()) + " bytes against "
            + std::to_string(quietSave.size()));

    // --- and the reopened one shows the world as it is now ----------------
    //
    // Not as it was when it last looked. A rebuilt interface reads the current
    // snapshot and nothing else, so its display has to match one that watched
    // the whole way through -- including the four ticks and the second batch of
    // commands it was not there for.
    Check(!displayAfterRebuild.empty(), "the rebuilt interface showed nothing");
    Check(displayAfterRebuild == displayNeverClosed,
        "a reopened interface shows something different from one that was "
        "never closed");

    // The display is not trivially equal because it is empty or constant: it
    // has to carry the levels the commands produced.
    std::size_t withNumbers = 0;
    for (const std::string& line : displayAfterRebuild)
    {
        if (line.rfind("  Level: ", 0) == 0 && line != "  Level: 0")
        {
            ++withNumbers;
        }
    }
    Check(withNumbers == watched.size(),
        "only " + std::to_string(withNumbers) + " of "
            + std::to_string(watched.size())
            + " watched provinces show a level, so the comparison above may "
              "be comparing blanks");

    if (failures != 0)
    {
        std::cerr << "presentation lifecycle: " << failures
                  << " failure(s)\n";
        return 4;
    }
    std::cout << "Presentation lifecycle: passed (closed at tick 4, reopened "
                 "at tick 9; the world is byte identical to one that was never "
                 "watched, and the reopened interface shows what it shows)"
              << std::endl;
    return 0;
}
