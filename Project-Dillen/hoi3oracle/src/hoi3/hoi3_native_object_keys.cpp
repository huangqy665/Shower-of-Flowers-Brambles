#include "hoi3_native_object_keys.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace core
{

bool PackHoi3CountryTag(std::string_view tag, uint64_t& stableId)
{
    stableId = 0;
    if (tag.size() != 3)
    {
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index)
    {
        const unsigned char raw = static_cast<unsigned char>(tag[index]);
        const unsigned char character = static_cast<unsigned char>(
            std::toupper(raw)
        );
        if (!((character >= 'A' && character <= 'Z')
              || (character >= '0' && character <= '9')
              || character == '-'))
        {
            stableId = 0;
            return false;
        }
        stableId |= static_cast<uint64_t>(character) << (index * 8);
    }
    return true;
}

bool UnpackHoi3CountryTag(uint64_t stableId, std::string& tag)
{
    tag.clear();
    if ((stableId & ~uint64_t{0x00FFFFFF}) != 0)
    {
        return false;
    }
    std::array<char, 3> characters{};
    for (std::size_t index = 0; index < characters.size(); ++index)
    {
        const unsigned char character = static_cast<unsigned char>(
            stableId >> (index * 8)
        );
        if (!((character >= 'A' && character <= 'Z')
              || (character >= '0' && character <= '9')
              || character == '-'))
        {
            return false;
        }
        characters[index] = static_cast<char>(character);
    }
    tag.assign(characters.data(), characters.size());
    return true;
}

bool PackHoi3RelationKey(
    std::string_view sourceTag,
    std::string_view targetTag,
    uint64_t& stableId
)
{
    uint64_t source = 0;
    uint64_t target = 0;
    stableId = 0;
    if (!PackHoi3CountryTag(sourceTag, source)
        || !PackHoi3CountryTag(targetTag, target)
        || source == target)
    {
        return false;
    }
    stableId = source | (target << 24);
    return true;
}

bool UnpackHoi3RelationKey(
    uint64_t stableId,
    std::string& sourceTag,
    std::string& targetTag
)
{
    if ((stableId >> 48) != 0)
    {
        sourceTag.clear();
        targetTag.clear();
        return false;
    }
    const uint64_t source = stableId & 0x00FFFFFFu;
    const uint64_t target = (stableId >> 24) & 0x00FFFFFFu;
    return source != target
        && UnpackHoi3CountryTag(source, sourceTag)
        && UnpackHoi3CountryTag(target, targetTag);
}

bool PackHoi3UnitKey(
    uint32_t id0,
    uint32_t id1,
    uint64_t& stableId
)
{
    stableId = static_cast<uint64_t>(id0)
        | (static_cast<uint64_t>(id1) << 32);
    return stableId != 0;
}

bool UnpackHoi3UnitKey(
    uint64_t stableId,
    uint32_t& id0,
    uint32_t& id1
)
{
    id0 = static_cast<uint32_t>(stableId & 0xFFFFFFFFu);
    id1 = static_cast<uint32_t>(stableId >> 32);
    return stableId != 0;
}

bool PackHoi3LeaderKey(
    uint32_t id0,
    uint32_t id1,
    uint64_t& stableId
)
{
    return PackHoi3UnitKey(id0, id1, stableId);
}

bool UnpackHoi3LeaderKey(
    uint64_t stableId,
    uint32_t& id0,
    uint32_t& id1
)
{
    return UnpackHoi3UnitKey(stableId, id0, id1);
}

std::string NormalizeHoi3DefinitionName(std::string_view name)
{
    std::string normalized(name);
    normalized.erase(
        std::remove_if(
            normalized.begin(),
            normalized.end(),
            [](unsigned char character)
            {
                return std::isspace(character) != 0;
            }
        ),
        normalized.end()
    );
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    return normalized;
}

}
