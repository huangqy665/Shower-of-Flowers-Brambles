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

    std::cout
        << "Persistence/Migration/Replay: passed (save checksum "
        << persistence::StableRuntimeChecksum(saved)
        << ", replay checksum "
        << firstReplay.finalStateChecksum
        << ")\n";
    return 0;
}
