#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mechanism_ids.hpp"

namespace dillen::kernel {

enum class AlgorithmBackend
{
    Declarative,
    Script,
    Native
};

enum class AlgorithmEntryPoint : std::uint32_t
{
    None = 0,
    Create = 1U << 0U,
    Tick = 1U << 1U,
    Event = 1U << 2U,
    Command = 1U << 3U,
    Destroy = 1U << 4U
};

AlgorithmEntryPoint operator|(
    AlgorithmEntryPoint first,
    AlgorithmEntryPoint second
) noexcept;
bool HasAlgorithmEntryPoint(
    AlgorithmEntryPoint value,
    AlgorithmEntryPoint flag
) noexcept;

struct AlgorithmDescriptor
{
    AlgorithmId id;
    std::string canonicalName;
    std::uint32_t version = 0;
    AlgorithmBackend backend = AlgorithmBackend::Declarative;
    AlgorithmEntryPoint entryPoints = AlgorithmEntryPoint::None;
    bool deterministic = true;
    std::vector<std::string> requiredCapabilities;
};

enum class AlgorithmRegisterResult
{
    Added,
    InvalidDescriptor,
    DuplicateVersion,
    IdCollision,
    Frozen
};

class AlgorithmRegistry
{
public:
    AlgorithmRegisterResult Register(AlgorithmDescriptor descriptor);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const AlgorithmDescriptor* Find(
        AlgorithmId id,
        std::uint32_t version
    ) const;
    const AlgorithmDescriptor* Find(
        std::string_view canonicalName,
        std::uint32_t version
    ) const;
    const AlgorithmDescriptor* Latest(AlgorithmId id) const;
    const std::vector<AlgorithmDescriptor>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<AlgorithmDescriptor> descriptors_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        indexByKey_;
    std::map<std::uint64_t, std::size_t> latestById_;
    bool frozen_ = false;
};

}
