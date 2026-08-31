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
#include "runtime_persistence.hpp"
#include "runtime_save_codec.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"

// Guard for the threading contract (memo section 3.9 / FROZEN_CONTRACTS.md
// section 4).
//
// The contract says algorithm dispatch may run on worker threads because
// determinism is guaranteed by construction, not by scheduling: each result is
// written to the slot fixed by its position in snapshot enumeration order, so
// which invocation finishes first cannot matter. That was frozen as a claim
// about code that did not exist -- dispatch appended results with push_back
// inside the filtering loop, which welded execution order to slot order.
//
// Dispatch is now two-phase (plan in enumeration order, then fill slot i from
// plan[i]), and this probe runs the whole world twice: once filling slots in
// plan order, once in reverse. Every authoritative byte must be identical --
// the save image and the Fact Stream, the latter encoded with the same
// RuntimeSaveCodec::EncodeFactStream that produces the replay checksum.
//
// What this does NOT cover: memory safety under genuinely concurrent
// execution. The 1-vs-N comparison probe still has to land with the worker
// pool. What it does cover is the failure mode that would make that pool
// unsafe -- an invocation observing state another invocation in the same stage
// produced, or a slot assignment that depends on execution order.
//
// The fixture is built to make reversal bite rather than to be tidy:
//
// - Two Definitions of the same Type with different Algorithms, so the plan
//   interleaves them (ids are hashes, so enumeration order is not spawn order).
// - Every `contender` instance advances the SAME RNG Stream on every Tick.
//   They all read the same `expectedDrawCount` from the immutable snapshot, so
//   exactly one commit succeeds and the rest are rejected -- this is the
//   example the memo itself uses to justify the contract. Which instance wins
//   must be decided by slot order, never by who ran first.
// - Scheduled events reach the Event stage, which plans a broadcast of
//   (events x eligible instances).
//
// A coverage note that has to stay honest. This fixture used to produce 21661
// facts over five ticks, because every committed transaction's audit event was
// fed back into the algorithm event queue and amplified. That feedback was
// removed on 2026-08-31 (memo section 3.9) and the same fixture now produces
// 105. The probe still proves what it claims -- reversing the fill order must
// not move a byte -- but its Event-stage plan is now small, so the stage that
// once dominated the comparison barely exercises it. Widening that coverage
// needs scheduled events authored into the fixture, not a return of the
// feedback loop.

namespace
{
using namespace dillen;
using namespace dillen::kernel;

constexpr std::uint32_t kInstancesPerDefinition = 3;
constexpr std::uint64_t kTicks = 5;

struct RunOutcome
{
    std::vector<std::uint8_t> save;
    std::uint64_t saveChecksum = 0;
    std::vector<std::uint8_t> factStream;
    std::uint64_t factStreamChecksum = 0;
    std::size_t factCount = 0;
    std::size_t instanceCount = 0;
    std::uint64_t rngDrawCount = 0;
    std::int64_t counterTotal = 0;
    bool ok = false;
    const char* failure = "";
};

AlgorithmInstructionDefinition AdvanceRng(RngStreamId stream)
{
    AlgorithmInstructionDefinition instruction;
    instruction.kind = AlgorithmInstructionKind::AdvanceRngStream;
    instruction.rngStream = stream;
    instruction.rngCount = 1;
    return instruction;
}

AlgorithmInstructionDefinition ScheduleSelfEvent(AlgorithmEventTypeId type)
{
    AlgorithmInstructionDefinition instruction;
    instruction.kind = AlgorithmInstructionKind::ScheduleEvent;
    instruction.eventType = type;
    instruction.dueTickOffset = 2;
    instruction.priority = 0;
    instruction.payload = MechanismValue(std::int64_t{1});
    return instruction;
}

AlgorithmDescriptor MakeAlgorithm(
    const std::string& name,
    AlgorithmEventTypeId eventType,
    const RngStreamId* contendedStream,
    std::int64_t tickIncrement,
    std::int64_t eventIncrement
)
{
    AlgorithmDescriptor algorithm;
    algorithm.id = StableAlgorithmId(name);
    algorithm.canonicalName = name;
    algorithm.version = 1;
    algorithm.entryPoints = AlgorithmEntryPoint::Create
        | AlgorithmEntryPoint::Tick
        | AlgorithmEntryPoint::Event;
    algorithm.deterministic = true;
    algorithm.executionPolicy.instructionBudget = 16;

    std::vector<AlgorithmInstructionDefinition> create = {
        AlgorithmInstructionDefinition::TransitionLifecycle(
            MechanismLifecycleState::Active
        ),
        ScheduleSelfEvent(eventType)
    };
    std::vector<AlgorithmInstructionDefinition> tickStage = {
        AlgorithmInstructionDefinition::AddField(
            "counter",
            MechanismValue(tickIncrement)
        )
    };
    if (contendedStream != nullptr)
    {
        tickStage.push_back(AdvanceRng(*contendedStream));
    }
    std::vector<AlgorithmInstructionDefinition> eventStage = {
        AlgorithmInstructionDefinition::AddField(
            "roll",
            MechanismValue(eventIncrement)
        )
    };

    algorithm.program.stages[AlgorithmEntryPoint::Create] = std::move(create);
    algorithm.program.stages[AlgorithmEntryPoint::Tick] = std::move(tickStage);
    algorithm.program.stages[AlgorithmEntryPoint::Event] =
        std::move(eventStage);
    return algorithm;
}

RunOutcome Run(runtime::DispatchExecutionOrder order)
{
    RunOutcome outcome;
    const auto fail = [&outcome](const char* reason)
    {
        outcome.failure = reason;
        return outcome;
    };

    const std::string typeName = "dillen.threadcontract.node";
    const RngStreamId contended =
        StableRngStreamId("dillen.threadcontract.shared");
    const AlgorithmEventTypeId eventType =
        StableAlgorithmEventTypeId("dillen.threadcontract.pulse");

    MechanismSchema schema;
    schema.type = StableMechanismTypeId(typeName);
    schema.canonicalName = typeName;
    schema.version = 1;
    for (const char* fieldName : {"counter", "roll"})
    {
        MechanismFieldSchema field;
        field.name = fieldName;
        field.kind = MechanismValueKind::Integer;
        field.defaultValue = MechanismValue(std::int64_t{0});
        schema.fields.push_back(field);
    }
    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added)
    {
        return fail("schema registration");
    }
    schemas.Freeze();

    const std::string contenderName = "dillen.threadcontract.contender";
    const std::string steadyName = "dillen.threadcontract.steady";
    AlgorithmRegistry algorithms;
    if (algorithms.Register(
            MakeAlgorithm(contenderName, eventType, &contended, 1, 3))
        != AlgorithmRegisterResult::Added
        || algorithms.Register(
            MakeAlgorithm(steadyName, eventType, nullptr, 2, 7))
        != AlgorithmRegisterResult::Added)
    {
        return fail("algorithm registration");
    }
    algorithms.Freeze();

    MechanismDefinitionRegistry definitions;
    std::vector<MechanismDefinitionId> definitionIds;
    const std::pair<const char*, const std::string*> definitionSpecs[] = {
        {"dillen.threadcontract.node.alpha", &contenderName},
        {"dillen.threadcontract.node.beta", &steadyName}
    };
    for (const auto& spec : definitionSpecs)
    {
        MechanismDefinition definition;
        definition.type = StableMechanismTypeId(typeName);
        definition.canonicalName = spec.first;
        definition.id = StableMechanismDefinitionId(
            definition.type,
            definition.canonicalName
        );
        definition.schemaVersion = 1;
        definition.algorithm = StableAlgorithmId(*spec.second);
        definition.algorithmVersion = 1;
        definition.source.sourceName = "thread_contract_probe";
        definitionIds.push_back(definition.id);
        if (definitions.Declare(definition, schemas, algorithms)
            != MechanismDefinitionDeclareResult::Added)
        {
            return fail("definition registration");
        }
    }
    definitions.Freeze();

    MechanismSpawnDefinitionRegistry spawns;
    std::vector<MechanismSpawnDefinitionId> spawnIds;
    for (const MechanismDefinitionId definitionId : definitionIds)
    {
        MechanismSpawnDefinition spawn;
        spawn.definition = definitionId;
        spawn.canonicalName = std::to_string(definitionId.value) + ".swarm";
        spawn.id = StableMechanismSpawnDefinitionId(
            spawn.definition,
            spawn.canonicalName
        );
        spawn.count = kInstancesPerDefinition;
        spawn.source.sourceName = "thread_contract_probe";
        spawnIds.push_back(spawn.id);
        if (spawns.Declare(spawn, definitions, schemas)
            != MechanismSpawnDeclareResult::Added)
        {
            return fail("spawn registration");
        }
    }
    spawns.Freeze();

    PackageManifest manifest;
    manifest.canonicalName = "dillen.threadcontract.package";
    manifest.id = StablePackageId(manifest.canonicalName);
    manifest.version = {1, 0, 0};
    manifest.contentDigest = std::string(64, '0');
    PackageManifestRegistry manifests;
    if (manifests.Register(manifest) != PackageManifestRegisterResult::Added)
    {
        return fail("manifest registration");
    }
    manifests.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.threadcontract.ruleset";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    PackageVersionRange range;
    range.minimumInclusive = {1, 0, 0};
    range.maximumExclusive = {2, 0, 0};
    ruleset.packages.push_back({manifest.id, manifest.canonicalName, range});
    ruleset.requiredSchemas.push_back({StableMechanismTypeId(typeName), 1});
    for (const MechanismDefinitionId definitionId : definitionIds)
    {
        ruleset.requiredDefinitions.push_back(definitionId);
    }
    for (const MechanismSpawnDefinitionId spawnId : spawnIds)
    {
        ruleset.requiredMechanismSpawns.push_back(spawnId);
    }
    ruleset.requiredAlgorithms.push_back({StableAlgorithmId(contenderName), 1});
    ruleset.requiredAlgorithms.push_back({StableAlgorithmId(steadyName), 1});

    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests, ruleset, packageLock, lockReport))
    {
        return fail("package lock");
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
        return fail("compile");
    }

    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport buildReport;
    if (!world::InitialWorldBuilder{}.Build(catalog, world, buildReport))
    {
        return fail("world build");
    }

    runtime::KernelRuntime kernelRuntime(world, catalog);
    kernelRuntime.SetAlgorithmExecutionOrder(order);

    // Created outside any algorithm so that every `contender` instance finds
    // the stream already present and the contention lands on Advance, not on
    // a duplicate Create.
    WorldTransaction seedStream;
    seedStream.commands = {
        WorldCommand::CreateRngStream(contended, 0x5eed5eed5eed5eedULL)
    };
    if (!kernelRuntime.ApplyImmediate(seedStream, 0))
    {
        return fail("rng stream seed");
    }

    for (std::uint64_t tick = 1; tick <= kTicks; ++tick)
    {
        if (!kernelRuntime.RunTick(tick))
        {
            return fail("tick");
        }
    }

    persistence::RuntimePersistenceService service;
    if (!service.Save(kernelRuntime, outcome.save))
    {
        return fail("save");
    }
    outcome.saveChecksum = persistence::StableRuntimeChecksum(outcome.save);

    // The Fact Stream is never persisted, so it has to be read live. Nothing
    // drains the queue on its own, so Pending() still holds every fact from
    // every tick.
    const std::vector<WorldEvent>& facts = kernelRuntime.Events().Pending();
    outcome.factCount = facts.size();
    if (!persistence::RuntimeSaveCodec{}.EncodeFactStream(
            facts,
            outcome.factStream))
    {
        return fail("fact stream encode");
    }
    outcome.factStreamChecksum =
        persistence::StableRuntimeChecksum(outcome.factStream);

    const MechanismQuerySnapshot& mechanisms =
        kernelRuntime.Query().Mechanisms();
    outcome.instanceCount = mechanisms.Size();
    const DeterministicRngStream* stream =
        kernelRuntime.RngSnapshot().Find(contended);
    outcome.rngDrawCount = stream == nullptr ? 0 : stream->drawCount;
    const auto counterSlot = catalog.ResolveDefinitionFieldSlot(
        definitionIds.front(),
        "counter"
    );
    if (counterSlot)
    {
        for (const auto& entry : mechanisms.All())
        {
            const MechanismValue* value = mechanisms.FindField(
                entry.first,
                *counterSlot
            );
            if (value != nullptr)
            {
                outcome.counterTotal += std::get<std::int64_t>(value->data);
            }
        }
    }

    outcome.ok = true;
    return outcome;
}

}

int main()
{
    const RunOutcome forward = Run(
        runtime::DispatchExecutionOrder::Enumeration
    );
    if (!forward.ok)
    {
        std::cerr << "thread contract probe: forward run failed at "
                  << forward.failure << '\n';
        return 1;
    }
    const RunOutcome reversed = Run(
        runtime::DispatchExecutionOrder::Reversed
    );
    if (!reversed.ok)
    {
        std::cerr << "thread contract probe: reversed run failed at "
                  << reversed.failure << '\n';
        return 2;
    }

    // A fixture that exercises nothing would pass this probe trivially, so
    // assert it actually built the shapes the contract is about before
    // comparing the two runs.
    const std::size_t expectedInstances =
        static_cast<std::size_t>(kInstancesPerDefinition) * 2U;
    if (forward.instanceCount != expectedInstances
        || forward.factCount < expectedInstances
        || forward.rngDrawCount == 0
        || forward.counterTotal == 0)
    {
        std::cerr << "thread contract probe: fixture is not exercising "
                     "dispatch (instances "
                  << forward.instanceCount << " of " << expectedInstances
                  << ", facts " << forward.factCount
                  << ", rng draws " << forward.rngDrawCount
                  << ", counter total " << forward.counterTotal << ")\n";
        return 3;
    }

    if (forward.save != reversed.save
        || forward.saveChecksum != reversed.saveChecksum)
    {
        std::cerr << "thread contract probe: reversing dispatch execution "
                     "order changed the save image ("
                  << forward.save.size() << " bytes / checksum "
                  << forward.saveChecksum << " vs "
                  << reversed.save.size() << " bytes / checksum "
                  << reversed.saveChecksum
                  << ").\nAlgorithm dispatch must write each result into the "
                     "slot fixed by snapshot enumeration order, so execution "
                     "order cannot reach authoritative state. See memo "
                     "section 3.9 and FROZEN_CONTRACTS.md section 4.\n";
        return 4;
    }

    if (forward.factStream != reversed.factStream
        || forward.factStreamChecksum != reversed.factStreamChecksum)
    {
        std::cerr << "thread contract probe: reversing dispatch execution "
                     "order changed the Fact Stream ("
                  << forward.factCount << " facts / checksum "
                  << forward.factStreamChecksum << " vs "
                  << reversed.factCount << " facts / checksum "
                  << reversed.factStreamChecksum
                  << ").\nThe save image matched, so authoritative state "
                     "survived but event publication order did not -- that "
                     "still shifts every deterministic Replay Checksum.\n";
        return 5;
    }

    std::cout << "thread contract probe: passed ("
              << forward.instanceCount << " instances x " << kTicks
              << " ticks; enumeration and reversed dispatch agree on "
              << forward.save.size() << " save bytes and "
              << forward.factCount << " facts; " << forward.rngDrawCount
              << " contended RNG draws committed)\n";
    return 0;
}
