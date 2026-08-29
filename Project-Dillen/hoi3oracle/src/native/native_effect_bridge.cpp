#include "native_effect_bridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

#include "capability_registry.h"

namespace core
{

namespace
{

constexpr std::size_t MaximumEffectsPerBatch = 256;

NativeEffectResult MakeFailure(
    NativeEffectStatus status,
    uint64_t transactionId,
    std::string code,
    std::string message = {}
)
{
    NativeEffectResult result;
    result.status = status;
    result.transactionId = transactionId;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

}

struct NativeEffectService::Impl
{
    NativeEffectPrepareHandler FindHandler(
        std::string_view operation
    ) const
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        const auto found = handlers.find(
            NormalizeNativeEffectName(operation)
        );
        return found == handlers.end()
            ? NativeEffectPrepareHandler{}
            : found->second;
    }

    mutable std::mutex registryMutex;
    std::unordered_map<std::string, NativeEffectPrepareHandler> handlers;

    mutable std::mutex contextMutex;
    std::mutex executionMutex;
    bool gameplayActive = false;
    std::string playerTag;
    uint64_t lifecycleGeneration = 0;
    uint64_t nextTransactionId = 1;
    NativeEffectSafetyGate safetyGate;
    CapabilityRegistry* capabilities = nullptr;
    std::optional<engine::VersionId> capabilityVersion;
};

const NativeEffectValue* NativeEffect::Find(
    std::string_view name
) const
{
    const auto found = arguments.find(
        NormalizeNativeEffectName(name)
    );
    return found == arguments.end() ? nullptr : &found->second;
}

void NativeEffectService::SetSafetyGate(NativeEffectSafetyGate gate)
{
    std::lock_guard<std::mutex> lock(impl_->contextMutex);
    impl_->safetyGate = std::move(gate);
}

NativeEffectService::NativeEffectService()
    : impl_(std::make_unique<Impl>())
{
}

NativeEffectService::~NativeEffectService() = default;

bool NativeEffectService::RegisterHandler(
    std::string operation,
    NativeEffectPrepareHandler handler,
    std::string& error
)
{
    operation = NormalizeNativeEffectName(operation);
    if (operation.empty() || !handler)
    {
        error = "native_effect_handler_invalid";
        return false;
    }
    CapabilityRegistry* capabilities = nullptr;
    std::optional<engine::VersionId> capabilityVersion;
    {
        std::lock_guard<std::mutex> lock(impl_->registryMutex);
        if (impl_->handlers.find(operation) != impl_->handlers.end())
        {
            error = "native_effect_handler_duplicate: " + operation;
            return false;
        }
        impl_->handlers.emplace(operation, std::move(handler));
        capabilities = impl_->capabilities;
        capabilityVersion = impl_->capabilityVersion;
    }
    if (capabilities)
    {
        CapabilityDescriptor capability;
        capability.id = "effect." + operation;
        capability.provider = "native_effect_service";
        capability.kind = CapabilityKind::NativeEffect;
        capability.access = CapabilityAccess::Write;
        capability.rollback = CapabilityRollback::Conditional;
        capability.persistence = CapabilityPersistence::Unknown;
        capability.multiplayer = CapabilityMultiplayer::Unknown;
        capability.version = capabilityVersion;
        if (!capabilities->Register(std::move(capability), error))
        {
            std::lock_guard<std::mutex> lock(impl_->registryMutex);
            impl_->handlers.erase(operation);
            return false;
        }
    }
    error.clear();
    return true;
}

bool NativeEffectService::UnregisterHandler(
    std::string_view operation
)
{
    const std::string normalized = NormalizeNativeEffectName(operation);
    CapabilityRegistry* capabilities = nullptr;
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(impl_->registryMutex);
        removed = impl_->handlers.erase(normalized) > 0;
        capabilities = impl_->capabilities;
    }
    if (removed && capabilities)
    {
        capabilities->Unregister("effect." + normalized);
    }
    return removed;
}

void NativeEffectService::SetCapabilityRegistry(
    CapabilityRegistry* capabilities
)
{
    std::lock_guard<std::mutex> lock(impl_->registryMutex);
    impl_->capabilities = capabilities;
}

void NativeEffectService::SetCapabilityVersion(
    std::optional<engine::VersionId> version
)
{
    std::lock_guard<std::mutex> lock(impl_->registryMutex);
    impl_->capabilityVersion = version;
}

bool NativeEffectService::HasHandler(
    std::string_view operation
) const
{
    std::lock_guard<std::mutex> lock(impl_->registryMutex);
    return impl_->handlers.find(NormalizeNativeEffectName(operation))
        != impl_->handlers.end();
}

std::vector<std::string> NativeEffectService::Operations() const
{
    std::vector<std::string> operations;
    {
        std::lock_guard<std::mutex> lock(impl_->registryMutex);
        operations.reserve(impl_->handlers.size());
        for (const auto& entry : impl_->handlers)
        {
            operations.push_back(entry.first);
        }
    }
    std::sort(operations.begin(), operations.end());
    return operations;
}

void NativeEffectService::SetGameplayContext(
    bool active,
    std::string playerTag,
    uint64_t lifecycleGeneration
)
{
    std::lock_guard<std::mutex> lock(impl_->contextMutex);
    impl_->gameplayActive = active;
    impl_->playerTag = std::move(playerTag);
    impl_->lifecycleGeneration = lifecycleGeneration;
}

void NativeEffectService::ResetExecutionThread()
{
    std::lock_guard<std::mutex> lock(impl_->executionMutex);
}

NativeEffectResult NativeEffectService::ExecuteImmediate(
    NativeEffectBatch batch,
    uint64_t callerStateId,
    uint64_t callerThreadId
)
{
    if (batch.effects.empty()
        || batch.effects.size() > MaximumEffectsPerBatch)
    {
        return MakeFailure(
            NativeEffectStatus::InvalidRequest,
            0,
            "native_effect_batch_size_invalid"
        );
    }
    if (callerThreadId == 0)
    {
        return MakeFailure(
            NativeEffectStatus::InvalidRequest,
            0,
            "native_effect_thread_missing"
        );
    }

    std::unique_lock<std::mutex> executionLock(
        impl_->executionMutex
    );

    NativeEffectExecutionContext context;
    NativeEffectSafetyGate safetyGate;
    {
        std::lock_guard<std::mutex> lock(impl_->contextMutex);
        if (!impl_->gameplayActive || impl_->playerTag.empty())
        {
            return MakeFailure(
                NativeEffectStatus::GameplayInactive,
                0,
                "native_effect_gameplay_inactive"
            );
        }
        context.transactionId = impl_->nextTransactionId++;
        context.lifecycleGeneration = impl_->lifecycleGeneration;
        context.callerStateId = callerStateId;
        context.callerThreadId = callerThreadId;
        context.playerTag = impl_->playerTag;
        safetyGate = impl_->safetyGate;
    }
    std::shared_ptr<void> safetyLease;
    if (safetyGate)
    {
        safetyLease = safetyGate();
        if (!safetyLease)
        {
            return MakeFailure(
                NativeEffectStatus::GameplayInactive,
                0,
                "native_effect_save_load_barrier_closed"
            );
        }
    }
    context.safetyLease = safetyLease;
    context.source = batch.source.empty()
        ? "lua"
        : NormalizeNativeEffectName(batch.source);

    struct PreparedEntry
    {
        std::string operation;
        PreparedNativeEffect prepared;
    };
    std::vector<PreparedEntry> preparedEntries;
    preparedEntries.reserve(batch.effects.size());

    for (NativeEffect& effect : batch.effects)
    {
        effect.operation = NormalizeNativeEffectName(effect.operation);
        if (effect.operation.empty())
        {
            NativeEffectResult result = MakeFailure(
                NativeEffectStatus::InvalidRequest,
                context.transactionId,
                "native_effect_operation_missing"
            );
            result.preparedCount = preparedEntries.size();
            return result;
        }
        NativeEffectPrepareHandler handler = impl_->FindHandler(
            effect.operation
        );
        if (!handler)
        {
            NativeEffectResult result = MakeFailure(
                NativeEffectStatus::HandlerMissing,
                context.transactionId,
                "native_effect_handler_missing",
                effect.operation
            );
            result.preparedCount = preparedEntries.size();
            return result;
        }

        PreparedEntry entry;
        entry.operation = effect.operation;
        std::string error;
        if (!handler(effect, context, entry.prepared, error)
            || !entry.prepared.apply)
        {
            NativeEffectResult result = MakeFailure(
                NativeEffectStatus::PreparationFailed,
                context.transactionId,
                "native_effect_prepare_failed",
                error.empty() ? effect.operation : std::move(error)
            );
            result.preparedCount = preparedEntries.size();
            return result;
        }
        if (batch.atomic
            && batch.effects.size() > 1
            && !entry.prepared.rollback)
        {
            NativeEffectResult result = MakeFailure(
                NativeEffectStatus::AtomicRollbackUnavailable,
                context.transactionId,
                "native_effect_atomic_rollback_missing",
                effect.operation
            );
            result.preparedCount = preparedEntries.size();
            return result;
        }
        preparedEntries.push_back(std::move(entry));
    }

    {
        std::lock_guard<std::mutex> lock(impl_->contextMutex);
        if (!impl_->gameplayActive
            || impl_->playerTag != context.playerTag
            || impl_->lifecycleGeneration
                != context.lifecycleGeneration)
        {
            NativeEffectResult result = MakeFailure(
                NativeEffectStatus::GameplayInactive,
                context.transactionId,
                "native_effect_lifecycle_changed"
            );
            result.preparedCount = preparedEntries.size();
            return result;
        }
    }

    std::size_t appliedCount = 0;
    for (PreparedEntry& entry : preparedEntries)
    {
        std::string error;
        if (!entry.prepared.apply(error))
        {
            if (entry.prepared.rollback)
            {
                entry.prepared.rollback();
            }
            if (batch.atomic)
            {
                while (appliedCount > 0)
                {
                    --appliedCount;
                    PreparedEntry& applied = preparedEntries[appliedCount];
                    if (applied.prepared.rollback)
                    {
                        applied.prepared.rollback();
                    }
                }
            }
            NativeEffectResult result = MakeFailure(
                NativeEffectStatus::ApplyFailed,
                context.transactionId,
                "native_effect_apply_failed",
                error.empty() ? entry.operation : std::move(error)
            );
            result.preparedCount = preparedEntries.size();
            result.appliedCount = appliedCount;
            return result;
        }
        ++appliedCount;
    }

    NativeEffectResult result;
    result.status = NativeEffectStatus::Applied;
    result.transactionId = context.transactionId;
    result.preparedCount = preparedEntries.size();
    result.appliedCount = appliedCount;
    result.code = "ok";
    return result;
}

NativeEffectService& GetNativeEffectService()
{
    static NativeEffectService service;
    return service;
}

std::string NormalizeNativeEffectName(std::string_view value)
{
    const auto begin = std::find_if_not(
        value.begin(),
        value.end(),
        [](unsigned char character)
        {
            return std::isspace(character) != 0;
        }
    );
    const auto end = std::find_if_not(
        value.rbegin(),
        value.rend(),
        [](unsigned char character)
        {
            return std::isspace(character) != 0;
        }
    ).base();
    if (begin >= end)
    {
        return {};
    }
    std::string normalized(begin, end);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    return normalized;
}

bool NativeEffectValueToBool(
    const NativeEffectValue& value,
    bool& output
)
{
    if (const auto* boolean = std::get_if<bool>(&value))
    {
        output = *boolean;
        return true;
    }
    return false;
}

bool NativeEffectValueToInteger(
    const NativeEffectValue& value,
    int64_t& output
)
{
    if (const auto* integer = std::get_if<int64_t>(&value))
    {
        output = *integer;
        return true;
    }
    if (const auto* number = std::get_if<double>(&value))
    {
        if (std::isfinite(*number)
            && std::floor(*number) == *number
            && *number >= static_cast<double>(
                std::numeric_limits<int64_t>::min()
            )
            && *number <= static_cast<double>(
                std::numeric_limits<int64_t>::max()
            ))
        {
            output = static_cast<int64_t>(*number);
            return true;
        }
    }
    return false;
}

bool NativeEffectValueToNumber(
    const NativeEffectValue& value,
    double& output
)
{
    if (const auto* number = std::get_if<double>(&value))
    {
        output = *number;
        return true;
    }
    if (const auto* integer = std::get_if<int64_t>(&value))
    {
        output = static_cast<double>(*integer);
        return true;
    }
    return false;
}

bool NativeEffectValueToString(
    const NativeEffectValue& value,
    std::string& output
)
{
    if (const auto* text = std::get_if<std::string>(&value))
    {
        output = *text;
        return true;
    }
    return false;
}

bool NativeEffectValueToIntegerList(
    const NativeEffectValue& value,
    std::vector<int64_t>& output
)
{
    const auto* list = std::get_if<NativeEffectList>(&value);
    if (!list)
    {
        return false;
    }
    std::vector<int64_t> converted;
    converted.reserve(list->size());
    for (const NativeEffectScalar& scalar : *list)
    {
        if (const auto* integer = std::get_if<int64_t>(&scalar))
        {
            converted.push_back(*integer);
            continue;
        }
        if (const auto* number = std::get_if<double>(&scalar))
        {
            if (std::isfinite(*number)
                && std::floor(*number) == *number
                && *number >= static_cast<double>(
                    std::numeric_limits<int64_t>::min()
                )
                && *number <= static_cast<double>(
                    std::numeric_limits<int64_t>::max()
                ))
            {
                converted.push_back(static_cast<int64_t>(*number));
                continue;
            }
        }
        return false;
    }
    output = std::move(converted);
    return true;
}

}
