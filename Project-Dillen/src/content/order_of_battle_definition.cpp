#include "order_of_battle_definition.hpp"

namespace dillen::content {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

}

OrderOfBattleDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    OrderOfBattleDefinitionId first,
    OrderOfBattleDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    OrderOfBattleDefinitionId first,
    OrderOfBattleDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    OrderOfBattleDefinitionId first,
    OrderOfBattleDefinitionId second
) noexcept
{
    return first.value < second.value;
}

std::string NormalizeOrderOfBattlePath(std::string_view path)
{
    std::string normalized;
    normalized.reserve(path.size());
    bool previousSlash = false;
    for (char character : path)
    {
        if (character == '\\' || character == '/')
        {
            if (!previousSlash && !normalized.empty())
            {
                normalized.push_back('/');
            }
            previousSlash = true;
            continue;
        }
        previousSlash = false;
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
        normalized.push_back(character);
    }
    while (normalized.size() >= 2
        && normalized[0] == '.'
        && normalized[1] == '/')
    {
        normalized.erase(0, 2);
    }
    if (!normalized.empty() && normalized.back() == '/')
    {
        normalized.pop_back();
    }
    return normalized;
}

OrderOfBattleDefinitionId StableOrderOfBattleDefinitionId(
    std::string_view virtualPath
)
{
    const std::string normalized = NormalizeOrderOfBattlePath(virtualPath);
    std::uint64_t hash = kFnvOffset;
    for (unsigned char character : normalized)
    {
        hash ^= character;
        hash *= kFnvPrime;
    }
    if (hash == 0)
    {
        hash = 1;
    }
    return {hash};
}

}
