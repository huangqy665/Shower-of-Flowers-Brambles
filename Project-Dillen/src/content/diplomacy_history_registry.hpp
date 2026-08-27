#pragma once

#include <cstddef>
#include <vector>

#include "diplomacy_history.hpp"

namespace dillen::content {

enum class DiplomacyHistoryAppendResult
{
    AddedTimeline,
    AppendedPeriod,
    InvalidPeriod,
    Frozen
};

class DiplomacyHistoryRegistry
{
public:
    DiplomacyHistoryAppendResult Append(
        DiplomacyHistoryKey key,
        DiplomaticRelationPeriod period
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t PeriodCount() const noexcept;
    const DiplomacyHistoryTimeline* Find(
        DiplomacyHistoryKey key
    ) const;
    const std::vector<DiplomacyHistoryTimeline>& All() const noexcept;

private:
    std::vector<DiplomacyHistoryTimeline> timelines_;
    std::uint64_t nextSequence_ = 0;
    std::size_t periodCount_ = 0;
    bool frozen_ = false;
};

}
