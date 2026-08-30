#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "algorithm_inbox.hpp"
#include "authoritative_world.hpp"
#include "package_lock.hpp"
#include "source_lock.hpp"
#include "world_command_queue.hpp"

namespace dillen::persistence {

inline constexpr std::uint32_t kCurrentRuntimeSaveFormatVersion = 4;

using RuntimeSourceLockEntry = kernel::SourceLockEntry;

struct RuntimeSaveIdentity
{
    std::uint32_t formatVersion = kCurrentRuntimeSaveFormatVersion;
    kernel::RulesetId ruleset;
    std::uint32_t rulesetVersion = 0;
    std::vector<kernel::AppliedRulesetExtension> rulesetExtensions;
    kernel::RulesetFingerprint rulesetFingerprint;
    std::vector<kernel::PackageLockEntry> packageLock;
    std::vector<RuntimeSourceLockEntry> sourceLock;
};

struct RuntimeSaveImage
{
    RuntimeSaveIdentity identity;
    std::uint64_t worldTick = 0;
    std::uint64_t worldRevision = 0;
    std::vector<world::EntityRecord> entities;
    std::vector<world::ComponentRecord> components;
    std::vector<world::RelationRecord> relations;
    std::vector<kernel::MechanismInstance> mechanisms;
    std::map<kernel::MechanismDefinitionId, std::uint64_t>
        nextMechanismOrdinalByDefinition;
    std::vector<kernel::ScheduledAlgorithmEvent> scheduledInbox;
    std::uint64_t nextScheduledEventSequence = 1;
    std::vector<kernel::DeterministicRngStream> rngStreams;
    std::vector<kernel::QueuedWorldTransaction> commandQueue;
    std::uint64_t nextCommandSequence = 1;
    std::uint64_t nextFactSequence = 1;
};

}
