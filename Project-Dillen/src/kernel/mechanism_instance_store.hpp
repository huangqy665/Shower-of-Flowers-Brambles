#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <memory>

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

    // Copy-on-write. Copying the store shares the payload and costs a
    // refcount bump; the first write through a shared handle clones it. The
    // World Transaction executor copies all six stores to stage a transaction,
    // so this turns "stage a transaction" from O(world) into O(what it
    // touches). Reads go through Read(), writes through Mutable() -- taking a
    // non-const reference out of Read() would mutate a payload someone else
    // may still be holding.
    //
    // use_count() is only meaningful because commit is single-threaded by
    // contract (memo section 3.9): worker threads may run algorithm dispatch,
    // never the store writes below.
    struct Data
    {
        InstanceMap instances;
        std::map<MechanismDefinitionId, std::vector<MechanismInstanceId>>
            instancesByDefinition;
        std::map<MechanismTypeId, std::vector<MechanismInstanceId>>
            instancesByType;
        std::map<MechanismDefinitionId, std::uint64_t>
            nextOrdinalByDefinition;
    };

    const Data& Read() const noexcept { return *data_; }
    Data& Mutable()
    {
        if (data_.use_count() > 1)
        {
            data_ = std::make_shared<Data>(*data_);
        }
        return *data_;
    }

    std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

}
