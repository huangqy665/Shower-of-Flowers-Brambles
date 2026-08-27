#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "province_history.hpp"

namespace dillen::content {

enum class ProvinceHistoryAppendResult
{
    Added,
    Merged,
    InvalidSource,
    Frozen
};

class ProvinceHistoryRegistry
{
public:
    ProvinceHistoryAppendResult Append(
        ProvinceDefinitionId province,
        ProvinceHistorySource source
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t SourceCount() const noexcept;
    const ProvinceHistoryTimeline* Find(
        ProvinceDefinitionId province
    ) const;
    const ProvinceHistoryTimeline* Find(std::uint32_t province) const;
    const std::vector<ProvinceHistoryTimeline>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<ProvinceHistoryTimeline> timelines_;
    std::unordered_map<std::uint32_t, std::size_t> indexByProvince_;
    std::uint64_t nextPatchSequence_ = 0;
    std::size_t sourceCount_ = 0;
    bool frozen_ = false;
};

}
