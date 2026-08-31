#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>

#include "mechanism_ids.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::kernel {

struct DeterministicRngStream
{
    RngStreamId id;
    std::uint64_t seed = 0;
    std::uint64_t drawCount = 0;
};

enum class RngStreamCreateResult
{
    Created,
    InvalidStream,
    DuplicateStream
};

enum class RngStreamAdvanceResult
{
    Advanced,
    InvalidAdvance,
    StreamMissing,
    DrawCountMismatch,
    DrawCountOverflow
};

class DeterministicRngRegistry
{
public:
    using StreamMap = std::map<RngStreamId, DeterministicRngStream>;

    RngStreamCreateResult Create(RngStreamId stream, std::uint64_t seed);
    RngStreamAdvanceResult Advance(
        RngStreamId stream,
        std::uint64_t expectedDrawCount,
        std::uint64_t count
    );
    const DeterministicRngStream* Find(RngStreamId stream) const;
    std::uint64_t Preview(RngStreamId stream, std::uint64_t offset) const;
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const StreamMap& All() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    // Copy-on-write. Copying the registry shares the payload and costs a
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
        StreamMap streams;
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

class DeterministicRngSnapshot
{
public:
    void Publish(
        const DeterministicRngRegistry& registry,
        std::uint64_t tick,
        std::uint64_t revision
    );
    void Clear();
    bool IsPublished() const noexcept;
    std::uint64_t Tick() const noexcept;
    std::uint64_t Revision() const noexcept;
    const DeterministicRngStream* Find(RngStreamId stream) const;
    std::uint64_t Preview(RngStreamId stream, std::uint64_t offset) const;

private:
    DeterministicRngRegistry registry_;
    std::uint64_t tick_ = 0;
    std::uint64_t revision_ = 0;
    bool published_ = false;
};

std::uint64_t DeterministicRngValue(
    std::uint64_t seed,
    std::uint64_t drawIndex
) noexcept;

}
