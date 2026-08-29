#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace core
{

class CapabilityRegistry;
namespace engine
{
enum class VersionId;
}

using NativeEffectScalar = std::variant<
    bool,
    int64_t,
    double,
    std::string
>;

using NativeEffectList = std::vector<NativeEffectScalar>;

using NativeEffectValue = std::variant<
    bool,
    int64_t,
    double,
    std::string,
    NativeEffectList
>;

struct NativeEffect
{
    std::string operation;
    std::unordered_map<std::string, NativeEffectValue> arguments;

    const NativeEffectValue* Find(std::string_view name) const;
};

struct NativeEffectBatch
{
    std::string source;
    bool atomic = true;
    std::vector<NativeEffect> effects;
};

struct NativeEffectExecutionContext
{
    uint64_t transactionId = 0;
    uint64_t lifecycleGeneration = 0;
    uint64_t callerStateId = 0;
    uint64_t callerThreadId = 0;
    std::shared_ptr<void> safetyLease;
    std::string playerTag;
    std::string source;
};

struct PreparedNativeEffect
{
    std::function<bool(std::string&)> apply;
    std::function<void()> rollback;
};

using NativeEffectPrepareHandler = std::function<bool(
    const NativeEffect&,
    const NativeEffectExecutionContext&,
    PreparedNativeEffect&,
    std::string&
)>;

using NativeEffectSafetyGate = std::function<std::shared_ptr<void>()>;

enum class NativeEffectStatus
{
    Applied,
    InvalidRequest,
    GameplayInactive,
    WrongExecutionThread,
    HandlerMissing,
    PreparationFailed,
    AtomicRollbackUnavailable,
    ApplyFailed
};

struct NativeEffectResult
{
    NativeEffectStatus status = NativeEffectStatus::InvalidRequest;
    uint64_t transactionId = 0;
    std::size_t preparedCount = 0;
    std::size_t appliedCount = 0;
    std::string code;
    std::string message;

    bool Succeeded() const
    {
        return status == NativeEffectStatus::Applied;
    }
};

class NativeEffectService
{
public:
    NativeEffectService();
    ~NativeEffectService();

    NativeEffectService(const NativeEffectService&) = delete;
    NativeEffectService& operator=(const NativeEffectService&) = delete;

    bool RegisterHandler(
        std::string operation,
        NativeEffectPrepareHandler handler,
        std::string& error
    );
    bool UnregisterHandler(std::string_view operation);
    bool HasHandler(std::string_view operation) const;
    std::vector<std::string> Operations() const;

    void SetGameplayContext(
        bool active,
        std::string playerTag,
        uint64_t lifecycleGeneration
    );
    void SetSafetyGate(NativeEffectSafetyGate gate);
    void SetCapabilityRegistry(CapabilityRegistry* capabilities);
    void SetCapabilityVersion(std::optional<engine::VersionId> version);
    void ResetExecutionThread();

    NativeEffectResult ExecuteImmediate(
        NativeEffectBatch batch,
        uint64_t callerStateId,
        uint64_t callerThreadId
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

NativeEffectService& GetNativeEffectService();

std::string NormalizeNativeEffectName(std::string_view value);
bool NativeEffectValueToBool(
    const NativeEffectValue& value,
    bool& output
);
bool NativeEffectValueToInteger(
    const NativeEffectValue& value,
    int64_t& output
);
bool NativeEffectValueToNumber(
    const NativeEffectValue& value,
    double& output
);
bool NativeEffectValueToString(
    const NativeEffectValue& value,
    std::string& output
);
bool NativeEffectValueToIntegerList(
    const NativeEffectValue& value,
    std::vector<int64_t>& output
);

}
