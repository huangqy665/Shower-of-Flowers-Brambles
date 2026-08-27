#include "mechanism_ids.hpp"

namespace dillen::kernel {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t HashText(std::string_view domain, std::string_view text)
{
    std::uint64_t hash = kFnvOffset;
    const auto append = [&hash](std::string_view value)
    {
        for (const unsigned char character : value)
        {
            hash ^= character;
            hash *= kFnvPrime;
        }
    };
    append(domain);
    append(text);
    return hash == 0 ? 1 : hash;
}

std::uint64_t HashDefinition(
    MechanismTypeId type,
    std::string_view canonicalName
)
{
    std::uint64_t hash = HashText("mechanism_definition:", canonicalName);
    std::uint64_t typeValue = type.value;
    for (std::size_t index = 0; index < sizeof(typeValue); ++index)
    {
        hash ^= static_cast<unsigned char>(typeValue & 0xFFU);
        hash *= kFnvPrime;
        typeValue >>= 8U;
    }
    return hash == 0 ? 1 : hash;
}

void AppendUnsigned(std::uint64_t& hash, std::uint64_t value)
{
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        hash ^= static_cast<unsigned char>(value & 0xFFU);
        hash *= kFnvPrime;
        value >>= 8U;
    }
}

std::uint64_t HashInstance(
    MechanismDefinitionId definition,
    std::uint64_t creationOrdinal
)
{
    std::uint64_t hash = HashText("mechanism_instance:", "v1");
    AppendUnsigned(hash, definition.value);
    AppendUnsigned(hash, creationOrdinal);
    return hash == 0 ? 1 : hash;
}

}

MechanismTypeId::operator bool() const noexcept
{
    return value != 0;
}

MechanismDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

MechanismInstanceId::operator bool() const noexcept
{
    return value != 0;
}

AlgorithmId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(MechanismTypeId first, MechanismTypeId second) noexcept
{
    return first.value == second.value;
}

bool operator!=(MechanismTypeId first, MechanismTypeId second) noexcept
{
    return !(first == second);
}

bool operator<(MechanismTypeId first, MechanismTypeId second) noexcept
{
    return first.value < second.value;
}

bool operator==(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept
{
    return first.value < second.value;
}

bool operator==(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept
{
    return first.value < second.value;
}

bool operator==(AlgorithmId first, AlgorithmId second) noexcept
{
    return first.value == second.value;
}

bool operator!=(AlgorithmId first, AlgorithmId second) noexcept
{
    return !(first == second);
}

bool operator<(AlgorithmId first, AlgorithmId second) noexcept
{
    return first.value < second.value;
}

std::string NormalizeMechanismSymbol(std::string_view symbol)
{
    std::string normalized(symbol);
    for (char& character : normalized)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
        else if (character == '\\')
        {
            character = '/';
        }
    }
    return normalized;
}

bool IsValidMechanismSymbol(std::string_view symbol) noexcept
{
    if (symbol.empty())
    {
        return false;
    }
    bool hasAlphaNumeric = false;
    for (const char character : symbol)
    {
        const bool alphaNumeric =
            (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9');
        hasAlphaNumeric = hasAlphaNumeric || alphaNumeric;
        if (!alphaNumeric
            && character != '_'
            && character != '-'
            && character != '.'
            && character != ':'
            && character != '/')
        {
            return false;
        }
    }
    return hasAlphaNumeric
        && symbol.front() != '.'
        && symbol.front() != ':'
        && symbol.front() != '/'
        && symbol.back() != '.'
        && symbol.back() != ':'
        && symbol.back() != '/';
}

MechanismTypeId StableMechanismTypeId(std::string_view canonicalName)
{
    return {HashText(
        "mechanism_type:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

MechanismDefinitionId StableMechanismDefinitionId(
    MechanismTypeId type,
    std::string_view canonicalName
)
{
    return {HashDefinition(
        type,
        NormalizeMechanismSymbol(canonicalName)
    )};
}

MechanismInstanceId StableMechanismInstanceId(
    MechanismDefinitionId definition,
    std::uint64_t creationOrdinal
)
{
    return {HashInstance(definition, creationOrdinal)};
}

AlgorithmId StableAlgorithmId(std::string_view canonicalName)
{
    return {HashText(
        "mechanism_algorithm:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

}
