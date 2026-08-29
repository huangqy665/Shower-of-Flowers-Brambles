#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

#include "algorithm_registry.hpp"
#include "algorithm_runtime.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"

namespace {

struct CatalogFixture
{
    dillen::kernel::FrozenRuntimeCatalog catalog;
    dillen::kernel::AlgorithmId algorithm;
    dillen::kernel::MechanismDefinitionId definition;
    dillen::kernel::MechanismFieldSlotId valueSlot;
};

bool BuildCatalog(
    const std::string& name,
    dillen::kernel::AlgorithmEntryPoint entryPoints,
    dillen::kernel::AlgorithmExecutionPolicy executionPolicy,
    std::uint32_t count,
    CatalogFixture& output
)
{
    using namespace dillen::kernel;

    const std::string typeName = name + ".mechanism";
    const MechanismTypeId type = StableMechanismTypeId(typeName);
    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;
    MechanismFieldSchema field;
    field.name = "value";
    field.kind = MechanismValueKind::Integer;
    field.defaultValue = MechanismValue(std::int64_t{0});
    schema.fields.push_back(field);
    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        return false;
    }
    schemas.Freeze();

    AlgorithmDescriptor algorithm;
    algorithm.canonicalName = name + ".algorithm";
    algorithm.id = StableAlgorithmId(algorithm.canonicalName);
    algorithm.version = 1;
    algorithm.backend = AlgorithmBackend::Native;
    algorithm.entryPoints = entryPoints;
    algorithm.executionPolicy = executionPolicy;
    AlgorithmRegistry algorithms;
    if (algorithms.Register(algorithm) != AlgorithmRegisterResult::Added)
    {
        return false;
    }
    algorithms.Freeze();

    MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = name + ".definition";
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.algorithm = algorithm.id;
    definition.algorithmVersion = algorithm.version;
    definition.source.sourceName = "algorithm_lifecycle_fault_probe";
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(definition, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added)
    {
        return false;
    }
    definitions.Freeze();

    MechanismSpawnDefinition spawn;
    spawn.canonicalName = name + ".spawn";
    spawn.definition = definition.id;
    spawn.id = StableMechanismSpawnDefinitionId(
        definition.id,
        spawn.canonicalName
    );
    spawn.count = count;
    spawn.source.sourceName = "algorithm_lifecycle_fault_probe";
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, definitions, schemas)
        != MechanismSpawnDeclareResult::Added)
    {
        return false;
    }
    spawns.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = name + ".ruleset";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    ruleset.requiredSchemas.push_back({type, 1});
    ruleset.requiredAlgorithms.push_back({algorithm.id, 1});
    ruleset.requiredDefinitions.push_back(definition.id);
    ruleset.requiredMechanismSpawns.push_back(spawn.id);

    ComponentSchemaRegistry componentSchemas;
    EntityDefinitionRegistry entityDefinitions;
    RuntimeCapabilityContractRegistry capabilities;
    componentSchemas.Freeze();
    entityDefinitions.Freeze();
    capabilities.Freeze();
    PackageManifestRegistry manifests;
    manifests.Freeze();
    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport))
    {
        return false;
    }
    RuntimeCompileReport report;
    if (!RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            schemas,
            componentSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            spawns,
            capabilities,
            output.catalog,
            report))
    {
        return false;
    }
    output.algorithm = algorithm.id;
    output.definition = definition.id;
    output.valueSlot = *output.catalog.ResolveDefinitionFieldSlot(
        definition.id,
        "value"
    );
    return true;
}

bool BuildWorld(
    const CatalogFixture& fixture,
    dillen::world::AuthoritativeWorld& output
)
{
    dillen::world::InitialWorldBuildReport report;
    return dillen::world::InitialWorldBuilder{}.Build(
        fixture.catalog,
        output,
        report
    );
}

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    AlgorithmDescriptor invalidBudget;
    invalidBudget.canonicalName = "dillen.test.lifecycle.invalid_budget";
    invalidBudget.id = StableAlgorithmId(invalidBudget.canonicalName);
    invalidBudget.version = 1;
    invalidBudget.backend = AlgorithmBackend::Declarative;
    invalidBudget.entryPoints = AlgorithmEntryPoint::Tick;
    invalidBudget.executionPolicy.instructionBudget = 1;
    invalidBudget.program.stages[AlgorithmEntryPoint::Tick] = {
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Completed
        ),
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Failed
        )
    };
    AlgorithmRegistry invalidBudgetRegistry;
    if (invalidBudgetRegistry.Register(std::move(invalidBudget))
        != AlgorithmRegisterResult::InvalidDescriptor)
    {
        std::cerr << "Over-budget Algorithm was registered\n";
        return 1;
    }

    CatalogFixture destroyFixture;
    if (!BuildCatalog(
            "dillen.test.lifecycle.destroy",
            AlgorithmEntryPoint::Create
                | AlgorithmEntryPoint::Tick
                | AlgorithmEntryPoint::Destroy,
            {8, 1000000, AlgorithmFailurePolicy::FailInstance},
            1,
            destroyFixture))
    {
        std::cerr << "Destroy catalog construction failed\n";
        return 2;
    }
    std::size_t destroyCalls = 0;
    runtime::AlgorithmExecutorRegistry destroyExecutors;
    runtime::AlgorithmExecutorBinding destroyExecutor;
    destroyExecutor.algorithm = destroyFixture.algorithm;
    destroyExecutor.version = 1;
    destroyExecutor.backend = AlgorithmBackend::Native;
    destroyExecutor.execute = [&destroyCalls, &destroyFixture](
        const runtime::AlgorithmInvocationContext& context,
        runtime::AlgorithmExecutionOutput& output)
    {
        if (!context.budget.Consume())
        {
            return false;
        }
        if (context.stage == runtime::AlgorithmRuntimeStage::Create)
        {
            output.transaction = WorldTransaction::FromMechanismCommands({
                MechanismCommand::TransitionLifecycle(
                    context.instance.id,
                    MechanismLifecycleState::Active
                )
            });
        }
        else if (context.stage == runtime::AlgorithmRuntimeStage::Tick)
        {
            output.transaction = WorldTransaction::FromMechanismCommands({
                MechanismCommand::SetField(
                    context.instance.id,
                    destroyFixture.valueSlot,
                    MechanismValue(std::int64_t{1})
                ),
                MechanismCommand::TransitionLifecycle(
                    context.instance.id,
                    MechanismLifecycleState::Completed
                )
            });
        }
        else if (context.stage == runtime::AlgorithmRuntimeStage::Destroy)
        {
            ++destroyCalls;
        }
        else
        {
            return false;
        }
        return true;
    };
    if (destroyExecutors.Register(std::move(destroyExecutor))
            != runtime::AlgorithmExecutorRegisterResult::Added)
    {
        return 3;
    }
    destroyExecutors.Freeze();
    world::AuthoritativeWorld destroyWorld;
    if (!BuildWorld(destroyFixture, destroyWorld))
    {
        return 4;
    }
    const MechanismInstanceId destroyedId = StableMechanismInstanceId(
        destroyFixture.definition,
        0
    );
    runtime::KernelRuntime destroyRuntime(
        destroyWorld,
        destroyFixture.catalog,
        destroyExecutors
    );
    WorldTransaction scheduled;
    scheduled.commands.push_back(WorldCommand::ScheduleEvent(
        StableAlgorithmEventTypeId("dillen.test.lifecycle.pending"),
        destroyedId,
        4,
        0,
        {}
    ));
    if (!destroyRuntime.ApplyImmediate(scheduled, 0)
        || !destroyRuntime.RunTick(1)
        || destroyCalls != 1
        || destroyRuntime.LastDestroyAlgorithms().CompletedCount() != 1
        || destroyRuntime.Snapshot().Find(destroyedId) != nullptr)
    {
        std::cerr << "Destroy lifecycle execution failed\n";
        return 5;
    }
    bool cancelledTargetEvent = false;
    for (const WorldEvent& event : destroyRuntime.DrainEvents())
    {
        const auto* cancelled = std::get_if<ScheduledEventCancelledChange>(
            &event.payload
        );
        cancelledTargetEvent = cancelledTargetEvent
            || (cancelled != nullptr
                && cancelled->event.target == destroyedId);
    }
    if (!cancelledTargetEvent)
    {
        std::cerr << "Destroy did not clean the Algorithm Inbox\n";
        return 6;
    }

    CatalogFixture pauseFixture;
    if (!BuildCatalog(
            "dillen.test.lifecycle.pause",
            AlgorithmEntryPoint::Create | AlgorithmEntryPoint::Tick,
            {8, 1000000, AlgorithmFailurePolicy::PauseInstance},
            2,
            pauseFixture))
    {
        return 6;
    }
    std::size_t pauseTickCalls = 0;
    runtime::AlgorithmExecutorRegistry pauseExecutors;
    runtime::AlgorithmExecutorBinding pauseExecutor;
    pauseExecutor.algorithm = pauseFixture.algorithm;
    pauseExecutor.version = 1;
    pauseExecutor.backend = AlgorithmBackend::Native;
    pauseExecutor.execute = [&pauseTickCalls, &pauseFixture](
        const runtime::AlgorithmInvocationContext& context,
        runtime::AlgorithmExecutionOutput& output)
    {
        if (!context.budget.Consume())
        {
            return false;
        }
        if (context.stage == runtime::AlgorithmRuntimeStage::Create)
        {
            output.transaction = WorldTransaction::FromMechanismCommands({
                MechanismCommand::TransitionLifecycle(
                    context.instance.id,
                    MechanismLifecycleState::Active
                )
            });
            return true;
        }
        if (context.stage != runtime::AlgorithmRuntimeStage::Tick)
        {
            return false;
        }
        ++pauseTickCalls;
        if (context.instance.creationOrdinal == 0)
        {
            throw std::runtime_error("isolated test fault");
        }
        const std::int64_t value = std::get<std::int64_t>(
            context.instance.values[pauseFixture.valueSlot.value].data
        );
        output.transaction = WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                context.instance.id,
                pauseFixture.valueSlot,
                MechanismValue(value + 1)
            )
        });
        return true;
    };
    pauseExecutors.Register(std::move(pauseExecutor));
    pauseExecutors.Freeze();
    world::AuthoritativeWorld pauseWorld;
    if (!BuildWorld(pauseFixture, pauseWorld))
    {
        return 7;
    }
    runtime::KernelRuntime pauseRuntime(
        pauseWorld,
        pauseFixture.catalog,
        pauseExecutors
    );
    const MechanismInstanceId pausedId = StableMechanismInstanceId(
        pauseFixture.definition,
        0
    );
    const MechanismInstanceId healthyId = StableMechanismInstanceId(
        pauseFixture.definition,
        1
    );
    if (!pauseRuntime.RunTick(1)
        || pauseRuntime.LastTickAlgorithms().FailedCount() != 1
        || pauseRuntime.LastTickAlgorithms().CompletedCount() != 1
        || pauseRuntime.Snapshot().Find(pausedId)->lifecycle
            != MechanismLifecycleState::Paused
        || !pauseRuntime.Snapshot().Find(pausedId)->algorithmFault.isolated
        || pauseRuntime.Snapshot().Find(healthyId)
                ->values[pauseFixture.valueSlot.value]
            != MechanismValue(std::int64_t{1})
        || !pauseRuntime.RunTick(2)
        || pauseRuntime.LastTickAlgorithms().invocations.size() != 1
        || pauseTickCalls != 3)
    {
        std::cerr << "Per-instance pause isolation failed\n";
        return 8;
    }
    if (!pauseRuntime.ApplyMechanismImmediate({
            MechanismCommand::ClearAlgorithmFault(pausedId),
            MechanismCommand::TransitionLifecycle(
                pausedId,
                MechanismLifecycleState::Active
            )
        }, 2)
        || pauseRuntime.Snapshot().Find(pausedId)->algorithmFault.isolated)
    {
        std::cerr << "Algorithm fault recovery failed\n";
        return 9;
    }

    CatalogFixture wallClockFixture;
    if (!BuildCatalog(
            "dillen.test.lifecycle.wall_clock_warning",
            AlgorithmEntryPoint::Create | AlgorithmEntryPoint::Tick,
            {8, 500, AlgorithmFailurePolicy::FailInstance},
            1,
            wallClockFixture))
    {
        return 10;
    }
    runtime::AlgorithmExecutorRegistry wallClockExecutors;
    runtime::AlgorithmExecutorBinding wallClockExecutor;
    wallClockExecutor.algorithm = wallClockFixture.algorithm;
    wallClockExecutor.version = 1;
    wallClockExecutor.backend = AlgorithmBackend::Native;
    wallClockExecutor.execute = [&wallClockFixture](
        const runtime::AlgorithmInvocationContext& context,
        runtime::AlgorithmExecutionOutput& output)
    {
        if (!context.budget.Consume())
        {
            return false;
        }
        if (context.stage == runtime::AlgorithmRuntimeStage::Create)
        {
            output.transaction = WorldTransaction::FromMechanismCommands({
                MechanismCommand::TransitionLifecycle(
                    context.instance.id,
                    MechanismLifecycleState::Active
                )
            });
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        output.transaction = WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                context.instance.id,
                wallClockFixture.valueSlot,
                MechanismValue(std::int64_t{17})
            )
        });
        return true;
    };
    wallClockExecutors.Register(std::move(wallClockExecutor));
    wallClockExecutors.Freeze();
    world::AuthoritativeWorld wallClockWorld;
    if (!BuildWorld(wallClockFixture, wallClockWorld))
    {
        return 11;
    }
    runtime::KernelRuntime wallClockRuntime(
        wallClockWorld,
        wallClockFixture.catalog,
        wallClockExecutors
    );
    const MechanismInstanceId wallClockId = StableMechanismInstanceId(
        wallClockFixture.definition,
        0
    );
    if (!wallClockRuntime.RunTick(1)
        || wallClockRuntime.LastTickAlgorithms().FailedCount() != 0
        || wallClockRuntime.LastTickAlgorithms().invocations.front().status
            != runtime::AlgorithmInvocationStatus::Completed
        || !wallClockRuntime.LastTickAlgorithms().invocations.front()
            .budget.wallClockWarningExceeded
        || wallClockRuntime.Snapshot().Find(wallClockId)->lifecycle
            != MechanismLifecycleState::Active
        || wallClockRuntime.Snapshot().Find(wallClockId)->algorithmFault.code
            != AlgorithmFaultCode::None
        || wallClockRuntime.Snapshot().Find(wallClockId)
                ->values[wallClockFixture.valueSlot.value]
            != MechanismValue(std::int64_t{17}))
    {
        std::cerr << "Wall-clock diagnostic changed authoritative state\n";
        return 12;
    }
    if (wallClockRuntime.ApplyMechanismImmediate({
            MechanismCommand::RecordAlgorithmFault(
                wallClockId,
                AlgorithmFaultCode::WallClockTimeoutLegacy,
                AlgorithmFaultStage::Tick
            )
        }, 1)
        || wallClockRuntime.Snapshot().Find(wallClockId)
                ->algorithmFault.code
            != AlgorithmFaultCode::None)
    {
        std::cerr << "Legacy wall-clock fault entered authoritative state\n";
        return 13;
    }

    CatalogFixture isolateFixture;
    if (!BuildCatalog(
            "dillen.test.lifecycle.isolate",
            AlgorithmEntryPoint::Create,
            {8, 1000000, AlgorithmFailurePolicy::IsolateInstance},
            1,
            isolateFixture))
    {
        return 13;
    }
    runtime::AlgorithmExecutorRegistry isolateExecutors;
    runtime::AlgorithmExecutorBinding isolateExecutor;
    isolateExecutor.algorithm = isolateFixture.algorithm;
    isolateExecutor.version = 1;
    isolateExecutor.backend = AlgorithmBackend::Native;
    isolateExecutor.execute = [](
        const runtime::AlgorithmInvocationContext& context,
        runtime::AlgorithmExecutionOutput& output)
    {
        output.transaction = WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                context.instance.id,
                MechanismFieldSlotId{999},
                MechanismValue(std::int64_t{1})
            )
        });
        return true;
    };
    isolateExecutors.Register(std::move(isolateExecutor));
    isolateExecutors.Freeze();
    world::AuthoritativeWorld isolateWorld;
    if (!BuildWorld(isolateFixture, isolateWorld))
    {
        return 14;
    }
    runtime::KernelRuntime isolateRuntime(
        isolateWorld,
        isolateFixture.catalog,
        isolateExecutors
    );
    const MechanismInstanceId isolatedId = StableMechanismInstanceId(
        isolateFixture.definition,
        0
    );
    if (!isolateRuntime.RunTick(1)
        || isolateRuntime.Snapshot().Find(isolatedId)->lifecycle
            != MechanismLifecycleState::Created
        || !isolateRuntime.Snapshot().Find(isolatedId)
                ->algorithmFault.isolated
        || isolateRuntime.LastCreateAlgorithms().invocations.front().status
            != runtime::AlgorithmInvocationStatus::TransactionRejected
        || isolateRuntime.Snapshot().Find(isolatedId)->algorithmFault.code
            != AlgorithmFaultCode::TransactionRejected
        || !isolateRuntime.RunTick(2)
        || !isolateRuntime.LastCreateAlgorithms().invocations.empty())
    {
        std::cerr << "Isolate-only failure policy failed\n";
        return 15;
    }

    std::cout << "Algorithm lifecycle, budget and fault isolation: passed\n";
    return 0;
}
