#include "reverse_probe_framework.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

#include "capability_registry.h"

namespace core
{
namespace
{

bool AccessExceeds(
    ReverseProbeAccess requested,
    ReverseProbeAccess maximum
)
{
    return static_cast<int>(requested) > static_cast<int>(maximum);
}

bool IsMutation(ReverseProbeAccess access)
{
    return access == ReverseProbeAccess::ReversiblePatch
        || access == ReverseProbeAccess::WriteMemory;
}

std::string EscapeJson(std::string_view value)
{
    std::string output;
    output.reserve(value.size() + 8);
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character >= 0x20)
            {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return output;
}

ReverseProbeResult MakeResult(
    ReverseProbeStatus status,
    ReverseProbeEvidence evidence,
    std::string message
)
{
    ReverseProbeResult result;
    result.status = status;
    result.evidence = evidence;
    result.message = std::move(message);
    return result;
}

void RefreshSafetySnapshot(ReverseProbeContext& context)
{
    if (!context.saveLoadBarrier)
    {
        return;
    }
    const NativeSaveLoadBarrierSnapshot barrier =
        context.saveLoadBarrier->Snapshot();
    context.safety.nativeWritesAllowed = barrier.nativeWritesAllowed;
    context.safety.barrierGeneration = barrier.generation;
    context.safety.barrierReason =
        NativeSaveLoadBarrierReasonName(barrier.reason);
}

void PopulateReportMetadata(
    ReverseProbeReport& report,
    const ReverseProbeContext& context
)
{
    report.runId = context.runId;
    report.timestampMilliseconds = context.timestampMilliseconds;
    report.lifecycleGeneration = context.lifecycle.generation;
    report.barrierGeneration = context.safety.barrierGeneration;
    report.playerTag = context.lifecycle.playerTag;
}

ReverseProbeResult AuditRegistryStructure(
    const ReverseProbeContext& context
)
{
    if (!context.engine)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_registry_missing"
        );
    }
    const engine::VersionProfile* profile =
        context.engine->ActiveProfile();
    if (!profile)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_registry_inactive"
        );
    }
    if (profile->version.id == engine::VersionId::Unknown
        || profile->version.name.empty()
        || profile->symbols.size()
            != static_cast<std::size_t>(engine::SymbolId::Count)
        || profile->fields.size()
            != static_cast<std::size_t>(engine::FieldId::Count))
    {
        return MakeResult(
            ReverseProbeStatus::Failed,
            ReverseProbeEvidence::Candidate,
            "engine_profile_cardinality_invalid"
        );
    }

    std::set<std::string_view> symbolNames;
    for (std::size_t index = 0; index < profile->symbols.size(); ++index)
    {
        const engine::SymbolDescriptor& symbol = profile->symbols[index];
        if (symbol.id != static_cast<engine::SymbolId>(index)
            || symbol.name.empty()
            || symbol.rva >= profile->version.executable.imageSize
            || !symbolNames.insert(symbol.name).second)
        {
            return MakeResult(
                ReverseProbeStatus::Failed,
                ReverseProbeEvidence::Candidate,
                "engine_symbol_descriptor_invalid"
            );
        }
    }

    std::set<std::string_view> typeNames;
    for (const engine::TypeDescriptor& type : profile->types)
    {
        if (type.name.empty() || !typeNames.insert(type.name).second)
        {
            return MakeResult(
                ReverseProbeStatus::Failed,
                ReverseProbeEvidence::Candidate,
                "engine_type_descriptor_invalid"
            );
        }
    }

    std::set<std::string_view> fieldNames;
    for (std::size_t index = 0; index < profile->fields.size(); ++index)
    {
        const engine::FieldDescriptor& field = profile->fields[index];
        if (field.id != static_cast<engine::FieldId>(index)
            || field.name.empty()
            || !fieldNames.insert(field.name).second
            || !context.engine->FindType(field.owner))
        {
            return MakeResult(
                ReverseProbeStatus::Failed,
                ReverseProbeEvidence::Candidate,
                "engine_field_descriptor_invalid"
            );
        }
    }
    return MakeResult(
        ReverseProbeStatus::Passed,
        ReverseProbeEvidence::Confirmed,
        "engine_registry_structure_valid"
    );
}

ReverseProbeResult ValidateKnownSymbols(
    const ReverseProbeContext& context
)
{
    if (!context.engine)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_registry_missing"
        );
    }
    const engine::VersionProfile* profile =
        context.engine->ActiveProfile();
    if (!profile)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_registry_inactive"
        );
    }

    std::size_t checked = 0;
    std::size_t failed = 0;
    std::string firstError;
    for (const engine::SymbolDescriptor& symbol : profile->symbols)
    {
        if (symbol.confidence == engine::Confidence::Candidate)
        {
            continue;
        }
        if (symbol.signature.empty() && !symbol.expectedCallTarget)
        {
            continue;
        }
        ++checked;
        std::string error;
        if (!context.engine->ValidateSymbol(symbol.id, error))
        {
            ++failed;
            if (firstError.empty())
            {
                firstError = std::string(symbol.name) + ":" + error;
            }
        }
    }
    if (failed > 0)
    {
        return MakeResult(
            ReverseProbeStatus::Failed,
            ReverseProbeEvidence::Confirmed,
            "engine_symbol_validation_failed="
                + std::to_string(failed)
                + ", first=" + firstError
        );
    }
    return MakeResult(
        ReverseProbeStatus::Passed,
        ReverseProbeEvidence::Proven,
        "engine_symbols_validated=" + std::to_string(checked)
    );
}

ReverseProbeResult ValidateCandidateSymbols(
    const ReverseProbeContext& context
)
{
    if (!context.engine)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_registry_missing"
        );
    }
    const engine::VersionProfile* profile =
        context.engine->ActiveProfile();
    if (!profile)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_registry_inactive"
        );
    }

    std::size_t checked = 0;
    std::size_t failed = 0;
    std::string firstError;
    for (const engine::SymbolDescriptor& symbol : profile->symbols)
    {
        if (symbol.confidence != engine::Confidence::Candidate
            || (symbol.signature.empty()
                && !symbol.expectedCallTarget))
        {
            continue;
        }
        ++checked;
        std::string error;
        if (!context.engine->ValidateSymbol(symbol.id, error))
        {
            ++failed;
            if (firstError.empty())
            {
                firstError = std::string(symbol.name) + ":" + error;
            }
        }
    }
    if (checked == 0)
    {
        return MakeResult(
            ReverseProbeStatus::Skipped,
            ReverseProbeEvidence::None,
            "engine_candidate_symbols_missing"
        );
    }
    if (failed > 0)
    {
        return MakeResult(
            ReverseProbeStatus::Failed,
            ReverseProbeEvidence::Candidate,
            "engine_candidate_validation_failed="
                + std::to_string(failed)
                + ", first=" + firstError
        );
    }
    return MakeResult(
        ReverseProbeStatus::Passed,
        ReverseProbeEvidence::Confirmed,
        "engine_candidate_symbols_validated="
            + std::to_string(checked)
    );
}

}

bool ReverseProbeReport::Passed() const
{
    return std::all_of(
        results.begin(),
        results.end(),
        [](const ReverseProbeResult& result)
        {
            return result.status == ReverseProbeStatus::Passed
                || result.status == ReverseProbeStatus::Skipped;
        }
    );
}

bool ReverseProbeFramework::Register(
    ReverseProbeDefinition definition,
    std::string& error
)
{
    if (definition.id.empty() || !definition.execute)
    {
        error = "reverse_probe_definition_invalid";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(
        definitions_.begin(),
        definitions_.end(),
        [&definition](const ReverseProbeDefinition& value)
        {
            return value.id == definition.id;
        }
    );
    if (found != definitions_.end())
    {
        error = "reverse_probe_duplicate: " + definition.id;
        return false;
    }
    definitions_.push_back(std::move(definition));
    error.clear();
    return true;
}

bool ReverseProbeFramework::Unregister(std::string_view id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(
        definitions_.begin(),
        definitions_.end(),
        [id](const ReverseProbeDefinition& value)
        {
            return value.id == id;
        }
    );
    if (found == definitions_.end())
    {
        return false;
    }
    definitions_.erase(found);
    return true;
}

bool ReverseProbeFramework::Contains(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(
        definitions_.begin(),
        definitions_.end(),
        [id](const ReverseProbeDefinition& value)
        {
            return value.id == id;
        }
    );
}

std::vector<std::string> ReverseProbeFramework::ProbeIds() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> output;
    output.reserve(definitions_.size());
    for (const ReverseProbeDefinition& definition : definitions_)
    {
        output.push_back(definition.id);
    }
    return output;
}

ReverseProbeResult ReverseProbeFramework::Run(
    std::string_view id,
    ReverseProbeContext context,
    const ReverseProbePolicy& policy
)
{
    RefreshSafetySnapshot(context);
    ReverseProbeDefinition definition;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(
            definitions_.begin(),
            definitions_.end(),
            [id](const ReverseProbeDefinition& value)
            {
                return value.id == id;
            }
        );
        if (found == definitions_.end())
        {
            ReverseProbeResult result;
            result.id.assign(id);
            result.status = ReverseProbeStatus::Skipped;
            result.message = "reverse_probe_missing";
            return result;
        }
        definition = *found;
        context.runId = nextRunId_++;
    }
    return RunDefinition(definition, context, policy);
}

ReverseProbeReport ReverseProbeFramework::RunAll(
    ReverseProbeContext context,
    const ReverseProbePolicy& policy
)
{
    RefreshSafetySnapshot(context);
    std::vector<ReverseProbeDefinition> definitions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        definitions = definitions_;
        context.runId = nextRunId_++;
    }

    ReverseProbeReport report;
    PopulateReportMetadata(report, context);
    report.results.reserve(definitions.size());
    for (const ReverseProbeDefinition& definition : definitions)
    {
        ReverseProbeResult result = RunDefinition(
            definition,
            context,
            policy
        );
        const bool failed = result.status == ReverseProbeStatus::Failed
            || result.status == ReverseProbeStatus::Rejected;
        report.results.push_back(std::move(result));
        if (failed && !policy.continueAfterFailure)
        {
            break;
        }
    }
    return report;
}

ReverseProbeReport ReverseProbeFramework::RunSelected(
    const std::vector<std::string>& ids,
    ReverseProbeContext context,
    const ReverseProbePolicy& policy
)
{
    RefreshSafetySnapshot(context);
    std::vector<std::optional<ReverseProbeDefinition>> definitions;
    definitions.reserve(ids.size());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        context.runId = nextRunId_++;
        for (const std::string& id : ids)
        {
            const auto found = std::find_if(
                definitions_.begin(),
                definitions_.end(),
                [&id](const ReverseProbeDefinition& value)
                {
                    return value.id == id;
                }
            );
            definitions.emplace_back(
                found == definitions_.end()
                    ? std::nullopt
                    : std::optional<ReverseProbeDefinition>(*found)
            );
        }
    }

    ReverseProbeReport report;
    PopulateReportMetadata(report, context);
    report.results.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        ReverseProbeResult result;
        if (!definitions[index])
        {
            result.id = ids[index];
            result.runId = context.runId;
            result.status = ReverseProbeStatus::Skipped;
            result.message = "reverse_probe_missing";
        }
        else
        {
            result = RunDefinition(*definitions[index], context, policy);
        }
        const bool failed = result.status == ReverseProbeStatus::Failed
            || result.status == ReverseProbeStatus::Rejected;
        report.results.push_back(std::move(result));
        if (failed && !policy.continueAfterFailure)
        {
            break;
        }
    }
    return report;
}

ReverseProbeResult ReverseProbeFramework::RunDefinition(
    const ReverseProbeDefinition& definition,
    const ReverseProbeContext& context,
    const ReverseProbePolicy& policy
) const
{
    ReverseProbeContext callbackContext = context;
    ReverseProbeResult output;
    output.id = definition.id;
    output.category = definition.category;
    output.access = definition.access;
    output.runId = context.runId;
    if (context.engine && context.engine->ActiveVersion())
    {
        output.version.assign(context.engine->ActiveVersion()->name);
    }

    const auto reject = [&output](std::string message)
    {
        output.status = ReverseProbeStatus::Rejected;
        output.message = std::move(message);
        return output;
    };
    const auto skip = [&output](std::string message)
    {
        output.status = ReverseProbeStatus::Skipped;
        output.message = std::move(message);
        return output;
    };

    if (AccessExceeds(definition.access, policy.maximumAccess))
    {
        return reject("reverse_probe_access_denied");
    }
    if (policy.requireActiveRegistry
        && (!context.engine || !context.engine->IsActive()))
    {
        return skip("engine_registry_inactive");
    }
    if (definition.requiredVersion)
    {
        const engine::VersionDescriptor* version = context.engine
            ? context.engine->ActiveVersion()
            : nullptr;
        if (!version || version->id != *definition.requiredVersion)
        {
            return skip("reverse_probe_version_mismatch");
        }
    }
    for (const engine::SymbolId symbol : definition.requiredSymbols)
    {
        if (!context.engine
            || context.engine->Resolve(symbol) == 0
            || context.engine->SymbolValidation(symbol)
                == engine::SymbolValidationState::Invalid)
        {
            return skip("reverse_probe_required_symbol_unavailable");
        }
    }
    for (const engine::TypeId type : definition.requiredTypes)
    {
        if (!context.engine || !context.engine->FindType(type))
        {
            return skip("reverse_probe_required_type_unavailable");
        }
    }
    for (const engine::FieldId field : definition.requiredFields)
    {
        if (!context.engine || !context.engine->FindField(field))
        {
            return skip("reverse_probe_required_field_unavailable");
        }
    }
    for (const std::string& capabilityId : definition.requiredCapabilities)
    {
        const auto capability = context.capabilities
            ? context.capabilities->Query(capabilityId, context.engine)
            : std::optional<CapabilitySnapshot>{};
        if (!capability || !capability->Available())
        {
            return skip("reverse_probe_required_capability_unavailable: "
                + capabilityId);
        }
    }
    if (definition.requiresGameplay
        && (!context.lifecycle.runtimeActive
            || context.lifecycle.phase != GamePhase::Gameplay))
    {
        return skip("reverse_probe_gameplay_required");
    }
    const bool mustHoldStableLease = definition.requiresStableBarrier
        || (IsMutation(definition.access)
            && policy.requireOpenSaveLoadBarrierForMutation);
    if (mustHoldStableLease)
    {
        if (!context.saveLoadBarrier)
        {
            return IsMutation(definition.access)
                ? reject("reverse_probe_save_load_barrier_closed")
                : skip("reverse_probe_save_load_barrier_closed");
        }
        auto lease = context.saveLoadBarrier->TryAcquireStableLease();
        if (!lease)
        {
            return IsMutation(definition.access)
                ? reject("reverse_probe_save_load_barrier_closed")
                : skip("reverse_probe_save_load_barrier_closed");
        }
        callbackContext.safetyLease = std::static_pointer_cast<void>(
            std::make_shared<NativeSaveLoadWriteLease>(
                std::move(*lease)
            )
        );
    }
    if (IsMutation(definition.access))
    {
        if (policy.requireGameplayForMutation
            && (!context.lifecycle.runtimeActive
                || context.lifecycle.phase != GamePhase::Gameplay))
        {
            return reject("reverse_probe_gameplay_required");
        }
    }

    const auto started = std::chrono::steady_clock::now();
    try
    {
        output = definition.execute(callbackContext);
    }
    catch (const std::exception& exception)
    {
        output.status = ReverseProbeStatus::Failed;
        output.message = std::string("reverse_probe_exception: ")
            + exception.what();
    }
    catch (...)
    {
        output.status = ReverseProbeStatus::Failed;
        output.message = "reverse_probe_exception";
    }
    const auto finished = std::chrono::steady_clock::now();
    output.id = definition.id;
    output.category = definition.category;
    output.access = definition.access;
    output.runId = context.runId;
    output.durationMicroseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            finished - started
        ).count()
    );
    if (context.engine && context.engine->ActiveVersion())
    {
        output.version.assign(context.engine->ActiveVersion()->name);
    }
    return output;
}

bool ReverseProbeFramework::AppendReport(
    const std::filesystem::path& path,
    const ReverseProbeReport& report,
    std::string& error
) const
{
    std::ofstream stream(path, std::ios::app | std::ios::binary);
    if (!stream)
    {
        error = "reverse_probe_report_open_failed";
        return false;
    }
    for (const ReverseProbeResult& result : report.results)
    {
        stream
            << "{\"run_id\":" << result.runId
            << ",\"timestamp_ms\":" << report.timestampMilliseconds
            << ",\"lifecycle_generation\":"
            << report.lifecycleGeneration
            << ",\"barrier_generation\":"
            << report.barrierGeneration
            << ",\"player_tag\":\""
            << EscapeJson(report.playerTag)
            << "\""
            << ",\"id\":\"" << EscapeJson(result.id)
            << "\",\"category\":\"" << EscapeJson(result.category)
            << "\",\"access\":\"" << ReverseProbeAccessName(result.access)
            << "\",\"status\":\"" << ReverseProbeStatusName(result.status)
            << "\",\"evidence\":\"" << ReverseProbeEvidenceName(result.evidence)
            << "\",\"duration_us\":" << result.durationMicroseconds
            << ",\"version\":\"" << EscapeJson(result.version)
            << "\",\"message\":\"" << EscapeJson(result.message)
            << "\"}\n";
    }
    if (!stream)
    {
        error = "reverse_probe_report_write_failed";
        return false;
    }
    error.clear();
    return true;
}

bool RegisterCoreReverseProbes(
    ReverseProbeFramework& framework,
    std::string& error
)
{
    ReverseProbeDefinition structure;
    structure.id = "engine.registry.structure";
    structure.category = "engine_registry";
    structure.access = ReverseProbeAccess::MetadataOnly;
    structure.execute = AuditRegistryStructure;
    if (!framework.Contains(structure.id)
        && !framework.Register(std::move(structure), error))
    {
        return false;
    }

    ReverseProbeDefinition symbols;
    symbols.id = "engine.registry.validated_symbols";
    symbols.category = "engine_registry";
    symbols.access = ReverseProbeAccess::ReadMemory;
    symbols.execute = ValidateKnownSymbols;
    if (!framework.Contains(symbols.id))
    {
        if (!framework.Register(std::move(symbols), error))
        {
            return false;
        }
    }

    ReverseProbeDefinition candidates;
    candidates.id = "engine.registry.candidate_symbols";
    candidates.category = "engine_registry";
    candidates.access = ReverseProbeAccess::ReadMemory;
    candidates.execute = ValidateCandidateSymbols;
    if (!framework.Contains(candidates.id)
        && !framework.Register(std::move(candidates), error))
    {
        return false;
    }
    error.clear();
    return true;
}

const char* ReverseProbeAccessName(ReverseProbeAccess value)
{
    switch (value)
    {
    case ReverseProbeAccess::MetadataOnly:
        return "metadata";
    case ReverseProbeAccess::ReadMemory:
        return "read_memory";
    case ReverseProbeAccess::ReversiblePatch:
        return "reversible_patch";
    case ReverseProbeAccess::WriteMemory:
        return "write_memory";
    default:
        return "unknown";
    }
}

const char* ReverseProbeStatusName(ReverseProbeStatus value)
{
    switch (value)
    {
    case ReverseProbeStatus::Passed:
        return "passed";
    case ReverseProbeStatus::Failed:
        return "failed";
    case ReverseProbeStatus::Skipped:
        return "skipped";
    case ReverseProbeStatus::Rejected:
        return "rejected";
    default:
        return "unknown";
    }
}

const char* ReverseProbeEvidenceName(ReverseProbeEvidence value)
{
    switch (value)
    {
    case ReverseProbeEvidence::None:
        return "none";
    case ReverseProbeEvidence::Candidate:
        return "candidate";
    case ReverseProbeEvidence::Confirmed:
        return "confirmed";
    case ReverseProbeEvidence::Proven:
        return "proven";
    case ReverseProbeEvidence::VerifiedWrite:
        return "verified_write";
    default:
        return "unknown";
    }
}

}
