#pragma once

#include <cstdint>
#include <string>

#include "package_lock.hpp"
#include "source_lock.hpp"
#include "ruleset.hpp"

namespace dillen::kernel {

struct RulesetFingerprint
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    explicit operator bool() const noexcept;
    std::string ToHex() const;
};

bool operator==(
    RulesetFingerprint first,
    RulesetFingerprint second
) noexcept;
bool operator!=(
    RulesetFingerprint first,
    RulesetFingerprint second
) noexcept;

RulesetFingerprint ComputeRulesetFingerprint(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock
);
RulesetFingerprint ComputeRulesetFingerprint(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const SourceLock& sourceLock
);

}
