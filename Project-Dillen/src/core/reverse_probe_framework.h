#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_lifecycle.h"
#include "engine_registry.h"
#include "native_save_load_barrier.h"

namespace core
{

class CapabilityRegistry;
class NativeEffectService;
class NativeObjectResolverService;
class NativeQueryService;

enum class ReverseProbeAccess
{
    MetadataOnly,
    ReadMemory,
    ReversiblePatch,
    WriteMemory
};

enum class ReverseProbeStatus
{
    Passed,
    Failed,
    Skipped,
    Rejected
};

enum class ReverseProbeEvidence
{
    None,
    Candidate,
    Confirmed,
    Proven,
    VerifiedWrite
};

struct ReverseProbeSafetySnapshot
{
    bool nativeWritesAllowed = false;
    uint64_t barrierGeneration = 0;
    std::string barrierReason;
};

struct ReverseProbeContext
{
    engine::EngineRegistry* engine = nullptr;
    NativeSaveLoadBarrier* saveLoadBarrier = nullptr;
    NativeEffectService* effects = nullptr;
    NativeQueryService* queries = nullptr;
    NativeObjectResolverService* objectResolvers = nullptr;
    CapabilityRegistry* capabilities = nullptr;
    LifecycleSnapshot lifecycle;
    ReverseProbeSafetySnapshot safety;
    std::shared_ptr<void> safetyLease;
    uint64_t runId = 0;
    uint64_t timestampMilliseconds = 0;
    uint64_t callerStateId = 0;
    uint64_t callerThreadId = 0;
};

struct ReverseProbeResult
{
    std::string id;
    std::string category;
    ReverseProbeAccess access = ReverseProbeAccess::MetadataOnly;
    ReverseProbeStatus status = ReverseProbeStatus::Skipped;
    ReverseProbeEvidence evidence = ReverseProbeEvidence::None;
    uint64_t runId = 0;
    uint64_t durationMicroseconds = 0;
    std::string version;
    std::string message;

    bool Succeeded() const
    {
        return status == ReverseProbeStatus::Passed;
    }
};

using ReverseProbeCallback = std::function<ReverseProbeResult(
    const ReverseProbeContext&
)>;

struct ReverseProbeDefinition
{
    std::string id;
    std::string category;
    ReverseProbeAccess access = ReverseProbeAccess::MetadataOnly;
    std::optional<engine::VersionId> requiredVersion;
    std::vector<engine::SymbolId> requiredSymbols;
    std::vector<engine::TypeId> requiredTypes;
    std::vector<engine::FieldId> requiredFields;
    std::vector<std::string> requiredCapabilities;
    bool requiresGameplay = false;
    bool requiresStableBarrier = false;
    ReverseProbeCallback execute;
};

struct ReverseProbePolicy
{
    ReverseProbeAccess maximumAccess = ReverseProbeAccess::ReadMemory;
    bool requireActiveRegistry = true;
    bool requireGameplayForMutation = true;
    bool requireOpenSaveLoadBarrierForMutation = true;
    bool continueAfterFailure = true;
};

struct ReverseProbeReport
{
    uint64_t runId = 0;
    uint64_t timestampMilliseconds = 0;
    uint64_t lifecycleGeneration = 0;
    uint64_t barrierGeneration = 0;
    std::string playerTag;
    std::vector<ReverseProbeResult> results;

    bool Passed() const;
};

class ReverseProbeFramework
{
public:
    bool Register(
        ReverseProbeDefinition definition,
        std::string& error
    );
    bool Unregister(std::string_view id);
    bool Contains(std::string_view id) const;
    std::vector<std::string> ProbeIds() const;

    ReverseProbeResult Run(
        std::string_view id,
        ReverseProbeContext context,
        const ReverseProbePolicy& policy = {}
    );
    ReverseProbeReport RunAll(
        ReverseProbeContext context,
        const ReverseProbePolicy& policy = {}
    );
    ReverseProbeReport RunSelected(
        const std::vector<std::string>& ids,
        ReverseProbeContext context,
        const ReverseProbePolicy& policy = {}
    );

    bool AppendReport(
        const std::filesystem::path& path,
        const ReverseProbeReport& report,
        std::string& error
    ) const;

private:
    ReverseProbeResult RunDefinition(
        const ReverseProbeDefinition& definition,
        const ReverseProbeContext& context,
        const ReverseProbePolicy& policy
    ) const;

    mutable std::mutex mutex_;
    std::vector<ReverseProbeDefinition> definitions_;
    uint64_t nextRunId_ = 1;
};

bool RegisterCoreReverseProbes(
    ReverseProbeFramework& framework,
    std::string& error
);

const char* ReverseProbeAccessName(ReverseProbeAccess value);
const char* ReverseProbeStatusName(ReverseProbeStatus value);
const char* ReverseProbeEvidenceName(ReverseProbeEvidence value);

}
