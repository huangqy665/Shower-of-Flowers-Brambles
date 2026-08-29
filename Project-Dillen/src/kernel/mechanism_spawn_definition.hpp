#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mechanism_definition.hpp"

namespace dillen::kernel {

struct MechanismSpawnDefinition
{
    MechanismSpawnDefinitionId id;
    std::string canonicalName;
    MechanismDefinitionId definition;
    std::uint32_t count = 1;
    std::map<std::string, MechanismValue> initialFields;
    std::map<std::string, std::vector<MechanismReference>> initialRoles;
    MechanismDefinitionSource source;
};

}
