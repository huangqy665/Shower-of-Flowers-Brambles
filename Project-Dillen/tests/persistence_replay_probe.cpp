#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "algorithm_registry.hpp"
#include "component_schema.hpp"
#include "deterministic_replay.hpp"
#include "entity_definition_registry.hpp"
#include "initial_world_builder.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_manifest.hpp"
#include "ruleset.hpp"
#include "runtime_compiler.hpp"
#include "runtime_persistence.hpp"

namespace {

struct ProbeCatalog
{
    dillen::kernel::FrozenRuntimeCatalog catalog;
    dillen::kernel::EntityTypeId entityType;
    dillen::kernel::ComponentTypeId componentType;
    dillen::kernel::EntityDefinitionId firstDefinition;
    dillen::kernel::EntityDefinitionId secondDefinition;
    dillen::kernel::EntityId firstEntity;
    dillen::kernel::EntityId secondEntity;
    dillen::kernel::ComponentFieldSlotId populationSlot;
    dillen::kernel::RelationTypeId relationType;
    dillen::kernel::RelationId relation;
    dillen::kernel::MechanismTypeId mechanismType;
    dillen::kernel::MechanismDefinitionId mechanismDefinition;
    dillen::kernel::MechanismSpawnDefinitionId mechanismSpawn;
    dillen::kernel::MechanismInstanceId mechanismInstance;
    dillen::kernel::MechanismFieldSlotId levelSlot;
};

bool BuildCatalog(ProbeCatalog& output)
{
    using namespace dillen::kernel;
    output.entityType = StableEntityTypeId("dillen.test.persistence.entity");
    output.componentType = StableComponentTypeId(
        "dillen.test.persistence.identity"
    );
    ComponentSchema componentSchema;
    componentSchema.type = output.componentType;
    componentSchema.canonicalName = "dillen.test.persistence.identity";
    componentSchema.version = 1;
    MechanismFieldSchema population;
    population.name = "population";
    population.kind = MechanismValueKind::Integer;
    population.defaultValue = MechanismValue(std::int64_t{0});
    componentSchema.fields.push_back(population);
    ComponentSchemaRegistry componentSchemas;
    if (componentSchemas.Register(std::move(componentSchema))
            != ComponentSchemaRegisterResult::Added)
    {
        return false;
    }
    componentSchemas.Freeze();

    const auto makeEntity = [&](std::string name, std::int64_t value)
    {
        EntityDefinition definition;
        definition.type = output.entityType;
        definition.canonicalName = std::move(name);
        definition.id = StableEntityDefinitionId(
            definition.type,
            definition.canonicalName
        );
        definition.components.push_back({output.componentType, 1, {
            {"population", MechanismValue(value)}
        }});
        definition.source.sourceName = "persistence_replay_probe";
        return definition;
    };
    EntityDefinition first = makeEntity("dillen.test.persistence.alpha", 100);
    EntityDefinition second = makeEntity("dillen.test.persistence.beta", 80);
    output.firstDefinition = first.id;
    output.secondDefinition = second.id;
    output.firstEntity = StableEntityId(first.id);
    output.secondEntity = StableEntityId(second.id);
    EntityDefinitionRegistry entities;
    if (entities.Declare(first, componentSchemas)
            != EntityDefinitionDeclareResult::Added
        || entities.Declare(second, componentSchemas)
            != EntityDefinitionDeclareResult::Added)
    {
        return false;
    }
    entities.Freeze();

    output.mechanismType = StableMechanismTypeId(
        "dillen.test.persistence.policy"
    );
    MechanismSchema mechanismSchema;
    mechanismSchema.type = output.mechanismType;
    mechanismSchema.canonicalName = "dillen.test.persistence.policy";
    mechanismSchema.version = 1;
    MechanismFieldSchema level;
    level.name = "level";
    level.kind = MechanismValueKind::Integer;
    level.defaultValue = MechanismValue(std::int64_t{1});
    mechanismSchema.fields.push_back(level);
    MechanismRoleSchema owner;
    owner.name = "owner";
    owner.referenceKind = MechanismReferenceKind::Entity;
    owner.referenceType = output.entityType.value;
    owner.minimumCount = 1;
    owner.maximumCount = 1;
    mechanismSchema.roles.push_back(owner);
    MechanismSchemaRegistry mechanisms;
    if (mechanisms.Register(std::move(mechanismSchema))
            != MechanismSchemaRegisterResult::Added)
    {
        return false;
    }
    mechanisms.Freeze();

    AlgorithmRegistry algorithms;
    algorithms.Freeze();
    MechanismDefinition definition;
    definition.type = output.mechanismType;
    definition.canonicalName = "dillen.test.persistence.default_policy";
    definition.id = StableMechanismDefinitionId(
        definition.type,
        definition.canonicalName
    );
    definition.schemaVersion = 1;
    definition.fields["level"] = MechanismValue(std::int64_t{3});
    definition.roles["owner"] = {{
        MechanismReferenceKind::Entity,
        output.entityType.value,
        output.firstEntity.value
    }};
    definition.source.sourceName = "persistence_replay_probe";
    output.mechanismDefinition = definition.id;
    MechanismDefinitionRegistry definitions;
    if (definitions.Declare(definition, mechanisms, algorithms)
            != MechanismDefinitionDeclareResult::Added)
    {
        return false;
    }
    definitions.Freeze();

    MechanismSpawnDefinition spawn;
    spawn.canonicalName = "dillen.test.persistence.initial_policy";
    spawn.definition = output.mechanismDefinition;
    spawn.id = StableMechanismSpawnDefinitionId(
        spawn.definition,
        spawn.canonicalName
    );
    spawn.count = 1;
    spawn.source.sourceName = "persistence_replay_probe";
    output.mechanismSpawn = spawn.id;
    output.mechanismInstance = StableMechanismInstanceId(
        output.mechanismDefinition,
        0
    );
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, definitions, mechanisms)
            != MechanismSpawnDeclareResult::Added)
    {
        return false;
    }
    spawns.Freeze();

    PackageManifest package;
    package.canonicalName = "dillen.test.persistence.package";
    package.id = StablePackageId(package.canonicalName);
    package.version = {1, 0, 0};
    package.contentDigest = std::string(64, 'a');
    PackageManifestRegistry packages;
    if (packages.Register(package) != PackageManifestRegisterResult::Added)
    {
        return false;
    }
    packages.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.persistence.ruleset";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    RulesetPackageRequirement packageRequirement;
    packageRequirement.package = package.id;
    packageRequirement.canonicalName = package.canonicalName;
    packageRequirement.versions.minimumInclusive = package.version;
    packageRequirement.versions.maximumExclusive = PackageVersion{2, 0, 0};
    ruleset.packages.push_back(packageRequirement);
    ruleset.requiredSchemas.push_back({output.mechanismType, 1});
    ruleset.requiredComponents.push_back({output.componentType, 1});
    ruleset.requiredDefinitions.push_back(output.mechanismDefinition);
    ruleset.requiredEntityDefinitions = {
        output.firstDefinition,
        output.secondDefinition
    };
    ruleset.requiredMechanismSpawns.push_back(output.mechanismSpawn);
    PackageLock packageLock;
    PackageLockReport packageReport;
    if (!PackageLockBuilder{}.Resolve(
            packages,
            ruleset,
            packageLock,
            packageReport))
    {
        return false;
    }

    RuntimeCapabilityContractRegistry capabilities;
    capabilities.Freeze();
    RuntimeCompileReport compileReport;
    if (!RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            mechanisms,
            componentSchemas,
            algorithms,
            definitions,
            entities,
            spawns,
            capabilities,
            output.catalog,
            compileReport))
    {
        return false;
    }
    output.populationSlot = *output.catalog.ResolveComponentFieldSlot(
        output.componentType,
        1,
        "population"
    );
    output.levelSlot = *output.catalog.ResolveDefinitionFieldSlot(
        output.mechanismDefinition,
        "level"
    );
    output.relationType = StableRelationTypeId(
        "dillen.test.persistence.relation"
    );
    output.relation = StableRelationId(
        output.relationType,
        output.firstEntity,
        output.secondEntity
    );
    return true;
}

dillen::kernel::WorldTransaction SetPopulation(
    const ProbeCatalog& catalog,
    std::int64_t population
)
{
    dillen::kernel::WorldTransaction transaction;
    transaction.commands.push_back(
        dillen::kernel::WorldCommand::SetComponentField(
            catalog.firstEntity,
            catalog.componentType,
            catalog.populationSlot,
            dillen::kernel::MechanismValue(population)
        )
    );
    return transaction;
}

}

// Frozen encoding of every World command payload (Demo 0.2).
//
// The canonical world below only ever leaves ONE command type in the persisted
// queue, so its golden bytes do not exercise most of WriteWorldCommand. This
// encodes a hand-built Save Image whose command queue holds one transaction per
// WorldCommandPayload alternative -- all 11 -- plus a spread of
// MechanismCommandOperation alternatives, so every command writer branch is
// covered by a golden. Verified byte-identical on Windows MSVC and Linux GCC.
// Multi-step migration.
//
// The registry has always walked a chain -- it loops until the image identity
// matches the target, and it carries a visited set to break cycles -- but the
// only migration ever exercised was a single hop. A chain that needs two steps,
// a chain that loops, and a chain that runs out of steps halfway all went
// through code no test had ever entered.
//
// This matters more than an ordinary coverage gap. "Bump the save version and
// supply a migration" is the *only* escape hatch the freeze rules offer for a
// breaking change (FROZEN_CONTRACTS section 0), so an untested escape hatch
// means the freeze has no sanctioned way out.
bool CheckMigrationChain(const ProbeCatalog& probe)
{
    using namespace dillen;

    const persistence::RuntimeSaveIdentity target =
        persistence::RuntimePersistenceService::IdentityFor(probe.catalog);

    // Two ancestors of the current identity, distinguished only by format
    // version, which is what the registry keys a step's source on.
    persistence::RuntimeSaveIdentity v1 = target;
    v1.formatVersion = target.formatVersion - 1;
    persistence::RuntimeSaveIdentity v0 = target;
    v0.formatVersion = target.formatVersion - 2;

    // Each step stamps a marker into the first mechanism's level field, so the
    // final value proves both steps ran and proves the order they ran in.
    const auto step = [](
        const std::string& name,
        const persistence::RuntimeSaveIdentity& from,
        const persistence::RuntimeSaveIdentity& to,
        std::int64_t stamp)
    {
        persistence::RuntimeMigrationStep migration;
        migration.canonicalName = name;
        migration.source = from;
        migration.target = to;
        migration.migrate = [to, stamp](
            persistence::RuntimeSaveImage& image,
            std::string& message)
        {
            if (image.mechanisms.empty())
            {
                message = "no mechanism to stamp";
                return false;
            }
            kernel::MechanismInstance& instance = image.mechanisms.front();
            if (instance.values.empty())
            {
                message = "mechanism has no values";
                return false;
            }
            const auto* current =
                std::get_if<std::int64_t>(&instance.values.back().data);
            if (current == nullptr)
            {
                message = "unexpected value kind";
                return false;
            }
            // Multiply-then-add, so a swapped order produces a different
            // number rather than the same one.
            instance.values.back() =
                kernel::MechanismValue(*current * 10 + stamp);
            image.identity = to;
            return true;
        };
        return migration;
    };

    persistence::RuntimeSaveImage image;
    image.identity = v0;
    image.worldTick = 1;
    kernel::MechanismInstance instance;
    instance.id = kernel::MechanismInstanceId{0x99ULL};
    instance.type = probe.mechanismType;
    instance.schemaVersion = 1;
    instance.values.push_back(kernel::MechanismValue(std::int64_t{1}));
    image.mechanisms.push_back(instance);

    // --- the chain runs end to end ---
    {
        persistence::RuntimeMigrationRegistry registry;
        if (registry.Register(step("v0_to_v1", v0, v1, 2))
                != persistence::RuntimeMigrationRegisterResult::Added
            || registry.Register(step("v1_to_v2", v1, target, 3))
                != persistence::RuntimeMigrationRegisterResult::Added)
        {
            std::cerr << "Migration chain: registration failed\n";
            return false;
        }
        registry.Freeze();
        persistence::RuntimeSaveImage walked = image;
        const persistence::RuntimeMigrationReport report =
            registry.Migrate(walked, target);
        const auto* value =
            std::get_if<std::int64_t>(&walked.mechanisms.front().values.back().data);
        // 1 -> 1*10+2 = 12 -> 12*10+3 = 123. Any other order or count of steps
        // gives a different number.
        if (report.status != persistence::RuntimeMigrationStatus::Migrated
            || value == nullptr
            || *value != 123
            || !persistence::SameRuntimeSaveIdentity(walked.identity, target))
        {
            std::cerr << "Migration chain: two-step walk failed, value="
                      << (value ? std::to_string(*value) : std::string("none"))
                      << '\n';
            return false;
        }
    }

    // --- a chain that never reaches the target is rejected, not half-applied ---
    {
        persistence::RuntimeMigrationRegistry registry;
        if (registry.Register(step("v0_to_v1", v0, v1, 2))
                != persistence::RuntimeMigrationRegisterResult::Added)
        {
            std::cerr << "Migration chain: stalled-case registration failed\n";
            return false;
        }
        registry.Freeze();
        persistence::RuntimeSaveImage stalled = image;
        const persistence::RuntimeMigrationReport report =
            registry.Migrate(stalled, target);
        if (report.status != persistence::RuntimeMigrationStatus::PathMissing)
        {
            std::cerr << "Migration chain: a broken chain was not rejected\n";
            return false;
        }
    }

    // --- a step that moves the format version backwards is refused outright ---
    //
    // Registration already forbids it, so one whole class of cycle can never
    // be built: a chain cannot walk back down the version ladder. Asserting it
    // here keeps that structural guarantee from being relaxed by accident.
    {
        persistence::RuntimeMigrationRegistry registry;
        if (registry.Register(step("v1_back_to_v0", v1, v0, 4))
                != persistence::RuntimeMigrationRegisterResult::InvalidStep)
        {
            std::cerr << "Migration chain: a backwards step was accepted\n";
            return false;
        }
    }

    // --- a cycle within one format version is detected instead of looping ---
    //
    // Steps that keep the format version and change only the Ruleset
    // Fingerprint are legal, so A -> B -> A is constructible. This is the
    // shape the cycle detector actually exists for.
    //
    // Two independent guards catch it, which is worth knowing before either is
    // touched: the visited set, and a cap on applied steps at the registry
    // size. Disabling either alone still terminates -- only disabling both
    // hangs -- so a change that removes one will not show up here as a
    // failure, it will show up as the other guard doing all the work.
    {
        persistence::RuntimeSaveIdentity fingerprintA = target;
        fingerprintA.rulesetFingerprint = {0xA1A1ULL, 0xA2A2ULL};
        persistence::RuntimeSaveIdentity fingerprintB = target;
        fingerprintB.rulesetFingerprint = {0xB1B1ULL, 0xB2B2ULL};

        persistence::RuntimeMigrationRegistry registry;
        if (registry.Register(step("a_to_b", fingerprintA, fingerprintB, 5))
                != persistence::RuntimeMigrationRegisterResult::Added
            || registry.Register(step("b_to_a", fingerprintB, fingerprintA, 6))
                != persistence::RuntimeMigrationRegisterResult::Added)
        {
            std::cerr << "Migration chain: cycle-case registration failed\n";
            return false;
        }
        registry.Freeze();
        persistence::RuntimeSaveImage looping = image;
        looping.identity = fingerprintA;
        const persistence::RuntimeMigrationReport report =
            registry.Migrate(looping, target);
        if (report.status != persistence::RuntimeMigrationStatus::CycleDetected)
        {
            std::cerr << "Migration chain: a cycle was not detected\n";
            return false;
        }
    }

    return true;
}

bool CheckFrozenCommandEncoding()
{
    using namespace dillen;
    using namespace dillen::kernel;

    const EntityId entity{0x1111ULL};
    const EntityDefinitionId entityDefinition{0x2222ULL};
    const ComponentTypeId component{0x3333ULL};
    const ComponentFieldSlotId componentField{4};
    const RelationTypeId relationType{0x4444ULL};
    const RelationId relation{0x5555ULL};
    const MechanismSpawnDefinitionId spawn{0x6666ULL};
    const MechanismInstanceId instance{0x7777ULL};
    const MechanismFieldSlotId field{9};
    const AlgorithmEventTypeId eventType{0x8888ULL};
    const RngStreamId stream{0x9999ULL};
    const CapabilityId capability{0xAAAAULL};
    const AlgorithmEventTypeId deliveryType{0xBBBBULL};

    WorldTransaction all;
    all.commands = {
        WorldCommand::CreateEntity(entityDefinition),
        WorldCommand::SetComponentField(
            entity, component, componentField,
            MechanismValue(std::int64_t{-7})),
        WorldCommand::AddRelation(relationType, entity, EntityId{0x1112ULL}),
        WorldCommand::RemoveRelation(relation),
        WorldCommand::SpawnMechanism(spawn),
        WorldCommand::Mechanism(MechanismCommand::SetField(
            instance, field, MechanismValue(std::string("frozen")))),
        WorldCommand::ScheduleEvent(
            eventType, instance, 12, -3, MechanismValue(2.5)),
        WorldCommand::CancelEvent(77),
        WorldCommand::CreateRngStream(stream, 0xFEEDULL),
        WorldCommand::AdvanceRngStream(stream, 5, 3),
        WorldCommand::InvokeCapability(
            capability, deliveryType, 20, 4,
            MechanismValue(std::int64_t{11}), instance, 2),
    };
    // Remaining MechanismCommandOperation alternatives.
    WorldTransaction operations;
    operations.commands = {
        WorldCommand::Mechanism(MechanismCommand::TransitionLifecycle(
            instance, MechanismLifecycleState::Paused)),
        WorldCommand::Mechanism(
            MechanismCommand::CompleteAlgorithmCreate(instance)),
        WorldCommand::Mechanism(MechanismCommand::RecordAlgorithmFault(
            instance,
            AlgorithmFaultCode::InstructionBudgetExceeded,
            AlgorithmFaultStage::Tick)),
        WorldCommand::Mechanism(MechanismCommand::ClearAlgorithmFault(
            instance)),
        WorldCommand::Mechanism(MechanismCommand::Destroy(instance)),
        WorldCommand::Mechanism(MechanismCommand::ReplaceAlgorithmState(
            instance,
            {MechanismValue(std::int64_t{1}), MechanismValue(true)},
            {{AlgorithmEntryPoint::Tick, 3}})),
        WorldCommand::Mechanism(MechanismCommand::AddField(
            instance, field, MechanismValue(std::int64_t{9}))),
    };

    persistence::RuntimeSaveImage image;
    image.worldTick = 42;
    image.worldRevision = 7;
    image.commandQueue.push_back({1, 5, 0, std::move(all)});
    image.commandQueue.push_back({2, 6, -1, std::move(operations)});
    image.nextCommandSequence = 3;

    std::vector<std::uint8_t> bytes;
    if (!persistence::RuntimeSaveCodec{}.Encode(image, bytes))
    {
        std::cerr << "Frozen command encoding: Encode failed\n";
        return false;
    }

    // See the golden note in main(): an accidental change is a bug to fix, a
    // deliberate one needs a format-version bump and a migration.
    constexpr std::size_t kGoldenBytes = 539;
    constexpr std::uint64_t kGoldenChecksum = 11380329816255759537ULL;
    if (bytes.size() != kGoldenBytes
        || persistence::StableRuntimeChecksum(bytes) != kGoldenChecksum)
    {
        std::cerr << "Frozen command encoding drifted:\n"
                  << "  bytes    : " << bytes.size()
                  << " (expected " << kGoldenBytes << ")\n"
                  << "  checksum : "
                  << persistence::StableRuntimeChecksum(bytes)
                  << " (expected " << kGoldenChecksum << ")\n";
        return false;
    }

    // Round trip: every alternative must decode back to the same tag.
    persistence::RuntimeSaveImage decoded;
    if (!persistence::RuntimeSaveCodec{}.Decode(bytes, decoded)
        || decoded.commandQueue.size() != 2
        || decoded.commandQueue[0].transaction.commands.size() != 11
        || decoded.commandQueue[1].transaction.commands.size() != 7)
    {
        std::cerr << "Frozen command encoding: round trip failed\n";
        return false;
    }
    // The first transaction lists the alternatives in declaration order, so
    // each decoded command must land back on its own tag -- this catches a
    // reader/writer pair that drifted together and so kept the byte count.
    const auto& roundTripped = decoded.commandQueue[0].transaction;
    for (std::size_t command = 0; command < roundTripped.commands.size();
        ++command)
    {
        if (roundTripped.commands[command].payload.index() != command)
        {
            std::cerr << "Frozen command encoding: tag " << command
                      << " decoded as "
                      << roundTripped.commands[command].payload.index()
                      << '\n';
            return false;
        }
    }

    // The inner variant needs the same treatment, and did not have it. Until
    // now MechanismCommandOperation was only counted, so a reader and writer
    // that drifted together on a Mechanism operation -- swapping two tags,
    // say -- kept both the byte count and the outer tags, and nothing here
    // noticed. Every one of the eight alternatives is asserted on its own
    // position: SetField rides in the first transaction at outer tag 5, and
    // the second transaction carries the remaining seven in declaration order.
    //
    // Appending an alternative means appending a command here. A new tag that
    // no command constructs is a tag the golden does not cover, which is the
    // freeze hole this block exists to close.
    const auto operationTag = [](const WorldCommand& command,
                                 std::size_t& tag)
    {
        const auto* mechanism = std::get_if<MechanismCommand>(&command.payload);
        if (mechanism == nullptr)
        {
            return false;
        }
        tag = mechanism->operation.index();
        return true;
    };

    std::size_t setFieldTag = 0;
    if (!operationTag(roundTripped.commands[5], setFieldTag)
        || setFieldTag != 0)
    {
        std::cerr << "Frozen command encoding: MechanismCommandOperation tag 0"
                     " (SetField) decoded as " << setFieldTag << '\n';
        return false;
    }

    const auto& operationsRoundTripped =
        decoded.commandQueue[1].transaction;
    for (std::size_t operation = 0;
        operation < operationsRoundTripped.commands.size();
        ++operation)
    {
        std::size_t tag = 0;
        // Declaration order minus SetField, so alternative n+1 sits at n.
        const std::size_t expected = operation + 1;
        if (!operationTag(operationsRoundTripped.commands[operation], tag)
            || tag != expected)
        {
            std::cerr << "Frozen command encoding: "
                         "MechanismCommandOperation tag " << expected
                      << " decoded as " << tag << '\n';
            return false;
        }
    }
    return true;
}

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;
    ProbeCatalog probe;
    if (!BuildCatalog(probe))
    {
        std::cerr << "Persistence probe Runtime Catalog failed\n";
        return 1;
    }

    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport worldReport;
    if (!world::InitialWorldBuilder{}.Build(probe.catalog, world, worldReport))
    {
        std::cerr << "Persistence probe initial World failed\n";
        return 2;
    }
    runtime::KernelRuntime runtime(world, probe.catalog);
    const RngStreamId rng = StableRngStreamId(
        "dillen.test.persistence.rng"
    );
    const AlgorithmEventTypeId eventType = StableAlgorithmEventTypeId(
        "dillen.test.persistence.scheduled"
    );
    WorldTransaction initial;
    initial.commands = {
        WorldCommand::SetComponentField(
            probe.firstEntity,
            probe.componentType,
            probe.populationSlot,
            MechanismValue(std::int64_t{150})
        ),
        WorldCommand::AddRelation(
            probe.relationType,
            probe.firstEntity,
            probe.secondEntity
        ),
        WorldCommand::Mechanism(MechanismCommand::SetField(
            probe.mechanismInstance,
            probe.levelSlot,
            MechanismValue(std::int64_t{4})
        )),
        WorldCommand::CreateRngStream(rng, 0xD111EULL),
        WorldCommand::AdvanceRngStream(rng, 0, 2),
        WorldCommand::ScheduleEvent(
            eventType,
            probe.mechanismInstance,
            3,
            0,
            MechanismValue(std::string("scheduled"))
        )
    };
    if (!runtime.ApplyImmediate(initial, 0))
    {
        std::cerr << "Persistence probe initial transaction failed\n";
        return 3;
    }
    runtime.Enqueue(SetPopulation(probe, 200), 2, 10);

    persistence::RuntimePersistenceService persistence;
    persistence::RuntimeSaveImage captured;
    std::vector<std::uint8_t> saved;
    if (!persistence.Capture(runtime, captured)
        || !persistence.Save(runtime, saved)
        || captured.identity.packageLock.size() != 1
        || !captured.identity.sourceLock.empty()
        || captured.entities.size() != 2
        || captured.components.size() != 2
        || captured.relations.size() != 1
        || captured.mechanisms.size() != 1
        || captured.scheduledInbox.size() != 1
        || captured.rngStreams.size() != 1
        || captured.commandQueue.size() != 1)
    {
        std::cerr << "Complete Runtime Save capture failed\n";
        return 4;
    }

    if (!runtime.RunTick(1)
        || !runtime.ApplyImmediate(SetPopulation(probe, 999), 1)
        || !persistence.Load(runtime, saved))
    {
        std::cerr << "Runtime Save restore failed\n";
        return 5;
    }
    std::vector<std::uint8_t> restoredBytes;
    const runtime::WorldQuerySnapshotHandle restoredSnapshot =
        runtime.AcquireQuerySnapshot();
    const MechanismValue* restoredPopulation =
        restoredSnapshot->Components().FindField(
            probe.firstEntity,
            probe.componentType,
            probe.populationSlot
        );
    if (!persistence.Save(runtime, restoredBytes)
        || restoredBytes != saved
        || world.Tick() != 0
        || restoredSnapshot->Entities().FindByType(probe.entityType).size()
            != 2
        || restoredSnapshot->Relations().Find(probe.relation) == nullptr
        || restoredSnapshot->Mechanisms().Find(probe.mechanismInstance)
            == nullptr
        || restoredPopulation == nullptr
        || *restoredPopulation != MechanismValue(std::int64_t{150})
        || runtime.Commands().Size() != 1
        || !runtime.Events().Empty())
    {
        std::cerr << "Restored authority or derived indexes mismatch\n";
        return 6;
    }

    std::vector<std::uint8_t> corrupted = saved;
    corrupted[corrupted.size() / 2] ^= 0x5a;
    if (persistence.Load(runtime, corrupted))
    {
        std::cerr << "Corrupted Runtime Save was accepted\n";
        return 7;
    }
    persistence::RuntimeSaveImage incompatible = captured;
    incompatible.identity.rulesetFingerprint.high ^= 1;
    if (persistence.Restore(runtime, incompatible)
        || world.Tick() != 0)
    {
        std::cerr << "Incompatible Ruleset Save was accepted\n";
        return 8;
    }

    persistence::RuntimeSaveImage legacy = captured;
    legacy.identity.formatVersion = 0;
    legacy.identity.rulesetFingerprint = {0x1111ULL, 0x2222ULL};
    legacy.mechanisms.front().schemaVersion = 0;
    legacy.mechanisms.front().values.clear();
    persistence::RuntimeMigrationRegistry migrations;
    persistence::RuntimeMigrationStep migration;
    migration.canonicalName = "dillen.test.persistence.policy.v0_to_v1";
    migration.source = legacy.identity;
    migration.target = persistence::RuntimePersistenceService::IdentityFor(
        probe.catalog
    );
    migration.migrate = [type = probe.mechanismType](
        persistence::RuntimeSaveImage& image,
        std::string& message)
    {
        for (MechanismInstance& instance : image.mechanisms)
        {
            if (instance.type != type || instance.schemaVersion != 0)
            {
                message = "unexpected legacy Mechanism layout";
                return false;
            }
            instance.schemaVersion = 1;
            instance.values.push_back(MechanismValue(std::int64_t{4}));
        }
        return true;
    };
    if (migrations.Register(std::move(migration))
            != persistence::RuntimeMigrationRegisterResult::Added)
    {
        std::cerr << "Schema Migration registration failed\n";
        return 9;
    }
    migrations.Freeze();
    world::AuthoritativeWorld migratedWorld;
    runtime::KernelRuntime migratedRuntime(migratedWorld, probe.catalog);
    std::vector<std::uint8_t> legacyBytes;
    if (!persistence::RuntimeSaveCodec{}.Encode(legacy, legacyBytes))
    {
        std::cerr << "Legacy Save encoding failed\n";
        return 10;
    }
    const persistence::RuntimePersistenceReport migrated =
        persistence.Load(migratedRuntime, legacyBytes, &migrations);
    const MechanismValue* migratedLevel =
        migratedRuntime.Query().Mechanisms().FindField(
            probe.mechanismInstance,
            probe.levelSlot
        );
    if (!migrated
        || migrated.migration.status
            != persistence::RuntimeMigrationStatus::Migrated
        || migrated.migration.appliedSteps.size() != 1
        || migratedLevel == nullptr
        || *migratedLevel != MechanismValue(std::int64_t{4}))
    {
        std::cerr << "Schema Migration execution failed\n";
        return 11;
    }

    persistence::ReplayCommandLog commandLog;
    commandLog.finalTick = 3;
    WorldTransaction firstReplayCommand;
    firstReplayCommand.commands = {
        WorldCommand::AdvanceRngStream(rng, 2, 1),
        WorldCommand::Mechanism(MechanismCommand::SetField(
            probe.mechanismInstance,
            probe.levelSlot,
            MechanismValue(std::int64_t{8})
        ))
    };
    commandLog.entries.push_back({0, 1, 0, firstReplayCommand});
    commandLog.entries.push_back({1, 2, -5, SetPopulation(probe, 300)});
    runtime::AlgorithmExecutorRegistry executors;
    executors.Freeze();
    const persistence::DeterministicReplayResult firstReplay =
        persistence::DeterministicReplayService{}.Replay(
            captured,
            commandLog,
            probe.catalog,
            executors
        );
    const persistence::DeterministicReplayResult secondReplay =
        persistence::DeterministicReplayService{}.Replay(
            captured,
            commandLog,
            probe.catalog,
            executors
        );
    persistence::RuntimeSaveImage replayFinal;
    if (!firstReplay
        || !secondReplay
        || firstReplay.finalSave != secondReplay.finalSave
        || firstReplay.factStream != secondReplay.factStream
        || firstReplay.finalStateChecksum != secondReplay.finalStateChecksum
        || firstReplay.factStreamChecksum != secondReplay.factStreamChecksum
        || firstReplay.factStream.empty()
        || !persistence::RuntimeSaveCodec{}.Decode(
            firstReplay.finalSave,
            replayFinal)
        || replayFinal.worldTick != 3
        || !replayFinal.commandQueue.empty()
        || !replayFinal.scheduledInbox.empty()
        || replayFinal.rngStreams.front().drawCount != 3)
    {
        std::cerr << "Deterministic Command Log Replay failed\n";
        return 12;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Frozen save-format golden values (Demo 0.2 Kernel Contract Freeze).
    //
    // The static_asserts in runtime_save_codec.cpp pin the on-disk variant
    // TAGS. These pin everything else: field order, encoding, padding and the
    // Fact Stream layout, for a world that exercises entities, components,
    // relations, mechanism fields, an RNG stream, a scheduled event and a
    // queued command. Verified identical on Windows MSVC and Linux GCC, so a
    // mismatch is a real format change and not a platform difference.
    //
    // If one of these fails, ask which happened:
    //   * an ACCIDENTAL format change -- fix the code, do not touch the number;
    //   * a DELIBERATE format change -- bump kCurrentRuntimeSaveFormatVersion,
    //     provide a migration, update memo section 4.2, then update these.
    // Silently re-baselining a golden defeats the entire freeze.
    // ─────────────────────────────────────────────────────────────────────
    constexpr std::size_t kGoldenSaveBytes = 688;
    constexpr std::uint64_t kGoldenSaveChecksum = 7194244525752032699ULL;
    constexpr std::uint64_t kGoldenFinalStateChecksum =
        9515266196334764553ULL;
    constexpr std::uint64_t kGoldenFactStreamChecksum =
        14511951199989717232ULL;
    if (saved.size() != kGoldenSaveBytes
        || persistence::StableRuntimeChecksum(saved) != kGoldenSaveChecksum
        || firstReplay.finalStateChecksum != kGoldenFinalStateChecksum
        || firstReplay.factStreamChecksum != kGoldenFactStreamChecksum)
    {
        std::cerr << "Frozen save format drifted -- see the note above.\n"
                  << "  save bytes    : " << saved.size()
                  << " (expected " << kGoldenSaveBytes << ")\n"
                  << "  save checksum : "
                  << persistence::StableRuntimeChecksum(saved)
                  << " (expected " << kGoldenSaveChecksum << ")\n"
                  << "  final state   : " << firstReplay.finalStateChecksum
                  << " (expected " << kGoldenFinalStateChecksum << ")\n"
                  << "  fact stream   : " << firstReplay.factStreamChecksum
                  << " (expected " << kGoldenFactStreamChecksum << ")\n";
        return 13;
    }
    if (!CheckFrozenCommandEncoding())
    {
        return 14;
    }

    if (!CheckMigrationChain(probe))
    {
        return 15;
    }

    std::cout
        << "Persistence/Migration/Replay: passed (save checksum "
        << persistence::StableRuntimeChecksum(saved)
        << ", replay checksum "
        << firstReplay.finalStateChecksum
        << ")\n";
    return 0;
}
