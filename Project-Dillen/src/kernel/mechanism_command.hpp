#pragma once

#include <string>
#include <variant>

#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct MechanismSetFieldOperation
{
    std::string field;
    MechanismValue value;
};

struct MechanismTransitionLifecycleOperation
{
    MechanismLifecycleState target = MechanismLifecycleState::Created;
};

using MechanismCommandOperation = std::variant<
    MechanismSetFieldOperation,
    MechanismTransitionLifecycleOperation
>;

struct MechanismCommand
{
    MechanismInstanceId target;
    MechanismCommandOperation operation;

    static MechanismCommand SetField(
        MechanismInstanceId target,
        std::string field,
        MechanismValue value
    );
    static MechanismCommand TransitionLifecycle(
        MechanismInstanceId target,
        MechanismLifecycleState state
    );
};

}
