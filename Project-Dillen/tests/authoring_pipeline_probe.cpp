#include <filesystem>
#include <iostream>

#include "authoring_pipeline.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_value.hpp"
#include "parser_registry.hpp"
#include "resolver.hpp"
#include "template_registry.hpp"

using namespace dillen;

int main()
{
    const std::string rootName = "dillen.demo.root";
    const std::string extensionName = "dillen.demo.audit_extension";
    authoring::AuthoringLaunchSelection selection;
    selection.root = {
        kernel::StableRulesetId(rootName),
        rootName,
        1
    };
    selection.extensions.push_back({
        kernel::StableRulesetId(extensionName),
        extensionName,
        1
    });
    authoring::AuthoringSession session(std::move(selection));

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    if (!session.Register(templates, parsers, resolver))
    {
        std::cerr << "Dillen Authoring frontend registration failed\n";
        return 1;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    parser::DiagnosticBag diagnostics;
    parser::FileCatalog fileCatalog;
    const std::filesystem::path fixtureRoot =
        "Project-Dillen/tests/fixtures/dillen_authoring";
    if (!fileCatalog.AddLayer({
            1,
            "dillen_demo_package",
            fixtureRoot,
            0,
            {}
        })
        || !fileCatalog.AddLayer({
            2,
            "dillen_demo_overlay",
            "Project-Dillen/tests/fixtures/dillen_authoring_overlay",
            100,
            {}
        })
        || !fileCatalog.Build(templates, diagnostics)
        || fileCatalog.ActiveClassifiedFileCount() != 14)
    {
        std::cerr << "Dillen Authoring source catalog failed\n";
        return 2;
    }

    parser::ParseWorkspace workspace;
    if (!fileCatalog.Parse(parsers, workspace, diagnostics)
        || !resolver.Resolve(workspace, diagnostics))
    {
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            std::cerr << parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "Dillen Authoring parse/resolve pipeline failed\n";
        return 3;
    }

    const kernel::FrozenRuntimeCatalog& catalog = session.RuntimeCatalog();
    const kernel::MechanismTypeId counterType =
        kernel::StableMechanismTypeId("dillen.demo.counter");
    const kernel::MechanismDefinitionId counterDefinition =
        kernel::StableMechanismDefinitionId(
            counterType,
            "dillen.demo.default_counter"
        );
    const kernel::MechanismSpawnDefinitionId counterSpawn =
        kernel::StableMechanismSpawnDefinitionId(
            counterDefinition,
            "dillen.demo.initial_counter"
        );
    const kernel::CompiledMechanismDefinition* compiledDefinition =
        catalog.FindDefinition(counterDefinition);
    const kernel::CompiledMechanismSpawnDefinition* compiledSpawn =
        catalog.FindSpawnDefinition(counterSpawn);
    const auto valueSlot = catalog.ResolveDefinitionFieldSlot(
        counterDefinition,
        "value"
    );
    const kernel::AlgorithmDescriptor* counterAlgorithm =
        catalog.FindAlgorithm(
            kernel::StableAlgorithmId("dillen.demo.counter_algorithm"),
            1
        );

    if (diagnostics.HasErrors()
        || session.MechanismSchemas().Size() != 1
        || session.Algorithms().Size() != 2
        || session.MechanismDefinitions().Size() != 1
        || session.MechanismSpawns().Size() != 1
        || session.RelationSchemas().Size() != 1
        || session.RelationDefinitions().Size() != 1
        || session.LockedPackages().Size() != 2
        || session.LockedSources().Size() != 14
        || session.Rulesets().Size() != 1
        || session.ComposedRuleset() == nullptr
        || session.ComposedRuleset()->appliedExtensions.size() != 1
        || session.ComposedRuleset()->appliedExtensions.front().id
            != kernel::StableRulesetId(extensionName)
        || !catalog.IsFrozen()
        || catalog.LayoutCount() != 1
        || catalog.AlgorithmCount() != 2
        || catalog.AlgorithmProgramCount() != 1
        || catalog.DefinitionCount() != 1
        || catalog.SpawnDefinitionCount() != 1
        || catalog.ComponentLayoutCount() != 1
        || catalog.EntityDefinitionCount() != 2
        || catalog.RelationLayoutCount() != 1
        || catalog.RelationDefinitionCount() != 1
        || catalog.RulesetExtensions().size() != 1
        || !catalog.Fingerprint()
        || compiledDefinition == nullptr
        || compiledSpawn == nullptr
        || counterAlgorithm == nullptr
        || counterAlgorithm->executionPolicy.instructionBudget != 16
        || counterAlgorithm->executionPolicy.wallClockWarningMicroseconds
            != 1000000
        || counterAlgorithm->executionPolicy.failurePolicy
            != kernel::AlgorithmFailurePolicy::PauseInstance
        || !valueSlot
        || compiledDefinition->initialValues[valueSlot->value]
            != kernel::MechanismValue(std::int64_t{5})
        || compiledSpawn->initialValues[valueSlot->value]
            != kernel::MechanismValue(std::int64_t{9})
        || compiledSpawn->count != 2)
    {
        std::cerr << "External Authoring Frozen Catalog mismatch\n";
        return 4;
    }

    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport worldReport;
    if (!world::InitialWorldBuilder{}.Build(catalog, world, worldReport))
    {
        std::cerr << "External Authoring World construction failed\n";
        return 5;
    }
    runtime::KernelRuntime runtime(world, catalog);
    const kernel::MechanismInstanceId firstCounter =
        kernel::StableMechanismInstanceId(counterDefinition, 0);
    const kernel::MechanismInstanceId secondCounter =
        kernel::StableMechanismInstanceId(counterDefinition, 1);
    kernel::WorldTransaction setup;
    setup.commands.push_back(kernel::WorldCommand::CreateRngStream(
        kernel::StableRngStreamId("dillen.demo.counter_rng"),
        7
    ));
    setup.commands.push_back(kernel::WorldCommand::ScheduleEvent(
        kernel::StableAlgorithmEventTypeId("dillen.demo.counter_pulse"),
        firstCounter,
        1,
        0,
        kernel::MechanismValue(std::int64_t{1})
    ));
    if (!runtime.ApplyImmediate(setup, 0))
    {
        std::cerr << "Declarative Query/Event/RNG setup failed\n";
        return 6;
    }
    const kernel::EntityId alpha = kernel::StableEntityId(
        kernel::StableEntityDefinitionId(
            kernel::StableEntityTypeId("dillen.demo.actor"),
            "dillen.demo.alpha"
        )
    );
    const kernel::ComponentTypeId identity =
        kernel::StableComponentTypeId("dillen.demo.identity");
    if (runtime.Query().Entities().Size() != 2
        || runtime.Query().Relations().Size() != 1
        || !runtime.RunTick(1)
        || runtime.LastCreateAlgorithms().CompletedCount() != 2
        || runtime.LastTickAlgorithms().CompletedCount() != 2
        || runtime.Snapshot().Find(firstCounter) == nullptr
        || runtime.Snapshot().Find(secondCounter) == nullptr
        || runtime.Snapshot().Find(firstCounter)->lifecycle
            != kernel::MechanismLifecycleState::Active
        || runtime.Snapshot().Find(secondCounter)->lifecycle
            != kernel::MechanismLifecycleState::Active
        || runtime.Snapshot().FindField(firstCounter, *valueSlot)
            == nullptr
        || *runtime.Snapshot().FindField(firstCounter, *valueSlot)
            != kernel::MechanismValue(std::int64_t{78})
        || runtime.Snapshot().FindField(secondCounter, *valueSlot)
            == nullptr
        || *runtime.Snapshot().FindField(secondCounter, *valueSlot)
            != kernel::MechanismValue(std::int64_t{10})
        || runtime.Query().Components().FindField(
            alpha,
            identity,
            kernel::ComponentFieldSlotId{0}) == nullptr
        || *runtime.Query().Components().FindField(
            alpha,
            identity,
            kernel::ComponentFieldSlotId{0})
            != kernel::MechanismValue(std::int64_t{7}))
    {
        std::cerr << "External declarative Algorithm execution failed\n";
        return 6;
    }

    std::cout
        << "External Dillen Authoring pipeline: passed (fingerprint "
        << catalog.Fingerprint().ToHex()
        << ", bytecode programs "
        << catalog.AlgorithmProgramCount()
        << ")\n";
    return 0;
}
