#pragma once

#include <cstdint>
#include <string>

namespace dillen::compatibility::hoi3::content {

struct DefinitionOrigin
{
    std::string virtualPath;
    std::string sourceLayer;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

}
