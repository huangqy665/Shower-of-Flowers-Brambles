#include "runtime_capability_contract.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "package_lock.hpp"

namespace dillen::kernel {

bool CapabilityVersionRange::IsValid() const noexcept
{
    return minimumInclusive != 0
        && (!maximumExclusive
            || minimumInclusive < *maximumExclusive);
}

bool CapabilityVersionRange::Contains(std::uint32_t version) const noexcept
{
    return IsValid()
        && version >= minimumInclusive
        && (!maximumExclusive || version < *maximumExclusive);
}

bool IsValidCapabilityRequirement(
    const CapabilityRequirement& requirement
)
{
    return requirement.capability
        && requirement.versions.IsValid()
        && IsValidMechanismSymbol(requirement.canonicalName)
        && requirement.canonicalName
            == NormalizeMechanismSymbol(requirement.canonicalName)
        && requirement.capability
            == StableCapabilityId(requirement.canonicalName);
}

bool IsValidCapabilityProvision(const CapabilityProvision& provision)
{
    return provision.capability
        && provision.version != 0
        && IsValidMechanismSymbol(provision.canonicalName)
        && provision.canonicalName
            == NormalizeMechanismSymbol(provision.canonicalName)
        && provision.capability
            == StableCapabilityId(provision.canonicalName);
}

CapabilityContractRegisterResult
RuntimeCapabilityContractRegistry::Register(
    RuntimeCapabilityContract contract
)
{
    if (frozen_)
    {
        return CapabilityContractRegisterResult::Frozen;
    }
    if (!contract.id
        || contract.version == 0
        || !IsValidMechanismSymbol(contract.canonicalName)
        || contract.canonicalName
            != NormalizeMechanismSymbol(contract.canonicalName)
        || contract.id != StableCapabilityId(contract.canonicalName))
    {
        return CapabilityContractRegisterResult::InvalidContract;
    }
    std::set<std::string> operations;
    for (const std::string& operation : contract.operations)
    {
        if (!IsValidMechanismSymbol(operation)
            || operation != NormalizeMechanismSymbol(operation)
            || !operations.emplace(operation).second)
        {
            return CapabilityContractRegisterResult::InvalidContract;
        }
    }
    const auto key = std::make_pair(contract.id.value, contract.version);
    if (indexByVersion_.find(key) != indexByVersion_.end())
    {
        return CapabilityContractRegisterResult::DuplicateVersion;
    }
    for (const RuntimeCapabilityContract& existing : contracts_)
    {
        if (existing.id == contract.id
            && existing.canonicalName != contract.canonicalName)
        {
            return CapabilityContractRegisterResult::IdCollision;
        }
    }
    indexByVersion_[key] = contracts_.size();
    contracts_.push_back(std::move(contract));
    return CapabilityContractRegisterResult::Added;
}

void RuntimeCapabilityContractRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    contracts_.clear();
    indexByVersion_.clear();
    latestById_.clear();
}

void RuntimeCapabilityContractRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        contracts_.begin(),
        contracts_.end(),
        [](const RuntimeCapabilityContract& first,
           const RuntimeCapabilityContract& second)
        {
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool RuntimeCapabilityContractRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t RuntimeCapabilityContractRegistry::Size() const noexcept
{
    return contracts_.size();
}

const RuntimeCapabilityContract* RuntimeCapabilityContractRegistry::Find(
    CapabilityId capability,
    std::uint32_t version
) const
{
    const auto iterator = indexByVersion_.find({capability.value, version});
    return iterator == indexByVersion_.end()
        ? nullptr
        : &contracts_[iterator->second];
}

const RuntimeCapabilityContract* RuntimeCapabilityContractRegistry::Latest(
    CapabilityId capability
) const
{
    const auto iterator = latestById_.find(capability.value);
    return iterator == latestById_.end()
        ? nullptr
        : &contracts_[iterator->second];
}

const std::vector<RuntimeCapabilityContract>&
RuntimeCapabilityContractRegistry::All() const noexcept
{
    return contracts_;
}

void RuntimeCapabilityContractRegistry::RebuildIndexes()
{
    indexByVersion_.clear();
    latestById_.clear();
    for (std::size_t index = 0; index < contracts_.size(); ++index)
    {
        const RuntimeCapabilityContract& contract = contracts_[index];
        indexByVersion_[{contract.id.value, contract.version}] = index;
        latestById_[contract.id.value] = index;
    }
}

CapabilityResolveResult RuntimeCapabilityResolver::Resolve(
    const CapabilityRequirement& requirement,
    const RuntimeCapabilityContractRegistry& contracts,
    const PackageLock& packageLock,
    ResolvedCapabilityContract& output
) const
{
    output = {};
    if (!contracts.IsFrozen())
    {
        return CapabilityResolveResult::RegistryNotFrozen;
    }
    if (!IsValidCapabilityRequirement(requirement))
    {
        return CapabilityResolveResult::InvalidRequirement;
    }
    if (!packageLock.IsResolved())
    {
        return CapabilityResolveResult::CompatibleVersionMissing;
    }
    const RuntimeCapabilityContract* selected = nullptr;
    for (const PackageLockEntry& package : packageLock.Entries())
    {
        for (const CapabilityProvision& provision
            : package.providedCapabilities)
        {
            if (provision.capability != requirement.capability
                || provision.canonicalName != requirement.canonicalName
                || !requirement.versions.Contains(provision.version))
            {
                continue;
            }
            const RuntimeCapabilityContract* contract = contracts.Find(
                provision.capability,
                provision.version
            );
            if (contract != nullptr
                && (selected == nullptr
                    || selected->version < contract->version))
            {
                selected = contract;
            }
        }
    }
    if (selected == nullptr)
    {
        return CapabilityResolveResult::CompatibleVersionMissing;
    }
    output = {selected->id, selected->version};
    return CapabilityResolveResult::Resolved;
}

}
