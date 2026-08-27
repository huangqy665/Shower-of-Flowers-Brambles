#pragma once

#include <cstddef>
#include <vector>

#include "mechanism_change.hpp"
#include "mechanism_ids.hpp"

namespace dillen::kernel {

enum class MechanismTransactionStatus
{
    Committed,
    DefinitionRegistryNotFrozen,
    SchemaRegistryNotFrozen,
    TargetMissing,
    DefinitionMissing,
    InstanceDefinitionMismatch,
    SchemaMissing,
    UnknownField,
    FieldValueInvalid,
    TickRegression,
    LifecycleTransitionInvalid
};

struct MechanismTransactionResult
{
    MechanismTransactionStatus status =
        MechanismTransactionStatus::Committed;
    std::size_t commandIndex = 0;
    MechanismInstanceId target;
    std::size_t changedInstances = 0;
    std::vector<MechanismChange> changes;

    explicit operator bool() const noexcept;
};

}
