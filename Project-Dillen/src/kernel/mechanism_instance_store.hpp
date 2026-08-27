#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "mechanism_command.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_instance.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_transaction.hpp"

namespace dillen::kernel {

enum class MechanismInstanceCreateResult
{
    Created,
    DefinitionRegistryNotFrozen,
    DefinitionMissing,
    IdCollision
};

class MechanismInstanceStore
{
public:
    using InstanceMap = std::map<MechanismInstanceId, MechanismInstance>;

    MechanismInstanceCreateResult CreateFromDefinition(
        MechanismDefinitionId definition,
        const MechanismDefinitionRegistry& definitions,
        std::uint64_t currentTick,
        MechanismInstanceId& outputId
    );
    MechanismTransactionResult ApplyTransaction(
        const std::vector<MechanismCommand>& commands,
        const MechanismDefinitionRegistry& definitions,
        const MechanismSchemaRegistry& schemas,
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
    InstanceMap instances_;
    std::map<MechanismDefinitionId, std::vector<MechanismInstanceId>>
        instancesByDefinition_;
    std::map<MechanismTypeId, std::vector<MechanismInstanceId>>
        instancesByType_;
    std::map<MechanismDefinitionId, std::uint64_t>
        nextOrdinalByDefinition_;
};

}
