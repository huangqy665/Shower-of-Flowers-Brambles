#pragma once

#include <optional>
#include <string>
#include <variant>

#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct MechanismFieldChange
{
    MechanismInstanceId target;
    std::string field;
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

using MechanismChange = std::variant<
    MechanismFieldChange,
    MechanismLifecycleChange
>;

}
