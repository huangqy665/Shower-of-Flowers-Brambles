#include "relation_schema.hpp"

#include <algorithm>
#include <utility>

namespace dillen::kernel {

RelationSchemaRegisterResult RelationSchemaRegistry::Register(
    RelationSchema schema
)
{
    if (frozen_)
    {
        return RelationSchemaRegisterResult::Frozen;
    }
    if (!schema.type
        || schema.version == 0
        || !IsValidMechanismSymbol(schema.canonicalName)
        || schema.canonicalName
            != NormalizeMechanismSymbol(schema.canonicalName)
        || schema.type != StableRelationTypeId(schema.canonicalName))
    {
        return RelationSchemaRegisterResult::InvalidSchema;
    }
    const auto key = std::make_pair(schema.type.value, schema.version);
    if (indexByVersion_.find(key) != indexByVersion_.end())
    {
        return RelationSchemaRegisterResult::DuplicateVersion;
    }
    for (const RelationSchema& existing : schemas_)
    {
        if (existing.type == schema.type
            && existing.canonicalName != schema.canonicalName)
        {
            return RelationSchemaRegisterResult::TypeCollision;
        }
    }
    indexByVersion_[key] = schemas_.size();
    schemas_.push_back(std::move(schema));
    return RelationSchemaRegisterResult::Added;
}

void RelationSchemaRegistry::Clear()
{
    if (!frozen_)
    {
        schemas_.clear();
        indexByVersion_.clear();
    }
}

void RelationSchemaRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        schemas_.begin(),
        schemas_.end(),
        [](const RelationSchema& first, const RelationSchema& second)
        {
            return first.type != second.type
                ? first.type < second.type
                : first.version < second.version;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool RelationSchemaRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t RelationSchemaRegistry::Size() const noexcept
{
    return schemas_.size();
}

const RelationSchema* RelationSchemaRegistry::Find(
    RelationTypeId type,
    std::uint32_t version
) const
{
    const auto iterator = indexByVersion_.find({type.value, version});
    return iterator == indexByVersion_.end()
        ? nullptr
        : &schemas_[iterator->second];
}

const RelationSchema* RelationSchemaRegistry::Find(
    std::string_view canonicalName,
    std::uint32_t version
) const
{
    const RelationSchema* schema = Find(
        StableRelationTypeId(canonicalName),
        version
    );
    return schema != nullptr && schema->canonicalName == canonicalName
        ? schema
        : nullptr;
}

const std::vector<RelationSchema>& RelationSchemaRegistry::All()
    const noexcept
{
    return schemas_;
}

void RelationSchemaRegistry::RebuildIndex()
{
    indexByVersion_.clear();
    for (std::size_t index = 0; index < schemas_.size(); ++index)
    {
        indexByVersion_[{
            schemas_[index].type.value,
            schemas_[index].version
        }] = index;
    }
}

}
