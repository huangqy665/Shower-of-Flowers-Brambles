#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "package_manifest.hpp"
#include "ruleset.hpp"

namespace dillen::kernel {

struct LockedPackageDependency
{
    PackageId package;
    PackageVersion version;
};

struct PackageLockEntry
{
    PackageId package;
    std::string canonicalName;
    PackageVersion version;
    std::string contentDigest;
    std::size_t loadIndex = 0;
    std::vector<LockedPackageDependency> dependencies;
    std::vector<CapabilityProvision> providedCapabilities;
};

class PackageLock
{
public:
    bool IsResolved() const noexcept;
    std::size_t Size() const noexcept;
    const PackageLockEntry* Find(PackageId package) const;
    const std::vector<PackageLockEntry>& Entries() const noexcept;

private:
    friend class PackageLockBuilder;

    void RebuildIndex();

    std::vector<PackageLockEntry> entries_;
    std::map<PackageId, std::size_t> indexByPackage_;
    bool resolved_ = false;
};

enum class PackageLockIssueCode
{
    InvalidRuleset,
    ManifestRegistryNotFrozen,
    PackageUnavailable,
    VersionConflict,
    DependencyCycle
};

struct PackageLockIssue
{
    PackageLockIssueCode code = PackageLockIssueCode::PackageUnavailable;
    PackageId package;
    std::string message;
};

struct PackageLockReport
{
    std::vector<PackageLockIssue> issues;

    bool Success() const noexcept;
};

class PackageLockBuilder
{
public:
    bool Resolve(
        const PackageManifestRegistry& manifests,
        const RulesetDefinition& ruleset,
        PackageLock& output,
        PackageLockReport& report
    ) const;
};

}
