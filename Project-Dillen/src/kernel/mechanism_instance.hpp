#pragma once

#include <cstdint>
#include <vector>

#include "algorithm_execution_policy.hpp"
#include "controlled_script.hpp"
#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct MechanismInstance
{
    MechanismInstanceId id;
    MechanismDefinitionId definition;
    MechanismTypeId type;
    std::uint32_t schemaVersion = 0;
    AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    std::uint64_t creationOrdinal = 0;
    MechanismLifecycleState lifecycle = MechanismLifecycleState::Created;
    std::vector<MechanismValue> values;
    std::vector<std::vector<MechanismReference>> roles;
    std::vector<MechanismValue> algorithmState;
    std::vector<ControlledScriptContinuation> algorithmContinuations;
    bool algorithmInitialized = false;
    AlgorithmFaultState algorithmFault;
    std::uint64_t createdTick = 0;
    std::uint64_t updatedTick = 0;
};

}
