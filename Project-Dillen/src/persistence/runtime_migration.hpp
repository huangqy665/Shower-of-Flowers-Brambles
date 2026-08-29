#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "runtime_save_image.hpp"

namespace dillen::persistence {

using RuntimeMigrationFunction = std::function<bool(
    RuntimeSaveImage&,
    std::string&
)>;

struct RuntimeMigrationStep
{
    std::string canonicalName;
    RuntimeSaveIdentity source;
    RuntimeSaveIdentity target;
    RuntimeMigrationFunction migrate;
};

enum class RuntimeMigrationRegisterResult
{
    Added,
    InvalidStep,
    DuplicateSource,
    Frozen
};

enum class RuntimeMigrationStatus
{
    NotRequired,
    Migrated,
    RegistryNotFrozen,
    PathMissing,
    StepRejected,
    CycleDetected
};

struct RuntimeMigrationReport
{
    RuntimeMigrationStatus status = RuntimeMigrationStatus::NotRequired;
    std::vector<std::string> appliedSteps;
    std::string message;

    explicit operator bool() const noexcept;
};

class RuntimeMigrationRegistry
{
public:
    RuntimeMigrationRegisterResult Register(RuntimeMigrationStep step);
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    RuntimeMigrationReport Migrate(
        RuntimeSaveImage& image,
        const RuntimeSaveIdentity& target
    ) const;

private:
    using SourceKey = std::pair<std::uint32_t, std::pair<std::uint64_t,
        std::uint64_t>>;

    static SourceKey Key(const RuntimeSaveIdentity& source) noexcept;

    std::vector<RuntimeMigrationStep> steps_;
    std::map<SourceKey, std::size_t> indexBySource_;
    bool frozen_ = false;
};

bool SameRuntimeSaveIdentity(
    const RuntimeSaveIdentity& first,
    const RuntimeSaveIdentity& second
) noexcept;

}
