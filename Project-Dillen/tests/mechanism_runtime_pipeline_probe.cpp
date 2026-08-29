#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "algorithm_registry.hpp"
#include "definition_registry.hpp"
#include "mechanism_command.hpp"
#include "mechanism_definition_registry.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_query_snapshot.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_lock.hpp"
#include "package_manifest.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"
#include "world_builder.hpp"
#include "world_event.hpp"
#include "world_transaction.hpp"

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
    definition.source.virtualPath = "tests/mechanism_runtime.txt";
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
    ruleset.canonicalName = "dillen.test.runtime_pipeline";
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
    using namespace dillen::runtime;

    const std::string typeName = "dillen.test.runtime_pipeline";
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
        std::cerr << "Runtime pipeline Schema registration failed\n";
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
        std::cerr << "Runtime pipeline Definition registration failed\n";
        return 2;
    }
    definitions.Freeze();

    FrozenRuntimeCatalog catalog;
    if (!CompileCatalog(schemas, algorithms, definitions, catalog))
    {
        std::cerr << "Runtime Catalog compilation failed\n";
        return 3;
    }
    const MechanismFieldSlotId counterSlot =
        *catalog.ResolveDefinitionFieldSlot(alphaDefinition, "counter");
    const MechanismFieldSlotId labelSlot =
        *catalog.ResolveDefinitionFieldSlot(alphaDefinition, "label");

    dillen::compatibility::hoi3::content::DefinitionRegistry contentDefinitions;
    contentDefinitions.Freeze();
    compatibility::hoi3::worldbuilder::WorldBuilder builder;
    compatibility::hoi3::worldbuilder::WorldBuildReport buildReport;
    compatibility::hoi3::worldbuilder::Hoi3WorldState world;
    if (!builder.Build(
            contentDefinitions,
            catalog,
            {1936, 1, 1},
            world,
            buildReport))
    {
        std::cerr << "Runtime pipeline world construction failed\n";
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
    const MechanismQuerySnapshot initialSnapshot =
        kernelRuntime.Snapshot();
    if (!initialSnapshot.IsPublished()
        || initialSnapshot.Tick() != 0
        || initialSnapshot.Revision() != 0
        || initialSnapshot.Size() != 2
        || initialSnapshot.Find(alphaId) == nullptr
        || initialSnapshot.Find(alphaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{0}))
    {
        std::cerr << "Initial Query Snapshot mismatch\n";
        return 4;
    }

    const std::uint64_t firstSequence = kernelRuntime.Enqueue(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                alphaId,
                counterSlot,
                MechanismValue(std::int64_t{5})
            ),
            MechanismCommand::TransitionLifecycle(
                alphaId,
                MechanismLifecycleState::Active
            )
        }),
        1
    );
    const std::uint64_t rejectedSequence = kernelRuntime.Enqueue(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                counterSlot,
                MechanismValue("invalid")
            )
        }),
        1
    );
    const std::uint64_t delayedSequence = kernelRuntime.Enqueue(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                counterSlot,
                MechanismValue(std::int64_t{3})
            )
        }),
        2
    );
    if (firstSequence != 1
        || rejectedSequence != 2
        || delayedSequence != 3
        || kernelRuntime.Commands().Size() != 3)
    {
        std::cerr << "World Command Queue sequence mismatch\n";
        return 5;
    }

    const MechanismSchedulerTickResult firstTick =
        kernelRuntime.RunTick(1);
    if (!firstTick
        || firstTick.processedTransactions != 2
        || firstTick.committedTransactions != 1
        || firstTick.rejectedTransactions != 1
        || firstTick.transactions.size() != 2
        || firstTick.transactions[0].sequence != firstSequence
        || firstTick.transactions[1].sequence != rejectedSequence
        || world.World().Tick() != 1
        || world.World().Revision() != 1
        || kernelRuntime.Commands().Size() != 1
        || world.Mechanisms().Find(alphaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{5})
        || kernelRuntime.Snapshot().Tick() != 1
        || kernelRuntime.Snapshot().Revision() != 1
        || kernelRuntime.Snapshot().Find(alphaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{5})
        || initialSnapshot.Find(alphaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{0}))
    {
        std::cerr << "First Scheduler Tick mismatch\n";
        return 6;
    }

    const std::vector<WorldEvent>& pendingEvents =
        kernelRuntime.Events().Pending();
    if (pendingEvents.size() != 4
        || pendingEvents[0].sequence != 1
        || pendingEvents[0].transactionSequence != firstSequence
        || !std::holds_alternative<WorldTransactionCommittedEvent>(
            pendingEvents[0].payload)
        || !std::holds_alternative<MechanismFieldChange>(
            pendingEvents[1].payload)
        || !std::holds_alternative<MechanismLifecycleChange>(
            pendingEvents[2].payload)
        || pendingEvents[3].transactionSequence != rejectedSequence
        || !std::holds_alternative<WorldTransactionRejectedEvent>(
            pendingEvents[3].payload))
    {
        std::cerr << "World Event publication mismatch\n";
        return 7;
    }
    if (kernelRuntime.DrainEvents().size() != 4
        || !kernelRuntime.Events().Empty())
    {
        std::cerr << "World Event drain mismatch\n";
        return 8;
    }

    const MechanismSchedulerTickResult duplicateTick =
        kernelRuntime.RunTick(1);
    if (duplicateTick.status
            != MechanismSchedulerStatus::TickSequenceInvalid
        || kernelRuntime.Commands().Size() != 1
        || world.World().Tick() != 1)
    {
        std::cerr << "Scheduler Tick sequence barrier mismatch\n";
        return 9;
    }

    const MechanismSchedulerTickResult secondTick =
        kernelRuntime.RunTick(2);
    if (!secondTick
        || secondTick.processedTransactions != 1
        || secondTick.transactions.front().sequence != delayedSequence
        || world.World().Revision() != 2
        || !kernelRuntime.Commands().Empty()
        || kernelRuntime.Snapshot().Find(betaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{3})
        || kernelRuntime.Snapshot().FindByDefinition(betaDefinition).size()
            != 1
        || kernelRuntime.Snapshot().FindByType(type).size() != 2)
    {
        std::cerr << "Delayed Scheduler transaction mismatch\n";
        return 10;
    }
    kernelRuntime.DrainEvents();

    const WorldTransactionResult worldRejected = kernelRuntime.ApplyImmediate(
        WorldTransaction::FromMechanismCommands({
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
        }),
        2
    );
    if (worldRejected
        || worldRejected.status != WorldTransactionStatus::MechanismRejected
        || world.Mechanisms().Find(alphaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{5})
        || world.World().Revision() != 2
        || kernelRuntime.Snapshot().Revision() != 2)
    {
        std::cerr << "World transaction rollback mismatch\n";
        return 11;
    }
    kernelRuntime.DrainEvents();

    const WorldTransactionResult worldCommitted =
        kernelRuntime.ApplyImmediate(
            WorldTransaction::FromMechanismCommands({
                MechanismCommand::SetField(
                    alphaId,
                    labelSlot,
                    MechanismValue("alpha_committed")
                ),
                MechanismCommand::SetField(
                    betaId,
                    labelSlot,
                    MechanismValue("beta_committed")
                )
            }),
            2
        );
    if (!worldCommitted
        || worldCommitted.mechanism.changedInstances != 2
        || world.World().Revision() != 3
        || kernelRuntime.Snapshot().Revision() != 3
        || kernelRuntime.Snapshot().Find(alphaId)->values.at(labelSlot.value)
            != MechanismValue("alpha_committed")
        || kernelRuntime.Snapshot().Find(betaId)->values.at(labelSlot.value)
            != MechanismValue("beta_committed"))
    {
        std::cerr << "World transaction commit mismatch\n";
        return 12;
    }
    kernelRuntime.DrainEvents();

    const WorldTransactionResult tickRejected = kernelRuntime.ApplyImmediate(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                counterSlot,
                MechanismValue(std::int64_t{4})
            )
        }),
        1
    );
    if (tickRejected.status != WorldTransactionStatus::TickRegression
        || world.World().Tick() != 2
        || world.World().Revision() != 3)
    {
        std::cerr << "World transaction Tick barrier mismatch\n";
        return 13;
    }
    kernelRuntime.DrainEvents();

    FrozenRuntimeCatalog unfrozenCatalog;
    runtime::KernelRuntime invalidRuntime(world.World(), unfrozenCatalog);
    invalidRuntime.Enqueue(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                counterSlot,
                MechanismValue(std::int64_t{4})
            )
        }),
        3
    );
    const MechanismSchedulerTickResult frozenBarrier =
        invalidRuntime.RunTick(3);
    if (frozenBarrier.status
            != MechanismSchedulerStatus::RuntimeCatalogNotFrozen
        || invalidRuntime.Commands().Size() != 1
        || world.World().Tick() != 2)
    {
        std::cerr << "Scheduler Registry barrier mismatch\n";
        return 14;
    }

    kernelRuntime.Enqueue(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                betaId,
                counterSlot,
                MechanismValue(std::int64_t{4})
            )
        }),
        3
    );

    const MechanismSchedulerTickResult thirdTick =
        kernelRuntime.RunTick(3);
    if (!thirdTick
        || thirdTick.processedTransactions != 1
        || world.World().Tick() != 3
        || world.World().Revision() != 4
        || kernelRuntime.Snapshot().Tick() != 3
        || kernelRuntime.Snapshot().Revision() != 4
        || kernelRuntime.Snapshot().Find(betaId)->values.at(counterSlot.value)
            != MechanismValue(std::int64_t{4}))
    {
        std::cerr << "Final Scheduler Tick mismatch\n";
        return 15;
    }

    std::cout
        << "Mechanism runtime pipeline: passed (tick "
        << world.World().Tick()
        << ", revision "
        << world.World().Revision()
        << ")\n";
    return 0;
}
