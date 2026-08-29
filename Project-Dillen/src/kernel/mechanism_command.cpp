#include "mechanism_command.hpp"

#include <utility>

namespace dillen::kernel {

MechanismCommand MechanismCommand::SetField(
    MechanismInstanceId target,
    MechanismFieldSlotId field,
    MechanismValue value
)
{
    return {
        target,
        MechanismSetFieldOperation{
            field,
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

MechanismCommand MechanismCommand::CompleteAlgorithmCreate(
    MechanismInstanceId target
)
{
    return {
        target,
        MechanismCompleteAlgorithmCreateOperation{}
    };
}

MechanismCommand MechanismCommand::RecordAlgorithmFault(
    MechanismInstanceId target,
    AlgorithmFaultCode code,
    AlgorithmFaultStage stage
)
{
    return {
        target,
        MechanismRecordAlgorithmFaultOperation{code, stage}
    };
}

MechanismCommand MechanismCommand::ClearAlgorithmFault(
    MechanismInstanceId target
)
{
    return {target, MechanismClearAlgorithmFaultOperation{}};
}

MechanismCommand MechanismCommand::Destroy(MechanismInstanceId target)
{
    return {target, MechanismDestroyOperation{}};
}

}
