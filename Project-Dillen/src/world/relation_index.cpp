#include "relation_index.hpp"

#include <algorithm>

#include "sorted_id_index.hpp"

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
    const auto existing = Read().relations.find(id);
    if (existing != Read().relations.end())
    {
        const RelationRecord& relation = existing->second;
        return relation.type == type
            && relation.source == source
            && relation.target == target
            ? RelationAddResult::DuplicateRelation
            : RelationAddResult::IdCollision;
    }
    Data& data = Mutable();
    data.relations.emplace(id, RelationRecord{id, type, source, target});
    kernel::InsertSortedId(data.relationsByType[type], id);
    kernel::InsertSortedId(data.outgoing[{type, source}], id);
    kernel::InsertSortedId(data.incoming[{type, target}], id);
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
    const auto reader = Read().relations.find(relation);
    if (reader == Read().relations.end())
    {
        return RelationRemoveResult::RelationMissing;
    }
    removed = reader->second;
    Data& data = Mutable();
    auto& byType = data.relationsByType[removed.type];
    byType.erase(
        std::remove(byType.begin(), byType.end(), relation),
        byType.end()
    );
    if (byType.empty())
    {
        data.relationsByType.erase(removed.type);
    }
    auto& outgoing = data.outgoing[{removed.type, removed.source}];
    outgoing.erase(
        std::remove(outgoing.begin(), outgoing.end(), relation),
        outgoing.end()
    );
    if (outgoing.empty())
    {
        data.outgoing.erase({removed.type, removed.source});
    }
    auto& incoming = data.incoming[{removed.type, removed.target}];
    incoming.erase(
        std::remove(incoming.begin(), incoming.end(), relation),
        incoming.end()
    );
    if (incoming.empty())
    {
        data.incoming.erase({removed.type, removed.target});
    }
    data.relations.erase(relation);
    return RelationRemoveResult::Removed;
}

void RelationIndex::Clear()
{
    Data& data = Mutable();
    data.relations.clear();
    data.relationsByType.clear();
    data.outgoing.clear();
    data.incoming.clear();
}

bool RelationIndex::Empty() const noexcept
{
    return Read().relations.empty();
}

std::size_t RelationIndex::Size() const noexcept
{
    return Read().relations.size();
}

const RelationRecord* RelationIndex::Find(kernel::RelationId id) const
{
    const auto iterator = Read().relations.find(id);
    return iterator == Read().relations.end() ? nullptr : &iterator->second;
}

const std::vector<kernel::RelationId>& RelationIndex::FindByType(
    kernel::RelationTypeId type
) const
{
    const auto iterator = Read().relationsByType.find(type);
    return iterator == Read().relationsByType.end()
        ? EmptyRelationIds()
        : iterator->second;
}

const std::vector<kernel::RelationId>& RelationIndex::Outgoing(
    kernel::RelationTypeId type,
    kernel::EntityId source
) const
{
    const auto iterator = Read().outgoing.find({type, source});
    return iterator == Read().outgoing.end()
        ? EmptyRelationIds()
        : iterator->second;
}

const std::vector<kernel::RelationId>& RelationIndex::Incoming(
    kernel::RelationTypeId type,
    kernel::EntityId target
) const
{
    const auto iterator = Read().incoming.find({type, target});
    return iterator == Read().incoming.end()
        ? EmptyRelationIds()
        : iterator->second;
}

const RelationIndex::RelationMap& RelationIndex::All() const noexcept
{
    return Read().relations;
}

}
