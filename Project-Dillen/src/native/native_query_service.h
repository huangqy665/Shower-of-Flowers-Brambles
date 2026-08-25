#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "capability_registry.h"

namespace core
{

enum class NativeQueryValueKind
{
    Null,
    Boolean,
    Integer,
    Number,
    String,
    List,
    Object
};

struct NativeQueryValue
{
    NativeQueryValueKind kind = NativeQueryValueKind::Null;
    std::variant<std::monostate, bool, int64_t, double, std::string> scalar;
    std::vector<NativeQueryValue> items;
    std::unordered_map<std::string, NativeQueryValue> fields;

    NativeQueryValue() = default;
    NativeQueryValue(bool value);
    NativeQueryValue(int64_t value);
    NativeQueryValue(double value);
    NativeQueryValue(std::string value);
    NativeQueryValue(const char* value);

    static NativeQueryValue List(std::vector<NativeQueryValue> values);
    static NativeQueryValue Object(
        std::unordered_map<std::string, NativeQueryValue> values
    );
    const NativeQueryValue* Find(std::string_view name) const;
};

struct NativeQueryRequest
{
    std::string key;
    std::string operation;
    std::unordered_map<std::string, NativeQueryValue> arguments;

    const NativeQueryValue* Find(std::string_view name) const;
};

struct NativeQueryExecutionContext
{
    uint64_t lifecycleGeneration = 0;
    uint64_t callerStateId = 0;
    uint64_t callerThreadId = 0;
    std::string playerTag;
    std::shared_ptr<void> safetyLease;
};

using NativeQueryHandler = std::function<bool(
    const NativeQueryRequest&,
    const NativeQueryExecutionContext&,
    NativeQueryValue&,
    std::string&
)>;

using NativeQuerySafetyGate = std::function<std::shared_ptr<void>()>;

enum class NativeQueryStatus
{
    Succeeded,
    InvalidRequest,
    GameplayInactive,
    WrongExecutionThread,
    HandlerMissing,
    HandlerFailed,
    CapabilityUnavailable
};

struct NativeQueryResult
{
    std::string key;
    std::string operation;
    NativeQueryStatus status = NativeQueryStatus::InvalidRequest;
    NativeQueryValue value;
    std::string code;
    std::string message;

    bool Succeeded() const
    {
        return status == NativeQueryStatus::Succeeded;
    }
};

struct NativeQuerySnapshot
{
    NativeQueryStatus status = NativeQueryStatus::InvalidRequest;
    uint64_t snapshotId = 0;
    uint64_t lifecycleGeneration = 0;
    uint64_t callerStateId = 0;
    std::string playerTag;
    std::vector<NativeQueryResult> results;
    std::string code;
    std::string message;

    bool Succeeded() const
    {
        return status == NativeQueryStatus::Succeeded;
    }
};

struct NativeQueryDescriptor
{
    std::string operation;
    std::string provider;
    std::vector<engine::SymbolId> requiredSymbols;
    std::vector<engine::TypeId> requiredTypes;
    std::vector<engine::FieldId> requiredFields;
    CapabilityMultiplayer multiplayer = CapabilityMultiplayer::Deterministic;
};

class NativeQueryService
{
public:
    void Configure(
        CapabilityRegistry* capabilities,
        engine::EngineRegistry* engine = nullptr
    );
    void SetGameplayContext(
        bool active,
        std::string playerTag,
        uint64_t lifecycleGeneration
    );
    void SetSafetyGate(NativeQuerySafetyGate gate);
    void ResetExecutionThread();

    bool RegisterHandler(
        NativeQueryDescriptor descriptor,
        NativeQueryHandler handler,
        std::string& error
    );
    bool UnregisterHandler(std::string_view operation);
    std::size_t UnregisterProvider(std::string_view provider);
    bool HasHandler(std::string_view operation) const;
    std::vector<std::string> Operations() const;

    NativeQueryResult ExecuteImmediate(
        NativeQueryRequest request,
        uint64_t callerStateId,
        uint64_t callerThreadId
    );
    NativeQuerySnapshot ExecuteSnapshot(
        std::vector<NativeQueryRequest> requests,
        uint64_t callerStateId,
        uint64_t callerThreadId
    );
    NativeQuerySnapshot ExecuteSnapshotGuarded(
        std::vector<NativeQueryRequest> requests,
        uint64_t callerStateId,
        uint64_t callerThreadId,
        std::shared_ptr<void> safetyLease
    );

private:
    struct Entry
    {
        NativeQueryDescriptor descriptor;
        NativeQueryHandler handler;
    };

    NativeQuerySnapshot ExecuteSnapshotInternal(
        std::vector<NativeQueryRequest> requests,
        uint64_t callerStateId,
        uint64_t callerThreadId,
        std::shared_ptr<void> suppliedSafetyLease
    );

    mutable std::mutex registryMutex_;
    std::unordered_map<std::string, Entry> handlers_;
    CapabilityRegistry* capabilities_ = nullptr;
    engine::EngineRegistry* engine_ = nullptr;

    mutable std::mutex contextMutex_;
    std::mutex executionMutex_;
    bool gameplayActive_ = false;
    std::string playerTag_;
    uint64_t lifecycleGeneration_ = 0;
    uint64_t executionThreadId_ = 0;
    NativeQuerySafetyGate safetyGate_;
    std::atomic<uint64_t> nextSnapshotId_{1};
};

NativeQueryService& GetNativeQueryService();

std::string NormalizeNativeQueryName(std::string_view value);
bool NativeQueryValueToBool(const NativeQueryValue& value, bool& output);
bool NativeQueryValueToInteger(
    const NativeQueryValue& value,
    int64_t& output
);
bool NativeQueryValueToNumber(
    const NativeQueryValue& value,
    double& output
);
bool NativeQueryValueToString(
    const NativeQueryValue& value,
    std::string& output
);

}
