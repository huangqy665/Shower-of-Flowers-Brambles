#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace dillen::kernel {

// A zero-cost strong identity. `Tag` makes each alias a distinct type,
// `Underlying` is the stored representation and `Empty` is the sentinel that
// `operator bool` treats as "unset". The type stays an aggregate, so
// `Id{}`, `Id{rawValue}` and `return {rawValue};` all keep working, and
// `value` stays a public mutable member for the persistence codec.
template <class Tag, class Underlying, Underlying Empty>
struct StrongId
{
    Underlying value = Empty;

    constexpr explicit operator bool() const noexcept
    {
        return value != Empty;
    }

    friend constexpr bool operator==(
        StrongId first,
        StrongId second
    ) noexcept
    {
        return first.value == second.value;
    }
    friend constexpr bool operator!=(
        StrongId first,
        StrongId second
    ) noexcept
    {
        return first.value != second.value;
    }
    friend constexpr bool operator<(
        StrongId first,
        StrongId second
    ) noexcept
    {
        return first.value < second.value;
    }
};

// 64-bit stable identities: namespace-qualified symbol hashes; empty == 0.
using MechanismTypeId =
    StrongId<struct MechanismTypeIdTag, std::uint64_t, 0>;
using MechanismDefinitionId =
    StrongId<struct MechanismDefinitionIdTag, std::uint64_t, 0>;
using MechanismInstanceId =
    StrongId<struct MechanismInstanceIdTag, std::uint64_t, 0>;
using MechanismSpawnDefinitionId =
    StrongId<struct MechanismSpawnDefinitionIdTag, std::uint64_t, 0>;
using AlgorithmId =
    StrongId<struct AlgorithmIdTag, std::uint64_t, 0>;
using PackageId =
    StrongId<struct PackageIdTag, std::uint64_t, 0>;
using RulesetId =
    StrongId<struct RulesetIdTag, std::uint64_t, 0>;
using CapabilityId =
    StrongId<struct CapabilityIdTag, std::uint64_t, 0>;
using AlgorithmEventTypeId =
    StrongId<struct AlgorithmEventTypeIdTag, std::uint64_t, 0>;
using RngStreamId =
    StrongId<struct RngStreamIdTag, std::uint64_t, 0>;
using EntityTypeId =
    StrongId<struct EntityTypeIdTag, std::uint64_t, 0>;
using EntityDefinitionId =
    StrongId<struct EntityDefinitionIdTag, std::uint64_t, 0>;
using EntityId =
    StrongId<struct EntityIdTag, std::uint64_t, 0>;
using ComponentTypeId =
    StrongId<struct ComponentTypeIdTag, std::uint64_t, 0>;
using RelationTypeId =
    StrongId<struct RelationTypeIdTag, std::uint64_t, 0>;
using RelationDefinitionId =
    StrongId<struct RelationDefinitionIdTag, std::uint64_t, 0>;
using RelationId =
    StrongId<struct RelationIdTag, std::uint64_t, 0>;

// 32-bit runtime slots: dense indices into a Frozen Runtime Catalog layout;
// empty == UINT32_MAX.
using MechanismFieldSlotId =
    StrongId<struct MechanismFieldSlotIdTag, std::uint32_t, UINT32_MAX>;
using MechanismRoleSlotId =
    StrongId<struct MechanismRoleSlotIdTag, std::uint32_t, UINT32_MAX>;
using ComponentFieldSlotId =
    StrongId<struct ComponentFieldSlotIdTag, std::uint32_t, UINT32_MAX>;
using CapabilityBindingSlotId =
    StrongId<struct CapabilityBindingSlotIdTag, std::uint32_t, UINT32_MAX>;

namespace detail {

template <class Id, class Underlying>
inline constexpr bool kIdLayoutMatches =
    sizeof(Id) == sizeof(Underlying)
    && alignof(Id) == alignof(Underlying)
    && std::is_trivially_copyable_v<Id>
    && std::is_standard_layout_v<Id>;

static_assert(kIdLayoutMatches<MechanismTypeId, std::uint64_t>);
static_assert(kIdLayoutMatches<MechanismDefinitionId, std::uint64_t>);
static_assert(kIdLayoutMatches<MechanismInstanceId, std::uint64_t>);
static_assert(kIdLayoutMatches<MechanismSpawnDefinitionId, std::uint64_t>);
static_assert(kIdLayoutMatches<AlgorithmId, std::uint64_t>);
static_assert(kIdLayoutMatches<PackageId, std::uint64_t>);
static_assert(kIdLayoutMatches<RulesetId, std::uint64_t>);
static_assert(kIdLayoutMatches<CapabilityId, std::uint64_t>);
static_assert(kIdLayoutMatches<AlgorithmEventTypeId, std::uint64_t>);
static_assert(kIdLayoutMatches<RngStreamId, std::uint64_t>);
static_assert(kIdLayoutMatches<EntityTypeId, std::uint64_t>);
static_assert(kIdLayoutMatches<EntityDefinitionId, std::uint64_t>);
static_assert(kIdLayoutMatches<EntityId, std::uint64_t>);
static_assert(kIdLayoutMatches<ComponentTypeId, std::uint64_t>);
static_assert(kIdLayoutMatches<RelationTypeId, std::uint64_t>);
static_assert(kIdLayoutMatches<RelationDefinitionId, std::uint64_t>);
static_assert(kIdLayoutMatches<RelationId, std::uint64_t>);
static_assert(kIdLayoutMatches<MechanismFieldSlotId, std::uint32_t>);
static_assert(kIdLayoutMatches<MechanismRoleSlotId, std::uint32_t>);
static_assert(kIdLayoutMatches<ComponentFieldSlotId, std::uint32_t>);
static_assert(kIdLayoutMatches<CapabilityBindingSlotId, std::uint32_t>);

}

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
// Deterministic Algorithm Inbox event type used to deliver a Capability
// invocation to its providers. Lives in its own hash domain so it can never
// collide with an authored `algorithm_event_type`.
AlgorithmEventTypeId CapabilityDeliveryEventType(
    std::string_view capabilityCanonicalName
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

// Enables std::unordered_map / std::unordered_set keyed by any StrongId.
// No container in the tree uses this yet; it only removes the barrier for
// swapping hot ordered lookups to hashed ones later. The stored value is
// already a namespace-qualified symbol hash, so identity hashing is fine.
namespace std {

template <class Tag, class Underlying, Underlying Empty>
struct hash<::dillen::kernel::StrongId<Tag, Underlying, Empty>>
{
    std::size_t operator()(
        ::dillen::kernel::StrongId<Tag, Underlying, Empty> id
    ) const noexcept
    {
        return std::hash<Underlying>{}(id.value);
    }
};

}
