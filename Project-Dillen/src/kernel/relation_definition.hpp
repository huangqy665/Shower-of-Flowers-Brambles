#pragma once

#include <cstdint>
#include <string>

#include "mechanism_ids.hpp"

namespace dillen::kernel {

struct RelationDefinitionSource
{
    std::string sourceName;
    std::string virtualPath;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct RelationDefinition
{
    RelationDefinitionId id;
    RelationTypeId type;
    std::uint32_t schemaVersion = 0;
    std::string canonicalName;
    EntityDefinitionId source;
    EntityDefinitionId target;
    RelationDefinitionSource origin;
};

}
