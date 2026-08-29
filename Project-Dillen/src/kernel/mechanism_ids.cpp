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

PackageId::operator bool() const noexcept
{
    return value != 0;
}

RulesetId::operator bool() const noexcept
{
    return value != 0;
}

CapabilityId::operator bool() const noexcept
{
    return value != 0;
}

AlgorithmEventTypeId::operator bool() const noexcept
{
    return value != 0;
}

RngStreamId::operator bool() const noexcept
{
    return value != 0;
}

EntityTypeId::operator bool() const noexcept
{
    return value != 0;
}

EntityDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

EntityId::operator bool() const noexcept
{
    return value != 0;
}

ComponentTypeId::operator bool() const noexcept
{
    return value != 0;
}

RelationTypeId::operator bool() const noexcept
{
    return value != 0;
}

RelationDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

RelationId::operator bool() const noexcept
{
    return value != 0;
}

MechanismSpawnDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

MechanismFieldSlotId::operator bool() const noexcept
{
    return value != UINT32_MAX;
}

MechanismRoleSlotId::operator bool() const noexcept
{
    return value != UINT32_MAX;
}

ComponentFieldSlotId::operator bool() const noexcept
{
    return value != UINT32_MAX;
}

CapabilityBindingSlotId::operator bool() const noexcept
{
    return value != UINT32_MAX;
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

bool operator==(PackageId first, PackageId second) noexcept
{
    return first.value == second.value;
}

bool operator!=(PackageId first, PackageId second) noexcept
{
    return !(first == second);
}

bool operator<(PackageId first, PackageId second) noexcept
{
    return first.value < second.value;
}

bool operator==(RulesetId first, RulesetId second) noexcept
{
    return first.value == second.value;
}

bool operator!=(RulesetId first, RulesetId second) noexcept
{
    return !(first == second);
}

bool operator<(RulesetId first, RulesetId second) noexcept
{
    return first.value < second.value;
}

#define DILLEN_DEFINE_ID_OPERATORS(Type) \
bool operator==(Type first, Type second) noexcept \
{ \
    return first.value == second.value; \
} \
bool operator!=(Type first, Type second) noexcept \
{ \
    return !(first == second); \
} \
bool operator<(Type first, Type second) noexcept \
{ \
    return first.value < second.value; \
}

DILLEN_DEFINE_ID_OPERATORS(EntityTypeId)
DILLEN_DEFINE_ID_OPERATORS(CapabilityId)
DILLEN_DEFINE_ID_OPERATORS(AlgorithmEventTypeId)
DILLEN_DEFINE_ID_OPERATORS(RngStreamId)
DILLEN_DEFINE_ID_OPERATORS(EntityDefinitionId)
DILLEN_DEFINE_ID_OPERATORS(EntityId)
DILLEN_DEFINE_ID_OPERATORS(ComponentTypeId)
DILLEN_DEFINE_ID_OPERATORS(RelationTypeId)
DILLEN_DEFINE_ID_OPERATORS(RelationDefinitionId)
DILLEN_DEFINE_ID_OPERATORS(RelationId)
DILLEN_DEFINE_ID_OPERATORS(MechanismSpawnDefinitionId)

#undef DILLEN_DEFINE_ID_OPERATORS

bool operator==(
    MechanismFieldSlotId first,
    MechanismFieldSlotId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    MechanismFieldSlotId first,
    MechanismFieldSlotId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    MechanismFieldSlotId first,
    MechanismFieldSlotId second
) noexcept
{
    return first.value < second.value;
}

bool operator==(
    MechanismRoleSlotId first,
    MechanismRoleSlotId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    MechanismRoleSlotId first,
    MechanismRoleSlotId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    MechanismRoleSlotId first,
    MechanismRoleSlotId second
) noexcept
{
    return first.value < second.value;
}

bool operator==(
    ComponentFieldSlotId first,
    ComponentFieldSlotId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    ComponentFieldSlotId first,
    ComponentFieldSlotId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    ComponentFieldSlotId first,
    ComponentFieldSlotId second
) noexcept
{
    return first.value < second.value;
}

bool operator==(
    CapabilityBindingSlotId first,
    CapabilityBindingSlotId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    CapabilityBindingSlotId first,
    CapabilityBindingSlotId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    CapabilityBindingSlotId first,
    CapabilityBindingSlotId second
) noexcept
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

PackageId StablePackageId(std::string_view canonicalName)
{
    return {HashText(
        "dillen_package:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

RulesetId StableRulesetId(std::string_view canonicalName)
{
    return {HashText(
        "dillen_ruleset:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

CapabilityId StableCapabilityId(std::string_view canonicalName)
{
    return {HashText(
        "runtime_capability:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

AlgorithmEventTypeId StableAlgorithmEventTypeId(
    std::string_view canonicalName
)
{
    return {HashText(
        "algorithm_event_type:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

RngStreamId StableRngStreamId(std::string_view canonicalName)
{
    return {HashText(
        "rng_stream:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

EntityTypeId StableEntityTypeId(std::string_view canonicalName)
{
    return {HashText(
        "entity_type:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

EntityDefinitionId StableEntityDefinitionId(
    EntityTypeId type,
    std::string_view canonicalName
)
{
    return {HashDefinition(
        MechanismTypeId{type.value},
        "entity:" + NormalizeMechanismSymbol(canonicalName)
    )};
}

EntityId StableEntityId(
    EntityDefinitionId definition,
    std::uint64_t creationOrdinal
)
{
    return {HashInstance(
        MechanismDefinitionId{definition.value},
        creationOrdinal
    )};
}

ComponentTypeId StableComponentTypeId(std::string_view canonicalName)
{
    return {HashText(
        "component_type:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

RelationTypeId StableRelationTypeId(std::string_view canonicalName)
{
    return {HashText(
        "relation_type:",
        NormalizeMechanismSymbol(canonicalName)
    )};
}

RelationDefinitionId StableRelationDefinitionId(
    RelationTypeId type,
    std::string_view canonicalName
)
{
    return {HashDefinition(
        MechanismTypeId{type.value},
        "relation_definition:"
            + NormalizeMechanismSymbol(canonicalName)
    )};
}

RelationId StableRelationId(
    RelationTypeId type,
    EntityId source,
    EntityId target
)
{
    std::uint64_t hash = HashText("relation:", "v1");
    AppendUnsigned(hash, type.value);
    AppendUnsigned(hash, source.value);
    AppendUnsigned(hash, target.value);
    return {hash == 0 ? 1 : hash};
}

MechanismSpawnDefinitionId StableMechanismSpawnDefinitionId(
    MechanismDefinitionId definition,
    std::string_view canonicalName
)
{
    std::uint64_t hash = HashText(
        "mechanism_spawn:",
        NormalizeMechanismSymbol(canonicalName)
    );
    AppendUnsigned(hash, definition.value);
    return {hash == 0 ? 1 : hash};
}

}
