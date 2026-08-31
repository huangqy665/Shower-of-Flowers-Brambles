#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "runtime_capability_contract.hpp"

namespace dillen::kernel {

enum class PackageRole
{
    Unspecified,
    Contract,
    Mechanism,
    Content,
    Presentation
};

std::string_view ToString(PackageRole role) noexcept;

struct PackageVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

bool operator==(PackageVersion first, PackageVersion second) noexcept;
bool operator!=(PackageVersion first, PackageVersion second) noexcept;
bool operator<(PackageVersion first, PackageVersion second) noexcept;
bool operator<=(PackageVersion first, PackageVersion second) noexcept;
bool operator>(PackageVersion first, PackageVersion second) noexcept;
bool operator>=(PackageVersion first, PackageVersion second) noexcept;
std::string ToString(PackageVersion version);

struct PackageVersionRange
{
    std::optional<PackageVersion> minimumInclusive;
    std::optional<PackageVersion> maximumExclusive;

    bool IsValid() const noexcept;
    bool Contains(PackageVersion version) const noexcept;
};

struct PackageDependency
{
    PackageId package;
    std::string canonicalName;
    PackageVersionRange versions;
    bool required = true;
};

struct PackageManifest
{
    PackageId id;
    std::string canonicalName;
    PackageVersion version;
    PackageRole role = PackageRole::Unspecified;
    std::string contentDigest;
    std::int32_t loadPriority = 0;
    std::vector<PackageDependency> dependencies;
    std::vector<CapabilityProvision> providedCapabilities;
};

enum class PackageManifestRegisterResult
{
    Added,
    InvalidManifest,
    DuplicateVersion,
    IdCollision,
    Frozen
};

class PackageManifestRegistry
{
public:
    PackageManifestRegisterResult Register(PackageManifest manifest);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const PackageManifest* Find(
        PackageId package,
        PackageVersion version
    ) const;
    const std::vector<PackageManifest>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<PackageManifest> manifests_;
    std::map<
        std::pair<std::uint64_t, PackageVersion>,
        std::size_t
    > indexByVersion_;
    bool frozen_ = false;
};

bool IsValidPackageContentDigest(std::string_view digest) noexcept;

}
