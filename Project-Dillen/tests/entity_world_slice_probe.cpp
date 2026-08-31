#include <cstdint>
#include <iostream>
#include <string>

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
#include "ruleset.hpp"
#include "runtime_compiler.hpp"

int main()
{
    using namespace dillen;
    using namespace dillen::kernel;
    using namespace dillen::runtime;

    const EntityTypeId countryType = StableEntityTypeId(
        "dillen.entity.country"
    );
    const ComponentTypeId identityType = StableComponentTypeId(
        "dillen.component.identity"
    );
    ComponentSchema identitySchema;
    identitySchema.type = identityType;
    identitySchema.canonicalName = "dillen.component.identity";
    identitySchema.version = 1;
    MechanismFieldSchema tagField;
    tagField.name = "tag";
    tagField.kind = MechanismValueKind::String;
    tagField.required = true;
    identitySchema.fields.push_back(tagField);
    MechanismFieldSchema populationField;
    populationField.name = "population";
    populationField.kind = MechanismValueKind::Integer;
    populationField.defaultValue = MechanismValue(std::int64_t{0});
    identitySchema.fields.push_back(populationField);

    ComponentSchemaRegistry componentSchemas;
    if (componentSchemas.Register(std::move(identitySchema))
        != ComponentSchemaRegisterResult::Added)
    {
        std::cerr << "Component Schema registration failed\n";
        return 1;
    }
    componentSchemas.Freeze();

    EntityDefinition firstCountry;
    firstCountry.type = countryType;
    firstCountry.canonicalName = "country.alpha";
    firstCountry.id = StableEntityDefinitionId(
        countryType,
        firstCountry.canonicalName
    );
    firstCountry.source.sourceName = "entity_world_slice_probe";
    firstCountry.components.push_back({identityType, 1, {
        {"tag", MechanismValue(std::string("AAA"))},
        {"population", MechanismValue(std::int64_t{100})}
    }});
    EntityDefinition secondCountry;
    secondCountry.type = countryType;
    secondCountry.canonicalName = "country.beta";
    secondCountry.id = StableEntityDefinitionId(
        countryType,
        secondCountry.canonicalName
    );
    secondCountry.source.sourceName = "entity_world_slice_probe";
    secondCountry.components.push_back({identityType, 1, {
        {"tag", MechanismValue(std::string("BBB"))}
    }});
    const EntityDefinitionId firstDefinition = firstCountry.id;
    const EntityDefinitionId secondDefinition = secondCountry.id;
    const EntityId firstEntity = StableEntityId(firstDefinition);
    const EntityId secondEntity = StableEntityId(secondDefinition);

    EntityDefinitionRegistry entityDefinitions;
    if (entityDefinitions.Declare(firstCountry, componentSchemas)
            != EntityDefinitionDeclareResult::Added
        || entityDefinitions.Declare(secondCountry, componentSchemas)
            != EntityDefinitionDeclareResult::Added)
    {
        std::cerr << "Entity Definition registration failed\n";
        return 2;
    }
    entityDefinitions.Freeze();

    const MechanismTypeId mechanismType = StableMechanismTypeId(
        "dillen.mechanism.policy"
    );
    MechanismSchema mechanismSchema;
    mechanismSchema.type = mechanismType;
    mechanismSchema.canonicalName = "dillen.mechanism.policy";
    mechanismSchema.version = 1;
    MechanismFieldSchema levelField;
    levelField.name = "level";
    levelField.kind = MechanismValueKind::Integer;
    levelField.defaultValue = MechanismValue(std::int64_t{1});
    mechanismSchema.fields.push_back(levelField);
    MechanismRoleSchema ownerRole;
    ownerRole.name = "owner";
    ownerRole.referenceKind = MechanismReferenceKind::Entity;
    ownerRole.referenceType = countryType.value;
    ownerRole.minimumCount = 1;
    ownerRole.maximumCount = 1;
    mechanismSchema.roles.push_back(ownerRole);
    MechanismSchemaRegistry mechanismSchemas;
    if (mechanismSchemas.Register(std::move(mechanismSchema))
        != MechanismSchemaRegisterResult::Added)
    {
        std::cerr << "Mechanism Schema registration failed\n";
        return 3;
    }
    mechanismSchemas.Freeze();
    AlgorithmRegistry algorithms;
    algorithms.Freeze();

    const auto makeDefinition = [mechanismType, countryType](
        std::string name,
        EntityId owner)
    {
        MechanismDefinition definition;
        definition.type = mechanismType;
        definition.canonicalName = std::move(name);
        definition.id = StableMechanismDefinitionId(
            mechanismType,
            definition.canonicalName
        );
        definition.schemaVersion = 1;
        definition.roles["owner"] = {{
            MechanismReferenceKind::Entity,
            countryType.value,
            owner.value
        }};
        definition.source.sourceName = "entity_world_slice_probe";
        return definition;
    };
    MechanismDefinition active = makeDefinition("active_policy", firstEntity);
    MechanismDefinition dormant = makeDefinition("dormant_policy", secondEntity);
    const MechanismDefinitionId activeDefinition = active.id;
    const MechanismDefinitionId dormantDefinition = dormant.id;
    MechanismDefinitionRegistry mechanismDefinitions;
    if (mechanismDefinitions.Declare(
            active,
            mechanismSchemas,
            algorithms) != MechanismDefinitionDeclareResult::Added
        || mechanismDefinitions.Declare(
            dormant,
            mechanismSchemas,
            algorithms) != MechanismDefinitionDeclareResult::Added)
    {
        std::cerr << "Mechanism Definition registration failed\n";
        return 4;
    }
    mechanismDefinitions.Freeze();

    MechanismSpawnDefinition spawn;
    spawn.canonicalName = "initial_active_policies";
    spawn.definition = activeDefinition;
    spawn.id = StableMechanismSpawnDefinitionId(
        spawn.definition,
        spawn.canonicalName
    );
    spawn.count = 2;
    spawn.initialFields["level"] = MechanismValue(std::int64_t{3});
    spawn.source.sourceName = "entity_world_slice_probe";
    MechanismSpawnDefinitionRegistry spawns;
    if (spawns.Declare(spawn, mechanismDefinitions, mechanismSchemas)
        != MechanismSpawnDeclareResult::Added)
    {
        std::cerr << "Mechanism Spawn registration failed\n";
        return 5;
    }
    spawns.Freeze();

    RulesetDefinition ruleset;
    ruleset.canonicalName = "dillen.test.entity_world";
    ruleset.id = StableRulesetId(ruleset.canonicalName);
    ruleset.version = 1;
    ruleset.requiredSchemas.push_back({mechanismType, 1});
    ruleset.requiredComponents.push_back({identityType, 1});
    ruleset.requiredDefinitions = {activeDefinition, dormantDefinition};
    ruleset.requiredEntityDefinitions = {
        firstDefinition,
        secondDefinition
    };
    ruleset.requiredMechanismSpawns = {spawn.id};
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
        std::cerr << "Package Lock construction failed\n";
        return 6;
    }

    FrozenRuntimeCatalog catalog;
    RuntimeCompileReport compileReport;
    RuntimeCapabilityContractRegistry capabilityContracts;
    capabilityContracts.Freeze();
    if (!RuntimeCompiler{}.Compile(
            ruleset,
            packageLock,
            mechanismSchemas,
            componentSchemas,
            algorithms,
            mechanismDefinitions,
            entityDefinitions,
            spawns,
            capabilityContracts,
            catalog,
            compileReport))
    {
        std::cerr << "Runtime Catalog compilation failed\n";
        return 7;
    }

    world::AuthoritativeWorld authoritativeWorld;
    world::InitialWorldBuildReport worldReport;
    if (!world::InitialWorldBuilder{}.Build(
            catalog,
            authoritativeWorld,
            worldReport))
    {
        std::cerr << "Initial World construction failed\n";
        return 8;
    }
    const ComponentFieldSlotId populationSlot =
        *catalog.ResolveComponentFieldSlot(identityType, 1, "population");
    const MechanismFieldSlotId levelSlot =
        *catalog.ResolveDefinitionFieldSlot(activeDefinition, "level");
    const MechanismRoleSlotId ownerSlot =
        *catalog.ResolveRoleSlot(mechanismType, 1, "owner");
    const world::ComponentRecord* firstIdentity =
        authoritativeWorld.Components().Find(firstEntity, identityType);
    const world::ComponentRecord* secondIdentity =
        authoritativeWorld.Components().Find(secondEntity, identityType);
    if (authoritativeWorld.Entities().Size() != 2
        || authoritativeWorld.Components().Size() != 2
        || authoritativeWorld.Mechanisms().Size() != 2
        || authoritativeWorld.Mechanisms().FindByDefinition(
            activeDefinition).size() != 2
        || !authoritativeWorld.Mechanisms().FindByDefinition(
            dormantDefinition).empty()
        || firstIdentity == nullptr
        || secondIdentity == nullptr
        || firstIdentity->values[populationSlot.value]
            != MechanismValue(std::int64_t{100})
        || secondIdentity->values[populationSlot.value]
            != MechanismValue(std::int64_t{0}))
    {
        std::cerr << "Authoritative Entity/Component/Spawn state mismatch\n";
        return 9;
    }

    world::RelationIndex relations;
    const RelationTypeId allianceType = StableRelationTypeId(
        "dillen.relation.alliance"
    );
    RelationId relation;
    if (relations.Add(
            allianceType,
            firstEntity,
            secondEntity,
            authoritativeWorld.Entities(),
            relation) != world::RelationAddResult::Added
        || relations.Outgoing(allianceType, firstEntity).size() != 1
        || relations.Incoming(allianceType, secondEntity).size() != 1
        || relations.Find(relation) == nullptr)
    {
        std::cerr << "Relation Index mismatch\n";
        return 10;
    }

    world::AuthoritativeWorld transactionWorld;
    runtime::KernelRuntime runtime(transactionWorld, catalog);
    const MechanismInstanceId spawnedMechanism =
        StableMechanismInstanceId(activeDefinition, 0);
    const RngStreamId simulationRng = StableRngStreamId(
        "dillen.rng.simulation"
    );
    const AlgorithmEventTypeId lowPriorityEvent =
        StableAlgorithmEventTypeId("dillen.event.low_priority");
    const AlgorithmEventTypeId normalPriorityEvent =
        StableAlgorithmEventTypeId("dillen.event.normal_priority");
    const AlgorithmEventTypeId secondNormalPriorityEvent =
        StableAlgorithmEventTypeId("dillen.event.normal_priority_second");
    const AlgorithmEventTypeId highPriorityEvent =
        StableAlgorithmEventTypeId("dillen.event.high_priority");

    WorldTransaction initialTransaction;
    initialTransaction.commands = {
        WorldCommand::CreateEntity(firstDefinition),
        WorldCommand::CreateEntity(secondDefinition),
        WorldCommand::SetComponentField(
            firstEntity,
            identityType,
            populationSlot,
            MechanismValue(std::int64_t{150})
        ),
        WorldCommand::AddRelation(
            allianceType,
            firstEntity,
            secondEntity
        ),
        WorldCommand::SpawnMechanism(spawn.id),
        WorldCommand::CreateRngStream(simulationRng, 0xD111EULL),
        WorldCommand::AdvanceRngStream(simulationRng, 0, 2),
        WorldCommand::ScheduleEvent(
            highPriorityEvent,
            spawnedMechanism,
            2,
            10,
            MechanismValue(std::string("high"))
        ),
        WorldCommand::ScheduleEvent(
            lowPriorityEvent,
            spawnedMechanism,
            2,
            -10,
            MechanismValue(std::string("low"))
        ),
        WorldCommand::ScheduleEvent(
            normalPriorityEvent,
            spawnedMechanism,
            2,
            0,
            MechanismValue(std::string("normal"))
        ),
        WorldCommand::ScheduleEvent(
            secondNormalPriorityEvent,
            spawnedMechanism,
            2,
            0,
            MechanismValue(std::string("normal_second"))
        )
    };
    const WorldTransactionResult initialCommit =
        runtime.ApplyImmediate(initialTransaction, 0);
    const world::ComponentRecord* committedIdentity =
        transactionWorld.Components().Find(firstEntity, identityType);
    const DeterministicRngStream* committedRng =
        transactionWorld.RngStreams().Find(simulationRng);
    const auto& scheduledEvents =
        transactionWorld.AlgorithmEvents().Pending();
    if (!initialCommit
        || initialCommit.commandIndex != initialTransaction.commands.size()
        || transactionWorld.Entities().Size() != 2
        || transactionWorld.Components().Size() != 2
        || transactionWorld.Relations().Size() != 1
        || transactionWorld.Mechanisms().Find(spawnedMechanism) == nullptr
        || committedIdentity == nullptr
        || committedIdentity->values[populationSlot.value]
            != MechanismValue(std::int64_t{150})
        || committedRng == nullptr
        || committedRng->drawCount != 2
        || scheduledEvents.size() != 4
        || scheduledEvents[0].type != lowPriorityEvent
        || scheduledEvents[1].type != normalPriorityEvent
        || scheduledEvents[2].type != secondNormalPriorityEvent
        || scheduledEvents[1].sequence >= scheduledEvents[2].sequence
        || scheduledEvents[3].type != highPriorityEvent
        || transactionWorld.Revision() != 1
        || runtime.RngSnapshot().Preview(simulationRng, 0)
            != DeterministicRngValue(0xD111EULL, 2))
    {
        std::cerr << "Unified World transaction commit mismatch\n";
        return 11;
    }

    const runtime::WorldQuerySnapshotHandle committedSnapshot =
        runtime.AcquireQuerySnapshot();
    if (committedSnapshot == nullptr)
    {
        std::cerr << "World Query snapshot publication missing\n";
        return 12;
    }
    const MechanismValue* committedPopulation =
        committedSnapshot->Components().FindField(
            firstEntity,
            identityType,
            populationSlot
        );
    if (!committedSnapshot->IsPublished()
        || committedSnapshot->Tick() != transactionWorld.Tick()
        || committedSnapshot->Revision() != transactionWorld.Revision()
        || committedSnapshot->Mechanisms().Tick()
            != committedSnapshot->Tick()
        || committedSnapshot->Mechanisms().Revision()
            != committedSnapshot->Revision()
        || committedSnapshot->Entities().Find(firstEntity) == nullptr
        || committedSnapshot->Entities().FindByDefinition(
            firstDefinition).size() != 1
        || committedSnapshot->Entities().FindByType(countryType).size() != 2
        || committedSnapshot->Components().FindOwners(identityType).size()
            != 2
        || committedSnapshot->Components().FindTypes(firstEntity).size()
            != 1
        || committedPopulation == nullptr
        || *committedPopulation != MechanismValue(std::int64_t{150})
        || committedSnapshot->Relations().Find(relation) == nullptr
        || committedSnapshot->Relations().FindByType(allianceType).size()
            != 1
        || committedSnapshot->Relations().Outgoing(
            allianceType,
            firstEntity).size() != 1
        || committedSnapshot->Relations().Incoming(
            allianceType,
            secondEntity).size() != 1
        || committedSnapshot->Mechanisms().Find(spawnedMechanism) == nullptr
        || committedSnapshot->Mechanisms().FindField(
            spawnedMechanism,
            levelSlot) == nullptr
        || *committedSnapshot->Mechanisms().FindField(
            spawnedMechanism,
            levelSlot) != MechanismValue(std::int64_t{3})
        || committedSnapshot->Mechanisms().FindRole(
            spawnedMechanism,
            ownerSlot) == nullptr
        || committedSnapshot->Mechanisms().FindRole(
            spawnedMechanism,
            ownerSlot)->front().value != firstEntity.value
        || runtime.Query().Publication()
            != committedSnapshot->Publication())
    {
        std::cerr << "Consistent World Query snapshot mismatch\n";
        return 12;
    }

    const RelationId allianceId = StableRelationId(
        allianceType,
        firstEntity,
        secondEntity
    );
    WorldTransaction removeAndCancelTransaction;
    removeAndCancelTransaction.commands = {
        WorldCommand::RemoveRelation(allianceId),
        WorldCommand::AddRelation(
            allianceType,
            firstEntity,
            secondEntity
        ),
        WorldCommand::CancelEvent(4),
        WorldCommand::ScheduleEvent(
            secondNormalPriorityEvent,
            spawnedMechanism,
            2,
            0,
            MechanismValue(std::string("normal_second_rescheduled"))
        )
    };
    const WorldTransactionResult removeAndCancelCommit =
        runtime.ApplyImmediate(removeAndCancelTransaction, 0);
    const auto& rescheduledEvents =
        transactionWorld.AlgorithmEvents().Pending();
    if (!removeAndCancelCommit
        || transactionWorld.Relations().Find(allianceId) == nullptr
        || rescheduledEvents.size() != 4
        || rescheduledEvents[1].type != normalPriorityEvent
        || rescheduledEvents[2].type != secondNormalPriorityEvent
        || rescheduledEvents[1].sequence >= rescheduledEvents[2].sequence
        || transactionWorld.Revision() != 2)
    {
        std::cerr << "Relation removal or Event cancellation mismatch\n";
        return 13;
    }

    WorldTransaction rejectedTransaction;
    rejectedTransaction.commands = {
        WorldCommand::SetComponentField(
            firstEntity,
            identityType,
            populationSlot,
            MechanismValue(std::int64_t{200})
        ),
        WorldCommand::AddRelation(
            allianceType,
            firstEntity,
            secondEntity
        ),
        WorldCommand::AdvanceRngStream(simulationRng, 2, 1)
    };
    const WorldTransactionResult rejectedCommit =
        runtime.ApplyImmediate(rejectedTransaction, 0);
    const world::ComponentRecord* rolledBackIdentity =
        transactionWorld.Components().Find(firstEntity, identityType);
    if (rejectedCommit
        || rejectedCommit.status != WorldTransactionStatus::RelationRejected
        || rejectedCommit.commandIndex != 1
        || rolledBackIdentity == nullptr
        || rolledBackIdentity->values[populationSlot.value]
            != MechanismValue(std::int64_t{150})
        || transactionWorld.RngStreams().Find(simulationRng)->drawCount != 2
        || transactionWorld.Revision() != 2)
    {
        std::cerr << "Unified World transaction rollback mismatch\n";
        return 14;
    }

    WorldTransaction rngConflictTransaction;
    rngConflictTransaction.commands = {
        WorldCommand::SetComponentField(
            firstEntity,
            identityType,
            populationSlot,
            MechanismValue(std::int64_t{210})
        ),
        WorldCommand::AdvanceRngStream(simulationRng, 0, 1)
    };
    const WorldTransactionResult rngConflict =
        runtime.ApplyImmediate(rngConflictTransaction, 0);
    if (rngConflict
        || rngConflict.status != WorldTransactionStatus::RngRejected
        || rngConflict.commandIndex != 1
        || transactionWorld.Components().Find(
            firstEntity,
            identityType)->values[populationSlot.value]
            != MechanismValue(std::int64_t{150})
        || transactionWorld.RngStreams().Find(simulationRng)->drawCount != 2
        || transactionWorld.Revision() != 2)
    {
        std::cerr << "RNG optimistic transaction rollback mismatch\n";
        return 15;
    }

    const auto populationTransaction = [=](std::int64_t population)
    {
        WorldTransaction transaction;
        transaction.commands.push_back(WorldCommand::SetComponentField(
            firstEntity,
            identityType,
            populationSlot,
            MechanismValue(population)
        ));
        return transaction;
    };
    runtime.Enqueue(populationTransaction(110), 1, 10);
    runtime.Enqueue(populationTransaction(120), 1, -10);
    runtime.Enqueue(populationTransaction(130), 1, 0);
    runtime.Enqueue(populationTransaction(140), 1, 0);
    const MechanismSchedulerTickResult orderedTick = runtime.RunTick(1);
    const world::ComponentRecord* orderedIdentity =
        transactionWorld.Components().Find(firstEntity, identityType);
    if (!orderedTick
        || orderedTick.transactions.size() != 4
        || orderedTick.transactions[0].priority != -10
        || orderedTick.transactions[1].priority != 0
        || orderedTick.transactions[2].priority != 0
        || orderedTick.transactions[1].sequence
            >= orderedTick.transactions[2].sequence
        || orderedTick.transactions[3].priority != 10
        || orderedIdentity == nullptr
        || orderedIdentity->values[populationSlot.value]
            != MechanismValue(std::int64_t{110})
        || transactionWorld.AlgorithmEvents().Size() != 4)
    {
        std::cerr << "Stable World command ordering mismatch\n";
        return 16;
    }

    const runtime::WorldQuerySnapshotHandle orderedSnapshot =
        runtime.AcquireQuerySnapshot();
    if (orderedSnapshot == nullptr)
    {
        std::cerr << "Updated World Query snapshot publication missing\n";
        return 17;
    }
    const MechanismValue* frozenPopulation =
        committedSnapshot->Components().FindField(
            firstEntity,
            identityType,
            populationSlot
        );
    const MechanismValue* orderedPopulation =
        orderedSnapshot->Components().FindField(
            firstEntity,
            identityType,
            populationSlot
        );
    if (orderedSnapshot->Publication()
            <= committedSnapshot->Publication()
        || orderedSnapshot->Tick() != 1
        || orderedSnapshot->Revision() != transactionWorld.Revision()
        || frozenPopulation == nullptr
        || *frozenPopulation != MechanismValue(std::int64_t{150})
        || orderedPopulation == nullptr
        || *orderedPopulation != MechanismValue(std::int64_t{110}))
    {
        std::cerr << "Stable World Query generation mismatch\n";
        return 17;
    }

    const std::uint64_t revisionBeforeInbox = transactionWorld.Revision();
    const MechanismSchedulerTickResult inboxTick = runtime.RunTick(2);
    if (!inboxTick
        || !transactionWorld.AlgorithmEvents().Empty()
        || transactionWorld.Revision() != revisionBeforeInbox + 1)
    {
        std::cerr << "Authoritative Algorithm Inbox mismatch\n";
        return 18;
    }

    // The Query Snapshots share the stores' copy-on-write payloads instead of
    // rebuilding their own secondary indexes, which is only sound while the
    // stores keep those indexes in the order the snapshots used to produce:
    // ascending id (kernel/sorted_id_index.hpp). A regression here silently
    // reorders every Query that iterates an index, and therefore the commands
    // it emits and the bytes they save to.
    //
    // This world holds only a couple of ids per index, so it is a breadth
    // check -- it covers all six index kinds, but is too small to be sure a
    // creation-order regression would land out of order. scale_probe asserts
    // the same property over 250 ids, where it cannot pass by luck.
    const auto ascending = [](const auto& ids)
    {
        for (std::size_t index = 1; index < ids.size(); ++index)
        {
            if (!(ids[index - 1] < ids[index]))
            {
                return false;
            }
        }
        return true;
    };
    const runtime::WorldQuerySnapshot& indexQuery = runtime.Query();
    const std::vector<EntityId>& storedByType =
        transactionWorld.Entities().FindByType(countryType);
    const std::vector<EntityId>& storedByDefinition =
        transactionWorld.Entities().FindByDefinition(firstDefinition);
    const std::vector<EntityId>& storedOwners =
        transactionWorld.Components().FindOwners(identityType);
    const std::vector<ComponentTypeId>& storedTypes =
        transactionWorld.Components().FindTypes(firstEntity);
    const std::vector<RelationId>& storedRelations =
        transactionWorld.Relations().FindByType(allianceType);
    const std::vector<MechanismInstanceId>& storedInstances =
        transactionWorld.Mechanisms().FindByType(mechanismType);
    if (storedByType.empty()
        || storedOwners.empty()
        || storedTypes.empty()
        || storedRelations.empty()
        || storedInstances.empty()
        || !ascending(storedByType)
        || !ascending(storedByDefinition)
        || !ascending(storedOwners)
        || !ascending(storedTypes)
        || !ascending(storedRelations)
        || !ascending(storedInstances)
        || !ascending(indexQuery.Entities().FindByType(countryType))
        || !ascending(indexQuery.Entities().FindByDefinition(firstDefinition))
        || !ascending(indexQuery.Components().FindOwners(identityType))
        || !ascending(indexQuery.Components().FindTypes(firstEntity))
        || !ascending(indexQuery.Relations().FindByType(allianceType))
        || !ascending(indexQuery.Mechanisms().FindByType(mechanismType)))
    {
        std::cerr << "Query Snapshot secondary index order mismatch\n";
        return 19;
    }

    std::cout
        << "Entity/Component/Relation/Spawn/Transaction slice: passed ("
        << authoritativeWorld.Entities().Size()
        << " entities, "
        << authoritativeWorld.Mechanisms().Size()
        << " explicit mechanism instances)\n";
    return 0;
}
