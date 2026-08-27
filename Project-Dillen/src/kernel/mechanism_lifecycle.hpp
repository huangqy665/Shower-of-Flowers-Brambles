#pragma once

namespace dillen::kernel {

enum class MechanismLifecycleState
{
    Created,
    Active,
    Paused,
    Completed,
    Failed
};

bool IsTerminalMechanismLifecycleState(
    MechanismLifecycleState state
) noexcept;
bool CanTransitionMechanismLifecycle(
    MechanismLifecycleState current,
    MechanismLifecycleState target
) noexcept;

}
