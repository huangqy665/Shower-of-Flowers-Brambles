#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "world_transaction.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::kernel {

struct QueuedWorldTransaction
{
    std::uint64_t sequence = 0;
    std::uint64_t notBeforeTick = 0;
    std::int32_t priority = 0;
    WorldTransaction transaction;
};

class WorldCommandQueue
{
public:
    std::uint64_t Enqueue(
        WorldTransaction transaction,
        std::uint64_t notBeforeTick,
        std::int32_t priority = 0
    );
    std::uint64_t ReserveSequence();
    std::vector<QueuedWorldTransaction> TakeReady(std::uint64_t tick);
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const std::vector<QueuedWorldTransaction>& Pending() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    std::vector<QueuedWorldTransaction> pending_;
    std::uint64_t nextSequence_ = 1;
};

}
