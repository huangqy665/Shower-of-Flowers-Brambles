#include "package_manifest.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace dillen::kernel {

namespace {

std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> VersionTuple(
    PackageVersion version
) noexcept
{
    return {version.major, version.minor, version.patch};
}

bool ValidDependency(const PackageDependency& dependency)
{
    return dependency.package
        && dependency.versions.IsValid()
        && IsValidMechanismSymbol(dependency.canonicalName)
        && dependency.canonicalName
            == NormalizeMechanismSymbol(dependency.canonicalName)
        && dependency.package
            == StablePackageId(dependency.canonicalName);
}

}

bool operator==(PackageVersion first, PackageVersion second) noexcept
{
    return VersionTuple(first) == VersionTuple(second);
}

bool operator!=(PackageVersion first, PackageVersion second) noexcept
{
    return !(first == second);
}

bool operator<(PackageVersion first, PackageVersion second) noexcept
{
    return VersionTuple(first) < VersionTuple(second);
}

bool operator<=(PackageVersion first, PackageVersion second) noexcept
{
    return !(second < first);
}

bool operator>(PackageVersion first, PackageVersion second) noexcept
{
    return second < first;
}

bool operator>=(PackageVersion first, PackageVersion second) noexcept
{
    return !(first < second);
}

std::string ToString(PackageVersion version)
{
    return std::to_string(version.major)
        + "." + std::to_string(version.minor)
        + "." + std::to_string(version.patch);
}

bool PackageVersionRange::IsValid() const noexcept
{
    return !minimumInclusive
        || !maximumExclusive
        || *minimumInclusive < *maximumExclusive;
}

bool PackageVersionRange::Contains(PackageVersion version) const noexcept
{
    return IsValid()
        && (!minimumInclusive || version >= *minimumInclusive)
        && (!maximumExclusive || version < *maximumExclusive);
}

bool IsValidPackageContentDigest(std::string_view digest) noexcept
{
    if (digest.size() != 64)
    {
        return false;
    }
    return std::all_of(
        digest.begin(),
        digest.end(),
        [](char character)
        {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        }
    );
}

PackageManifestRegisterResult PackageManifestRegistry::Register(
    PackageManifest manifest
)
{
    if (frozen_)
    {
        return PackageManifestRegisterResult::Frozen;
    }
    if (!manifest.id
        || !IsValidMechanismSymbol(manifest.canonicalName)
        || manifest.canonicalName
            != NormalizeMechanismSymbol(manifest.canonicalName)
        || manifest.id != StablePackageId(manifest.canonicalName)
        || !IsValidPackageContentDigest(manifest.contentDigest))
    {
        return PackageManifestRegisterResult::InvalidManifest;
    }

    std::set<PackageId> dependencyIds;
    for (const PackageDependency& dependency : manifest.dependencies)
    {
        if (!ValidDependency(dependency)
            || dependency.package == manifest.id
            || !dependencyIds.emplace(dependency.package).second)
        {
            return PackageManifestRegisterResult::InvalidManifest;
        }
    }
    std::unordered_set<std::uint64_t> capabilities;
    for (const CapabilityProvision& capability
        : manifest.providedCapabilities)
    {
        if (!IsValidCapabilityProvision(capability)
            || !capabilities.emplace(capability.capability.value).second)
        {
            return PackageManifestRegisterResult::InvalidManifest;
        }
    }

    const auto key = std::make_pair(manifest.id.value, manifest.version);
    if (indexByVersion_.find(key) != indexByVersion_.end())
    {
        return PackageManifestRegisterResult::DuplicateVersion;
    }
    for (const PackageManifest& existing : manifests_)
    {
        if (existing.id == manifest.id
            && existing.canonicalName != manifest.canonicalName)
        {
            return PackageManifestRegisterResult::IdCollision;
        }
    }

    indexByVersion_[key] = manifests_.size();
    manifests_.push_back(std::move(manifest));
    return PackageManifestRegisterResult::Added;
}

void PackageManifestRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    manifests_.clear();
    indexByVersion_.clear();
}

void PackageManifestRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        manifests_.begin(),
        manifests_.end(),
        [](const PackageManifest& first, const PackageManifest& second)
        {
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool PackageManifestRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t PackageManifestRegistry::Size() const noexcept
{
    return manifests_.size();
}

const PackageManifest* PackageManifestRegistry::Find(
    PackageId package,
    PackageVersion version
) const
{
    const auto iterator = indexByVersion_.find({package.value, version});
    return iterator == indexByVersion_.end()
        ? nullptr
        : &manifests_[iterator->second];
}

const std::vector<PackageManifest>&
PackageManifestRegistry::All() const noexcept
{
    return manifests_;
}

void PackageManifestRegistry::RebuildIndex()
{
    indexByVersion_.clear();
    for (std::size_t index = 0; index < manifests_.size(); ++index)
    {
        indexByVersion_[{
            manifests_[index].id.value,
            manifests_[index].version
        }] = index;
    }
}

}
