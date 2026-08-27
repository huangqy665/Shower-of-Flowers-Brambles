#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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
    std::map<std::string, MechanismValue> values;
    std::map<std::string, std::vector<MechanismReference>> roles;
    MechanismValue::Object algorithmState;
    std::uint64_t createdTick = 0;
    std::uint64_t updatedTick = 0;
};

}
