#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "mechanism_instance_store.hpp"

namespace dillen::kernel {

// Holds the store by value. The store is copy-on-write, so publishing is a
// refcount bump rather than a deep copy of the instance map plus a rebuild of
// both secondary indexes -- see the note on EntityQuerySnapshot in
// runtime/world_query_snapshot.hpp for why the indexes had to move into the
// store for this to be sound.
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
    MechanismInstanceStore instances_;
    std::uint64_t tick_ = 0;
    std::uint64_t revision_ = 0;
    bool published_ = false;
};

}
