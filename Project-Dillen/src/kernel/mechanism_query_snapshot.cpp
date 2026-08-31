#include "mechanism_query_snapshot.hpp"

#include <utility>

namespace dillen::kernel {

void MechanismQuerySnapshot::Publish(
    const MechanismInstanceStore& store,
    std::uint64_t tick,
    std::uint64_t revision
)
{
    instances_ = store;
    tick_ = tick;
    revision_ = revision;
    published_ = true;
}

void MechanismQuerySnapshot::Clear()
{
    instances_.Clear();
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
    return instances_.Size();
}

const MechanismInstance* MechanismQuerySnapshot::Find(
    MechanismInstanceId id
) const
{
    return instances_.Find(id);
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
    return instances_.FindByDefinition(definition);
}

const std::vector<MechanismInstanceId>&
MechanismQuerySnapshot::FindByType(MechanismTypeId type) const
{
    return instances_.FindByType(type);
}

const MechanismInstanceStore::InstanceMap&
MechanismQuerySnapshot::All() const noexcept
{
    return instances_.All();
}

}
