#include "world_transaction_executor.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace dillen::world {

namespace {

kernel::WorldTransactionResult Failure(
    kernel::WorldTransactionStatus status,
    std::size_t commandIndex,
    std::uint64_t subject
)
{
    kernel::WorldTransactionResult result;
    result.status = status;
    result.commandIndex = commandIndex;
    result.subject = subject;
    return result;
}

bool ValidateInstanceReferences(
    const kernel::MechanismInstance& instance,
    const EntityRegistry& entities,
    const kernel::MechanismInstanceStore& mechanisms,
    const kernel::FrozenRuntimeCatalog& catalog
)
{
    for (const auto& role : instance.roles)
    {
        for (const kernel::MechanismReference& reference : role)
        {
            switch (reference.kind)
            {
            case kernel::MechanismReferenceKind::Entity:
            {
                const EntityRecord* entity = entities.Find(
                    kernel::EntityId{reference.value}
                );
                if (entity == nullptr
                    || (reference.type != 0
                        && entity->type.value != reference.type))
                {
                    return false;
                }
                break;
            }
            case kernel::MechanismReferenceKind::MechanismDefinition:
                if (catalog.FindDefinition(
                        kernel::MechanismDefinitionId{reference.value})
                    == nullptr)
                {
                    return false;
                }
                break;
            case kernel::MechanismReferenceKind::MechanismInstance:
                if (mechanisms.Find(
                        kernel::MechanismInstanceId{reference.value})
                    == nullptr)
                {
                    return false;
                }
                break;
            case kernel::MechanismReferenceKind::Resource:
            case kernel::MechanismReferenceKind::Custom:
                break;
            }
        }
    }
    return true;
}

bool IsMechanismInstanceReferenced(
    kernel::MechanismInstanceId target,
    const kernel::MechanismInstanceStore& mechanisms
)
{
    for (const auto& entry : mechanisms.All())
    {
        if (entry.first == target)
        {
            continue;
        }
        for (const auto& role : entry.second.roles)
        {
            for (const kernel::MechanismReference& reference : role)
            {
                if (reference.kind
                        == kernel::MechanismReferenceKind::MechanismInstance
                    && reference.value == target.value)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

// The whole per-command engine, operating on caller-owned working copies of the
// six authoritative stores. On success the copies hold the new state; on any
// failure they are left in a partially-mutated state that the caller must
// discard rather than publish. Both the single-transaction executor (which owns
// fresh copies per call) and WorldTransactionBatch (which owns one set for many
// transactions) route through here, so there is exactly one implementation of
// transaction semantics.
kernel::WorldTransactionResult ApplyToStores(
    EntityRegistry& entities,
    ComponentStore& components,
    RelationIndex& relations,
    kernel::MechanismInstanceStore& mechanisms,
    kernel::AlgorithmInbox& algorithmInbox,
    kernel::DeterministicRngRegistry& rngStreams,
    const kernel::WorldTransaction& transaction,
    const kernel::FrozenRuntimeCatalog& catalog,
    std::uint64_t currentTick
)
{
    using namespace kernel;
    WorldTransactionResult result;
    result.mechanism.status = MechanismTransactionStatus::Committed;
    std::set<MechanismInstanceId> changedMechanisms;

    for (std::size_t index = 0; index < transaction.commands.size(); ++index)
    {
        const WorldCommand& command = transaction.commands[index];
        if (const auto* operation =
            std::get_if<EntityCreateCommand>(&command.payload))
        {
            EntityId entity;
            if (entities.CreateFromDefinition(
                    operation->definition,
                    catalog,
                    entity) != EntityCreateResult::Created)
            {
                return Failure(
                    WorldTransactionStatus::EntityRejected,
                    index,
                    operation->definition.value
                );
            }
            const CompiledEntityDefinition* definition =
                catalog.FindEntityDefinition(operation->definition);
            if (definition == nullptr)
            {
                return Failure(
                    WorldTransactionStatus::EntityRejected,
                    index,
                    operation->definition.value
                );
            }
            result.changes.emplace_back(EntityCreatedChange{
                entity,
                operation->definition
            });
            for (const CompiledEntityComponentDefinition& component
                : definition->components)
            {
                if (components.Attach(entity, component, entities)
                    != ComponentAttachResult::Attached)
                {
                    return Failure(
                        WorldTransactionStatus::EntityRejected,
                        index,
                        component.type.value
                    );
                }
                result.changes.emplace_back(ComponentAttachedChange{
                    entity,
                    component.type
                });
            }
            continue;
        }
        if (const auto* operation =
            std::get_if<ComponentSetFieldCommand>(&command.payload))
        {
            MechanismValue previous;
            const ComponentFieldSetResult updated = components.SetField(
                operation->owner,
                operation->component,
                operation->field,
                operation->value,
                entities,
                catalog,
                previous
            );
            if (updated != ComponentFieldSetResult::Updated
                && updated != ComponentFieldSetResult::Unchanged)
            {
                return Failure(
                    WorldTransactionStatus::ComponentRejected,
                    index,
                    operation->owner.value
                );
            }
            if (updated == ComponentFieldSetResult::Updated)
            {
                result.changes.emplace_back(ComponentFieldChange{
                    operation->owner,
                    operation->component,
                    operation->field,
                    std::move(previous),
                    operation->value
                });
            }
            continue;
        }
        if (const auto* operation =
            std::get_if<RelationAddCommand>(&command.payload))
        {
            RelationId relation;
            if (relations.Add(
                    operation->type,
                    operation->source,
                    operation->target,
                    entities,
                    catalog,
                    relation) != RelationAddResult::Added)
            {
                return Failure(
                    WorldTransactionStatus::RelationRejected,
                    index,
                    operation->type.value
                );
            }
            result.changes.emplace_back(RelationAddedChange{
                relation,
                operation->type,
                operation->source,
                operation->target
            });
            continue;
        }
        if (const auto* operation =
            std::get_if<RelationRemoveCommand>(&command.payload))
        {
            RelationRecord removed;
            if (relations.Remove(operation->relation, removed)
                != RelationRemoveResult::Removed)
            {
                return Failure(
                    WorldTransactionStatus::RelationRejected,
                    index,
                    operation->relation.value
                );
            }
            result.changes.emplace_back(RelationRemovedChange{
                removed.id,
                removed.type,
                removed.source,
                removed.target
            });
            continue;
        }
        if (const auto* operation =
            std::get_if<MechanismSpawnCommand>(&command.payload))
        {
            MechanismInstanceId instance;
            if (mechanisms.CreateFromSpawn(
                    operation->spawn,
                    catalog,
                    currentTick,
                    instance) != MechanismInstanceCreateResult::Created)
            {
                return Failure(
                    WorldTransactionStatus::MechanismRejected,
                    index,
                    operation->spawn.value
                );
            }
            if (!ValidateInstanceReferences(
                    *mechanisms.Find(instance),
                    entities,
                    mechanisms,
                    catalog))
            {
                return Failure(
                    WorldTransactionStatus::MechanismRejected,
                    index,
                    instance.value
                );
            }
            changedMechanisms.insert(instance);
            result.changes.emplace_back(MechanismSpawnedChange{
                instance,
                operation->spawn
            });
            continue;
        }
        if (const auto* operation =
            std::get_if<MechanismCommand>(&command.payload))
        {
            if (std::holds_alternative<MechanismDestroyOperation>(
                    operation->operation)
                && IsMechanismInstanceReferenced(
                    operation->target,
                    mechanisms))
            {
                result = Failure(
                    WorldTransactionStatus::MechanismRejected,
                    index,
                    operation->target.value
                );
                result.mechanism.status =
                    MechanismTransactionStatus::DestroyTargetReferenced;
                result.mechanism.commandIndex = index;
                result.mechanism.target = operation->target;
                return result;
            }
            MechanismTransactionResult mechanism =
                mechanisms.ApplyTransaction(
                    {*operation},
                    catalog,
                    currentTick
                );
            if (!mechanism)
            {
                mechanism.commandIndex = index;
                result = Failure(
                    WorldTransactionStatus::MechanismRejected,
                    index,
                    operation->target.value
                );
                result.mechanism = std::move(mechanism);
                return result;
            }
            for (const MechanismChange& change : mechanism.changes)
            {
                result.mechanism.changes.push_back(change);
                std::visit(
                    [&result](const auto& value)
                    {
                        result.changes.emplace_back(value);
                    },
                    change
                );
            }
            if (!mechanism.changes.empty())
            {
                changedMechanisms.insert(operation->target);
            }
            if (std::holds_alternative<MechanismDestroyOperation>(
                    operation->operation))
            {
                for (ScheduledAlgorithmEvent& removed
                    : algorithmInbox.CancelTarget(operation->target))
                {
                    result.changes.emplace_back(
                        ScheduledEventCancelledChange{std::move(removed)}
                    );
                }
            }
            continue;
        }
        if (const auto* operation =
            std::get_if<ScheduledEventScheduleCommand>(&command.payload))
        {
            if (operation->dueTick <= currentTick
                || (operation->target
                    && mechanisms.Find(operation->target) == nullptr))
            {
                return Failure(
                    WorldTransactionStatus::ScheduledEventRejected,
                    index,
                    operation->type.value
                );
            }
            std::uint64_t sequence = 0;
            if (algorithmInbox.Schedule(
                    operation->type,
                    operation->target,
                    operation->dueTick,
                    operation->priority,
                    operation->payload,
                    sequence) != AlgorithmInboxScheduleResult::Scheduled)
            {
                return Failure(
                    WorldTransactionStatus::ScheduledEventRejected,
                    index,
                    operation->type.value
                );
            }
            const auto scheduled = std::find_if(
                algorithmInbox.Pending().begin(),
                algorithmInbox.Pending().end(),
                [sequence](const ScheduledAlgorithmEvent& event)
                {
                    return event.sequence == sequence;
                }
            );
            if (scheduled == algorithmInbox.Pending().end())
            {
                return Failure(
                    WorldTransactionStatus::ScheduledEventRejected,
                    index,
                    operation->type.value
                );
            }
            result.changes.emplace_back(
                ScheduledEventAddedChange{*scheduled}
            );
            continue;
        }
        if (const auto* operation =
            std::get_if<ScheduledEventCancelCommand>(&command.payload))
        {
            ScheduledAlgorithmEvent removed;
            if (!algorithmInbox.Cancel(operation->sequence, removed))
            {
                return Failure(
                    WorldTransactionStatus::ScheduledEventRejected,
                    index,
                    operation->sequence
                );
            }
            result.changes.emplace_back(ScheduledEventCancelledChange{
                std::move(removed)
            });
            continue;
        }
        if (const auto* operation =
            std::get_if<InvokeCapabilityCommand>(&command.payload))
        {
            if (operation->dueTick <= currentTick)
            {
                return Failure(
                    WorldTransactionStatus::ScheduledEventRejected,
                    index,
                    operation->capability.value
                );
            }
            const auto provides =
                [operation](const CompiledMechanismDefinition* definition)
            {
                return definition != nullptr
                    && std::any_of(
                        definition->providedCapabilities.begin(),
                        definition->providedCapabilities.end(),
                        [operation](const CapabilityProvision& provision)
                        {
                            return provision.capability
                                    == operation->capability
                                && provision.version
                                    == operation->capabilityVersion;
                        });
            };
            bool delivered = true;
            const auto deliverTo =
                [&](MechanismInstanceId providerId)
            {
                std::uint64_t sequence = 0;
                if (algorithmInbox.Schedule(
                        operation->deliveryType,
                        providerId,
                        operation->dueTick,
                        operation->priority,
                        operation->payload,
                        sequence) != AlgorithmInboxScheduleResult::Scheduled)
                {
                    delivered = false;
                    return;
                }
                const auto scheduled = std::find_if(
                    algorithmInbox.Pending().begin(),
                    algorithmInbox.Pending().end(),
                    [sequence](const ScheduledAlgorithmEvent& event)
                    {
                        return event.sequence == sequence;
                    }
                );
                if (scheduled == algorithmInbox.Pending().end())
                {
                    delivered = false;
                    return;
                }
                result.changes.emplace_back(
                    ScheduledEventAddedChange{*scheduled}
                );
            };

            if (operation->targetInstance)
            {
                const MechanismInstance* provider =
                    mechanisms.Find(operation->targetInstance);
                if (provider == nullptr
                    || !provides(catalog.FindDefinition(provider->definition)))
                {
                    return Failure(
                        WorldTransactionStatus::ScheduledEventRejected,
                        index,
                        operation->capability.value
                    );
                }
                deliverTo(provider->id);
            }
            else
            {
                for (const auto& entry : mechanisms.All())
                {
                    const MechanismInstance& provider = entry.second;
                    if (!provides(
                            catalog.FindDefinition(provider.definition)))
                    {
                        continue;
                    }
                    deliverTo(provider.id);
                    if (!delivered)
                    {
                        break;
                    }
                }
            }
            if (!delivered)
            {
                return Failure(
                    WorldTransactionStatus::ScheduledEventRejected,
                    index,
                    operation->capability.value
                );
            }
            continue;
        }
        if (const auto* operation =
            std::get_if<RngStreamCreateCommand>(&command.payload))
        {
            if (rngStreams.Create(operation->stream, operation->seed)
                != RngStreamCreateResult::Created)
            {
                return Failure(
                    WorldTransactionStatus::RngRejected,
                    index,
                    operation->stream.value
                );
            }
            result.changes.emplace_back(RngStreamCreatedChange{
                operation->stream,
                operation->seed
            });
            continue;
        }

        const auto& operation = std::get<RngStreamAdvanceCommand>(
            command.payload
        );
        if (rngStreams.Advance(
                operation.stream,
                operation.expectedDrawCount,
                operation.count) != RngStreamAdvanceResult::Advanced)
        {
            return Failure(
                WorldTransactionStatus::RngRejected,
                index,
                operation.stream.value
            );
        }
        result.changes.emplace_back(RngStreamAdvancedChange{
            operation.stream,
            operation.expectedDrawCount,
            operation.expectedDrawCount + operation.count
        });
    }

    result.commandIndex = transaction.commands.size();
    result.mechanism.commandIndex = transaction.commands.size();
    result.mechanism.changedInstances = changedMechanisms.size();
    return result;
}

}

kernel::WorldTransactionResult WorldTransactionExecutor::Apply(
    AuthoritativeWorld& world,
    const kernel::WorldTransaction& transaction,
    const kernel::FrozenRuntimeCatalog& catalog,
    std::uint64_t currentTick
) const
{
    using namespace kernel;
    if (!catalog.IsFrozen())
    {
        return Failure(WorldTransactionStatus::RuntimeCatalogNotFrozen, 0, 0);
    }
    if (currentTick < world.tick_)
    {
        return Failure(WorldTransactionStatus::TickRegression, 0, 0);
    }

    EntityRegistry entities = world.entities_;
    ComponentStore components = world.components_;
    RelationIndex relations = world.relations_;
    MechanismInstanceStore mechanisms = world.mechanisms_;
    AlgorithmInbox algorithmInbox = world.algorithmInbox_;
    DeterministicRngRegistry rngStreams = world.rngStreams_;

    WorldTransactionResult result = ApplyToStores(
        entities,
        components,
        relations,
        mechanisms,
        algorithmInbox,
        rngStreams,
        transaction,
        catalog,
        currentTick
    );
    if (!result)
    {
        // The working copies may be half-updated; drop them so the world keeps
        // its pre-transaction state.
        return result;
    }
    world.entities_ = std::move(entities);
    world.components_ = std::move(components);
    world.relations_ = std::move(relations);
    world.mechanisms_ = std::move(mechanisms);
    world.algorithmInbox_ = std::move(algorithmInbox);
    world.rngStreams_ = std::move(rngStreams);
    return result;
}

WorldTransactionBatch::WorldTransactionBatch(
    AuthoritativeWorld& world,
    const kernel::FrozenRuntimeCatalog& catalog,
    std::uint64_t currentTick
)
    : world_(world),
      catalog_(catalog),
      currentTick_(currentTick),
      open_(catalog.IsFrozen() && currentTick >= world.tick_)
{
    if (!open_)
    {
        return;
    }
    entities_ = world.entities_;
    components_ = world.components_;
    relations_ = world.relations_;
    mechanisms_ = world.mechanisms_;
    algorithmInbox_ = world.algorithmInbox_;
    rngStreams_ = world.rngStreams_;
}

bool WorldTransactionBatch::IsOpen() const noexcept
{
    return open_;
}

const kernel::MechanismInstanceStore&
WorldTransactionBatch::Mechanisms() const noexcept
{
    return mechanisms_;
}

kernel::WorldTransactionResult WorldTransactionBatch::Apply(
    const kernel::WorldTransaction& transaction
)
{
    using namespace kernel;
    if (!open_)
    {
        return Failure(WorldTransactionStatus::RuntimeCatalogNotFrozen, 0, 0);
    }
    // Applied straight to the working copies -- no per-transaction copy, which
    // is the entire point of a batch. A rejected transaction may leave those
    // copies half-updated, so the batch closes itself: no further transaction
    // may be applied and Commit() becomes a no-op, leaving the authoritative
    // world untouched by every transaction in the batch. The caller must then
    // replay the phase through the per-transaction path.
    WorldTransactionResult result = ApplyToStores(
        entities_,
        components_,
        relations_,
        mechanisms_,
        algorithmInbox_,
        rngStreams_,
        transaction,
        catalog_,
        currentTick_
    );
    if (!result)
    {
        open_ = false;
    }
    return result;
}

void WorldTransactionBatch::Commit()
{
    if (!open_)
    {
        return;
    }
    world_.entities_ = std::move(entities_);
    world_.components_ = std::move(components_);
    world_.relations_ = std::move(relations_);
    world_.mechanisms_ = std::move(mechanisms_);
    world_.algorithmInbox_ = std::move(algorithmInbox_);
    world_.rngStreams_ = std::move(rngStreams_);
    open_ = false;
}

}
