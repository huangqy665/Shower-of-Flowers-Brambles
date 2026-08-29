#include "world_query_snapshot.hpp"

#include <atomic>

namespace dillen::runtime {

namespace {

template<typename Id>
const std::vector<Id>& EmptyIds()
{
    static const std::vector<Id> empty;
    return empty;
}

}

void EntityQuerySnapshot::Publish(
    const world::EntityRegistry& entities
)
{
    entities_ = entities.All();
    entitiesByDefinition_.clear();
    entitiesByType_.clear();
    for (const auto& stored : entities_)
    {
        entitiesByDefinition_[stored.second.definition].push_back(
            stored.first
        );
        entitiesByType_[stored.second.type].push_back(stored.first);
    }
}

std::size_t EntityQuerySnapshot::Size() const noexcept
{
    return entities_.size();
}

const world::EntityRecord* EntityQuerySnapshot::Find(
    kernel::EntityId id
) const
{
    const auto iterator = entities_.find(id);
    return iterator == entities_.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::EntityId>&
EntityQuerySnapshot::FindByDefinition(
    kernel::EntityDefinitionId definition
) const
{
    const auto iterator = entitiesByDefinition_.find(definition);
    return iterator == entitiesByDefinition_.end()
        ? EmptyIds<kernel::EntityId>()
        : iterator->second;
}

const std::vector<kernel::EntityId>& EntityQuerySnapshot::FindByType(
    kernel::EntityTypeId type
) const
{
    const auto iterator = entitiesByType_.find(type);
    return iterator == entitiesByType_.end()
        ? EmptyIds<kernel::EntityId>()
        : iterator->second;
}

const EntityQuerySnapshot::EntityMap&
EntityQuerySnapshot::All() const noexcept
{
    return entities_;
}

void ComponentQuerySnapshot::Publish(
    const world::ComponentStore& components
)
{
    components_ = components.All();
    ownersByType_.clear();
    typesByOwner_.clear();
    for (const auto& stored : components_)
    {
        const world::ComponentRecord& component = stored.second;
        ownersByType_[component.type].push_back(component.owner);
        typesByOwner_[component.owner].push_back(component.type);
    }
}

std::size_t ComponentQuerySnapshot::Size() const noexcept
{
    return components_.size();
}

const world::ComponentRecord* ComponentQuerySnapshot::Find(
    kernel::EntityId owner,
    kernel::ComponentTypeId type
) const
{
    const auto iterator = components_.find({owner, type});
    return iterator == components_.end() ? nullptr : &iterator->second;
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
    const auto iterator = ownersByType_.find(type);
    return iterator == ownersByType_.end()
        ? EmptyIds<kernel::EntityId>()
        : iterator->second;
}

const std::vector<kernel::ComponentTypeId>&
ComponentQuerySnapshot::FindTypes(kernel::EntityId owner) const
{
    const auto iterator = typesByOwner_.find(owner);
    return iterator == typesByOwner_.end()
        ? EmptyIds<kernel::ComponentTypeId>()
        : iterator->second;
}

const ComponentQuerySnapshot::ComponentMap&
ComponentQuerySnapshot::All() const noexcept
{
    return components_;
}

void RelationQuerySnapshot::Publish(
    const world::RelationIndex& relations
)
{
    relations_ = relations.All();
    relationsByType_.clear();
    outgoing_.clear();
    incoming_.clear();
    for (const auto& stored : relations_)
    {
        const world::RelationRecord& relation = stored.second;
        relationsByType_[relation.type].push_back(relation.id);
        outgoing_[{relation.type, relation.source}].push_back(relation.id);
        incoming_[{relation.type, relation.target}].push_back(relation.id);
    }
}

std::size_t RelationQuerySnapshot::Size() const noexcept
{
    return relations_.size();
}

const world::RelationRecord* RelationQuerySnapshot::Find(
    kernel::RelationId id
) const
{
    const auto iterator = relations_.find(id);
    return iterator == relations_.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::RelationId>& RelationQuerySnapshot::FindByType(
    kernel::RelationTypeId type
) const
{
    const auto iterator = relationsByType_.find(type);
    return iterator == relationsByType_.end()
        ? EmptyIds<kernel::RelationId>()
        : iterator->second;
}

const std::vector<kernel::RelationId>& RelationQuerySnapshot::Outgoing(
    kernel::RelationTypeId type,
    kernel::EntityId source
) const
{
    const auto iterator = outgoing_.find({type, source});
    return iterator == outgoing_.end()
        ? EmptyIds<kernel::RelationId>()
        : iterator->second;
}

const std::vector<kernel::RelationId>& RelationQuerySnapshot::Incoming(
    kernel::RelationTypeId type,
    kernel::EntityId target
) const
{
    const auto iterator = incoming_.find({type, target});
    return iterator == incoming_.end()
        ? EmptyIds<kernel::RelationId>()
        : iterator->second;
}

const RelationQuerySnapshot::RelationMap&
RelationQuerySnapshot::All() const noexcept
{
    return relations_;
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
