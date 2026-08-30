#pragma once

#include <optional>
#include <variant>

#include "algorithm_execution_policy.hpp"
#include "controlled_script.hpp"
#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct MechanismFieldChange
{
    MechanismInstanceId target;
    MechanismFieldSlotId field;
    std::optional<MechanismValue> previousValue;
    MechanismValue currentValue;
};

struct MechanismLifecycleChange
{
    MechanismInstanceId target;
    MechanismLifecycleState previousState =
        MechanismLifecycleState::Created;
    MechanismLifecycleState currentState =
        MechanismLifecycleState::Created;
};

struct MechanismAlgorithmInitializedChange
{
    MechanismInstanceId target;
};

struct MechanismAlgorithmFaultChange
{
    MechanismInstanceId target;
    AlgorithmFaultState previousState;
    AlgorithmFaultState currentState;
};

struct MechanismDestroyedChange
{
    MechanismInstanceId target;
    MechanismDefinitionId definition;
    MechanismTypeId type;
};

struct MechanismAlgorithmStateChange
{
    MechanismInstanceId target;
    std::vector<MechanismValue> previousState;
    std::vector<MechanismValue> currentState;
    std::vector<ControlledScriptContinuation> previousContinuations;
    std::vector<ControlledScriptContinuation> currentContinuations;
};

using MechanismChange = std::variant<
    MechanismFieldChange,
    MechanismLifecycleChange,
    MechanismAlgorithmInitializedChange,
    MechanismAlgorithmFaultChange,
    MechanismAlgorithmStateChange,
    MechanismDestroyedChange
>;

}
