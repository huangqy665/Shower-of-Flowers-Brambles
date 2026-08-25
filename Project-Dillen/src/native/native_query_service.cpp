#include "native_query_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace core
{
namespace
{

constexpr std::size_t MaximumSnapshotQueries = 256;

NativeQueryResult Failure(
    NativeQueryStatus status,
    std::string code,
    std::string message = {}
)
{
    NativeQueryResult result;
    result.status = status;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

NativeQuerySnapshot SnapshotFailure(
    NativeQueryStatus status,
    std::string code,
    std::string message = {}
)
{
    NativeQuerySnapshot snapshot;
    snapshot.status = status;
    snapshot.code = std::move(code);
    snapshot.message = std::move(message);
    return snapshot;
}

}

NativeQueryValue::NativeQueryValue(bool value)
    : kind(NativeQueryValueKind::Boolean), scalar(value)
{
}

NativeQueryValue::NativeQueryValue(int64_t value)
    : kind(NativeQueryValueKind::Integer), scalar(value)
{
}

NativeQueryValue::NativeQueryValue(double value)
    : kind(NativeQueryValueKind::Number), scalar(value)
{
}

NativeQueryValue::NativeQueryValue(std::string value)
    : kind(NativeQueryValueKind::String), scalar(std::move(value))
{
}

NativeQueryValue::NativeQueryValue(const char* value)
    : NativeQueryValue(std::string(value ? value : ""))
{
}

NativeQueryValue NativeQueryValue::List(
    std::vector<NativeQueryValue> values
)
{
    NativeQueryValue value;
    value.kind = NativeQueryValueKind::List;
    value.items = std::move(values);
    return value;
}

NativeQueryValue NativeQueryValue::Object(
    std::unordered_map<std::string, NativeQueryValue> values
)
{
    NativeQueryValue value;
    value.kind = NativeQueryValueKind::Object;
    value.fields = std::move(values);
    return value;
}

const NativeQueryValue* NativeQueryValue::Find(
    std::string_view name
) const
{
    const auto found = fields.find(NormalizeNativeQueryName(name));
    return found == fields.end() ? nullptr : &found->second;
}

const NativeQueryValue* NativeQueryRequest::Find(
    std::string_view name
) const
{
    const auto found = arguments.find(NormalizeNativeQueryName(name));
    return found == arguments.end() ? nullptr : &found->second;
}

void NativeQueryService::Configure(
    CapabilityRegistry* capabilities,
    engine::EngineRegistry* engine
)
{
    std::lock_guard<std::mutex> lock(registryMutex_);
    capabilities_ = capabilities;
    engine_ = engine;
}

void NativeQueryService::SetGameplayContext(
    bool active,
    std::string playerTag,
    uint64_t lifecycleGeneration
)
{
    std::lock_guard<std::mutex> lock(contextMutex_);
    gameplayActive_ = active;
    playerTag_ = std::move(playerTag);
    lifecycleGeneration_ = lifecycleGeneration;
}

void NativeQueryService::SetSafetyGate(NativeQuerySafetyGate gate)
{
    std::lock_guard<std::mutex> lock(contextMutex_);
    safetyGate_ = std::move(gate);
}

void NativeQueryService::ResetExecutionThread()
{
    std::lock_guard<std::mutex> executionLock(executionMutex_);
    executionThreadId_ = 0;
}

bool NativeQueryService::RegisterHandler(
    NativeQueryDescriptor descriptor,
    NativeQueryHandler handler,
    std::string& error
)
{
    descriptor.operation = NormalizeNativeQueryName(descriptor.operation);
    descriptor.provider = NormalizeCapabilityId(descriptor.provider);
    if (descriptor.operation.empty() || descriptor.provider.empty() || !handler)
    {
        error = "native_query_handler_invalid";
        return false;
    }
    CapabilityRegistry* capabilities = nullptr;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        if (handlers_.find(descriptor.operation) != handlers_.end())
        {
            error = "native_query_handler_duplicate: " + descriptor.operation;
            return false;
        }
        capabilities = capabilities_;
        handlers_.emplace(
            descriptor.operation,
            Entry{descriptor, std::move(handler)}
        );
    }
    if (capabilities)
    {
        CapabilityDescriptor capability;
        capability.id = "query." + descriptor.operation;
        capability.provider = descriptor.provider;
        capability.kind = CapabilityKind::NativeQuery;
        capability.access = CapabilityAccess::Read;
        capability.rollback = CapabilityRollback::NotApplicable;
        capability.persistence = CapabilityPersistence::None;
        capability.multiplayer = descriptor.multiplayer;
        capability.requiredSymbols = descriptor.requiredSymbols;
        capability.requiredTypes = descriptor.requiredTypes;
        capability.requiredFields = descriptor.requiredFields;
        if (!capabilities->Register(std::move(capability), error))
        {
            std::lock_guard<std::mutex> lock(registryMutex_);
            handlers_.erase(descriptor.operation);
            return false;
        }
    }
    error.clear();
    return true;
}

bool NativeQueryService::UnregisterHandler(std::string_view operation)
{
    const std::string normalized = NormalizeNativeQueryName(operation);
    CapabilityRegistry* capabilities = nullptr;
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        removed = handlers_.erase(normalized) > 0;
        capabilities = capabilities_;
    }
    if (removed && capabilities)
    {
        capabilities->Unregister("query." + normalized);
    }
    return removed;
}

std::size_t NativeQueryService::UnregisterProvider(
    std::string_view provider
)
{
    const std::string normalized = NormalizeCapabilityId(provider);
    std::vector<std::string> operations;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        for (const auto& entry : handlers_)
        {
            if (entry.second.descriptor.provider == normalized)
            {
                operations.push_back(entry.first);
            }
        }
    }
    for (const std::string& operation : operations)
    {
        UnregisterHandler(operation);
    }
    return operations.size();
}

bool NativeQueryService::HasHandler(std::string_view operation) const
{
    std::lock_guard<std::mutex> lock(registryMutex_);
    return handlers_.find(NormalizeNativeQueryName(operation))
        != handlers_.end();
}

std::vector<std::string> NativeQueryService::Operations() const
{
    std::vector<std::string> operations;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        operations.reserve(handlers_.size());
        for (const auto& entry : handlers_)
        {
            operations.push_back(entry.first);
        }
    }
    std::sort(operations.begin(), operations.end());
    return operations;
}

NativeQueryResult NativeQueryService::ExecuteImmediate(
    NativeQueryRequest request,
    uint64_t callerStateId,
    uint64_t callerThreadId
)
{
    const std::string operation = NormalizeNativeQueryName(
        request.operation
    );
    const std::string key = request.key.empty()
        ? operation
        : NormalizeNativeQueryName(request.key);
    NativeQuerySnapshot snapshot = ExecuteSnapshot(
        {std::move(request)},
        callerStateId,
        callerThreadId
    );
    if (!snapshot.results.empty())
    {
        return std::move(snapshot.results.front());
    }
    NativeQueryResult result = Failure(
        snapshot.status,
        std::move(snapshot.code),
        std::move(snapshot.message)
    );
    result.operation = operation;
    result.key = key;
    return result;
}

NativeQuerySnapshot NativeQueryService::ExecuteSnapshot(
    std::vector<NativeQueryRequest> requests,
    uint64_t callerStateId,
    uint64_t callerThreadId
)
{
    return ExecuteSnapshotInternal(
        std::move(requests),
        callerStateId,
        callerThreadId,
        {}
    );
}

NativeQuerySnapshot NativeQueryService::ExecuteSnapshotGuarded(
    std::vector<NativeQueryRequest> requests,
    uint64_t callerStateId,
    uint64_t callerThreadId,
    std::shared_ptr<void> safetyLease
)
{
    return ExecuteSnapshotInternal(
        std::move(requests),
        callerStateId,
        callerThreadId,
        std::move(safetyLease)
    );
}

NativeQuerySnapshot NativeQueryService::ExecuteSnapshotInternal(
    std::vector<NativeQueryRequest> requests,
    uint64_t callerStateId,
    uint64_t callerThreadId,
    std::shared_ptr<void> suppliedSafetyLease
)
{
    if (requests.empty()
        || requests.size() > MaximumSnapshotQueries
        || callerThreadId == 0)
    {
        return SnapshotFailure(
            NativeQueryStatus::InvalidRequest,
            "native_query_snapshot_request_invalid"
        );
    }

    std::unique_lock<std::mutex> executionLock(executionMutex_);
    NativeQueryExecutionContext context;
    NativeQuerySafetyGate safetyGate;
    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        if (!gameplayActive_ || playerTag_.empty())
        {
            return SnapshotFailure(
                NativeQueryStatus::GameplayInactive,
                "native_query_gameplay_inactive"
            );
        }
        if (executionThreadId_ != 0
            && executionThreadId_ != callerThreadId)
        {
            return SnapshotFailure(
                NativeQueryStatus::WrongExecutionThread,
                "native_query_wrong_execution_thread"
            );
        }
        executionThreadId_ = callerThreadId;
        context.lifecycleGeneration = lifecycleGeneration_;
        context.callerStateId = callerStateId;
        context.callerThreadId = callerThreadId;
        context.playerTag = playerTag_;
        safetyGate = safetyGate_;
    }
    context.safetyLease = suppliedSafetyLease
        ? std::move(suppliedSafetyLease)
        : (safetyGate ? safetyGate() : std::shared_ptr<void>{});
    if (safetyGate && !context.safetyLease)
    {
        return SnapshotFailure(
            NativeQueryStatus::GameplayInactive,
            "native_query_save_load_barrier_closed"
        );
    }

    NativeQuerySnapshot snapshot;
    snapshot.snapshotId = nextSnapshotId_.fetch_add(
        1,
        std::memory_order_relaxed
    );
    snapshot.lifecycleGeneration = context.lifecycleGeneration;
    snapshot.callerStateId = context.callerStateId;
    snapshot.playerTag = context.playerTag;

    std::vector<Entry> entries;
    entries.reserve(requests.size());
    std::unordered_set<std::string> keys;
    CapabilityRegistry* capabilities = nullptr;
    engine::EngineRegistry* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        for (NativeQueryRequest& request : requests)
        {
            request.operation = NormalizeNativeQueryName(
                request.operation
            );
            request.key = NormalizeNativeQueryName(
                request.key.empty() ? request.operation : request.key
            );
            if (request.operation.empty()
                || request.key.empty()
                || !keys.insert(request.key).second)
            {
                return SnapshotFailure(
                    NativeQueryStatus::InvalidRequest,
                    "native_query_snapshot_key_invalid",
                    request.key
                );
            }
            const auto found = handlers_.find(request.operation);
            if (found == handlers_.end())
            {
                NativeQueryResult result = Failure(
                    NativeQueryStatus::HandlerMissing,
                    "native_query_handler_missing",
                    request.operation
                );
                result.key = request.key;
                result.operation = request.operation;
                snapshot.results.push_back(std::move(result));
                snapshot.status = NativeQueryStatus::HandlerMissing;
                snapshot.code = "native_query_handler_missing";
                snapshot.message = request.operation;
                return snapshot;
            }
            entries.push_back(found->second);
        }
        capabilities = capabilities_;
        engine = engine_;
    }

    snapshot.results.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        NativeQueryRequest& request = requests[index];
        const Entry& entry = entries[index];
        NativeQueryResult result;
        result.key = request.key;
        result.operation = request.operation;
        if (capabilities)
        {
            const auto capability = capabilities->Query(
                "query." + request.operation,
                engine
            );
            if (!capability || !capability->Available())
            {
                result.status = NativeQueryStatus::CapabilityUnavailable;
                result.code = "native_query_capability_unavailable";
                result.message = capability
                    ? capability->reason
                    : request.operation;
                snapshot.results.push_back(std::move(result));
                if (snapshot.code.empty())
                {
                    snapshot.status = NativeQueryStatus::CapabilityUnavailable;
                    snapshot.code = "native_query_capability_unavailable";
                    snapshot.message = snapshot.results.back().message;
                }
                continue;
            }
        }

        NativeQueryValue value;
        std::string error;
        if (!entry.handler(request, context, value, error))
        {
            result.status = NativeQueryStatus::HandlerFailed;
            result.code = "native_query_handler_failed";
            result.message = error.empty()
                ? request.operation
                : std::move(error);
            snapshot.results.push_back(std::move(result));
            if (snapshot.code.empty())
            {
                snapshot.status = NativeQueryStatus::HandlerFailed;
                snapshot.code = "native_query_handler_failed";
                snapshot.message = snapshot.results.back().message;
            }
            continue;
        }
        result.status = NativeQueryStatus::Succeeded;
        result.value = std::move(value);
        result.code = "ok";
        snapshot.results.push_back(std::move(result));
    }
    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        if (!gameplayActive_
            || lifecycleGeneration_ != context.lifecycleGeneration
            || playerTag_ != context.playerTag)
        {
            NativeQuerySnapshot changed = SnapshotFailure(
                NativeQueryStatus::GameplayInactive,
                "native_query_lifecycle_changed"
            );
            changed.snapshotId = snapshot.snapshotId;
            changed.lifecycleGeneration = context.lifecycleGeneration;
            changed.callerStateId = context.callerStateId;
            changed.playerTag = context.playerTag;
            return changed;
        }
    }
    if (snapshot.code.empty())
    {
        snapshot.status = NativeQueryStatus::Succeeded;
        snapshot.code = "ok";
    }
    return snapshot;
}

NativeQueryService& GetNativeQueryService()
{
    static NativeQueryService service;
    return service;
}

std::string NormalizeNativeQueryName(std::string_view value)
{
    return NormalizeCapabilityId(value);
}

bool NativeQueryValueToBool(const NativeQueryValue& value, bool& output)
{
    if (value.kind != NativeQueryValueKind::Boolean)
    {
        return false;
    }
    output = std::get<bool>(value.scalar);
    return true;
}

bool NativeQueryValueToInteger(
    const NativeQueryValue& value,
    int64_t& output
)
{
    if (value.kind == NativeQueryValueKind::Integer)
    {
        output = std::get<int64_t>(value.scalar);
        return true;
    }
    if (value.kind == NativeQueryValueKind::Number)
    {
        const double number = std::get<double>(value.scalar);
        if (std::isfinite(number) && std::floor(number) == number)
        {
            output = static_cast<int64_t>(number);
            return true;
        }
    }
    return false;
}

bool NativeQueryValueToNumber(
    const NativeQueryValue& value,
    double& output
)
{
    if (value.kind == NativeQueryValueKind::Number)
    {
        output = std::get<double>(value.scalar);
        return std::isfinite(output);
    }
    if (value.kind == NativeQueryValueKind::Integer)
    {
        output = static_cast<double>(std::get<int64_t>(value.scalar));
        return true;
    }
    return false;
}

bool NativeQueryValueToString(
    const NativeQueryValue& value,
    std::string& output
)
{
    if (value.kind != NativeQueryValueKind::String)
    {
        return false;
    }
    output = std::get<std::string>(value.scalar);
    return true;
}

}
