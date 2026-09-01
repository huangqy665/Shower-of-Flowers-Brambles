#include "world_query_snapshot.hpp"

#include <atomic>

namespace dillen::runtime {

void EntityQuerySnapshot::Publish(
    const world::EntityRegistry& entities
)
{
    entities_ = entities;
}

std::size_t EntityQuerySnapshot::Size() const noexcept
{
    return entities_.Size();
}

const world::EntityRecord* EntityQuerySnapshot::Find(
    kernel::EntityId id
) const
{
    return entities_.Find(id);
}

const std::vector<kernel::EntityId>&
EntityQuerySnapshot::FindByDefinition(
    kernel::EntityDefinitionId definition
) const
{
    return entities_.FindByDefinition(definition);
}

const std::vector<kernel::EntityId>& EntityQuerySnapshot::FindByType(
    kernel::EntityTypeId type
) const
{
    return entities_.FindByType(type);
}

const EntityQuerySnapshot::EntityMap&
EntityQuerySnapshot::All() const noexcept
{
    return entities_.All();
}

void ComponentQuerySnapshot::Publish(
    const world::ComponentStore& components
)
{
    components_ = components;
}

std::size_t ComponentQuerySnapshot::Size() const noexcept
{
    return components_.Size();
}

const world::ComponentRecord* ComponentQuerySnapshot::Find(
    kernel::EntityId owner,
    kernel::ComponentTypeId type
) const
{
    return components_.Find(owner, type);
}

const kernel::MechanismValue* ComponentQuerySnapshot::FindField(
    kernel::EntityId owner,
    kernel::ComponentTypeId type,
    kernel::ComponentFieldSlotId field
) const
{
    const world::ComponentRecord* component = Find(owner, type);
    if (component == nullptr || field.value >= component->values.size())
    {
        return nullptr;
    }
    return &component->values[field.value];
}

const std::vector<kernel::EntityId>& ComponentQuerySnapshot::FindOwners(
    kernel::ComponentTypeId type
) const
{
    return components_.FindOwners(type);
}

const std::vector<kernel::ComponentTypeId>&
ComponentQuerySnapshot::FindTypes(kernel::EntityId owner) const
{
    return components_.FindTypes(owner);
}

const ComponentQuerySnapshot::ComponentMap&
ComponentQuerySnapshot::All() const noexcept
{
    return components_.All();
}

void RelationQuerySnapshot::Publish(
    const world::RelationIndex& relations
)
{
    relations_ = relations;
}

std::size_t RelationQuerySnapshot::Size() const noexcept
{
    return relations_.Size();
}

const world::RelationRecord* RelationQuerySnapshot::Find(
    kernel::RelationId id
) const
{
    return relations_.Find(id);
}

const std::vector<kernel::RelationId>& RelationQuerySnapshot::FindByType(
    kernel::RelationTypeId type
) const
{
    return relations_.FindByType(type);
}

const std::vector<kernel::RelationId>& RelationQuerySnapshot::Outgoing(
    kernel::RelationTypeId type,
    kernel::EntityId source
) const
{
    return relations_.Outgoing(type, source);
}

const std::vector<kernel::RelationId>& RelationQuerySnapshot::Incoming(
    kernel::RelationTypeId type,
    kernel::EntityId target
) const
{
    return relations_.Incoming(type, target);
}

const RelationQuerySnapshot::RelationMap&
RelationQuerySnapshot::All() const noexcept
{
    return relations_.All();
}

void ScheduledEventQuerySnapshot::Publish(const kernel::AlgorithmInbox& inbox)
{
    inbox_ = inbox;
}

std::size_t ScheduledEventQuerySnapshot::Size() const noexcept
{
    return inbox_.Size();
}

bool ScheduledEventQuerySnapshot::Empty() const noexcept
{
    return inbox_.Empty();
}

const std::vector<kernel::ScheduledAlgorithmEvent>&
ScheduledEventQuerySnapshot::Pending() const noexcept
{
    return inbox_.Pending();
}

void WorldQuerySnapshot::Publish(
    const world::AuthoritativeWorld& world,
    std::uint64_t publication
)
{
    entities_.Publish(world.Entities());
    components_.Publish(world.Components());
    relations_.Publish(world.Relations());
    mechanisms_.Publish(
        world.Mechanisms(),
        world.Tick(),
        world.Revision()
    );
    scheduledEvents_.Publish(world.AlgorithmEvents());
    stamp_ = {publication, world.Tick(), world.Revision()};
    published_ = true;
}

bool WorldQuerySnapshot::IsPublished() const noexcept
{
    return published_;
}

const WorldQueryStamp& WorldQuerySnapshot::Stamp() const noexcept
{
    return stamp_;
}

std::uint64_t WorldQuerySnapshot::Publication() const noexcept
{
    return stamp_.publication;
}

std::uint64_t WorldQuerySnapshot::Tick() const noexcept
{
    return stamp_.tick;
}

std::uint64_t WorldQuerySnapshot::Revision() const noexcept
{
    return stamp_.revision;
}

const EntityQuerySnapshot& WorldQuerySnapshot::Entities() const noexcept
{
    return entities_;
}

const ComponentQuerySnapshot&
WorldQuerySnapshot::Components() const noexcept
{
    return components_;
}

const RelationQuerySnapshot& WorldQuerySnapshot::Relations() const noexcept
{
    return relations_;
}

const kernel::MechanismQuerySnapshot&
WorldQuerySnapshot::Mechanisms() const noexcept
{
    return mechanisms_;
}

const ScheduledEventQuerySnapshot& WorldQuerySnapshot::ScheduledEvents()
    const noexcept
{
    return scheduledEvents_;
}

WorldQuerySnapshotHandle WorldQueryService::Publish(
    const world::AuthoritativeWorld& world
)
{
    auto candidate = std::make_shared<WorldQuerySnapshot>();
    candidate->Publish(world, nextPublication_++);
    WorldQuerySnapshotHandle published = std::move(candidate);
    std::atomic_store_explicit(
        &current_,
        published,
        std::memory_order_release
    );
    return published;
}

WorldQuerySnapshotHandle WorldQueryService::Acquire() const noexcept
{
    return std::atomic_load_explicit(
        &current_,
        std::memory_order_acquire
    );
}

}
