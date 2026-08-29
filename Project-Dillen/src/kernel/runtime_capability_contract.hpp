#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mechanism_ids.hpp"

namespace dillen::kernel {

class PackageLock;

struct CapabilityVersionRange
{
    std::uint32_t minimumInclusive = 1;
    std::optional<std::uint32_t> maximumExclusive;

    bool IsValid() const noexcept;
    bool Contains(std::uint32_t version) const noexcept;
};

struct CapabilityRequirement
{
    CapabilityId capability;
    std::string canonicalName;
    CapabilityVersionRange versions;
};

struct CapabilityProvision
{
    CapabilityId capability;
    std::string canonicalName;
    std::uint32_t version = 0;
};

bool IsValidCapabilityRequirement(
    const CapabilityRequirement& requirement
);
bool IsValidCapabilityProvision(const CapabilityProvision& provision);

struct RuntimeCapabilityContract
{
    CapabilityId id;
    std::string canonicalName;
    std::uint32_t version = 0;
    bool deterministic = true;
    std::vector<std::string> operations;
};

enum class CapabilityContractRegisterResult
{
    Added,
    InvalidContract,
    DuplicateVersion,
    IdCollision,
    Frozen
};

class RuntimeCapabilityContractRegistry
{
public:
    CapabilityContractRegisterResult Register(
        RuntimeCapabilityContract contract
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const RuntimeCapabilityContract* Find(
        CapabilityId capability,
        std::uint32_t version
    ) const;
    const RuntimeCapabilityContract* Latest(CapabilityId capability) const;
    const std::vector<RuntimeCapabilityContract>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<RuntimeCapabilityContract> contracts_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        indexByVersion_;
    std::map<std::uint64_t, std::size_t> latestById_;
    bool frozen_ = false;
};

enum class CapabilityResolveResult
{
    Resolved,
    RegistryNotFrozen,
    InvalidRequirement,
    CompatibleVersionMissing
};

struct ResolvedCapabilityContract
{
    CapabilityId capability;
    std::uint32_t version = 0;
};

class RuntimeCapabilityResolver
{
public:
    CapabilityResolveResult Resolve(
        const CapabilityRequirement& requirement,
        const RuntimeCapabilityContractRegistry& contracts,
        const PackageLock& packageLock,
        ResolvedCapabilityContract& output
    ) const;
};

}
