#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "algorithm_registry.hpp"
#include "authoring_parser.hpp"
#include "component_schema.hpp"
#include "controlled_script_vm.hpp"
#include "entity_definition_registry.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_manifest.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"
#include "runtime_persistence.hpp"
#include "diagnostic.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"

namespace {

struct Fixture
{
    dillen::kernel::FrozenRuntimeCatalog catalog;
    dillen::kernel::MechanismDefinitionId definition;
    dillen::kernel::MechanismInstanceId instance;
    dillen::kernel::MechanismFieldSlotId counter;
};

bool BuildFixture(Fixture& output, bool transactTick = false)
{
    using namespace dillen::kernel;
    const std::string typeName = "dillen.test.controlled_script";
    const MechanismTypeId type = StableMechanismTypeId(typeName);

    MechanismSchema schema;
    schema.type = type;
    schema.canonicalName = typeName;
    schema.version = 1;
    MechanismFieldSchema counter;
    counter.name = "counter";
    counter.kind = MechanismValueKind::Integer;
    counter.defaultValue = MechanismValue(std::int64_t{0});
    schema.fields.push_back(counter);
    MechanismSchemaRegistry schemas;
    if (schemas.Register(std::move(schema))
        != MechanismSchemaRegisterResult::Added) return false;
    schemas.Freeze();

    const std::string algorithmName =
        "dillen.test.controlled_script.algorithm";
    AlgorithmDescriptor algorithm;
    algorithm.id = StableAlgorithmId(algorithmName);
    algorithm.canonicalName = algorithmName;
    algorithm.version = 1;
    algorithm.backend = AlgorithmBackend::Script;
    algorithm.entryPoints = AlgorithmEntryPoint::Create
        | AlgorithmEntryPoint::Tick;
    algorithm.executionPolicy.instructionBudget = 4;
    algorithm.executionPolicy.scriptSliceInstructionBudget = 2;
    algorithm.executionPolicy.scriptMemoryLimitBytes = 64;
    algorithm.script.state.push_back({
        "iteration",
        MechanismValue(std::int64_t{0})
    });
    algorithm.script.state.push_back({
        "scratch",
        MechanismValue(std::string{})
    });
    ControlledScriptInstructionDefinition activate;
    activate.kind = ControlledScriptInstructionKind::TransitionLifecycle;
    activate.lifecycle = MechanismLifecycleState::Active;
    algorithm.script.stages[AlgorithmEntryPoint::Create] = {activate};

    ControlledScriptInstructionDefinition addState;
    addState.kind = ControlledScriptInstructionKind::AddState;
    addState.state = "iteration";
    addState.operand = MechanismValue(std::int64_t{1});
    ControlledScriptInstructionDefinition addField;
    addField.kind = ControlledScriptInstructionKind::AddField;
    addField.field = "counter";
    addField.operand = MechanismValue(std::int64_t{1});
    ControlledScriptInstructionDefinition branch;
    branch.kind = ControlledScriptInstructionKind::JumpIfStateEquals;
    branch.state = "iteration";
    branch.operand = MechanismValue(std::int64_t{3});
    branch.targetInstruction = 4;
    ControlledScriptInstructionDefinition loop;
    loop.kind = ControlledScriptInstructionKind::Jump;
    loop.targetInstruction = 0;
    ControlledScriptInstructionDefinition halt;
    halt.kind = ControlledScriptInstructionKind::Halt;
    if (transactTick)
    {
        // A Transact instruction: conditional field mutation lowered and run
        // through the shared declarative code path. counter starts at 0, so
        // the `field_equals counter == 0` guard holds on the first tick only.
        ControlledScriptInstructionDefinition guardedAdd;
        guardedAdd.kind = ControlledScriptInstructionKind::Transact;
        guardedAdd.action = AlgorithmInstructionDefinition::AddField(
            "counter",
            MechanismValue(std::int64_t{5})
        );
        AlgorithmConditionDefinition guard;
        guard.kind = AlgorithmConditionKind::SelfFieldEquals;
        guard.field = "counter";
        guard.value = MechanismValue(std::int64_t{0});
        guardedAdd.action.conditions.push_back(guard);
        algorithm.script.stages[AlgorithmEntryPoint::Tick] = {
            guardedAdd,
            halt
        };
    }
    else
    {
        algorithm.script.stages[AlgorithmEntryPoint::Tick] = {
            addState,
            addField,
            branch,
            loop,
            halt
        };
    }
    AlgorithmRegistry algorithms;
    if (algorithms.Register(std::move(algorithm))
        != AlgorithmRegisterResult::Added) return false;
    algorithms.Freeze();

    MechanismDefinition definition;
    definition.type = type;
    definition.canonicalName = "controlled_script_instance";
    definition.id = StableMechanismDefinitionId(
        type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.algorithm = StableAlgorithmId(algorithmName);
    definition.algorithmVersion = 1;
    definition.source.sourceName = "controlled_script_probe";
    output.definition = definition.id;
    output.instance = StableMechanismInstanceId(definition.id, 0);
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(definition, schemas, algorithms)
        != MechanismDefinitionDeclareResult::Added) return false;
    definitions.Freeze();

    MechanismSpawnDefinition spawn;
    spawn.canonicalName = "initial_controlled_script";
    spawn.definition = definition.id;
    spawn.id = StableMechanismSpawnDefinitionId(
        spawn.definition,
        spawn.canonicalName
    );
    spawn.source.sourceName = "controlled_script_probe";
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, definitions, schemas)
        != MechanismSpawnDeclareResult::Added) return false;
    spawns.Freeze();

    PackageManifestRegistry manifests;
    manifests.Freeze();
    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.controlled_script.ruleset";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    ruleset.requiredSchemas.push_back({type, 1});
    ruleset.requiredDefinitions.push_back(definition.id);
    ruleset.requiredMechanismSpawns.push_back(spawn.id);
    ruleset.requiredAlgorithms.push_back({definition.algorithm, 1});
    PackageLock packageLock;
    PackageLockReport lockReport;
    if (!PackageLockBuilder{}.Resolve(
            manifests,
            ruleset,
            packageLock,
            lockReport)) return false;

    ComponentSchemaRegistry components;
    EntityDefinitionRegistry entities;
    RuntimeCapabilityContractRegistry capabilities;
    components.Freeze();
    entities.Freeze();
    capabilities.Freeze();
    RuntimeCompileReport compileReport;
    if (!RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            schemas,
            components,
            algorithms,
            definitions,
            entities,
            spawns,
            capabilities,
            output.catalog,
            compileReport)) return false;
    const auto slot = output.catalog.ResolveDefinitionFieldSlot(
        definition.id,
        "counter"
    );
    if (!slot) return false;
    output.counter = *slot;
    return true;
}

bool ParseExternalScript()
{
    const std::string bytes = R"(
algorithm_descriptor = {
    name = dillen.test.external_script
    version = 1
    backend = script
    entry_points = { tick }
    execution_policy = {
        instruction_budget = 8
        script_slice_instruction_budget = 2
        script_memory_limit_bytes = 128
    }
    script = {
        state = { counter = 0 }
        tick = {
            add_state = { state = counter value = 1 }
            yield = yes
            halt = yes
        }
    }
}
)";
    dillen::parser::SourceBuffer source(
        1,
        "algorithms/external_script.dalgorithm",
        {},
        bytes,
        dillen::parser::SourceEncoding::Utf8
    );
    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::ParserCursor cursor(source, diagnostics);
    dillen::parser::ParseArtifact artifact;
    if (!dillen::authoring::ParseAlgorithmDescriptor(cursor, artifact)
        || diagnostics.HasErrors()) return false;
    const auto* document = artifact.As<
        dillen::authoring::AlgorithmDescriptorDocument>();
    return document != nullptr
        && document->value.backend
            == dillen::kernel::AlgorithmBackend::Script
        && document->value.executionPolicy.scriptSliceInstructionBudget == 2
        && document->value.executionPolicy.scriptMemoryLimitBytes == 128
        && document->value.script.state.size() == 1
        && document->value.script.stages.size() == 1;
}

// A controlled script that uses a declarative transaction instruction
// (schedule_event) and a conditional field mutation must parse into Transact
// instructions -- the shared path that gives it full parity with the
// declarative backend.
bool ParseScriptTransact()
{
    const std::string bytes = R"(
algorithm_descriptor = {
    name = dillen.test.transact_script
    version = 1
    backend = script
    entry_points = { tick }
    execution_policy = {
        instruction_budget = 8
        script_slice_instruction_budget = 4
        script_memory_limit_bytes = 128
    }
    script = {
        state = { counter = 0 }
        tick = {
            add_field = {
                field = counter
                value = 1
                when = { field_equals = { field = counter value = 0 } }
            }
            schedule_event = {
                type = dillen.test.reminder
                delay = 1
                priority = 0
                payload = 0
            }
            halt = yes
        }
    }
}
)";
    dillen::parser::SourceBuffer source(
        1,
        "algorithms/transact_script.dalgorithm",
        {},
        bytes,
        dillen::parser::SourceEncoding::Utf8
    );
    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::ParserCursor cursor(source, diagnostics);
    dillen::parser::ParseArtifact artifact;
    if (!dillen::authoring::ParseAlgorithmDescriptor(cursor, artifact)
        || diagnostics.HasErrors()) return false;
    const auto* document = artifact.As<
        dillen::authoring::AlgorithmDescriptorDocument>();
    if (document == nullptr) return false;
    const auto stage = document->value.script.stages.find(
        dillen::kernel::AlgorithmEntryPoint::Tick
    );
    if (stage == document->value.script.stages.end()
        || stage->second.size() != 3) return false;
    using Kind = dillen::kernel::ControlledScriptInstructionKind;
    return stage->second[0].kind == Kind::Transact
        && stage->second[0].action.conditions.size() == 1
        && stage->second[1].kind == Kind::Transact
        && stage->second[1].action.kind
            == dillen::kernel::AlgorithmInstructionKind::ScheduleEvent
        && stage->second[2].kind == Kind::Halt;
}

bool HasTickContinuation(const dillen::kernel::MechanismInstance& instance)
{
    for (const auto& continuation : instance.algorithmContinuations)
    {
        if (continuation.entryPoint
            == dillen::kernel::AlgorithmEntryPoint::Tick) return true;
    }
    return false;
}

}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;
    Fixture fixture;
    if (!ParseExternalScript())
    {
        std::cerr << "Controlled Script external authoring parse failed\n";
        return 1;
    }
    if (!ParseScriptTransact())
    {
        std::cerr << "Controlled Script Transact authoring parse failed\n";
        return 1;
    }

    // Execute a controlled script whose tick stage is a single guarded
    // Transact: it must lower and run through the shared declarative emitter,
    // so the counter grows by exactly 5 on the first tick (guard holds) and
    // never again (guard fails once counter != 0).
    {
        Fixture transactFixture;
        if (!BuildFixture(transactFixture, /*transactTick*/ true))
        {
            std::cerr << "Controlled Script Transact fixture failed\n";
            return 1;
        }
        world::AuthoritativeWorld transactWorld;
        world::InitialWorldBuildReport transactBuild;
        if (!world::InitialWorldBuilder{}.Build(
                transactFixture.catalog, transactWorld, transactBuild))
        {
            return 1;
        }
        runtime::KernelRuntime transactRuntime(
            transactWorld, transactFixture.catalog);
        if (!transactRuntime.RunTick(1) || !transactRuntime.RunTick(2))
        {
            return 1;
        }
        const MechanismInstance* after = transactRuntime.Snapshot().Find(
            transactFixture.instance
        );
        if (after == nullptr
            || transactRuntime.LastTickAlgorithms().FailedCount() != 0
            || after->values[transactFixture.counter.value]
                != MechanismValue(std::int64_t{5}))
        {
            std::cerr << "Controlled Script Transact execution mismatch\n";
            return 1;
        }
    }

    if (!BuildFixture(fixture))
    {
        std::cerr << "Controlled Script fixture construction failed\n";
        return 2;
    }

    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport buildReport;
    if (!world::InitialWorldBuilder{}.Build(
            fixture.catalog,
            world,
            buildReport)) return 2;
    runtime::KernelRuntime runtime(world, fixture.catalog);
    if (!runtime.RunTick(1)) return 3;
    const MechanismInstance* afterFirst = runtime.Snapshot().Find(
        fixture.instance
    );
    if (afterFirst == nullptr
        || !afterFirst->algorithmInitialized
        || afterFirst->lifecycle != MechanismLifecycleState::Active
        || !HasTickContinuation(*afterFirst)
        || afterFirst->values[fixture.counter.value]
            != MechanismValue(std::int64_t{1})
        || runtime.LastTickAlgorithms().invocations.front().status
            != runtime::AlgorithmInvocationStatus::Preempted)
    {
        std::cerr << "Controlled Script preemption state mismatch\n";
        return 4;
    }

    if (!runtime.RunTick(2) || !runtime.RunTick(3)) return 5;
    std::vector<std::uint8_t> saved;
    persistence::RuntimePersistenceService persistence;
    if (!persistence.Save(runtime, saved))
    {
        std::cerr << "Controlled Script save failed\n";
        return 6;
    }
    const MechanismInstance* checkpoint = runtime.Snapshot().Find(
        fixture.instance
    );
    if (checkpoint == nullptr
        || checkpoint->algorithmState[0]
            != MechanismValue(std::int64_t{2})
        || !HasTickContinuation(*checkpoint)) return 7;

    world::AuthoritativeWorld restoredWorld;
    if (!world::InitialWorldBuilder{}.Build(
            fixture.catalog,
            restoredWorld,
            buildReport)) return 8;
    runtime::KernelRuntime restored(restoredWorld, fixture.catalog);
    if (!persistence.Load(restored, saved))
    {
        std::cerr << "Controlled Script restore failed\n";
        return 9;
    }
    const MechanismInstance* restoredCheckpoint = restored.Snapshot().Find(
        fixture.instance
    );
    if (restoredCheckpoint == nullptr
        || restoredCheckpoint->algorithmState != checkpoint->algorithmState
        || restoredCheckpoint->algorithmContinuations
            != checkpoint->algorithmContinuations) return 10;

    for (std::uint64_t tick = 4; tick <= 6; ++tick)
    {
        if (!runtime.RunTick(tick) || !restored.RunTick(tick)) return 11;
    }
    const MechanismInstance* completed = restored.Snapshot().Find(
        fixture.instance
    );
    if (completed == nullptr
        || completed->values[fixture.counter.value]
            != MechanismValue(std::int64_t{3})
        || completed->algorithmState[0]
            != MechanismValue(std::int64_t{3})
        || HasTickContinuation(*completed))
    {
        std::cerr << "Controlled Script deterministic resume mismatch\n";
        return 12;
    }

    const CompiledControlledScriptProgram* compiled =
        fixture.catalog.FindControlledScriptProgram(fixture.definition);
    const AlgorithmDescriptor* descriptor = fixture.catalog.FindAlgorithm(
        completed->algorithm,
        completed->algorithmVersion
    );
    if (compiled == nullptr || descriptor == nullptr) return 13;
    CompiledControlledScriptProgram quotaProgram = *compiled;
    ControlledScriptInstruction setScratch;
    setScratch.opcode = ControlledScriptOpcode::SetStateConstant;
    setScratch.stateSlot = 1;
    setScratch.operand = MechanismValue(std::string(128, 'x'));
    quotaProgram.stages[AlgorithmEntryPoint::Tick] = {setScratch};
    AlgorithmExecutionPolicy quotaPolicy = descriptor->executionPolicy;
    quotaPolicy.scriptMemoryLimitBytes = 32;
    runtime::AlgorithmExecutionBudget budget(quotaPolicy);
    const std::vector<CapabilityBindingSlotId> noCapabilities;
    runtime::AlgorithmInvocationContext context{
        runtime::AlgorithmRuntimeStage::Tick,
        6,
        *completed,
        restored.Query(),
        restored.Snapshot(),
        restored.RngSnapshot(),
        fixture.catalog,
        noCapabilities,
        nullptr,
        nullptr,
        nullptr,
        budget
    };
    const runtime::ControlledScriptResult quota =
        runtime::ControlledScriptVm{}.Execute(
            quotaProgram,
            AlgorithmEntryPoint::Tick,
            context,
            quotaPolicy
        );
    if (quota.status
        != runtime::ControlledScriptStatus::MemoryQuotaExceeded)
    {
        std::cerr << "Controlled Script memory quota was not enforced\n";
        return 14;
    }

    std::cout << "Controlled Script sandbox/persistence: passed\n";
    return 0;
}
