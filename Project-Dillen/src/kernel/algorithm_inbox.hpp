#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

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

    // Copy-on-write. Copying the store shares the payload and costs a
    // refcount bump; the first write through a shared handle clones it. The
    // World Transaction executor copies all six stores to stage a transaction,
    // so this turns "stage a transaction" from O(world) into O(what it
    // touches). Reads go through Read(), writes through Mutable() -- taking a
    // non-const reference out of Read() would mutate a payload someone else
    // may still be holding.
    //
    // use_count() is only meaningful because commit is single-threaded by
    // contract (memo section 3.9): worker threads may run algorithm dispatch,
    // never the store writes below.
    struct Data
    {
        std::vector<ScheduledAlgorithmEvent> pending;
        std::uint64_t nextSequence = 1;
    };

    const Data& Read() const noexcept { return *data_; }
    Data& Mutable()
    {
        if (data_.use_count() > 1)
        {
            data_ = std::make_shared<Data>(*data_);
        }
        return *data_;
    }

    std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

}
