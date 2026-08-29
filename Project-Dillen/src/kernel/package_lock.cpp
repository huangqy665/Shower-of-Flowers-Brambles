#include "package_lock.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace dillen::kernel {

namespace {

using CandidateMap = std::map<
    PackageId,
    std::vector<const PackageManifest*>
>;
using ConstraintMap = std::map<
    PackageId,
    std::vector<PackageVersionRange>
>;
using SelectionMap = std::map<PackageId, const PackageManifest*>;

bool MatchesAll(
    PackageVersion version,
    const std::vector<PackageVersionRange>& constraints
)
{
    return std::all_of(
        constraints.begin(),
        constraints.end(),
        [version](const PackageVersionRange& constraint)
        {
            return constraint.Contains(version);
        }
    );
}

bool ResolveSelection(
    const CandidateMap& candidates,
    ConstraintMap constraints,
    SelectionMap selected,
    SelectionMap& output,
    PackageId& failedPackage
)
{
    for (const auto& choice : selected)
    {
        const auto constraint = constraints.find(choice.first);
        if (constraint != constraints.end()
            && !MatchesAll(choice.second->version, constraint->second))
        {
            failedPackage = choice.first;
            return false;
        }
    }

    auto unresolved = std::find_if(
        constraints.begin(),
        constraints.end(),
        [&selected](const auto& entry)
        {
            return selected.find(entry.first) == selected.end();
        }
    );
    if (unresolved == constraints.end())
    {
        output = std::move(selected);
        return true;
    }

    const auto available = candidates.find(unresolved->first);
    if (available == candidates.end())
    {
        failedPackage = unresolved->first;
        return false;
    }
    for (const PackageManifest* candidate : available->second)
    {
        if (!MatchesAll(candidate->version, unresolved->second))
        {
            continue;
        }
        SelectionMap branchSelected = selected;
        ConstraintMap branchConstraints = constraints;
        branchSelected[unresolved->first] = candidate;
        for (const PackageDependency& dependency : candidate->dependencies)
        {
            if (dependency.required)
            {
                branchConstraints[dependency.package].push_back(
                    dependency.versions
                );
            }
        }
        if (ResolveSelection(
                candidates,
                std::move(branchConstraints),
                std::move(branchSelected),
                output,
                failedPackage))
        {
            return true;
        }
    }
    failedPackage = unresolved->first;
    return false;
}

bool BuildLoadOrder(
    const SelectionMap& selected,
    std::vector<PackageId>& order
)
{
    std::map<PackageId, std::size_t> indegree;
    std::map<PackageId, std::vector<PackageId>> dependents;
    for (const auto& entry : selected)
    {
        indegree[entry.first] = 0;
    }
    for (const auto& entry : selected)
    {
        for (const PackageDependency& dependency
            : entry.second->dependencies)
        {
            if (!dependency.required
                || selected.find(dependency.package) == selected.end())
            {
                continue;
            }
            dependents[dependency.package].push_back(entry.first);
            ++indegree[entry.first];
        }
    }

    const auto ordering = [&selected](PackageId first, PackageId second)
    {
        const PackageManifest& firstManifest = *selected.at(first);
        const PackageManifest& secondManifest = *selected.at(second);
        if (firstManifest.loadPriority != secondManifest.loadPriority)
        {
            return firstManifest.loadPriority < secondManifest.loadPriority;
        }
        return first < second;
    };
    std::set<PackageId, decltype(ordering)> ready(ordering);
    for (const auto& entry : indegree)
    {
        if (entry.second == 0)
        {
            ready.emplace(entry.first);
        }
    }

    while (!ready.empty())
    {
        const PackageId package = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(package);
        for (PackageId dependent : dependents[package])
        {
            std::size_t& count = indegree[dependent];
            if (--count == 0)
            {
                ready.emplace(dependent);
            }
        }
    }
    return order.size() == selected.size();
}

}

bool PackageLock::IsResolved() const noexcept
{
    return resolved_;
}

std::size_t PackageLock::Size() const noexcept
{
    return entries_.size();
}

const PackageLockEntry* PackageLock::Find(PackageId package) const
{
    const auto iterator = indexByPackage_.find(package);
    return iterator == indexByPackage_.end()
        ? nullptr
        : &entries_[iterator->second];
}

const std::vector<PackageLockEntry>& PackageLock::Entries() const noexcept
{
    return entries_;
}

void PackageLock::RebuildIndex()
{
    indexByPackage_.clear();
    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        indexByPackage_[entries_[index].package] = index;
    }
}

bool PackageLockReport::Success() const noexcept
{
    return issues.empty();
}

bool PackageLockBuilder::Resolve(
    const PackageManifestRegistry& manifests,
    const RulesetDefinition& ruleset,
    PackageLock& output,
    PackageLockReport& report
) const
{
    output = {};
    report = {};
    if (!IsValidRulesetDefinition(ruleset))
    {
        report.issues.push_back({
            PackageLockIssueCode::InvalidRuleset,
            {},
            "Ruleset definition is structurally invalid"
        });
        return false;
    }
    if (!manifests.IsFrozen())
    {
        report.issues.push_back({
            PackageLockIssueCode::ManifestRegistryNotFrozen,
            {},
            "Package Manifest Registry must be frozen before resolution"
        });
        return false;
    }

    CandidateMap candidates;
    for (const PackageManifest& manifest : manifests.All())
    {
        candidates[manifest.id].push_back(&manifest);
    }
    for (auto& entry : candidates)
    {
        std::sort(
            entry.second.begin(),
            entry.second.end(),
            [](const PackageManifest* first, const PackageManifest* second)
            {
                return first->version > second->version;
            }
        );
    }

    ConstraintMap constraints;
    for (const RulesetPackageRequirement& requirement : ruleset.packages)
    {
        constraints[requirement.package].push_back(requirement.versions);
    }
    SelectionMap selected;
    PackageId failedPackage;
    if (!ResolveSelection(
            candidates,
            std::move(constraints),
            {},
            selected,
            failedPackage))
    {
        report.issues.push_back({
            candidates.find(failedPackage) == candidates.end()
                ? PackageLockIssueCode::PackageUnavailable
                : PackageLockIssueCode::VersionConflict,
            failedPackage,
            "No package version satisfies the complete dependency closure"
        });
        return false;
    }

    std::vector<PackageId> loadOrder;
    if (!BuildLoadOrder(selected, loadOrder))
    {
        report.issues.push_back({
            PackageLockIssueCode::DependencyCycle,
            {},
            "Required package dependencies contain a cycle"
        });
        return false;
    }

    output.entries_.reserve(loadOrder.size());
    for (std::size_t index = 0; index < loadOrder.size(); ++index)
    {
        const PackageManifest& manifest = *selected.at(loadOrder[index]);
        PackageLockEntry entry;
        entry.package = manifest.id;
        entry.canonicalName = manifest.canonicalName;
        entry.version = manifest.version;
        entry.contentDigest = manifest.contentDigest;
        entry.loadIndex = index;
        entry.providedCapabilities = manifest.providedCapabilities;
        for (const PackageDependency& dependency : manifest.dependencies)
        {
            const auto resolved = selected.find(dependency.package);
            if (resolved != selected.end())
            {
                entry.dependencies.push_back({
                    dependency.package,
                    resolved->second->version
                });
            }
        }
        std::sort(
            entry.dependencies.begin(),
            entry.dependencies.end(),
            [](const LockedPackageDependency& first,
               const LockedPackageDependency& second)
            {
                return first.package < second.package;
            }
        );
        std::sort(
            entry.providedCapabilities.begin(),
            entry.providedCapabilities.end(),
            [](const CapabilityProvision& first,
               const CapabilityProvision& second)
            {
                if (first.capability != second.capability)
                {
                    return first.capability < second.capability;
                }
                return first.version < second.version;
            }
        );
        output.entries_.push_back(std::move(entry));
    }
    output.RebuildIndex();
    output.resolved_ = true;
    return true;
}

}
