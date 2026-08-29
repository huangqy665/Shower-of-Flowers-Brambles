#include "runtime_persistence.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "mechanism_schema.hpp"

namespace dillen::persistence {

namespace {

RuntimePersistenceReport Failure(
    RuntimePersistenceStatus status,
    std::string message
)
{
    RuntimePersistenceReport report;
    report.status = status;
    report.message = std::move(message);
    return report;
}

bool ValidReferenceKind(kernel::MechanismReferenceKind kind) noexcept
{
    return kind >= kernel::MechanismReferenceKind::Entity
        && kind <= kernel::MechanismReferenceKind::Custom;
}

bool ValidLifecycle(kernel::MechanismLifecycleState state) noexcept
{
    return state >= kernel::MechanismLifecycleState::Created
        && state <= kernel::MechanismLifecycleState::Failed;
}

bool ValidFaultCode(kernel::AlgorithmFaultCode code) noexcept
{
    return code == kernel::AlgorithmFaultCode::None
        || kernel::IsAuthoritativeAlgorithmFaultCode(code);
}

bool ValidFaultStage(kernel::AlgorithmFaultStage stage) noexcept
{
    return stage >= kernel::AlgorithmFaultStage::Create
        && stage <= kernel::AlgorithmFaultStage::Destroy;
}

bool ValidateReference(
    const kernel::MechanismReference& reference,
    const world::EntityRegistry& entities,
    const kernel::MechanismInstanceStore& mechanisms,
    const kernel::FrozenRuntimeCatalog& catalog
)
{
    if (!ValidReferenceKind(reference.kind) || reference.value == 0)
    {
        return false;
    }
    switch (reference.kind)
    {
    case kernel::MechanismReferenceKind::Entity:
    {
        const world::EntityRecord* entity = entities.Find(
            kernel::EntityId{reference.value}
        );
        return entity != nullptr
            && (reference.type == 0 || entity->type.value == reference.type);
    }
    case kernel::MechanismReferenceKind::MechanismDefinition:
        return catalog.FindDefinition(
            kernel::MechanismDefinitionId{reference.value}) != nullptr;
    case kernel::MechanismReferenceKind::MechanismInstance:
        return mechanisms.Find(
            kernel::MechanismInstanceId{reference.value}) != nullptr;
    case kernel::MechanismReferenceKind::Resource:
    case kernel::MechanismReferenceKind::Custom:
        return reference.type != 0;
    }
    return false;
}

bool ValidateValueReferences(
    const kernel::MechanismValue& value,
    const world::EntityRegistry& entities,
    const kernel::MechanismInstanceStore& mechanisms,
    const kernel::FrozenRuntimeCatalog& catalog,
    std::size_t depth = 0
)
{
    if (depth > 64)
    {
        return false;
    }
    if (const auto* reference = std::get_if<kernel::MechanismReference>(
            &value.data))
    {
        return ValidateReference(*reference, entities, mechanisms, catalog);
    }
    if (const auto* list = std::get_if<kernel::MechanismValue::List>(
            &value.data))
    {
        return std::all_of(
            list->begin(),
            list->end(),
            [&](const kernel::MechanismValue& item)
            {
                return ValidateValueReferences(
                    item,
                    entities,
                    mechanisms,
                    catalog,
                    depth + 1
                );
            }
        );
    }
    if (const auto* object = std::get_if<kernel::MechanismValue::Object>(
            &value.data))
    {
        return std::all_of(
            object->begin(),
            object->end(),
            [&](const auto& item)
            {
                return ValidateValueReferences(
                    item.second,
                    entities,
                    mechanisms,
                    catalog,
                    depth + 1
                );
            }
        );
    }
    return true;
}

bool ValidCommand(const kernel::WorldCommand& command)
{
    return std::visit(
        [](const auto& operation) -> bool
        {
            using Operation = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<
                    Operation,
                    kernel::EntityCreateCommand>)
            {
                return static_cast<bool>(operation.definition);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::ComponentSetFieldCommand>)
            {
                return operation.owner
                    && operation.component
                    && operation.field;
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RelationAddCommand>)
            {
                return operation.type && operation.source && operation.target;
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RelationRemoveCommand>)
            {
                return static_cast<bool>(operation.relation);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismSpawnCommand>)
            {
                return static_cast<bool>(operation.spawn);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismCommand>)
            {
                return operation.target
                    && std::visit(
                        [](const auto& mechanismOperation) -> bool
                        {
                            using MechanismOperation = std::decay_t<
                                decltype(mechanismOperation)>;
                            if constexpr (std::is_same_v<
                                    MechanismOperation,
                                    kernel::MechanismSetFieldOperation>)
                            {
                                return static_cast<bool>(
                                    mechanismOperation.field
                                );
                            }
                            else if constexpr (std::is_same_v<
                                    MechanismOperation,
                                    kernel::MechanismTransitionLifecycleOperation>)
                            {
                                return ValidLifecycle(
                                    mechanismOperation.target
                                );
                            }
                            else if constexpr (std::is_same_v<
                                    MechanismOperation,
                                    kernel::MechanismRecordAlgorithmFaultOperation>)
                            {
                                return mechanismOperation.code
                                        != kernel::AlgorithmFaultCode::None
                                    && ValidFaultCode(mechanismOperation.code)
                                    && ValidFaultStage(mechanismOperation.stage);
                            }
                            return true;
                        },
                        operation.operation
                    );
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::ScheduledEventScheduleCommand>)
            {
                return operation.type && operation.dueTick != 0;
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::ScheduledEventCancelCommand>)
            {
                return operation.sequence != 0;
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RngStreamCreateCommand>)
            {
                return static_cast<bool>(operation.stream);
            }
            else
            {
                return operation.stream && operation.count != 0;
            }
        },
        command.payload
    );
}

}

bool RuntimePersistenceService::BuildCandidate(
    const RuntimeSaveImage& image,
    const kernel::FrozenRuntimeCatalog& catalog,
    world::AuthoritativeWorld& world,
    kernel::WorldCommandQueue& commands,
    std::uint64_t& nextFactSequence,
    std::string& message
)
{
    world::EntityRegistry entities;
    for (const world::EntityRecord& record : image.entities)
    {
        const kernel::CompiledEntityDefinition* definition =
            catalog.FindEntityDefinition(record.definition);
        if (!record.id
            || definition == nullptr
            || definition->type != record.type
            || record.id != kernel::StableEntityId(record.definition)
            || !entities.entities_.emplace(record.id, record).second)
        {
            message = "Entity state is incompatible with the Runtime Catalog";
            return false;
        }
    }
    for (const auto& entry : entities.entities_)
    {
        entities.entitiesByType_[entry.second.type].push_back(entry.first);
    }

    world::ComponentStore components;
    for (const world::ComponentRecord& record : image.components)
    {
        const kernel::CompiledComponentLayout* layout =
            catalog.FindComponentLayout(record.type, record.schemaVersion);
        if (!record.owner
            || !record.type
            || entities.Find(record.owner) == nullptr
            || layout == nullptr
            || layout->fields.size() != record.values.size())
        {
            message = "Component state is incompatible with the Runtime Catalog";
            return false;
        }
        for (std::size_t index = 0; index < record.values.size(); ++index)
        {
            if (!kernel::MechanismValueMatchesSchema(
                    layout->fields[index],
                    record.values[index]))
            {
                message = "Component field value violates its frozen layout";
                return false;
            }
        }
        const world::ComponentStore::ComponentKey key{
            record.owner,
            record.type
        };
        if (!components.components_.emplace(key, record).second)
        {
            message = "Component state contains a duplicate key";
            return false;
        }
    }
    for (const auto& entry : components.components_)
    {
        components.ownersByType_[entry.second.type].push_back(
            entry.second.owner
        );
    }
    for (const auto& entry : entities.entities_)
    {
        const kernel::CompiledEntityDefinition* definition =
            catalog.FindEntityDefinition(entry.second.definition);
        for (const kernel::CompiledEntityComponentDefinition& required
            : definition->components)
        {
            const world::ComponentRecord* component = components.Find(
                entry.first,
                required.type
            );
            if (component == nullptr
                || component->schemaVersion != required.schemaVersion)
            {
                message = "Entity state is missing a required Component";
                return false;
            }
        }
    }

    world::RelationIndex relations;
    for (const world::RelationRecord& record : image.relations)
    {
        if (!record.id
            || !record.type
            || entities.Find(record.source) == nullptr
            || entities.Find(record.target) == nullptr
            || record.id != kernel::StableRelationId(
                record.type,
                record.source,
                record.target)
            || !relations.relations_.emplace(record.id, record).second)
        {
            message = "Relation state is invalid or references a missing Entity";
            return false;
        }
    }
    for (const auto& entry : relations.relations_)
    {
        const world::RelationRecord& record = entry.second;
        relations.outgoing_[{record.type, record.source}].push_back(record.id);
        relations.incoming_[{record.type, record.target}].push_back(record.id);
    }

    kernel::MechanismInstanceStore mechanisms;
    mechanisms.nextOrdinalByDefinition_ =
        image.nextMechanismOrdinalByDefinition;
    for (const auto& next : mechanisms.nextOrdinalByDefinition_)
    {
        if (catalog.FindDefinition(next.first) == nullptr)
        {
            message = "Mechanism ordinal state names an unknown Definition";
            return false;
        }
    }
    for (const kernel::MechanismInstance& instance : image.mechanisms)
    {
        const kernel::CompiledMechanismDefinition* definition =
            catalog.FindDefinition(instance.definition);
        const kernel::CompiledMechanismLayout* layout =
            catalog.FindLayout(instance.type, instance.schemaVersion);
        const auto nextOrdinal = mechanisms.nextOrdinalByDefinition_.find(
            instance.definition
        );
        if (!instance.id
            || definition == nullptr
            || layout == nullptr
            || definition->type != instance.type
            || definition->schemaVersion != instance.schemaVersion
            || definition->algorithm != instance.algorithm
            || definition->algorithmVersion != instance.algorithmVersion
            || instance.id != kernel::StableMechanismInstanceId(
                instance.definition,
                instance.creationOrdinal)
            || instance.values.size() != layout->fields.size()
            || instance.roles.size() != layout->roles.size()
            || !ValidLifecycle(instance.lifecycle)
            || !ValidFaultCode(instance.algorithmFault.code)
            || !ValidFaultStage(instance.algorithmFault.stage)
            || (instance.algorithmFault.isolated
                && (instance.algorithmFault.code
                        == kernel::AlgorithmFaultCode::None
                    || instance.algorithmFault.failureCount == 0
                    || instance.algorithmFault.tick > image.worldTick))
            || (!instance.algorithmFault.isolated
                && (instance.algorithmFault.code
                        != kernel::AlgorithmFaultCode::None
                    || instance.algorithmFault.failureCount != 0
                    || instance.algorithmFault.tick != 0))
            || instance.updatedTick < instance.createdTick
            || instance.updatedTick > image.worldTick
            || nextOrdinal == mechanisms.nextOrdinalByDefinition_.end()
            || nextOrdinal->second <= instance.creationOrdinal
            || !mechanisms.instances_.emplace(instance.id, instance).second)
        {
            message = "Mechanism state is incompatible with its frozen Definition";
            return false;
        }
        for (std::size_t index = 0; index < instance.values.size(); ++index)
        {
            if (!kernel::MechanismValueMatchesSchema(
                    layout->fields[index],
                    instance.values[index]))
            {
                message = "Mechanism field value violates its frozen layout";
                return false;
            }
        }
        for (std::size_t index = 0; index < instance.roles.size(); ++index)
        {
            const kernel::MechanismRoleSchema& role = layout->roles[index];
            const auto& references = instance.roles[index];
            if (references.size() < role.minimumCount
                || (role.maximumCount
                    && references.size() > *role.maximumCount))
            {
                message = "Mechanism role cardinality violates its frozen layout";
                return false;
            }
            for (const kernel::MechanismReference& reference : references)
            {
                if (reference.kind != role.referenceKind
                    || (role.referenceType
                        && reference.type != *role.referenceType))
                {
                    message = "Mechanism role reference violates its frozen layout";
                    return false;
                }
            }
        }
    }
    for (const auto& entry : mechanisms.instances_)
    {
        const kernel::MechanismInstance& instance = entry.second;
        mechanisms.instancesByDefinition_[instance.definition].push_back(
            instance.id
        );
        mechanisms.instancesByType_[instance.type].push_back(instance.id);
    }

    for (const auto& entry : components.components_)
    {
        for (const kernel::MechanismValue& value : entry.second.values)
        {
            if (!ValidateValueReferences(value, entities, mechanisms, catalog))
            {
                message = "Component value contains a dangling authority reference";
                return false;
            }
        }
    }
    for (const auto& entry : mechanisms.instances_)
    {
        const kernel::MechanismInstance& instance = entry.second;
        for (const kernel::MechanismValue& value : instance.values)
        {
            if (!ValidateValueReferences(value, entities, mechanisms, catalog))
            {
                message = "Mechanism value contains a dangling authority reference";
                return false;
            }
        }
        for (const auto& role : instance.roles)
        {
            for (const kernel::MechanismReference& reference : role)
            {
                if (!ValidateReference(reference, entities, mechanisms, catalog))
                {
                    message = "Mechanism role contains a dangling authority reference";
                    return false;
                }
            }
        }
        for (const kernel::MechanismValue& value : instance.algorithmState)
        {
            if (!ValidateValueReferences(value, entities, mechanisms, catalog))
            {
                message = "Algorithm state contains a dangling authority reference";
                return false;
            }
        }
    }

    kernel::AlgorithmInbox inbox;
    inbox.pending_ = image.scheduledInbox;
    inbox.nextSequence_ = image.nextScheduledEventSequence;
    std::set<std::uint64_t> inboxSequences;
    std::uint64_t maximumInboxSequence = 0;
    for (const kernel::ScheduledAlgorithmEvent& event : inbox.pending_)
    {
        if (event.sequence == 0
            || !event.type
            || event.dueTick <= image.worldTick
            || (event.target && mechanisms.Find(event.target) == nullptr)
            || !ValidateValueReferences(
                event.payload,
                entities,
                mechanisms,
                catalog)
            || !inboxSequences.insert(event.sequence).second)
        {
            message = "Scheduled Algorithm Inbox contains an invalid event";
            return false;
        }
        maximumInboxSequence = std::max(maximumInboxSequence, event.sequence);
    }
    if (inbox.nextSequence_ == 0
        || inbox.nextSequence_ <= maximumInboxSequence)
    {
        message = "Scheduled Algorithm Inbox sequence regressed";
        return false;
    }
    inbox.SortPending();

    kernel::DeterministicRngRegistry rngStreams;
    for (const kernel::DeterministicRngStream& stream : image.rngStreams)
    {
        if (!stream.id
            || !rngStreams.streams_.emplace(stream.id, stream).second)
        {
            message = "RNG Registry contains an invalid or duplicate stream";
            return false;
        }
    }

    kernel::WorldCommandQueue commandQueue;
    commandQueue.pending_ = image.commandQueue;
    commandQueue.nextSequence_ = image.nextCommandSequence;
    std::set<std::uint64_t> commandSequences;
    std::uint64_t maximumCommandSequence = 0;
    for (const kernel::QueuedWorldTransaction& queued : commandQueue.pending_)
    {
        if (queued.sequence == 0
            || !commandSequences.insert(queued.sequence).second
            || !std::all_of(
                queued.transaction.commands.begin(),
                queued.transaction.commands.end(),
                ValidCommand))
        {
            message = "World Command Queue contains an invalid transaction";
            return false;
        }
        maximumCommandSequence = std::max(
            maximumCommandSequence,
            queued.sequence
        );
    }
    if (commandQueue.nextSequence_ == 0
        || commandQueue.nextSequence_ <= maximumCommandSequence)
    {
        message = "World Command Queue sequence regressed";
        return false;
    }
    if (image.nextFactSequence == 0)
    {
        message = "Fact Stream sequence is invalid";
        return false;
    }

    world = world::AuthoritativeWorld(
        std::move(entities),
        std::move(components),
        std::move(relations),
        std::move(mechanisms),
        std::move(inbox),
        std::move(rngStreams),
        image.worldTick,
        image.worldRevision
    );
    commands = std::move(commandQueue);
    nextFactSequence = image.nextFactSequence;
    return true;
}

RuntimePersistenceReport::operator bool() const noexcept
{
    return status == RuntimePersistenceStatus::Completed;
}

RuntimeSaveIdentity RuntimePersistenceService::IdentityFor(
    const kernel::FrozenRuntimeCatalog& catalog
)
{
    RuntimeSaveIdentity identity;
    identity.ruleset = catalog.ActiveRuleset();
    identity.rulesetVersion = catalog.ActiveRulesetVersion();
    identity.rulesetExtensions = catalog.RulesetExtensions();
    identity.rulesetFingerprint = catalog.Fingerprint();
    identity.packageLock = catalog.LockedPackages().Entries();
    identity.sourceLock = catalog.LockedSources().Entries();
    return identity;
}

RuntimePersistenceReport RuntimePersistenceService::Capture(
    const runtime::KernelRuntime& runtime,
    RuntimeSaveImage& output
) const
{
    if (!runtime.catalog_.IsFrozen())
    {
        return Failure(
            RuntimePersistenceStatus::RuntimeCatalogNotFrozen,
            "Frozen Runtime Catalog is required for persistence"
        );
    }
    RuntimeSaveImage image;
    image.identity = IdentityFor(runtime.catalog_);
    image.worldTick = runtime.world_.tick_;
    image.worldRevision = runtime.world_.revision_;
    for (const auto& entry : runtime.world_.entities_.entities_)
    {
        image.entities.push_back(entry.second);
    }
    for (const auto& entry : runtime.world_.components_.components_)
    {
        image.components.push_back(entry.second);
    }
    for (const auto& entry : runtime.world_.relations_.relations_)
    {
        image.relations.push_back(entry.second);
    }
    for (const auto& entry : runtime.world_.mechanisms_.instances_)
    {
        image.mechanisms.push_back(entry.second);
    }
    image.nextMechanismOrdinalByDefinition =
        runtime.world_.mechanisms_.nextOrdinalByDefinition_;
    image.scheduledInbox = runtime.world_.algorithmInbox_.pending_;
    image.nextScheduledEventSequence =
        runtime.world_.algorithmInbox_.nextSequence_;
    for (const auto& entry : runtime.world_.rngStreams_.streams_)
    {
        image.rngStreams.push_back(entry.second);
    }
    image.commandQueue = runtime.commands_.pending_;
    image.nextCommandSequence = runtime.commands_.nextSequence_;
    image.nextFactSequence = runtime.events_.nextSequence_;

    world::AuthoritativeWorld candidateWorld;
    kernel::WorldCommandQueue candidateCommands;
    std::uint64_t candidateFactSequence = 0;
    std::string message;
    if (!BuildCandidate(
            image,
            runtime.catalog_,
            candidateWorld,
            candidateCommands,
            candidateFactSequence,
            message))
    {
        return Failure(
            RuntimePersistenceStatus::InvalidWorldState,
            std::move(message)
        );
    }
    output = std::move(image);
    return {};
}

RuntimePersistenceReport RuntimePersistenceService::Save(
    const runtime::KernelRuntime& runtime,
    std::vector<std::uint8_t>& output
) const
{
    RuntimeSaveImage image;
    RuntimePersistenceReport report = Capture(runtime, image);
    if (!report)
    {
        return report;
    }
    report.codec = RuntimeSaveCodec{}.Encode(image, output);
    if (!report.codec)
    {
        report.status = RuntimePersistenceStatus::CodecFailed;
        report.message = report.codec.message;
    }
    return report;
}

RuntimePersistenceReport RuntimePersistenceService::Restore(
    runtime::KernelRuntime& runtime,
    RuntimeSaveImage image,
    const RuntimeMigrationRegistry* migrations
) const
{
    if (!runtime.catalog_.IsFrozen())
    {
        return Failure(
            RuntimePersistenceStatus::RuntimeCatalogNotFrozen,
            "Frozen Runtime Catalog is required for persistence"
        );
    }
    const RuntimeSaveIdentity target = IdentityFor(runtime.catalog_);
    RuntimePersistenceReport report;
    if (!SameRuntimeSaveIdentity(image.identity, target))
    {
        if (migrations == nullptr)
        {
            return Failure(
                RuntimePersistenceStatus::IdentityMismatch,
                "Save Package Lock, Source Lock, Ruleset or format is incompatible"
            );
        }
        report.migration = migrations->Migrate(image, target);
        if (!report.migration)
        {
            report.status = RuntimePersistenceStatus::MigrationFailed;
            report.message = report.migration.message;
            return report;
        }
    }

    world::AuthoritativeWorld candidateWorld;
    kernel::WorldCommandQueue candidateCommands;
    std::uint64_t nextFactSequence = 0;
    if (!BuildCandidate(
            image,
            runtime.catalog_,
            candidateWorld,
            candidateCommands,
            nextFactSequence,
            report.message))
    {
        report.status = RuntimePersistenceStatus::InvalidWorldState;
        return report;
    }

    runtime.world_ = std::move(candidateWorld);
    runtime.commands_ = std::move(candidateCommands);
    runtime.events_.pending_.clear();
    runtime.events_.nextSequence_ = nextFactSequence;
    runtime.pendingAlgorithmEvents_.clear();
    runtime.lastAlgorithmEventSequence_ = nextFactSequence - 1;
    runtime.lastCreateAlgorithms_ = {};
    runtime.lastTickAlgorithms_ = {};
    runtime.lastEventAlgorithms_ = {};
    runtime.lastCommandAlgorithms_ = {};
    runtime.lastDestroyAlgorithms_ = {};
    runtime.PublishSnapshots();
    return report;
}

RuntimePersistenceReport RuntimePersistenceService::Load(
    runtime::KernelRuntime& runtime,
    const std::vector<std::uint8_t>& bytes,
    const RuntimeMigrationRegistry* migrations
) const
{
    RuntimeSaveImage image;
    RuntimePersistenceReport report;
    report.codec = RuntimeSaveCodec{}.Decode(bytes, image);
    if (!report.codec)
    {
        report.status = RuntimePersistenceStatus::CodecFailed;
        report.message = report.codec.message;
        return report;
    }
    RuntimePersistenceReport restored = Restore(
        runtime,
        std::move(image),
        migrations
    );
    restored.codec = report.codec;
    return restored;
}

}
