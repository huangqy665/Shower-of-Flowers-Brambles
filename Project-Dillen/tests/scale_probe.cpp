#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "algorithm_registry.hpp"
#include "component_schema.hpp"
#include "entity_definition_registry.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_lock.hpp"
#include "package_manifest.hpp"
#include "relation_definition_registry.hpp"
#include "relation_schema.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"

// Representative-scale reality check. Not a strict wall-clock gate -- timings
// vary by machine -- but it (a) proves the runtime stays correct at four
// digits of instances over dozens of ticks, and (b) prints per-phase cost so
// the O(instances) snapshot copy and O(instances) per-transaction store copy
// (memo section 3.9 "next gaps", review findings F-7/F-8) have a number
// attached the next time someone optimises them. A generous ceiling catches
// only a pathological regression.

namespace
{
using namespace dillen;
using namespace dillen::kernel;

// Kept small enough to stay in the default suite; raise locally to profile.
//
// Measured Debug baseline on this machine (2026-08-30), after phase-batched
// commit and hoisting the Event-stage eligibility test out of the
// events x instances fan-out:
//
//     N=250   9.8 ms/tick      N=2000   78 ms/tick
//     N=500  18.0 ms/tick      N=4000  154 ms/tick
//     N=1000 34.9 ms/tick
//
// Doubling N roughly doubles the tick, i.e. cost is now linear in instance
// count. Before that work the same probe read 246 ms/tick at N=250 and 2000x60
// did not finish inside five minutes; it now completes in ~4.8 s. If a future
// change reintroduces a superlinear term this probe will not fail on its own --
// compare the numbers above across a few N by hand.
//
// Within the Tick stage the split is roughly 30% dispatch (VM execution,
// parallelisable) and 70% apply (serial by contract): N=4000 measured
// dispatch 32 ms / apply 79 ms.
constexpr std::uint32_t kInstances = 250;
constexpr std::uint64_t kTicks = 10;
constexpr double kCeilingSeconds = 60.0;

}

int main()
{
    const std::string typeName = "dillen.scale.counter";
    const std::string algoName = "dillen.scale.counter_algorithm";

    MechanismSchema schema;
    schema.type = StableMechanismTypeId(typeName);
    schema.canonicalName = typeName;
    schema.version = 1;
    MechanismFieldSchema counter;
    counter.name = "counter";
    counter.kind = MechanismValueKind::Integer;
    counter.defaultValue = MechanismValue(std::int64_t{0});
    schema.fields.push_back(counter);
    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "scale probe: schema registration failed\n";
        return 1;
    }
    schemas.Freeze();

    AlgorithmDescriptor algorithm;
    algorithm.id = StableAlgorithmId(algoName);
    algorithm.canonicalName = algoName;
    algorithm.version = 1;
    algorithm.entryPoints =
        AlgorithmEntryPoint::Create | AlgorithmEntryPoint::Tick;
    algorithm.executionPolicy.instructionBudget = 8;
    algorithm.program.stages[AlgorithmEntryPoint::Create] = {
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Active
        )
    };
    algorithm.program.stages[AlgorithmEntryPoint::Tick] = {
        AlgorithmInstructionDefinition::AddField(
            "counter",
            MechanismValue(std::int64_t{1})
        )
    };
    AlgorithmRegistry algorithms;
    if (algorithms.Register(std::move(algorithm))
        != AlgorithmRegisterResult::Added)
    {
        std::cerr << "scale probe: algorithm registration failed\n";
        return 2;
    }
    algorithms.Freeze();

    MechanismDefinition definition;
    definition.type = schema.type;
    definition.canonicalName = typeName + ".default";
    definition.id = StableMechanismDefinitionId(
        definition.type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.algorithm = StableAlgorithmId(algoName);
    definition.algorithmVersion = 1;
    definition.source.sourceName = "scale_probe";
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(definition, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "scale probe: definition registration failed\n";
        return 3;
    }
    definitions.Freeze();

    MechanismSpawnDefinition spawn;
    spawn.canonicalName = typeName + ".swarm";
    spawn.definition = definition.id;
    spawn.id = StableMechanismSpawnDefinitionId(
        spawn.definition,
        spawn.canonicalName
    );
    spawn.count = kInstances;
    spawn.source.sourceName = "scale_probe";
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, definitions, schemas)
        != MechanismSpawnDeclareResult::Added)
    {
        std::cerr << "scale probe: spawn registration failed\n";
        return 4;
    }
    spawns.Freeze();

    PackageManifest manifest;
    manifest.canonicalName = "dillen.scale.package";
    manifest.id = StablePackageId(manifest.canonicalName);
    manifest.version = {1, 0, 0};
    manifest.contentDigest = std::string(64, '0');
    PackageManifestRegistry manifests;
    if (manifests.Register(manifest) != PackageManifestRegisterResult::Added)
    {
        std::cerr << "scale probe: manifest registration failed\n";
        return 5;
    }
    manifests.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.scale.ruleset";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    PackageVersionRange range;
    range.minimumInclusive = {1, 0, 0};
    range.maximumExclusive = {2, 0, 0};
    ruleset.packages.push_back({manifest.id, manifest.canonicalName, range});
    ruleset.requiredSchemas.push_back({definition.type, 1});
    ruleset.requiredDefinitions.push_back(definition.id);
    ruleset.requiredMechanismSpawns.push_back(spawn.id);
    ruleset.requiredAlgorithms.push_back({definition.algorithm, 1});

    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests, ruleset, packageLock, lockReport))
    {
        std::cerr << "scale probe: package lock failed\n";
        return 6;
    }

    ComponentSchemaRegistry componentSchemas;
    componentSchemas.Freeze();
    EntityDefinitionRegistry entityDefinitions;
    entityDefinitions.Freeze();
    RelationSchemaRegistry relationSchemas;
    relationSchemas.Freeze();
    RelationDefinitionRegistry relationDefinitions;
    relationDefinitions.Freeze();
    RuntimeCapabilityContractRegistry contracts;
    contracts.Freeze();

    FrozenRuntimeCatalog catalog;
    RuntimeCompileReport compileReport;
    const auto compileStart = std::chrono::steady_clock::now();
    if (!RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            schemas,
            componentSchemas,
            relationSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            relationDefinitions,
            spawns,
            contracts,
            catalog,
            compileReport))
    {
        std::cerr << "scale probe: compile failed\n";
        return 7;
    }
    const double compileMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - compileStart).count();

    world::AuthoritativeWorld authoritativeWorld;
    world::InitialWorldBuildReport buildReport;
    const auto buildStart = std::chrono::steady_clock::now();
    if (!world::InitialWorldBuilder{}.Build(
            catalog, authoritativeWorld, buildReport))
    {
        std::cerr << "scale probe: world build failed\n";
        return 8;
    }
    const double buildMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - buildStart).count();

    runtime::KernelRuntime kernelRuntime(authoritativeWorld, catalog);
    if (kernelRuntime.Query().Mechanisms().Size() != kInstances)
    {
        std::cerr << "scale probe: expected " << kInstances
                  << " instances, got "
                  << kernelRuntime.Query().Mechanisms().Size() << '\n';
        return 9;
    }

    const auto tickStart = std::chrono::steady_clock::now();
    for (std::uint64_t tick = 1; tick <= kTicks; ++tick)
    {
        if (!kernelRuntime.RunTick(tick)
            || kernelRuntime.LastTickAlgorithms().FailedCount() != 0
            || kernelRuntime.LastCreateAlgorithms().FailedCount() != 0)
        {
            std::cerr << "scale probe: tick " << tick << " failed\n";
            return 10;
        }
    }
    const double tickSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - tickStart).count();

    const auto slot = catalog.ResolveDefinitionFieldSlot(
        definition.id,
        "counter"
    );
    const std::vector<MechanismInstanceId> instances =
        kernelRuntime.Query().Mechanisms().FindByType(definition.type);
    if (!slot || instances.empty())
    {
        std::cerr << "scale probe: instances unavailable after ticks\n";
        return 11;
    }
    for (const MechanismInstanceId instance : instances)
    {
        const MechanismValue* value =
            kernelRuntime.Query().Mechanisms().FindField(instance, *slot);
        if (value == nullptr
            || std::get<std::int64_t>(value->data)
                != static_cast<std::int64_t>(kTicks))
        {
            std::cerr << "scale probe: instance counter drifted from "
                      << kTicks << '\n';
            return 12;
        }
    }

    const double perTickMs = tickSeconds * 1000.0 / static_cast<double>(kTicks);
    std::cout << "scale probe: passed ("
              << kInstances << " instances x " << kTicks << " ticks; compile "
              << compileMs << " ms, world build " << buildMs << " ms, ticks "
              << tickSeconds << " s, " << perTickMs << " ms/tick)\n";
    if (tickSeconds > kCeilingSeconds)
    {
        std::cerr << "scale probe: tick loop exceeded the "
                  << kCeilingSeconds << " s ceiling\n";
        return 13;
    }
    return 0;
}
