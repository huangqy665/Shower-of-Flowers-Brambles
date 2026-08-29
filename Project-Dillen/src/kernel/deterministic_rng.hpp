#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

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

    StreamMap streams_;
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
