#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "package_manifest.hpp"

namespace dillen::kernel {

struct SourceLockEntry
{
    PackageId package;
    PackageVersion packageVersion;
    std::string sourceLayer;
    std::string virtualPath;
    std::uint64_t fingerprint = 0;
    std::uint64_t size = 0;
};

bool operator==(
    const SourceLockEntry& first,
    const SourceLockEntry& second
) noexcept;
bool operator!=(
    const SourceLockEntry& first,
    const SourceLockEntry& second
) noexcept;

class SourceLock
{
public:
    bool IsResolved() const noexcept;
    std::size_t Size() const noexcept;
    const std::vector<SourceLockEntry>& Entries() const noexcept;

private:
    friend class SourceLockBuilder;

    std::vector<SourceLockEntry> entries_;
    bool resolved_ = false;
};

class SourceLockBuilder
{
public:
    bool Build(
        std::vector<SourceLockEntry> entries,
        SourceLock& output,
        std::string& message
    ) const;
};

}
