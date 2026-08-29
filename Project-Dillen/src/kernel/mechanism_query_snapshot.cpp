#include "mechanism_query_snapshot.hpp"

#include <utility>

namespace dillen::kernel {

namespace {

const std::vector<MechanismInstanceId>& EmptyInstanceIds()
{
    static const std::vector<MechanismInstanceId> empty;
    return empty;
}

}

void MechanismQuerySnapshot::Publish(
    const MechanismInstanceStore& store,
    std::uint64_t tick,
    std::uint64_t revision
)
{
    MechanismInstanceStore::InstanceMap instances = store.All();
    std::map<MechanismDefinitionId, std::vector<MechanismInstanceId>>
        byDefinition;
    std::map<MechanismTypeId, std::vector<MechanismInstanceId>> byType;
    for (const auto& stored : instances)
    {
        byDefinition[stored.second.definition].push_back(stored.first);
        byType[stored.second.type].push_back(stored.first);
    }
    instances_ = std::move(instances);
    instancesByDefinition_ = std::move(byDefinition);
    instancesByType_ = std::move(byType);
    tick_ = tick;
    revision_ = revision;
    published_ = true;
}

void MechanismQuerySnapshot::Clear()
{
    instances_.clear();
    instancesByDefinition_.clear();
    instancesByType_.clear();
    tick_ = 0;
    revision_ = 0;
    published_ = false;
}

bool MechanismQuerySnapshot::IsPublished() const noexcept
{
    return published_;
}

std::uint64_t MechanismQuerySnapshot::Tick() const noexcept
{
    return tick_;
}

std::uint64_t MechanismQuerySnapshot::Revision() const noexcept
{
    return revision_;
}

std::size_t MechanismQuerySnapshot::Size() const noexcept
{
    return instances_.size();
}

const MechanismInstance* MechanismQuerySnapshot::Find(
    MechanismInstanceId id
) const
{
    const auto iterator = instances_.find(id);
    return iterator == instances_.end() ? nullptr : &iterator->second;
}

const MechanismValue* MechanismQuerySnapshot::FindField(
    MechanismInstanceId id,
    MechanismFieldSlotId field
) const
{
    const MechanismInstance* instance = Find(id);
    if (instance == nullptr || field.value >= instance->values.size())
    {
        return nullptr;
    }
    return &instance->values[field.value];
}

const std::vector<MechanismReference>* MechanismQuerySnapshot::FindRole(
    MechanismInstanceId id,
    MechanismRoleSlotId role
) const
{
    const MechanismInstance* instance = Find(id);
    if (instance == nullptr || role.value >= instance->roles.size())
    {
        return nullptr;
    }
    return &instance->roles[role.value];
}

const std::vector<MechanismInstanceId>&
MechanismQuerySnapshot::FindByDefinition(
    MechanismDefinitionId definition
) const
{
    const auto iterator = instancesByDefinition_.find(definition);
    return iterator == instancesByDefinition_.end()
        ? EmptyInstanceIds()
        : iterator->second;
}

const std::vector<MechanismInstanceId>&
MechanismQuerySnapshot::FindByType(MechanismTypeId type) const
{
    const auto iterator = instancesByType_.find(type);
    return iterator == instancesByType_.end()
        ? EmptyInstanceIds()
        : iterator->second;
}

const MechanismInstanceStore::InstanceMap&
MechanismQuerySnapshot::All() const noexcept
{
    return instances_;
}

}
