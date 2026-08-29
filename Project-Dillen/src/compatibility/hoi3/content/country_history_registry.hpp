#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "country_history.hpp"

namespace dillen::compatibility::hoi3::content {

enum class CountryHistoryAppendResult
{
    Added,
    Merged,
    InvalidSource,
    Frozen
};

class CountryHistoryRegistry
{
public:
    CountryHistoryAppendResult Append(
        CountryDefinitionId country,
        CountryHistorySource source
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t SourceCount() const noexcept;
    const CountryHistoryTimeline* Find(
        CountryDefinitionId country
    ) const;
    const CountryHistoryTimeline* Find(const CountryTag& tag) const;
    const CountryHistoryTimeline* Find(std::string_view tag) const;
    const std::vector<CountryHistoryTimeline>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<CountryHistoryTimeline> timelines_;
    std::unordered_map<std::uint32_t, std::size_t> indexByCountry_;
    std::uint64_t nextPatchSequence_ = 0;
    std::size_t sourceCount_ = 0;
    bool frozen_ = false;
};

}
