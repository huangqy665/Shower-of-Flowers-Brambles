#include "mechanism_command.hpp"

#include <utility>

namespace dillen::kernel {

MechanismCommand MechanismCommand::SetField(
    MechanismInstanceId target,
    std::string field,
    MechanismValue value
)
{
    return {
        target,
        MechanismSetFieldOperation{
            std::move(field),
            std::move(value)
        }
    };
}

MechanismCommand MechanismCommand::TransitionLifecycle(
    MechanismInstanceId target,
    MechanismLifecycleState state
)
{
    return {
        target,
        MechanismTransitionLifecycleOperation{state}
    };
}

}
