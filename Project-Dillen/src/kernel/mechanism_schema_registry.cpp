#include "mechanism_schema_registry.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace dillen::kernel {

namespace {

bool ValidRoleSchema(const MechanismRoleSchema& role)
{
    return IsValidMechanismSymbol(role.name)
        && role.name == NormalizeMechanismSymbol(role.name)
        && (!role.referenceType || *role.referenceType != 0)
        && (!role.maximumCount
            || role.minimumCount <= *role.maximumCount);
}

}

MechanismSchemaRegisterResult MechanismSchemaRegistry::Register(
    MechanismSchema schema
)
{
    if (frozen_)
    {
        return MechanismSchemaRegisterResult::Frozen;
    }
    if (!schema.type
        || schema.version == 0
        || !IsValidMechanismSymbol(schema.canonicalName)
        || schema.canonicalName
            != NormalizeMechanismSymbol(schema.canonicalName)
        || schema.type
            != StableMechanismTypeId(schema.canonicalName))
    {
        return MechanismSchemaRegisterResult::InvalidSchema;
    }
    const auto key = std::make_pair(schema.type.value, schema.version);
    if (indexByKey_.find(key) != indexByKey_.end())
    {
        return MechanismSchemaRegisterResult::DuplicateVersion;
    }
    for (const MechanismSchema& existing : schemas_)
    {
        if (existing.type == schema.type
            && existing.canonicalName != schema.canonicalName)
        {
            return MechanismSchemaRegisterResult::TypeCollision;
        }
    }

    std::unordered_set<std::string> fieldNames;
    for (const MechanismFieldSchema& field : schema.fields)
    {
        if (!IsValidMechanismFieldSchema(field)
            || !fieldNames.emplace(field.name).second)
        {
            return MechanismSchemaRegisterResult::InvalidSchema;
        }
    }
    std::unordered_set<std::string> roleNames;
    for (const MechanismRoleSchema& role : schema.roles)
    {
        if (!ValidRoleSchema(role)
            || !roleNames.emplace(role.name).second)
        {
            return MechanismSchemaRegisterResult::InvalidSchema;
        }
    }

    const std::size_t index = schemas_.size();
    schemas_.push_back(std::move(schema));
    indexByKey_[key] = index;
    const auto latest = latestByType_.find(key.first);
    if (latest == latestByType_.end()
        || schemas_[latest->second].version < key.second)
    {
        latestByType_[key.first] = index;
    }
    return MechanismSchemaRegisterResult::Added;
}

void MechanismSchemaRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    schemas_.clear();
    indexByKey_.clear();
    latestByType_.clear();
}

void MechanismSchemaRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        schemas_.begin(),
        schemas_.end(),
        [](const MechanismSchema& first, const MechanismSchema& second)
        {
            if (first.type != second.type)
            {
                return first.type < second.type;
            }
            return first.version < second.version;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool MechanismSchemaRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t MechanismSchemaRegistry::Size() const noexcept
{
    return schemas_.size();
}

const MechanismSchema* MechanismSchemaRegistry::Find(
    MechanismTypeId type,
    std::uint32_t version
) const
{
    const auto iterator = indexByKey_.find({type.value, version});
    return iterator == indexByKey_.end()
        ? nullptr
        : &schemas_[iterator->second];
}

const MechanismSchema* MechanismSchemaRegistry::Find(
    std::string_view canonicalName,
    std::uint32_t version
) const
{
    const MechanismTypeId type = StableMechanismTypeId(canonicalName);
    const MechanismSchema* schema = Find(type, version);
    return schema != nullptr && schema->canonicalName == canonicalName
        ? schema
        : nullptr;
}

const MechanismSchema* MechanismSchemaRegistry::Latest(
    MechanismTypeId type
) const
{
    const auto iterator = latestByType_.find(type.value);
    return iterator == latestByType_.end()
        ? nullptr
        : &schemas_[iterator->second];
}

const std::vector<MechanismSchema>&
MechanismSchemaRegistry::All() const noexcept
{
    return schemas_;
}

void MechanismSchemaRegistry::RebuildIndexes()
{
    indexByKey_.clear();
    latestByType_.clear();
    for (std::size_t index = 0; index < schemas_.size(); ++index)
    {
        const MechanismSchema& schema = schemas_[index];
        indexByKey_[{schema.type.value, schema.version}] = index;
        latestByType_[schema.type.value] = index;
    }
}

}
