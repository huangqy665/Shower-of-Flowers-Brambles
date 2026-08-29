#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "algorithm_registry.hpp"
#include "definition_registry.hpp"
#include "mechanism_command.hpp"
#include "mechanism_definition_registry.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_lock.hpp"
#include "package_manifest.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"
#include "world_builder.hpp"

namespace {

dillen::kernel::MechanismDefinition MakeDefinition(
    dillen::kernel::MechanismTypeId type,
    std::string name
)
{
    using namespace dillen::kernel;

    MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = std::move(name);
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.fields["label"] = MechanismValue(
        definition.canonicalName
    );
    definition.source.sourceName = "probe";
    definition.source.virtualPath = "tests/mechanism_commands.txt";
    return definition;
}

bool CompileCatalog(
    const dillen::kernel::MechanismSchemaRegistry& schemas,
    const dillen::kernel::AlgorithmRegistry& algorithms,
    const dillen::kernel::MechanismDefinitionRegistry& definitions,
    dillen::kernel::FrozenRuntimeCatalog& catalog
)
{
    using namespace dillen::kernel;
    PackageManifestRegistry manifests;
    manifests.Freeze();
    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.transaction";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    PackageLock packageLock;
    PackageLockReport lockReport;
    RuntimeCompileReport compileReport;
    ComponentSchemaRegistry componentSchemas;
    EntityDefinitionRegistry entityDefinitions;
    MechanismSpawnDefinitionRegistry spawns;
    RuntimeCapabilityContractRegistry capabilityContracts;
    componentSchemas.Freeze();
    entityDefinitions.Freeze();
    for (const MechanismDefinition& definition : definitions.All())
    {
        MechanismSpawnDefinition spawn;
        spawn.canonicalName = definition.canonicalName + "_initial";
        spawn.definition = definition.id;
        spawn.id = StableMechanismSpawnDefinitionId(
            spawn.definition,
            spawn.canonicalName
        );
        spawn.source.sourceName = "probe";
        if (spawns.Declare(spawn, definitions, schemas)
            != MechanismSpawnDeclareResult::Added)
        {
            return false;
        }
    }
    spawns.Freeze();
    capabilityContracts.Freeze();
    return PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport)
        && RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            schemas,
            componentSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            spawns,
            capabilityContracts,
            catalog,
            compileReport
        );
}

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    const std::string typeName = "dillen.test.transaction";
    const MechanismTypeId type = StableMechanismTypeId(typeName);

    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;

    MechanismFieldSchema label;
    label.name = "label";
    label.kind = MechanismValueKind::String;
    label.required = true;
    schema.fields.push_back(label);

    MechanismFieldSchema counter;
    counter.name = "counter";
    counter.kind = MechanismValueKind::Integer;
    counter.defaultValue = MechanismValue(std::int64_t{0});
    counter.minimumNumber = 0.0;
    counter.maximumNumber = 10.0;
    schema.fields.push_back(counter);

    MechanismSchemaRegistry schemas;
    AlgorithmRegistry algorithms;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Mechanism transaction Schema registration failed\n";
        return 1;
    }
    schemas.Freeze();
    algorithms.Freeze();

    MechanismDefinition alpha = MakeDefinition(type, "alpha");
    MechanismDefinition beta = MakeDefinition(type, "beta");
    const MechanismDefinitionId alphaDefinition = alpha.id;
    const MechanismDefinitionId betaDefinition = beta.id;
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(alpha, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added
        || definitions.Declare(beta, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "Mechanism transaction Definition registration failed\n";
        return 2;
    }
    definitions.Freeze();

    FrozenRuntimeCatalog catalog;
    if (!CompileCatalog(schemas, algorithms, definitions, catalog))
    {
        std::cerr << "Mechanism Runtime Catalog compilation failed\n";
        return 3;
    }
    const MechanismFieldSlotId counterSlot =
        *catalog.ResolveDefinitionFieldSlot(alphaDefinition, "counter");
    const MechanismFieldSlotId labelSlot =
        *catalog.ResolveDefinitionFieldSlot(alphaDefinition, "label");

    dillen::compatibility::hoi3::content::DefinitionRegistry contentDefinitions;
    contentDefinitions.Freeze();
    compatibility::hoi3::worldbuilder::WorldBuilder builder;
    compatibility::hoi3::worldbuilder::WorldBuildReport report;
    compatibility::hoi3::worldbuilder::Hoi3WorldState world;
    if (!builder.Build(
            contentDefinitions,
            catalog,
            {1936, 1, 1},
            world,
            report))
    {
        std::cerr << "Mechanism transaction world construction failed\n";
        return 3;
    }

    runtime::KernelRuntime kernelRuntime(world.World(), catalog);

    const MechanismInstanceId alphaId = StableMechanismInstanceId(
        alphaDefinition,
        0
    );
    const MechanismInstanceId betaId = StableMechanismInstanceId(
        betaDefinition,
        0
    );
    const std::vector<MechanismCommand> initialCommands = {
        MechanismCommand::SetField(
            alphaId,
            counterSlot,
            MechanismValue(std::int64_t{5})
        ),
        MechanismCommand::TransitionLifecycle(
            alphaId,
            MechanismLifecycleState::Active
        ),
        MechanismCommand::SetField(
            betaId,
            labelSlot,
            MechanismValue("beta_updated")
        )
    };
    const MechanismTransactionResult initial =
        kernelRuntime.ApplyMechanismImmediate(
            initialCommands,
            10
        );
    const MechanismInstance* alphaState = world.Mechanisms().Find(alphaId);
    const MechanismInstance* betaState = world.Mechanisms().Find(betaId);
    if (!initial
        || initial.changedInstances != 2
        || initial.commandIndex != initialCommands.size()
        || alphaState == nullptr
        || betaState == nullptr
        || alphaState->lifecycle != MechanismLifecycleState::Active
        || alphaState->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{5})
        || alphaState->updatedTick != 10
        || betaState->values.at(labelSlot.value)
            != MechanismValue("beta_updated")
        || betaState->updatedTick != 10)
    {
        std::cerr << "Mechanism transaction commit mismatch\n";
        return 4;
    }

    const MechanismTransactionResult noChange =
        kernelRuntime.ApplyMechanismImmediate(
            {
                MechanismCommand::SetField(
                    alphaId,
                    counterSlot,
                    MechanismValue(std::int64_t{5})
                ),
                MechanismCommand::TransitionLifecycle(
                    alphaId,
                    MechanismLifecycleState::Active
                )
            },
            11
        );
    if (!noChange
        || noChange.changedInstances != 0
        || world.Mechanisms().Find(alphaId)->updatedTick != 10)
    {
        std::cerr << "Mechanism transaction no-op mismatch\n";
        return 5;
    }

    const MechanismTransactionResult rejected =
        kernelRuntime.ApplyMechanismImmediate(
            {
                MechanismCommand::SetField(
                    alphaId,
                    counterSlot,
                    MechanismValue(std::int64_t{6})
                ),
                MechanismCommand::SetField(
                    betaId,
                    counterSlot,
                    MechanismValue("invalid")
                )
            },
            12
        );
    if (rejected
        || rejected.status
            != MechanismTransactionStatus::FieldValueInvalid
        || rejected.commandIndex != 1
        || rejected.target != betaId
        || world.Mechanisms().Find(alphaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{5})
        || world.Mechanisms().Find(alphaId)->updatedTick != 10)
    {
        std::cerr << "Mechanism transaction rollback mismatch\n";
        return 6;
    }

    const MechanismTransactionResult lifecycleBatch =
        kernelRuntime.ApplyMechanismImmediate(
            {
                MechanismCommand::TransitionLifecycle(
                    alphaId,
                    MechanismLifecycleState::Paused
                ),
                MechanismCommand::TransitionLifecycle(
                    alphaId,
                    MechanismLifecycleState::Active
                )
            },
            12
        );
    if (!lifecycleBatch
        || lifecycleBatch.changedInstances != 1
        || world.Mechanisms().Find(alphaId)->lifecycle
            != MechanismLifecycleState::Active
        || world.Mechanisms().Find(alphaId)->updatedTick != 12)
    {
        std::cerr << "Mechanism lifecycle batch mismatch\n";
        return 7;
    }

    const MechanismTransactionResult completed =
        kernelRuntime.ApplyMechanismImmediate(
            {MechanismCommand::TransitionLifecycle(
                alphaId,
                MechanismLifecycleState::Completed)},
            13
        );
    const MechanismTransactionResult terminalRejected =
        kernelRuntime.ApplyMechanismImmediate(
            {MechanismCommand::TransitionLifecycle(
                alphaId,
                MechanismLifecycleState::Active)},
            14
        );
    if (!completed
        || terminalRejected
        || terminalRejected.status
            != MechanismTransactionStatus::LifecycleTransitionInvalid
        || !IsTerminalMechanismLifecycleState(
            world.Mechanisms().Find(alphaId)->lifecycle)
        || world.Mechanisms().Find(alphaId)->updatedTick != 13)
    {
        std::cerr << "Mechanism terminal lifecycle mismatch\n";
        return 8;
    }

    const MechanismTransactionResult tickRejected =
        kernelRuntime.ApplyMechanismImmediate(
            {MechanismCommand::SetField(
                betaId,
                counterSlot,
                MechanismValue(std::int64_t{1}))},
            9
        );
    const MechanismTransactionResult targetRejected =
        kernelRuntime.ApplyMechanismImmediate(
            {MechanismCommand::SetField(
                MechanismInstanceId{1},
                counterSlot,
                MechanismValue(std::int64_t{1}))},
            20
        );
    if (tickRejected.status != MechanismTransactionStatus::TickRegression
        || targetRejected.status
            != MechanismTransactionStatus::TargetMissing)
    {
        std::cerr << "Mechanism transaction safety barrier mismatch\n";
        return 9;
    }

    FrozenRuntimeCatalog unfrozenCatalog;
    runtime::KernelRuntime invalidRuntime(world.World(), unfrozenCatalog);
    const MechanismTransactionResult catalogBarrier =
        invalidRuntime.ApplyMechanismImmediate({}, 20);
    if (catalogBarrier.status
            != MechanismTransactionStatus::RuntimeCatalogNotFrozen)
    {
        std::cerr << "Mechanism transaction Registry barrier mismatch\n";
        return 10;
    }

    std::cout
        << "Mechanism lifecycle and transactions: passed ("
        << world.Mechanisms().Size()
        << " instances)\n";
    return 0;
}
