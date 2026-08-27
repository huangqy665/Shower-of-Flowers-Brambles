#include "mechanism_lifecycle.hpp"

namespace dillen::kernel {

bool IsTerminalMechanismLifecycleState(
    MechanismLifecycleState state
) noexcept
{
    return state == MechanismLifecycleState::Completed
        || state == MechanismLifecycleState::Failed;
}

bool CanTransitionMechanismLifecycle(
    MechanismLifecycleState current,
    MechanismLifecycleState target
) noexcept
{
    if (current == target)
    {
        return true;
    }
    switch (current)
    {
    case MechanismLifecycleState::Created:
        return target == MechanismLifecycleState::Active
            || target == MechanismLifecycleState::Completed
            || target == MechanismLifecycleState::Failed;
    case MechanismLifecycleState::Active:
        return target == MechanismLifecycleState::Paused
            || target == MechanismLifecycleState::Completed
            || target == MechanismLifecycleState::Failed;
    case MechanismLifecycleState::Paused:
        return target == MechanismLifecycleState::Active
            || target == MechanismLifecycleState::Completed
            || target == MechanismLifecycleState::Failed;
    case MechanismLifecycleState::Completed:
    case MechanismLifecycleState::Failed:
        return false;
    }
    return false;
}

}
