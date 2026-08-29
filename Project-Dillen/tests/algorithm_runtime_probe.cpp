#include <cstdint>
#include <iostream>
#include <string>
#include <variant>

#include "algorithm_registry.hpp"
#include "algorithm_runtime.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_lock.hpp"
#include "package_manifest.hpp"
#include "ruleset.hpp"
#include "runtime_capability_contract.hpp"
#include "runtime_compiler.hpp"

namespace {

struct StageCounts
{
    std::size_t create = 0;
    std::size_t tick = 0;
    std::size_t event = 0;
    std::size_t scheduledEvent = 0;
    std::size_t command = 0;
};

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;

    const std::string capabilityName = "dillen.world.transaction";
    const CapabilityId capabilityId = StableCapabilityId(capabilityName);
    RuntimeCapabilityContractRegistry capabilityContracts;
    for (std::uint32_t version : {1U, 2U})
    {
        RuntimeCapabilityContract contract;
        contract.id = capabilityId;
        contract.canonicalName = capabilityName;
        contract.version = version;
        contract.operations = {"world.transaction.submit"};
        if (capabilityContracts.Register(std::move(contract))
            != CapabilityContractRegisterResult::Added)
        {
            std::cerr << "Capability Contract registration failed\n";
            return 1;
        }
    }
    capabilityContracts.Freeze();

    const CapabilityRequirement capabilityRequirement{
        capabilityId,
        capabilityName,
        {1, 3}
    };

    const std::string typeName = "dillen.test.algorithm_runtime";
    const MechanismTypeId type = StableMechanismTypeId(typeName);
    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;
    MechanismFieldSchema counterField;
    counterField.name = "counter";
    counterField.kind = MechanismValueKind::Integer;
    counterField.defaultValue = MechanismValue(std::int64_t{0});
    counterField.minimumNumber = 0.0;
    schema.fields.push_back(counterField);
    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Mechanism Schema registration failed\n";
        return 3;
    }
    schemas.Freeze();

    const std::string algorithmName = "dillen.test.algorithm_runtime.native";
    const AlgorithmId algorithmId = StableAlgorithmId(algorithmName);
    AlgorithmDescriptor algorithm;
    algorithm.id = algorithmId;
    algorithm.canonicalName = algorithmName;
    algorithm.version = 1;
    algorithm.backend = AlgorithmBackend::Native;
    algorithm.entryPoints = AlgorithmEntryPoint::Create
        | AlgorithmEntryPoint::Tick
        | AlgorithmEntryPoint::Event
        | AlgorithmEntryPoint::Command;
    algorithm.requiredCapabilities = {capabilityRequirement};
    AlgorithmRegistry algorithms;
    if (algorithms.Register(std::move(algorithm))
        != AlgorithmRegisterResult::Added)
    {
        std::cerr << "Algorithm registration failed\n";
        return 4;
    }
    algorithms.Freeze();

    MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = "algorithm_runtime_instance";
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.algorithm = algorithmId;
    definition.algorithmVersion = 1;
    definition.source.sourceName = "algorithm_runtime_probe";
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(definition, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "Mechanism Definition registration failed\n";
        return 5;
    }
    definitions.Freeze();

    MechanismSpawnDefinition spawn;
    spawn.canonicalName = "algorithm_runtime_initial";
    spawn.definition = definition.id;
    spawn.id = StableMechanismSpawnDefinitionId(
        spawn.definition,
        spawn.canonicalName
    );
    spawn.source.sourceName = "algorithm_runtime_probe";
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, definitions, schemas)
        != MechanismSpawnDeclareResult::Added)
    {
        std::cerr << "Mechanism Spawn registration failed\n";
        return 6;
    }
    spawns.Freeze();

    PackageManifest provider;
    provider.canonicalName = "dillen.test.algorithm_runtime.provider";
    provider.id = StablePackageId(provider.canonicalName);
    provider.version = {1, 0, 0};
    provider.contentDigest = std::string(64, 'a');
    provider.providedCapabilities.push_back({
        capabilityId,
        capabilityName,
        2
    });
    PackageManifestRegistry manifests;
    if (manifests.Register(provider)
        != PackageManifestRegisterResult::Added)
    {
        std::cerr << "Capability Provider registration failed\n";
        return 7;
    }
    manifests.Freeze();
    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.algorithm_runtime";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    ruleset.packages.push_back({
        provider.id,
        provider.canonicalName,
        {PackageVersion{1, 0, 0}, PackageVersion{2, 0, 0}}
    });
    ruleset.requiredSchemas.push_back({type, 1});
    ruleset.requiredDefinitions.push_back(definition.id);
    ruleset.requiredMechanismSpawns.push_back(spawn.id);
    ruleset.requiredAlgorithms.push_back({algorithmId, 1});
    ruleset.requiredCapabilities.push_back(capabilityRequirement);
    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport))
    {
        std::cerr << "Package Lock resolution failed\n";
        return 7;
    }
    ResolvedCapabilityContract resolvedCapability;
    if (RuntimeCapabilityResolver{}.Resolve(
            capabilityRequirement,
            capabilityContracts,
            packageLock,
            resolvedCapability) != CapabilityResolveResult::Resolved
        || resolvedCapability.version != 2)
    {
        std::cerr << "Capability version resolution mismatch\n";
        return 8;
    }
    CapabilityRequirement unavailableVersion = capabilityRequirement;
    unavailableVersion.versions = {1, 2};
    if (RuntimeCapabilityResolver{}.Resolve(
            unavailableVersion,
            capabilityContracts,
            packageLock,
            resolvedCapability)
        != CapabilityResolveResult::CompatibleVersionMissing)
    {
        std::cerr << "Unprovided Capability version was accepted\n";
        return 8;
    }

    ComponentSchemaRegistry componentSchemas;
    EntityDefinitionRegistry entityDefinitions;
    componentSchemas.Freeze();
    entityDefinitions.Freeze();
    FrozenRuntimeCatalog catalog;
    RuntimeCompileReport compileReport;
    const bool compiled = RuntimeCompiler{}.Compile(
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
            compileReport);
    const auto& capabilityBindings = catalog.AlgorithmCapabilities(
        algorithmId,
        1
    );
    const RuntimeCapabilityContract* boundCapability =
        capabilityBindings.empty()
            ? nullptr
            : catalog.FindCapability(capabilityBindings.front());
    if (!compiled
        || capabilityBindings.size() != 1
        || catalog.CapabilityCount() != 1
        || catalog.FindCapability(capabilityId, 1) != nullptr
        || boundCapability == nullptr
        || boundCapability->version != 2)
    {
        std::cerr << "Runtime Catalog capability binding failed\n";
        return 8;
    }

    const MechanismFieldSlotId counterSlot =
        *catalog.ResolveDefinitionFieldSlot(definition.id, "counter");
    StageCounts counts;
    runtime::AlgorithmExecutorRegistry executors;
    runtime::AlgorithmExecutorBinding executor;
    executor.algorithm = algorithmId;
    executor.version = 1;
    executor.backend = AlgorithmBackend::Native;
    executor.execute = [&counts, capabilityId, counterSlot](
        const runtime::AlgorithmInvocationContext& context,
        runtime::AlgorithmExecutionOutput& output)
    {
        if (context.query.Tick() != context.snapshot.Tick()
            || context.query.Revision() != context.snapshot.Revision()
            || context.query.Mechanisms().Find(context.instance.id)
                == nullptr)
        {
            return false;
        }
        const RuntimeCapabilityContract* capability =
            context.FindCapability(capabilityId);
        if (capability == nullptr || capability->version != 2)
        {
            return false;
        }
        switch (context.stage)
        {
        case runtime::AlgorithmRuntimeStage::Create:
            ++counts.create;
            output.transaction = WorldTransaction::FromMechanismCommands({
                MechanismCommand::TransitionLifecycle(
                    context.instance.id,
                    MechanismLifecycleState::Active
                )
            });
            break;
        case runtime::AlgorithmRuntimeStage::Tick:
        {
            ++counts.tick;
            const std::int64_t value = std::get<std::int64_t>(
                context.instance.values.at(counterSlot.value).data
            );
            output.transaction = WorldTransaction::FromMechanismCommands({
                MechanismCommand::SetField(
                    context.instance.id,
                    counterSlot,
                    MechanismValue(value + 1)
                )
            });
            break;
        }
        case runtime::AlgorithmRuntimeStage::Event:
            ++counts.event;
            if (context.scheduledEvent != nullptr)
            {
                ++counts.scheduledEvent;
            }
            if (context.event == nullptr
                && context.scheduledEvent == nullptr)
            {
                return false;
            }
            break;
        case runtime::AlgorithmRuntimeStage::Command:
            ++counts.command;
            if (context.command == nullptr)
            {
                return false;
            }
            break;
        case runtime::AlgorithmRuntimeStage::Destroy:
            return false;
        }
        return true;
    };
    if (executors.Register(std::move(executor))
        != runtime::AlgorithmExecutorRegisterResult::Added)
    {
        std::cerr << "Algorithm Executor registration failed\n";
        return 9;
    }
    executors.Freeze();

    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport worldReport;
    if (!world::InitialWorldBuilder{}.Build(catalog, world, worldReport))
    {
        std::cerr << "Initial World construction failed\n";
        return 10;
    }
    const MechanismInstanceId instanceId = StableMechanismInstanceId(
        definition.id,
        0
    );
    runtime::KernelRuntime runtime(world, catalog, executors);
    if (!runtime.RunTick(1)
        || counts.create != 1
        || counts.tick != 1
        || counts.event != 0
        || runtime.LastCreateAlgorithms().CompletedCount() != 1
        || runtime.LastTickAlgorithms().CompletedCount() != 1
        || !runtime.Snapshot().Find(instanceId)->algorithmInitialized
        || runtime.Snapshot().Find(instanceId)->lifecycle
            != MechanismLifecycleState::Active
        || runtime.Snapshot().Find(instanceId)->values[counterSlot.value]
            != MechanismValue(std::int64_t{1}))
    {
        std::cerr << "Create/Tick Algorithm execution mismatch\n";
        return 11;
    }

    const std::size_t drainedAfterCreate = runtime.DrainEvents().size();
    WorldTransaction scheduledEventTransaction;
    scheduledEventTransaction.commands.push_back(
        WorldCommand::ScheduleEvent(
            StableAlgorithmEventTypeId("dillen.event.algorithm_probe"),
            instanceId,
            3,
            0,
            MechanismValue(std::string("scheduled"))
        )
    );
    runtime.Enqueue(std::move(scheduledEventTransaction), 2);
    if (drainedAfterCreate == 0
        || !runtime.RunTick(2)
        || counts.create != 1
        || counts.tick != 2
        || counts.event == 0
        || runtime.LastEventAlgorithms().CompletedCount() == 0
        || runtime.Snapshot().Find(instanceId)->values[counterSlot.value]
            != MechanismValue(std::int64_t{2}))
    {
        std::cerr << "Event subscription survived Drain mismatch\n";
        return 12;
    }

    const std::size_t commandCount = counts.command;
    runtime.Enqueue(
        WorldTransaction::FromMechanismCommands({
            MechanismCommand::SetField(
                instanceId,
                counterSlot,
                MechanismValue(std::int64_t{4})
            )
        }),
        3
    );
    if (counts.command != commandCount
        || !runtime.RunTick(3)
        || counts.command != commandCount + 1
        || counts.scheduledEvent != 1
        || runtime.LastCommandAlgorithms().CompletedCount() != 1
        || runtime.Snapshot().Find(instanceId)->values[counterSlot.value]
            != MechanismValue(std::int64_t{5}))
    {
        std::cerr << "Command/Tick Algorithm execution mismatch\n";
        return 13;
    }

    std::cout
        << "Versioned Capability and Algorithm Runtime: passed (create "
        << counts.create
        << ", tick "
        << counts.tick
        << ", event "
        << counts.event
        << ", command "
        << counts.command
        << ")\n";
    return 0;
}
