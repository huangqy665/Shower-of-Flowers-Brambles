#include "source_lock.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace dillen::kernel {

bool operator==(
    const SourceLockEntry& first,
    const SourceLockEntry& second
) noexcept
{
    return first.package == second.package
        && first.packageVersion == second.packageVersion
        && first.sourceLayer == second.sourceLayer
        && first.virtualPath == second.virtualPath
        && first.fingerprint == second.fingerprint
        && first.size == second.size;
}

bool operator!=(
    const SourceLockEntry& first,
    const SourceLockEntry& second
) noexcept
{
    return !(first == second);
}

bool SourceLock::IsResolved() const noexcept
{
    return resolved_;
}

std::size_t SourceLock::Size() const noexcept
{
    return entries_.size();
}

const std::vector<SourceLockEntry>& SourceLock::Entries() const noexcept
{
    return entries_;
}

bool SourceLockBuilder::Build(
    std::vector<SourceLockEntry> entries,
    SourceLock& output,
    std::string& message
) const
{
    output = {};
    message.clear();
    std::set<std::pair<std::string, std::string>> identities;
    for (const SourceLockEntry& entry : entries)
    {
        if (!entry.package
            || entry.packageVersion == PackageVersion{}
            || entry.sourceLayer.empty()
            || entry.virtualPath.empty()
            || entry.fingerprint == 0
            || !identities.emplace(
                entry.sourceLayer,
                entry.virtualPath).second)
        {
            message = "Source Lock contains an invalid or duplicate file";
            return false;
        }
    }
    std::sort(
        entries.begin(),
        entries.end(),
        [](const SourceLockEntry& first, const SourceLockEntry& second)
        {
            if (first.package != second.package)
            {
                return first.package < second.package;
            }
            if (first.packageVersion != second.packageVersion)
            {
                return first.packageVersion < second.packageVersion;
            }
            if (first.sourceLayer != second.sourceLayer)
            {
                return first.sourceLayer < second.sourceLayer;
            }
            return first.virtualPath < second.virtualPath;
        }
    );
    output.entries_ = std::move(entries);
    output.resolved_ = true;
    return true;
}

}
