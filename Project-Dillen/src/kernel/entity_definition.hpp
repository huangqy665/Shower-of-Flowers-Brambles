#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct EntityDefinitionSource
{
    std::string sourceName;
    std::string virtualPath;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct EntityComponentDefinition
{
    ComponentTypeId type;
    std::uint32_t schemaVersion = 0;
    std::map<std::string, MechanismValue> fields;
};

struct EntityDefinition
{
    EntityDefinitionId id;
    EntityTypeId type;
    std::string canonicalName;
    std::vector<EntityComponentDefinition> components;
    EntityDefinitionSource source;
};

}
