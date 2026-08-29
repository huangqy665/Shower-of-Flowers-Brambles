#include "component_schema.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace dillen::kernel {

ComponentSchemaRegisterResult ComponentSchemaRegistry::Register(
    ComponentSchema schema
)
{
    if (frozen_)
    {
        return ComponentSchemaRegisterResult::Frozen;
    }
    if (!schema.type
        || schema.version == 0
        || !IsValidMechanismSymbol(schema.canonicalName)
        || schema.canonicalName
            != NormalizeMechanismSymbol(schema.canonicalName)
        || schema.type != StableComponentTypeId(schema.canonicalName))
    {
        return ComponentSchemaRegisterResult::InvalidSchema;
    }
    const auto key = std::make_pair(schema.type.value, schema.version);
    if (indexByVersion_.find(key) != indexByVersion_.end())
    {
        return ComponentSchemaRegisterResult::DuplicateVersion;
    }
    for (const ComponentSchema& existing : schemas_)
    {
        if (existing.type == schema.type
            && existing.canonicalName != schema.canonicalName)
        {
            return ComponentSchemaRegisterResult::TypeCollision;
        }
    }
    std::unordered_set<std::string> fields;
    for (const MechanismFieldSchema& field : schema.fields)
    {
        if (!IsValidMechanismFieldSchema(field)
            || !fields.emplace(field.name).second)
        {
            return ComponentSchemaRegisterResult::InvalidSchema;
        }
    }
    indexByVersion_[key] = schemas_.size();
    schemas_.push_back(std::move(schema));
    return ComponentSchemaRegisterResult::Added;
}

void ComponentSchemaRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    schemas_.clear();
    indexByVersion_.clear();
}

void ComponentSchemaRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        schemas_.begin(),
        schemas_.end(),
        [](const ComponentSchema& first, const ComponentSchema& second)
        {
            if (first.type != second.type)
            {
                return first.type < second.type;
            }
            return first.version < second.version;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool ComponentSchemaRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t ComponentSchemaRegistry::Size() const noexcept
{
    return schemas_.size();
}

const ComponentSchema* ComponentSchemaRegistry::Find(
    ComponentTypeId type,
    std::uint32_t version
) const
{
    const auto iterator = indexByVersion_.find({type.value, version});
    return iterator == indexByVersion_.end()
        ? nullptr
        : &schemas_[iterator->second];
}

const ComponentSchema* ComponentSchemaRegistry::Find(
    std::string_view canonicalName,
    std::uint32_t version
) const
{
    const ComponentTypeId type = StableComponentTypeId(canonicalName);
    const ComponentSchema* schema = Find(type, version);
    return schema != nullptr && schema->canonicalName == canonicalName
        ? schema
        : nullptr;
}

const std::vector<ComponentSchema>&
ComponentSchemaRegistry::All() const noexcept
{
    return schemas_;
}

void ComponentSchemaRegistry::RebuildIndex()
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
