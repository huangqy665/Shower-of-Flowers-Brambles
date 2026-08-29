#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dillen::kernel {

struct MechanismTypeId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct MechanismDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct MechanismInstanceId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct AlgorithmId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct PackageId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct RulesetId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct CapabilityId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct AlgorithmEventTypeId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct RngStreamId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct EntityTypeId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct EntityDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct EntityId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct ComponentTypeId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct RelationTypeId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct RelationDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct RelationId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct MechanismSpawnDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct MechanismFieldSlotId
{
    std::uint32_t value = UINT32_MAX;

    explicit operator bool() const noexcept;
};

struct MechanismRoleSlotId
{
    std::uint32_t value = UINT32_MAX;

    explicit operator bool() const noexcept;
};

struct ComponentFieldSlotId
{
    std::uint32_t value = UINT32_MAX;

    explicit operator bool() const noexcept;
};

struct CapabilityBindingSlotId
{
    std::uint32_t value = UINT32_MAX;

    explicit operator bool() const noexcept;
};

bool operator==(MechanismTypeId first, MechanismTypeId second) noexcept;
bool operator!=(MechanismTypeId first, MechanismTypeId second) noexcept;
bool operator<(MechanismTypeId first, MechanismTypeId second) noexcept;
bool operator==(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept;
bool operator!=(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept;
bool operator<(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept;
bool operator==(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept;
bool operator!=(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept;
bool operator<(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept;
bool operator==(AlgorithmId first, AlgorithmId second) noexcept;
bool operator!=(AlgorithmId first, AlgorithmId second) noexcept;
bool operator<(AlgorithmId first, AlgorithmId second) noexcept;
bool operator==(PackageId first, PackageId second) noexcept;
bool operator!=(PackageId first, PackageId second) noexcept;
bool operator<(PackageId first, PackageId second) noexcept;
bool operator==(RulesetId first, RulesetId second) noexcept;
bool operator!=(RulesetId first, RulesetId second) noexcept;
bool operator<(RulesetId first, RulesetId second) noexcept;
bool operator==(CapabilityId first, CapabilityId second) noexcept;
bool operator!=(CapabilityId first, CapabilityId second) noexcept;
bool operator<(CapabilityId first, CapabilityId second) noexcept;
bool operator==(
    AlgorithmEventTypeId first,
    AlgorithmEventTypeId second
) noexcept;
bool operator!=(
    AlgorithmEventTypeId first,
    AlgorithmEventTypeId second
) noexcept;
bool operator<(
    AlgorithmEventTypeId first,
    AlgorithmEventTypeId second
) noexcept;
bool operator==(RngStreamId first, RngStreamId second) noexcept;
bool operator!=(RngStreamId first, RngStreamId second) noexcept;
bool operator<(RngStreamId first, RngStreamId second) noexcept;
bool operator==(EntityTypeId first, EntityTypeId second) noexcept;
bool operator!=(EntityTypeId first, EntityTypeId second) noexcept;
bool operator<(EntityTypeId first, EntityTypeId second) noexcept;
bool operator==(
    EntityDefinitionId first,
    EntityDefinitionId second
) noexcept;
bool operator!=(
    EntityDefinitionId first,
    EntityDefinitionId second
) noexcept;
bool operator<(
    EntityDefinitionId first,
    EntityDefinitionId second
) noexcept;
bool operator==(EntityId first, EntityId second) noexcept;
bool operator!=(EntityId first, EntityId second) noexcept;
bool operator<(EntityId first, EntityId second) noexcept;
bool operator==(ComponentTypeId first, ComponentTypeId second) noexcept;
bool operator!=(ComponentTypeId first, ComponentTypeId second) noexcept;
bool operator<(ComponentTypeId first, ComponentTypeId second) noexcept;
bool operator==(RelationTypeId first, RelationTypeId second) noexcept;
bool operator!=(RelationTypeId first, RelationTypeId second) noexcept;
bool operator<(RelationTypeId first, RelationTypeId second) noexcept;
bool operator==(
    RelationDefinitionId first,
    RelationDefinitionId second
) noexcept;
bool operator!=(
    RelationDefinitionId first,
    RelationDefinitionId second
) noexcept;
bool operator<(
    RelationDefinitionId first,
    RelationDefinitionId second
) noexcept;
bool operator==(RelationId first, RelationId second) noexcept;
bool operator!=(RelationId first, RelationId second) noexcept;
bool operator<(RelationId first, RelationId second) noexcept;
bool operator==(
    MechanismSpawnDefinitionId first,
    MechanismSpawnDefinitionId second
) noexcept;
bool operator!=(
    MechanismSpawnDefinitionId first,
    MechanismSpawnDefinitionId second
) noexcept;
bool operator<(
    MechanismSpawnDefinitionId first,
    MechanismSpawnDefinitionId second
) noexcept;
bool operator==(
    MechanismFieldSlotId first,
    MechanismFieldSlotId second
) noexcept;
bool operator!=(
    MechanismFieldSlotId first,
    MechanismFieldSlotId second
) noexcept;
bool operator<(
    MechanismFieldSlotId first,
    MechanismFieldSlotId second
) noexcept;
bool operator==(
    MechanismRoleSlotId first,
    MechanismRoleSlotId second
) noexcept;
bool operator!=(
    MechanismRoleSlotId first,
    MechanismRoleSlotId second
) noexcept;
bool operator<(
    MechanismRoleSlotId first,
    MechanismRoleSlotId second
) noexcept;
bool operator==(
    ComponentFieldSlotId first,
    ComponentFieldSlotId second
) noexcept;
bool operator!=(
    ComponentFieldSlotId first,
    ComponentFieldSlotId second
) noexcept;
bool operator<(
    ComponentFieldSlotId first,
    ComponentFieldSlotId second
) noexcept;
bool operator==(
    CapabilityBindingSlotId first,
    CapabilityBindingSlotId second
) noexcept;
bool operator!=(
    CapabilityBindingSlotId first,
    CapabilityBindingSlotId second
) noexcept;
bool operator<(
    CapabilityBindingSlotId first,
    CapabilityBindingSlotId second
) noexcept;

std::string NormalizeMechanismSymbol(std::string_view symbol);
bool IsValidMechanismSymbol(std::string_view symbol) noexcept;
MechanismTypeId StableMechanismTypeId(std::string_view canonicalName);
MechanismDefinitionId StableMechanismDefinitionId(
    MechanismTypeId type,
    std::string_view canonicalName
);
MechanismInstanceId StableMechanismInstanceId(
    MechanismDefinitionId definition,
    std::uint64_t creationOrdinal
);
AlgorithmId StableAlgorithmId(std::string_view canonicalName);
PackageId StablePackageId(std::string_view canonicalName);
RulesetId StableRulesetId(std::string_view canonicalName);
CapabilityId StableCapabilityId(std::string_view canonicalName);
AlgorithmEventTypeId StableAlgorithmEventTypeId(
    std::string_view canonicalName
);
RngStreamId StableRngStreamId(std::string_view canonicalName);
EntityTypeId StableEntityTypeId(std::string_view canonicalName);
EntityDefinitionId StableEntityDefinitionId(
    EntityTypeId type,
    std::string_view canonicalName
);
EntityId StableEntityId(
    EntityDefinitionId definition,
    std::uint64_t creationOrdinal = 0
);
ComponentTypeId StableComponentTypeId(std::string_view canonicalName);
RelationTypeId StableRelationTypeId(std::string_view canonicalName);
RelationDefinitionId StableRelationDefinitionId(
    RelationTypeId type,
    std::string_view canonicalName
);
RelationId StableRelationId(
    RelationTypeId type,
    EntityId source,
    EntityId target
);
MechanismSpawnDefinitionId StableMechanismSpawnDefinitionId(
    MechanismDefinitionId definition,
    std::string_view canonicalName
);

}
