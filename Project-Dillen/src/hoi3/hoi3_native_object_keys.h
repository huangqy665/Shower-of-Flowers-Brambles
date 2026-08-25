#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace core
{

bool PackHoi3CountryTag(std::string_view tag, uint64_t& stableId);
bool UnpackHoi3CountryTag(uint64_t stableId, std::string& tag);
bool PackHoi3RelationKey(
    std::string_view sourceTag,
    std::string_view targetTag,
    uint64_t& stableId
);
bool UnpackHoi3RelationKey(
    uint64_t stableId,
    std::string& sourceTag,
    std::string& targetTag
);
bool PackHoi3UnitKey(
    uint32_t id0,
    uint32_t id1,
    uint64_t& stableId
);
bool UnpackHoi3UnitKey(
    uint64_t stableId,
    uint32_t& id0,
    uint32_t& id1
);
bool PackHoi3LeaderKey(
    uint32_t id0,
    uint32_t id1,
    uint64_t& stableId
);
bool UnpackHoi3LeaderKey(
    uint64_t stableId,
    uint32_t& id0,
    uint32_t& id1
);
std::string NormalizeHoi3DefinitionName(std::string_view name);

}
