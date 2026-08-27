#include "launch_definition.hpp"

namespace dillen::content {

namespace {

std::uint64_t Hash(std::string_view value, std::uint64_t domain) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL ^ domain;
    for (unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash == 0 ? 1 : hash;
}

std::string LowerAscii(std::string_view value)
{
    std::string normalized(value);
    for (char& character : normalized)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return normalized;
}

}

BookmarkDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    BookmarkDefinitionId first,
    BookmarkDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    BookmarkDefinitionId first,
    BookmarkDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    BookmarkDefinitionId first,
    BookmarkDefinitionId second
) noexcept
{
    return first.value < second.value;
}

ScenarioDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    ScenarioDefinitionId first,
    ScenarioDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    ScenarioDefinitionId first,
    ScenarioDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    ScenarioDefinitionId first,
    ScenarioDefinitionId second
) noexcept
{
    return first.value < second.value;
}

std::string NormalizeBookmarkKey(std::string_view key)
{
    return LowerAscii(key);
}

std::string NormalizeScenarioKey(std::string_view key)
{
    std::string normalized;
    normalized.reserve(key.size());
    bool separator = false;
    for (char character : key)
    {
        if (character == ' ' || character == '_' || character == '-')
        {
            separator = !normalized.empty();
            continue;
        }
        if (separator)
        {
            normalized.push_back('_');
            separator = false;
        }
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
        normalized.push_back(character);
    }
    return normalized;
}

BookmarkDefinitionId StableBookmarkDefinitionId(
    std::string_view key
)
{
    const std::string normalized = NormalizeBookmarkKey(key);
    return {normalized.empty() ? 0 : Hash(normalized, 0x424F4F4BULL)};
}

ScenarioDefinitionId StableScenarioDefinitionId(
    std::string_view key
)
{
    const std::string normalized = NormalizeScenarioKey(key);
    return {normalized.empty() ? 0 : Hash(normalized, 0x5343454EULL)};
}

}
