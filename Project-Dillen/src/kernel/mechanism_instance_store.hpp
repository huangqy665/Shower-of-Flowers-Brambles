#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "mechanism_command.hpp"
#include "frozen_runtime_catalog.hpp"
#include "mechanism_instance.hpp"
#include "mechanism_transaction.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::kernel {

enum class MechanismInstanceCreateResult
{
    Created,
    RuntimeCatalogNotFrozen,
    SpawnMissing,
    DefinitionMissing,
    IdCollision
};

class MechanismInstanceStore
{
public:
    using InstanceMap = std::map<MechanismInstanceId, MechanismInstance>;

    MechanismInstanceCreateResult CreateFromDefinition(
        MechanismDefinitionId definition,
        const FrozenRuntimeCatalog& catalog,
        std::uint64_t currentTick,
        MechanismInstanceId& outputId
    );
    MechanismInstanceCreateResult CreateFromSpawn(
        MechanismSpawnDefinitionId spawn,
        const FrozenRuntimeCatalog& catalog,
        std::uint64_t currentTick,
        MechanismInstanceId& outputId
    );
    MechanismTransactionResult ApplyTransaction(
        const std::vector<MechanismCommand>& commands,
        const FrozenRuntimeCatalog& catalog,
        std::uint64_t currentTick
    );
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const MechanismInstance* Find(MechanismInstanceId id) const;
    const std::vector<MechanismInstanceId>& FindByDefinition(
        MechanismDefinitionId definition
    ) const;
    const std::vector<MechanismInstanceId>& FindByType(
        MechanismTypeId type
    ) const;
    const InstanceMap& All() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    InstanceMap instances_;
    std::map<MechanismDefinitionId, std::vector<MechanismInstanceId>>
        instancesByDefinition_;
    std::map<MechanismTypeId, std::vector<MechanismInstanceId>>
        instancesByType_;
    std::map<MechanismDefinitionId, std::uint64_t>
        nextOrdinalByDefinition_;
};

}
