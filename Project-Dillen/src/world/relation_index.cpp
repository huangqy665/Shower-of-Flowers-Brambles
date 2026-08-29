#include "relation_index.hpp"

#include <algorithm>

namespace dillen::world {

namespace {

const std::vector<kernel::RelationId>& EmptyRelationIds()
{
    static const std::vector<kernel::RelationId> empty;
    return empty;
}

}

RelationAddResult RelationIndex::Add(
    kernel::RelationTypeId type,
    kernel::EntityId source,
    kernel::EntityId target,
    const EntityRegistry& entities,
    kernel::RelationId& outputId
)
{
    outputId = {};
    if (!type || !source || !target)
    {
        return RelationAddResult::InvalidRelation;
    }
    if (entities.Find(source) == nullptr)
    {
        return RelationAddResult::SourceMissing;
    }
    if (entities.Find(target) == nullptr)
    {
        return RelationAddResult::TargetMissing;
    }
    const kernel::RelationId id = kernel::StableRelationId(
        type,
        source,
        target
    );
    const auto existing = relations_.find(id);
    if (existing != relations_.end())
    {
        const RelationRecord& relation = existing->second;
        return relation.type == type
            && relation.source == source
            && relation.target == target
            ? RelationAddResult::DuplicateRelation
            : RelationAddResult::IdCollision;
    }
    relations_.emplace(id, RelationRecord{id, type, source, target});
    outgoing_[{type, source}].push_back(id);
    incoming_[{type, target}].push_back(id);
    outputId = id;
    return RelationAddResult::Added;
}

RelationAddResult RelationIndex::Add(
    kernel::RelationTypeId type,
    kernel::EntityId source,
    kernel::EntityId target,
    const EntityRegistry& entities,
    const kernel::FrozenRuntimeCatalog& catalog,
    kernel::RelationId& outputId
)
{
    const kernel::CompiledRelationLayout* schema =
        catalog.FindRelationLayout(type);
    if (schema == nullptr)
    {
        if (catalog.RelationLayoutCount() == 0)
        {
            return Add(type, source, target, entities, outputId);
        }
        return RelationAddResult::SchemaMissing;
    }
    const EntityRecord* sourceEntity = entities.Find(source);
    const EntityRecord* targetEntity = entities.Find(target);
    if (sourceEntity == nullptr)
    {
        return RelationAddResult::SourceMissing;
    }
    if (targetEntity == nullptr)
    {
        return RelationAddResult::TargetMissing;
    }
    if (schema->sourceType && sourceEntity->type != *schema->sourceType)
    {
        return RelationAddResult::SourceTypeMismatch;
    }
    if (schema->targetType && targetEntity->type != *schema->targetType)
    {
        return RelationAddResult::TargetTypeMismatch;
    }
    if (!schema->allowSelf && source == target)
    {
        return RelationAddResult::SelfRelationRejected;
    }
    return Add(type, source, target, entities, outputId);
}

RelationRemoveResult RelationIndex::Remove(
    kernel::RelationId relation,
    RelationRecord& removed
)
{
    removed = {};
    const auto iterator = relations_.find(relation);
    if (iterator == relations_.end())
    {
        return RelationRemoveResult::RelationMissing;
    }
    removed = iterator->second;
    auto& outgoing = outgoing_[{removed.type, removed.source}];
    outgoing.erase(
        std::remove(outgoing.begin(), outgoing.end(), relation),
        outgoing.end()
    );
    if (outgoing.empty())
    {
        outgoing_.erase({removed.type, removed.source});
    }
    auto& incoming = incoming_[{removed.type, removed.target}];
    incoming.erase(
        std::remove(incoming.begin(), incoming.end(), relation),
        incoming.end()
    );
    if (incoming.empty())
    {
        incoming_.erase({removed.type, removed.target});
    }
    relations_.erase(iterator);
    return RelationRemoveResult::Removed;
}

void RelationIndex::Clear()
{
    relations_.clear();
    outgoing_.clear();
    incoming_.clear();
}

bool RelationIndex::Empty() const noexcept
{
    return relations_.empty();
}

std::size_t RelationIndex::Size() const noexcept
{
    return relations_.size();
}

const RelationRecord* RelationIndex::Find(kernel::RelationId id) const
{
    const auto iterator = relations_.find(id);
    return iterator == relations_.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::RelationId>& RelationIndex::Outgoing(
    kernel::RelationTypeId type,
    kernel::EntityId source
) const
{
    const auto iterator = outgoing_.find({type, source});
    return iterator == outgoing_.end()
        ? EmptyRelationIds()
        : iterator->second;
}

const std::vector<kernel::RelationId>& RelationIndex::Incoming(
    kernel::RelationTypeId type,
    kernel::EntityId target
) const
{
    const auto iterator = incoming_.find({type, target});
    return iterator == incoming_.end()
        ? EmptyRelationIds()
        : iterator->second;
}

const RelationIndex::RelationMap& RelationIndex::All() const noexcept
{
    return relations_;
}

}
