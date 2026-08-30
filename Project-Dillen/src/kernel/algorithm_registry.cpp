#include "algorithm_registry.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace dillen::kernel {

namespace {

bool ProgramFitsInstructionBudget(
    const AlgorithmProgramDefinition& program,
    const AlgorithmExecutionPolicy& policy
) noexcept
{
    for (const auto& stage : program.stages)
    {
        if (stage.second.size() > policy.instructionBudget)
        {
            return false;
        }
    }
    return true;
}

bool IsBackendProgramValid(const AlgorithmDescriptor& descriptor) noexcept
{
    if (descriptor.backend == AlgorithmBackend::Declarative)
    {
        return descriptor.script.stages.empty()
            && descriptor.script.state.empty()
            && IsValidAlgorithmProgram(
                descriptor.program,
                descriptor.entryPoints)
            && ProgramFitsInstructionBudget(
                descriptor.program,
                descriptor.executionPolicy);
    }
    if (descriptor.backend == AlgorithmBackend::Script)
    {
        return descriptor.program.stages.empty()
            && descriptor.executionPolicy.scriptSliceInstructionBudget
                <= descriptor.executionPolicy.instructionBudget
            && IsValidControlledScriptProgram(
                descriptor.script,
                descriptor.entryPoints)
            && ControlledScriptStateFootprint(
                [&descriptor]
                {
                    std::vector<MechanismValue> values;
                    values.reserve(descriptor.script.state.size());
                    for (const auto& state : descriptor.script.state)
                        values.push_back(state.initialValue);
                    return values;
                }()) <= descriptor.executionPolicy.scriptMemoryLimitBytes;
    }
    return descriptor.program.stages.empty()
        && descriptor.script.stages.empty()
        && descriptor.script.state.empty();
}

}

AlgorithmEntryPoint operator|(
    AlgorithmEntryPoint first,
    AlgorithmEntryPoint second
) noexcept
{
    return static_cast<AlgorithmEntryPoint>(
        static_cast<std::uint32_t>(first)
        | static_cast<std::uint32_t>(second)
    );
}

bool HasAlgorithmEntryPoint(
    AlgorithmEntryPoint value,
    AlgorithmEntryPoint flag
) noexcept
{
    return (static_cast<std::uint32_t>(value)
        & static_cast<std::uint32_t>(flag)) != 0;
}

AlgorithmRegisterResult AlgorithmRegistry::Register(
    AlgorithmDescriptor descriptor
)
{
    if (frozen_)
    {
        return AlgorithmRegisterResult::Frozen;
    }
    constexpr std::uint32_t validEntryPoints =
        static_cast<std::uint32_t>(AlgorithmEntryPoint::Create)
        | static_cast<std::uint32_t>(AlgorithmEntryPoint::Tick)
        | static_cast<std::uint32_t>(AlgorithmEntryPoint::Event)
        | static_cast<std::uint32_t>(AlgorithmEntryPoint::Command)
        | static_cast<std::uint32_t>(AlgorithmEntryPoint::Destroy);
    const std::uint32_t entryPoints =
        static_cast<std::uint32_t>(descriptor.entryPoints);
    if (!descriptor.id
        || descriptor.version == 0
        || descriptor.entryPoints == AlgorithmEntryPoint::None
        || !IsValidAlgorithmExecutionPolicy(descriptor.executionPolicy)
        || (entryPoints & ~validEntryPoints) != 0
        || !IsValidMechanismSymbol(descriptor.canonicalName)
        || descriptor.canonicalName
            != NormalizeMechanismSymbol(descriptor.canonicalName)
        || descriptor.id != StableAlgorithmId(descriptor.canonicalName))
    {
        return AlgorithmRegisterResult::InvalidDescriptor;
    }
    if (!IsBackendProgramValid(descriptor))
    {
        return AlgorithmRegisterResult::InvalidDescriptor;
    }
    const auto key = std::make_pair(
        descriptor.id.value,
        descriptor.version
    );
    if (indexByKey_.find(key) != indexByKey_.end())
    {
        return AlgorithmRegisterResult::DuplicateVersion;
    }
    for (const AlgorithmDescriptor& existing : descriptors_)
    {
        if (existing.id == descriptor.id
            && existing.canonicalName != descriptor.canonicalName)
        {
            return AlgorithmRegisterResult::IdCollision;
        }
    }
    std::unordered_set<std::uint64_t> capabilities;
    for (const CapabilityRequirement& capability
        : descriptor.requiredCapabilities)
    {
        if (!IsValidCapabilityRequirement(capability)
            || !capabilities.emplace(capability.capability.value).second)
        {
            return AlgorithmRegisterResult::InvalidDescriptor;
        }
    }

    const std::size_t index = descriptors_.size();
    descriptors_.push_back(std::move(descriptor));
    indexByKey_[key] = index;
    const auto latest = latestById_.find(key.first);
    if (latest == latestById_.end()
        || descriptors_[latest->second].version < key.second)
    {
        latestById_[key.first] = index;
    }
    return AlgorithmRegisterResult::Added;
}

void AlgorithmRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    descriptors_.clear();
    indexByKey_.clear();
    latestById_.clear();
}

void AlgorithmRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        descriptors_.begin(),
        descriptors_.end(),
        [](const AlgorithmDescriptor& first,
           const AlgorithmDescriptor& second)
        {
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );
    RebuildIndexes();
    frozen_ = true;
}

bool AlgorithmRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t AlgorithmRegistry::Size() const noexcept
{
    return descriptors_.size();
}

const AlgorithmDescriptor* AlgorithmRegistry::Find(
    AlgorithmId id,
    std::uint32_t version
) const
{
    const auto iterator = indexByKey_.find({id.value, version});
    return iterator == indexByKey_.end()
        ? nullptr
        : &descriptors_[iterator->second];
}

const AlgorithmDescriptor* AlgorithmRegistry::Find(
    std::string_view canonicalName,
    std::uint32_t version
) const
{
    const AlgorithmId id = StableAlgorithmId(canonicalName);
    const AlgorithmDescriptor* descriptor = Find(id, version);
    return descriptor != nullptr
        && descriptor->canonicalName == canonicalName
        ? descriptor
        : nullptr;
}

const AlgorithmDescriptor* AlgorithmRegistry::Latest(AlgorithmId id) const
{
    const auto iterator = latestById_.find(id.value);
    return iterator == latestById_.end()
        ? nullptr
        : &descriptors_[iterator->second];
}

const std::vector<AlgorithmDescriptor>&
AlgorithmRegistry::All() const noexcept
{
    return descriptors_;
}

void AlgorithmRegistry::RebuildIndexes()
{
    indexByKey_.clear();
    latestById_.clear();
    for (std::size_t index = 0; index < descriptors_.size(); ++index)
    {
        const AlgorithmDescriptor& descriptor = descriptors_[index];
        indexByKey_[{descriptor.id.value, descriptor.version}] = index;
        latestById_[descriptor.id.value] = index;
    }
}

}
