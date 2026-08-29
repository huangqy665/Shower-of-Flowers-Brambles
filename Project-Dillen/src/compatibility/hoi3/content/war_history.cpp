#include "war_history.hpp"

namespace dillen::compatibility::hoi3::content {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

}

WarHistoryDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    WarHistoryDefinitionId first,
    WarHistoryDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    WarHistoryDefinitionId first,
    WarHistoryDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    WarHistoryDefinitionId first,
    WarHistoryDefinitionId second
) noexcept
{
    return first.value < second.value;
}

std::string NormalizeWarHistoryPath(std::string_view path)
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
    while (!normalized.empty() && normalized.back() == '/')
    {
        normalized.pop_back();
    }
    return normalized;
}

WarHistoryDefinitionId StableWarHistoryDefinitionId(
    std::string_view virtualPath
)
{
    const std::string normalized = NormalizeWarHistoryPath(virtualPath);
    std::uint64_t hash = kFnvOffset;
    for (const unsigned char character : normalized)
    {
        hash ^= character;
        hash *= kFnvPrime;
    }
    return {hash == 0 ? 1 : hash};
}

}
