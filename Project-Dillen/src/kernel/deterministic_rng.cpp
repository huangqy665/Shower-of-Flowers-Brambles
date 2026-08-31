#include "deterministic_rng.hpp"

#include <limits>

namespace dillen::kernel {

std::uint64_t DeterministicRngValue(
    std::uint64_t seed,
    std::uint64_t drawIndex
) noexcept
{
    std::uint64_t value = seed
        + 0x9e3779b97f4a7c15ULL * (drawIndex + 1ULL);
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

RngStreamCreateResult DeterministicRngRegistry::Create(
    RngStreamId stream,
    std::uint64_t seed
)
{
    if (!stream)
    {
        return RngStreamCreateResult::InvalidStream;
    }
    if (Read().streams.find(stream) != Read().streams.end())
    {
        return RngStreamCreateResult::DuplicateStream;
    }
    Mutable().streams.emplace(
        stream,
        DeterministicRngStream{stream, seed, 0}
    );
    return RngStreamCreateResult::Created;
}

RngStreamAdvanceResult DeterministicRngRegistry::Advance(
    RngStreamId stream,
    std::uint64_t expectedDrawCount,
    std::uint64_t count
)
{
    if (!stream || count == 0)
    {
        return RngStreamAdvanceResult::InvalidAdvance;
    }
    // Validate against the shared payload; clone only once the advance is
    // certain, so a rejected advance never costs a store copy.
    const auto reader = Read().streams.find(stream);
    if (reader == Read().streams.end())
    {
        return RngStreamAdvanceResult::StreamMissing;
    }
    if (reader->second.drawCount != expectedDrawCount)
    {
        return RngStreamAdvanceResult::DrawCountMismatch;
    }
    if (count > std::numeric_limits<std::uint64_t>::max()
            - reader->second.drawCount)
    {
        return RngStreamAdvanceResult::DrawCountOverflow;
    }
    Mutable().streams.at(stream).drawCount += count;
    return RngStreamAdvanceResult::Advanced;
}

const DeterministicRngStream* DeterministicRngRegistry::Find(
    RngStreamId stream
) const
{
    const auto iterator = Read().streams.find(stream);
    return iterator == Read().streams.end() ? nullptr : &iterator->second;
}

std::uint64_t DeterministicRngRegistry::Preview(
    RngStreamId stream,
    std::uint64_t offset
) const
{
    const DeterministicRngStream* state = Find(stream);
    if (state == nullptr
        || offset > std::numeric_limits<std::uint64_t>::max()
            - state->drawCount)
    {
        return 0;
    }
    return DeterministicRngValue(
        state->seed,
        state->drawCount + offset
    );
}

void DeterministicRngRegistry::Clear()
{
    Mutable().streams.clear();
}

bool DeterministicRngRegistry::Empty() const noexcept
{
    return Read().streams.empty();
}

std::size_t DeterministicRngRegistry::Size() const noexcept
{
    return Read().streams.size();
}

const DeterministicRngRegistry::StreamMap&
DeterministicRngRegistry::All() const noexcept
{
    return Read().streams;
}

void DeterministicRngSnapshot::Publish(
    const DeterministicRngRegistry& registry,
    std::uint64_t tick,
    std::uint64_t revision
)
{
    registry_ = registry;
    tick_ = tick;
    revision_ = revision;
    published_ = true;
}

void DeterministicRngSnapshot::Clear()
{
    registry_.Clear();
    tick_ = 0;
    revision_ = 0;
    published_ = false;
}

bool DeterministicRngSnapshot::IsPublished() const noexcept
{
    return published_;
}

std::uint64_t DeterministicRngSnapshot::Tick() const noexcept
{
    return tick_;
}

std::uint64_t DeterministicRngSnapshot::Revision() const noexcept
{
    return revision_;
}

const DeterministicRngStream* DeterministicRngSnapshot::Find(
    RngStreamId stream
) const
{
    return registry_.Find(stream);
}

std::uint64_t DeterministicRngSnapshot::Preview(
    RngStreamId stream,
    std::uint64_t offset
) const
{
    return registry_.Preview(stream, offset);
}

}
