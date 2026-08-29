#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "mechanism_instance_store.hpp"

namespace dillen::kernel {

class MechanismQuerySnapshot
{
public:
    void Publish(
        const MechanismInstanceStore& store,
        std::uint64_t tick,
        std::uint64_t revision
    );
    void Clear();
    bool IsPublished() const noexcept;
    std::uint64_t Tick() const noexcept;
    std::uint64_t Revision() const noexcept;
    std::size_t Size() const noexcept;
    const MechanismInstance* Find(MechanismInstanceId id) const;
    const MechanismValue* FindField(
        MechanismInstanceId id,
        MechanismFieldSlotId field
    ) const;
    const std::vector<MechanismReference>* FindRole(
        MechanismInstanceId id,
        MechanismRoleSlotId role
    ) const;
    const std::vector<MechanismInstanceId>& FindByDefinition(
        MechanismDefinitionId definition
    ) const;
    const std::vector<MechanismInstanceId>& FindByType(
        MechanismTypeId type
    ) const;
    const MechanismInstanceStore::InstanceMap& All() const noexcept;

private:
    MechanismInstanceStore::InstanceMap instances_;
    std::map<MechanismDefinitionId, std::vector<MechanismInstanceId>>
        instancesByDefinition_;
    std::map<MechanismTypeId, std::vector<MechanismInstanceId>>
        instancesByType_;
    std::uint64_t tick_ = 0;
    std::uint64_t revision_ = 0;
    bool published_ = false;
};

}
