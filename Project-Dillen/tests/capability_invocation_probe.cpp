#include <algorithm>
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
#include "runtime_persistence.hpp"

// Proves the cross-mechanism Capability invocation loop: a "pump" mechanism
// pushes a value to whoever provides the `market_pressure` contract, and a
// "sink" mechanism answers it. Neither algorithm, definition or schema names
// the other mechanism's type or any instance id -- the only shared symbol is
// the Capability Contract.

namespace
{
using namespace dillen;
using namespace dillen::kernel;

constexpr std::int64_t kPayload = 5;

// `targetType`, when set, adds an optional `preferred` role holding one
// mechanism-instance reference of that type -- the role `invoke_capability
// target_role` names.
MechanismSchema CounterSchema(
    const std::string& name,
    std::uint64_t targetType = 0
)
{
    MechanismSchema schema;
    schema.type = StableMechanismTypeId(name);
    schema.canonicalName = name;
    schema.version = 1;
    MechanismFieldSchema level;
    level.name = "level";
    level.kind = MechanismValueKind::Integer;
    level.defaultValue = MechanismValue(std::int64_t{0});
    schema.fields.push_back(level);
    if (targetType != 0)
    {
        MechanismRoleSchema preferred;
        preferred.name = "preferred";
        preferred.referenceKind = MechanismReferenceKind::MechanismInstance;
        preferred.referenceType = targetType;
        preferred.minimumCount = 0;
        preferred.maximumCount = 1;
        schema.roles.push_back(preferred);
    }
    return schema;
}

std::int64_t Level(
    const runtime::KernelRuntime& kernelRuntime,
    MechanismInstanceId instance,
    MechanismFieldSlotId slot
)
{
    const MechanismValue* value =
        kernelRuntime.Query().Mechanisms().FindField(instance, slot);
    return value != nullptr
        ? std::get<std::int64_t>(value->data)
        : -1;
}

// Builds a Frozen Runtime Catalog for the given spawn set. `withSink` controls
// whether the provider mechanism is spawned at all. `requestVersion` is the
// contract version the pump asks for (0 = leave the range open); `provideMin`
// is the lowest version the sink is willing to provide; `manifestVersion` is
// the single contract version the locked package declares. Both sides must
// resolve -- through the Package Lock, not the raw registry -- to a version
// the package owns, or the compile fails.
bool BuildCatalog(
    bool withSink,
    FrozenRuntimeCatalog& catalog,
    std::uint32_t requestVersion = 0,
    std::uint32_t provideMin = 1,
    std::uint32_t manifestVersion = 2,
    bool scriptPump = false,
    bool targetRole = false,
    bool secondProvider = false
)
{
    const std::string capabilityName = "dillen.test.market_pressure";
    const std::string pumpName = "dillen.test.pump";
    const std::string sinkName = "dillen.test.sink";
    const std::string pumpAlgoName = "dillen.test.pump_algorithm";
    const std::string sinkAlgoName = "dillen.test.sink_algorithm";

    RuntimeCapabilityContractRegistry contracts;
    for (std::uint32_t version : {1u, 2u})
    {
        RuntimeCapabilityContract contract;
        contract.id = StableCapabilityId(capabilityName);
        contract.canonicalName = capabilityName;
        contract.version = version;
        if (contracts.Register(std::move(contract))
            != CapabilityContractRegisterResult::Added)
        {
            return false;
        }
    }
    contracts.Freeze();

    const MechanismTypeId sinkType = StableMechanismTypeId(sinkName);
    MechanismSchemaRegistry schemas;
    if (schemas.Register(
            CounterSchema(pumpName, targetRole ? sinkType.value : 0))
            != MechanismSchemaRegisterResult::Added
        || schemas.Register(CounterSchema(sinkName))
            != MechanismSchemaRegisterResult::Added)
    {
        return false;
    }
    schemas.Freeze();

    AlgorithmDescriptor pumpAlgo;
    pumpAlgo.id = StableAlgorithmId(pumpAlgoName);
    pumpAlgo.canonicalName = pumpAlgoName;
    pumpAlgo.version = 1;
    pumpAlgo.entryPoints =
        AlgorithmEntryPoint::Create | AlgorithmEntryPoint::Tick;
    pumpAlgo.program.stages[AlgorithmEntryPoint::Create] = {
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Active
        )
    };
    AlgorithmInstructionDefinition invoke;
    invoke.kind = AlgorithmInstructionKind::InvokeCapability;
    invoke.capabilityName = capabilityName;
    invoke.payload = MechanismValue(kPayload);
    invoke.dueTickOffset = 1;
    if (requestVersion != 0)
    {
        invoke.capabilityVersions.minimumInclusive = requestVersion;
        invoke.capabilityVersions.maximumExclusive = requestVersion + 1;
    }
    if (targetRole)
    {
        invoke.targetRoleName = "preferred";
    }
    if (scriptPump)
    {
        // The pump is a Controlled Script whose only tick action is a
        // Transact-wrapped invoke_capability. Proves (a) the capability
        // reference reaches the compile closure from a script stage and
        // (b) it executes through the shared emitter.
        pumpAlgo.backend = AlgorithmBackend::Script;
        pumpAlgo.program.stages.clear();
        ControlledScriptInstructionDefinition activate;
        activate.kind =
            ControlledScriptInstructionKind::TransitionLifecycle;
        activate.lifecycle = MechanismLifecycleState::Active;
        pumpAlgo.script.stages[AlgorithmEntryPoint::Create] = {activate};
        ControlledScriptInstructionDefinition scriptInvoke;
        scriptInvoke.kind = ControlledScriptInstructionKind::Transact;
        scriptInvoke.action = invoke;
        ControlledScriptInstructionDefinition halt;
        halt.kind = ControlledScriptInstructionKind::Halt;
        pumpAlgo.script.stages[AlgorithmEntryPoint::Tick] = {
            scriptInvoke,
            halt
        };
    }
    else
    {
        pumpAlgo.program.stages[AlgorithmEntryPoint::Tick] = {invoke};
    }

    AlgorithmDescriptor sinkAlgo;
    sinkAlgo.id = StableAlgorithmId(sinkAlgoName);
    sinkAlgo.canonicalName = sinkAlgoName;
    sinkAlgo.version = 1;
    sinkAlgo.entryPoints =
        AlgorithmEntryPoint::Create | AlgorithmEntryPoint::Event;
    sinkAlgo.program.stages[AlgorithmEntryPoint::Create] = {
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Active
        )
    };
    AlgorithmInstructionDefinition grow;
    grow.kind = AlgorithmInstructionKind::AddField;
    grow.field = "level";
    grow.operandFromPayload = true;
    AlgorithmConditionDefinition invoked;
    invoked.kind = AlgorithmConditionKind::ScheduledEventTypeEquals;
    invoked.eventType = CapabilityDeliveryEventType(capabilityName);
    grow.conditions.push_back(invoked);
    sinkAlgo.program.stages[AlgorithmEntryPoint::Event] = {grow};

    AlgorithmRegistry algorithms;
    if (algorithms.Register(std::move(pumpAlgo))
            != AlgorithmRegisterResult::Added
        || algorithms.Register(std::move(sinkAlgo))
            != AlgorithmRegisterResult::Added)
    {
        return false;
    }
    algorithms.Freeze();

    const auto makeDefinition = [](
        const std::string& mechanismName,
        const std::string& algorithmName,
        std::vector<CapabilityProvisionDeclaration> provided)
        -> MechanismDefinition
    {
        MechanismDefinition definition;
        definition.type = StableMechanismTypeId(mechanismName);
        definition.canonicalName = mechanismName + ".default";
        definition.id = StableMechanismDefinitionId(
            definition.type,
            definition.canonicalName
        );
        definition.schemaVersion = 1;
        definition.algorithm = StableAlgorithmId(algorithmName);
        definition.algorithmVersion = 1;
        definition.providedCapabilities = std::move(provided);
        definition.source.sourceName = "capability_invocation_probe";
        definition.source.virtualPath = "tests/capability_invocation";
        return definition;
    };
    CapabilityProvisionDeclaration sinkProvides;
    sinkProvides.capabilityName = capabilityName;
    sinkProvides.versions.minimumInclusive = provideMin;
    MechanismDefinition pumpDef =
        makeDefinition(pumpName, pumpAlgoName, {});
    MechanismDefinition sinkDef = makeDefinition(
        sinkName,
        sinkAlgoName,
        {sinkProvides}
    );
    if (targetRole)
    {
        // Bind the pump's `preferred` role to the FIRST of the two sink
        // instances. Instance ids are stable functions of (definition,
        // ordinal), so the binding is authorable without running the world.
        pumpDef.roles["preferred"] = {{
            MechanismReferenceKind::MechanismInstance,
            sinkType.value,
            StableMechanismInstanceId(sinkDef.id, 0).value
        }};
    }
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(pumpDef, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added
        || definitions.Declare(sinkDef, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added)
    {
        return false;
    }
    definitions.Freeze();

    const auto makeSpawn = [](
        const MechanismDefinitionId definition,
        std::uint32_t count) -> MechanismSpawnDefinition
    {
        MechanismSpawnDefinition spawn;
        spawn.canonicalName = "spawn.initial";
        spawn.definition = definition;
        spawn.id = StableMechanismSpawnDefinitionId(
            spawn.definition,
            spawn.canonicalName
        );
        spawn.count = count;
        spawn.source.sourceName = "capability_invocation_probe";
        return spawn;
    };
    MechanismSpawnDefinition pumpSpawn = makeSpawn(pumpDef.id, 1);
    // Targeted delivery needs two providers so "only the named one grew" is
    // an actual claim rather than a tautology.
    MechanismSpawnDefinition sinkSpawn =
        makeSpawn(sinkDef.id, targetRole ? 2 : 1);
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(pumpSpawn, definitions, schemas)
        != MechanismSpawnDeclareResult::Added)
    {
        return false;
    }
    if (withSink
        && spawns.Declare(sinkSpawn, definitions, schemas)
            != MechanismSpawnDeclareResult::Added)
    {
        return false;
    }
    spawns.Freeze();

    PackageManifest manifest;
    manifest.canonicalName = "dillen.test.capability_package";
    manifest.id = StablePackageId(manifest.canonicalName);
    manifest.version = {1, 0, 0};
    manifest.contentDigest = std::string(64, '0');
    // The composed Ruleset may only bind Capability versions its locked
    // packages declare providing. A manifest owns one version per contract.
    manifest.providedCapabilities.push_back(
        {StableCapabilityId(capabilityName), capabilityName, manifestVersion});
    PackageManifest rival;
    rival.canonicalName = "dillen.test.rival_package";
    rival.id = StablePackageId(rival.canonicalName);
    rival.version = {1, 0, 0};
    rival.contentDigest = std::string(64, '0');
    rival.providedCapabilities.push_back(
        {StableCapabilityId(capabilityName), capabilityName, 1});
    PackageManifestRegistry manifests;
    if (manifests.Register(manifest) != PackageManifestRegisterResult::Added)
    {
        return false;
    }
    if (secondProvider
        && manifests.Register(rival) != PackageManifestRegisterResult::Added)
    {
        return false;
    }
    manifests.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.capability_ruleset";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    PackageVersionRange range;
    range.minimumInclusive = {1, 0, 0};
    range.maximumExclusive = {2, 0, 0};
    ruleset.packages.push_back({manifest.id, manifest.canonicalName, range});
    if (secondProvider)
    {
        ruleset.packages.push_back({rival.id, rival.canonicalName, range});
    }
    ruleset.requiredSchemas.push_back({pumpDef.type, 1});
    ruleset.requiredDefinitions.push_back(pumpDef.id);
    ruleset.requiredMechanismSpawns.push_back(pumpSpawn.id);
    ruleset.requiredAlgorithms.push_back({pumpDef.algorithm, 1});
    if (withSink)
    {
        ruleset.requiredSchemas.push_back({sinkDef.type, 1});
        ruleset.requiredDefinitions.push_back(sinkDef.id);
        ruleset.requiredMechanismSpawns.push_back(sinkSpawn.id);
        ruleset.requiredAlgorithms.push_back({sinkDef.algorithm, 1});
    }

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

    ComponentSchemaRegistry componentSchemas;
    componentSchemas.Freeze();
    EntityDefinitionRegistry entityDefinitions;
    entityDefinitions.Freeze();
    RelationSchemaRegistry relationSchemas;
    relationSchemas.Freeze();
    RelationDefinitionRegistry relationDefinitions;
    relationDefinitions.Freeze();

    RuntimeCompileReport report;
    return RuntimeCompiler{}.Compile(
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
        report
    );
}

bool CheckClosedLoop(
    std::uint32_t requestVersion = 0,
    std::uint32_t provideMin = 1,
    bool expectDelivery = true,
    bool scriptPump = false
)
{
    FrozenRuntimeCatalog catalog;
    if (!BuildCatalog(true, catalog, requestVersion, provideMin, 2, scriptPump)
        || catalog.CapabilityCount() < 1)
    {
        std::cerr << "  closed loop: catalog build failed"
                  << (scriptPump ? " (script pump)" : "") << '\n';
        return false;
    }

    world::AuthoritativeWorld authoritativeWorld;
    world::InitialWorldBuildReport buildReport;
    if (!world::InitialWorldBuilder{}.Build(
            catalog,
            authoritativeWorld,
            buildReport))
    {
        std::cerr << "  closed loop: world build failed\n";
        return false;
    }

    runtime::KernelRuntime kernelRuntime(authoritativeWorld, catalog);
    const MechanismDefinitionId sinkDefinition = StableMechanismDefinitionId(
        StableMechanismTypeId("dillen.test.sink"),
        "dillen.test.sink.default"
    );
    const auto slot = catalog.ResolveDefinitionFieldSlot(
        sinkDefinition,
        "level"
    );
    const std::vector<MechanismInstanceId> sinks =
        kernelRuntime.Query().Mechanisms().FindByType(
            StableMechanismTypeId("dillen.test.sink")
        );
    if (!slot || sinks.size() != 1)
    {
        std::cerr << "  closed loop: sink not spawned\n";
        return false;
    }
    const MechanismInstanceId sink = sinks.front();

    // Tick 1: both mechanisms activate; pump emits an invocation due tick 2.
    // Tick 2: the sink answers it and grows by exactly the payload.
    if (!kernelRuntime.RunTick(1)
        || kernelRuntime.LastCreateAlgorithms().FailedCount() != 0
        || kernelRuntime.LastTickAlgorithms().FailedCount() != 0)
    {
        std::cerr << "  closed loop: tick 1 faulted\n";
        return false;
    }
    if (Level(kernelRuntime, sink, *slot) != 0)
    {
        std::cerr << "  closed loop: sink grew before any delivery\n";
        return false;
    }
    if (!kernelRuntime.RunTick(2)
        || kernelRuntime.LastEventAlgorithms().FailedCount() != 0
        || kernelRuntime.LastTickAlgorithms().FailedCount() != 0)
    {
        std::cerr << "  closed loop: tick 2 faulted\n";
        return false;
    }
    const std::int64_t afterTick2 = Level(kernelRuntime, sink, *slot);
    const std::int64_t expectedTick2 = expectDelivery ? kPayload : 0;
    if (afterTick2 != expectedTick2)
    {
        std::cerr << "  closed loop: level=" << afterTick2
                  << " expected " << expectedTick2 << '\n';
        return false;
    }
    if (!kernelRuntime.RunTick(3))
    {
        std::cerr << "  closed loop: tick 3 faulted\n";
        return false;
    }
    const std::int64_t afterTick3 = Level(kernelRuntime, sink, *slot);
    const std::int64_t expectedTick3 = expectDelivery ? 2 * kPayload : 0;
    if (afterTick3 != expectedTick3)
    {
        std::cerr << "  closed loop: level=" << afterTick3
                  << " expected " << expectedTick3 << '\n';
        return false;
    }
    return true;
}

// 9b: a version request or provision range that the locked package does not
// own must be a compile-time rejection, not a silent fall-through.
bool CheckVersionRejection(
    std::uint32_t requestVersion,
    std::uint32_t provideMin,
    const char* label
)
{
    FrozenRuntimeCatalog catalog;
    if (BuildCatalog(true, catalog, requestVersion, provideMin))
    {
        std::cerr << "  version rejection (" << label
                  << "): compile unexpectedly succeeded\n";
        return false;
    }
    return true;
}

// A Capability Contract identity provided by two locked packages is a conflict:
// MechanismDefinitionSource carries only a source-layer name and
// AlgorithmDescriptor carries no origin, so nothing can say which package a
// given `provides_capabilities` or `invoke_capability` meant. Compile must
// reject rather than silently pick the highest version across packages.
bool CheckAmbiguousProviderRejected()
{
    FrozenRuntimeCatalog catalog;
    if (BuildCatalog(
            true, catalog, 0, 1, 2, false, false, /*secondProvider*/ true))
    {
        std::cerr << "  ambiguous provider: compile unexpectedly succeeded\n";
        return false;
    }
    return true;
}

// 9a end to end: role binding -> `invoke_capability target_role = preferred`
// -> only the named provider receives the delivery. Two sinks provide the same
// contract; the pump's role names the first. DSL-level role binding, compiler
// role-slot resolution, VM role read and Executor targeted fan-out all run.
bool CheckTargetedDelivery()
{
    FrozenRuntimeCatalog catalog;
    if (!BuildCatalog(
            true, catalog, 0, 1, 2, /*scriptPump*/ false, /*targetRole*/ true))
    {
        std::cerr << "  targeted: catalog build failed\n";
        return false;
    }
    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport buildReport;
    if (!world::InitialWorldBuilder{}.Build(catalog, world, buildReport))
    {
        std::cerr << "  targeted: world build failed\n";
        return false;
    }
    runtime::KernelRuntime kernelRuntime(world, catalog);
    const MechanismDefinitionId sinkDefinition = StableMechanismDefinitionId(
        StableMechanismTypeId("dillen.test.sink"),
        "dillen.test.sink.default"
    );
    const auto slot = catalog.ResolveDefinitionFieldSlot(
        sinkDefinition,
        "level"
    );
    std::vector<MechanismInstanceId> sinks =
        kernelRuntime.Query().Mechanisms().FindByType(
            StableMechanismTypeId("dillen.test.sink")
        );
    if (!slot || sinks.size() != 2)
    {
        std::cerr << "  targeted: expected two sinks, got " << sinks.size()
                  << '\n';
        return false;
    }
    const MechanismInstanceId named = StableMechanismInstanceId(
        sinkDefinition,
        0
    );
    const auto other = std::find_if(
        sinks.begin(),
        sinks.end(),
        [named](MechanismInstanceId id) { return id != named; }
    );
    if (std::find(sinks.begin(), sinks.end(), named) == sinks.end()
        || other == sinks.end())
    {
        std::cerr << "  targeted: role-named instance is not in the world\n";
        return false;
    }
    if (!kernelRuntime.RunTick(1) || !kernelRuntime.RunTick(2))
    {
        std::cerr << "  targeted: ticks faulted\n";
        return false;
    }
    if (kernelRuntime.LastTickAlgorithms().FailedCount() != 0
        || kernelRuntime.LastEventAlgorithms().FailedCount() != 0)
    {
        std::cerr << "  targeted: an algorithm faulted\n";
        return false;
    }
    const std::int64_t namedLevel = Level(kernelRuntime, named, *slot);
    const std::int64_t otherLevel = Level(kernelRuntime, *other, *slot);
    if (namedLevel != kPayload || otherLevel != 0)
    {
        std::cerr << "  targeted: named=" << namedLevel << " other="
                  << otherLevel << " (want " << kPayload << " and 0)\n";
        return false;
    }
    return true;
}

// Save v5: a queued InvokeCapabilityCommand carrying an explicit targetInstance
// and capabilityVersion must survive a Save -> Load -> Save byte-identical, and
// the reloaded queue must still hold those two fields.
bool CheckSaveRoundTrip()
{
    const std::string capabilityName = "dillen.test.market_pressure";
    FrozenRuntimeCatalog catalog;
    if (!BuildCatalog(true, catalog))
    {
        std::cerr << "  save round trip: catalog build failed\n";
        return false;
    }
    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport buildReport;
    if (!world::InitialWorldBuilder{}.Build(catalog, world, buildReport))
    {
        return false;
    }
    runtime::KernelRuntime kernelRuntime(world, catalog);
    const std::vector<MechanismInstanceId> sinks =
        kernelRuntime.Query().Mechanisms().FindByType(
            StableMechanismTypeId("dillen.test.sink")
        );
    if (sinks.size() != 1)
    {
        return false;
    }
    kernel::WorldTransaction queued;
    queued.commands.push_back(WorldCommand::InvokeCapability(
        StableCapabilityId(capabilityName),
        CapabilityDeliveryEventType(capabilityName),
        /*dueTick*/ 5,
        /*priority*/ 2,
        MechanismValue(std::int64_t{7}),
        /*targetInstance*/ sinks.front(),
        /*capabilityVersion*/ 2
    ));
    kernelRuntime.Enqueue(std::move(queued), /*notBeforeTick*/ 5);

    persistence::RuntimePersistenceService persistence;
    std::vector<std::uint8_t> first;
    if (!persistence.Save(kernelRuntime, first))
    {
        std::cerr << "  save round trip: first save failed\n";
        return false;
    }

    world::AuthoritativeWorld restoredWorld;
    if (!world::InitialWorldBuilder{}.Build(
            catalog, restoredWorld, buildReport))
    {
        return false;
    }
    runtime::KernelRuntime restored(restoredWorld, catalog);
    if (!persistence.Load(restored, first))
    {
        std::cerr << "  save round trip: load failed\n";
        return false;
    }
    std::vector<std::uint8_t> second;
    if (!persistence.Save(restored, second) || first != second)
    {
        std::cerr << "  save round trip: bytes drifted across load\n";
        return false;
    }
    const auto& pending = restored.Commands().Pending();
    if (pending.size() != 1 || pending.front().transaction.commands.size() != 1)
    {
        std::cerr << "  save round trip: queued command lost\n";
        return false;
    }
    const auto* reloaded = std::get_if<kernel::InvokeCapabilityCommand>(
        &pending.front().transaction.commands.front().payload
    );
    if (reloaded == nullptr
        || reloaded->targetInstance != sinks.front()
        || reloaded->capabilityVersion != 2
        || reloaded->dueTick != 5)
    {
        std::cerr << "  save round trip: v5 fields not preserved\n";
        return false;
    }
    return true;
}

bool CheckNoProviderIsHarmless()
{
    FrozenRuntimeCatalog catalog;
    if (!BuildCatalog(false, catalog))
    {
        std::cerr << "  no provider: catalog build failed\n";
        return false;
    }
    world::AuthoritativeWorld authoritativeWorld;
    world::InitialWorldBuildReport buildReport;
    if (!world::InitialWorldBuilder{}.Build(
            catalog,
            authoritativeWorld,
            buildReport))
    {
        std::cerr << "  no provider: world build failed\n";
        return false;
    }
    runtime::KernelRuntime kernelRuntime(authoritativeWorld, catalog);
    // The pump invokes every tick with nobody providing the contract. This
    // must not fault the pump or the transaction -- an invocation to zero
    // providers is simply delivered nowhere.
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
    {
        if (!kernelRuntime.RunTick(tick)
            || kernelRuntime.LastTickAlgorithms().FailedCount() != 0
            || kernelRuntime.LastCreateAlgorithms().FailedCount() != 0)
        {
            std::cerr << "  no provider: tick " << tick << " faulted\n";
            return false;
        }
    }
    return true;
}

}

int main()
{
    // Open request, provider open from v1: both sides resolve through the
    // Package Lock to the one owned version (v2) and the payload flows.
    if (!CheckClosedLoop())
    {
        std::cerr << "Capability invocation probe: closed-loop case failed\n";
        return 1;
    }
    // Explicit request for v2 with a provider floor of v2: still delivers.
    if (!CheckClosedLoop(/*requestVersion*/ 2, /*provideMin*/ 2, true))
    {
        std::cerr << "Capability invocation probe: explicit-v2 case failed\n";
        return 2;
    }
    // Same loop, but the pump is a Controlled Script -- the capability must
    // reach the compile closure from a script stage (regression guard for the
    // BuildCompileSelection script-stage walk) and deliver through the shared
    // emitter.
    if (!CheckClosedLoop(0, 1, true, /*scriptPump*/ true))
    {
        std::cerr
            << "Capability invocation probe: controlled-script pump failed\n";
        return 2;
    }
    // Consumer pins v1 but the locked package owns only v2: compile rejection,
    // not a silent bind to v2 nor a runtime no-op.
    if (!CheckVersionRejection(1, 1, "consumer pins v1, package owns v2"))
    {
        return 3;
    }
    // Consumer pins a version no contract defines.
    if (!CheckVersionRejection(3, 1, "consumer pins absent v3"))
    {
        return 4;
    }
    // Provider's minimum version floor sits above the owned version.
    if (!CheckVersionRejection(0, 3, "provider floor above owned version"))
    {
        return 5;
    }
    if (!CheckNoProviderIsHarmless())
    {
        std::cerr << "Capability invocation probe: no-provider case failed\n";
        return 6;
    }
    if (!CheckAmbiguousProviderRejected())
    {
        return 7;
    }
    if (!CheckTargetedDelivery())
    {
        std::cerr << "Capability invocation probe: targeted delivery failed\n";
        return 8;
    }
    if (!CheckSaveRoundTrip())
    {
        std::cerr << "Capability invocation probe: save round trip failed\n";
        return 9;
    }
    std::cout << "Capability invocation probe: passed\n";
    return 0;
}
