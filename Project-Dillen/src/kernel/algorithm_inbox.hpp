#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_value.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::kernel {

struct ScheduledAlgorithmEvent
{
    std::uint64_t sequence = 0;
    AlgorithmEventTypeId type;
    MechanismInstanceId target;
    std::uint64_t dueTick = 0;
    std::int32_t priority = 0;
    MechanismValue payload;
};

enum class AlgorithmInboxScheduleResult
{
    Scheduled,
    InvalidEvent,
    SequenceExhausted
};

class AlgorithmInbox
{
public:
    AlgorithmInboxScheduleResult Schedule(
        AlgorithmEventTypeId type,
        MechanismInstanceId target,
        std::uint64_t dueTick,
        std::int32_t priority,
        MechanismValue payload,
        std::uint64_t& outputSequence
    );
    bool Cancel(
        std::uint64_t sequence,
        ScheduledAlgorithmEvent& removed
    );
    std::vector<ScheduledAlgorithmEvent> CancelTarget(
        MechanismInstanceId target
    );
    std::vector<ScheduledAlgorithmEvent> TakeReady(std::uint64_t tick);
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const std::vector<ScheduledAlgorithmEvent>& Pending() const noexcept;
    std::uint64_t NextSequence() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    void SortPending();

    std::vector<ScheduledAlgorithmEvent> pending_;
    std::uint64_t nextSequence_ = 1;
};

}
