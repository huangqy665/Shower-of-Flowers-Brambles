#include "capability_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace core
{
namespace
{

constexpr std::string_view EngineProvider = "engine_registry";

CapabilityAccess FieldCapabilityAccess(engine::FieldAccess access)
{
    return access == engine::FieldAccess::ReadWrite
        ? CapabilityAccess::Write
        : CapabilityAccess::Read;
}

}

bool CapabilityRegistry::Register(
    CapabilityDescriptor descriptor,
    std::string& error
)
{
    descriptor.id = NormalizeCapabilityId(descriptor.id);
    descriptor.provider = NormalizeCapabilityId(descriptor.provider);
    if (descriptor.id.empty() || descriptor.provider.empty())
    {
        error = "capability_descriptor_invalid";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (descriptors_.find(descriptor.id) != descriptors_.end())
    {
        error = "capability_duplicate: " + descriptor.id;
        return false;
    }
    invalidations_.erase(descriptor.id);
    descriptors_.emplace(descriptor.id, std::move(descriptor));
    error.clear();
    return true;
}

bool CapabilityRegistry::Unregister(std::string_view id)
{
    const std::string normalized = NormalizeCapabilityId(id);
    std::lock_guard<std::mutex> lock(mutex_);
    invalidations_.erase(normalized);
    return descriptors_.erase(normalized) > 0;
}

std::size_t CapabilityRegistry::UnregisterProvider(
    std::string_view provider
)
{
    const std::string normalized = NormalizeCapabilityId(provider);
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t removed = 0;
    for (auto iterator = descriptors_.begin();
         iterator != descriptors_.end();)
    {
        if (iterator->second.provider == normalized)
        {
            invalidations_.erase(iterator->first);
            iterator = descriptors_.erase(iterator);
            ++removed;
        }
        else
        {
            ++iterator;
        }
    }
    return removed;
}

bool CapabilityRegistry::Invalidate(
    std::string_view id,
    std::string reason
)
{
    const std::string normalized = NormalizeCapabilityId(id);
    std::lock_guard<std::mutex> lock(mutex_);
    if (descriptors_.find(normalized) == descriptors_.end())
    {
        return false;
    }
    invalidations_[normalized] = reason.empty()
        ? "capability_invalidated"
        : std::move(reason);
    return true;
}

void CapabilityRegistry::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    descriptors_.clear();
    invalidations_.clear();
}

bool CapabilityRegistry::SynchronizeEngineProfile(
    const engine::EngineRegistry& engine,
    std::string& error
)
{
    UnregisterProvider(EngineProvider);
    const engine::VersionProfile* profile = engine.ActiveProfile();
    if (!profile)
    {
        error = "capability_engine_profile_unavailable";
        return false;
    }

    std::vector<CapabilityDescriptor> imported;
    imported.reserve(
        profile->symbols.size()
        + profile->types.size()
        + profile->fields.size()
    );
    for (const engine::SymbolDescriptor& symbol : profile->symbols)
    {
        CapabilityDescriptor descriptor;
        descriptor.id = "engine.symbol." + std::string(symbol.name);
        descriptor.provider = std::string(EngineProvider);
        descriptor.kind = CapabilityKind::EngineSymbol;
        descriptor.access = CapabilityAccess::ControlFlow;
        descriptor.version = profile->version.id;
        descriptor.requiredSymbols.push_back(symbol.id);
        imported.push_back(std::move(descriptor));
    }
    for (const engine::TypeDescriptor& type : profile->types)
    {
        CapabilityDescriptor descriptor;
        descriptor.id = "engine.type." + std::string(type.name);
        descriptor.provider = std::string(EngineProvider);
        descriptor.kind = CapabilityKind::EngineType;
        descriptor.access = CapabilityAccess::Metadata;
        descriptor.version = profile->version.id;
        descriptor.requiredTypes.push_back(type.id);
        imported.push_back(std::move(descriptor));
    }
    for (const engine::FieldDescriptor& field : profile->fields)
    {
        CapabilityDescriptor descriptor;
        descriptor.id = "engine.field." + std::string(field.name);
        descriptor.provider = std::string(EngineProvider);
        descriptor.kind = CapabilityKind::EngineField;
        descriptor.access = FieldCapabilityAccess(field.access);
        descriptor.version = profile->version.id;
        descriptor.requiredFields.push_back(field.id);
        imported.push_back(std::move(descriptor));
    }

    for (CapabilityDescriptor& descriptor : imported)
    {
        if (!Register(std::move(descriptor), error))
        {
            UnregisterProvider(EngineProvider);
            return false;
        }
    }
    error.clear();
    return true;
}

std::optional<CapabilitySnapshot> CapabilityRegistry::Query(
    std::string_view id,
    const engine::EngineRegistry* engine
) const
{
    const std::string normalized = NormalizeCapabilityId(id);
    CapabilityDescriptor descriptor;
    std::string invalidation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = descriptors_.find(normalized);
        if (found == descriptors_.end())
        {
            return std::nullopt;
        }
        descriptor = found->second;
        const auto invalid = invalidations_.find(normalized);
        if (invalid != invalidations_.end())
        {
            invalidation = invalid->second;
        }
    }
    return Evaluate(descriptor, engine, invalidation);
}

std::vector<CapabilitySnapshot> CapabilityRegistry::Snapshot(
    const engine::EngineRegistry* engine
) const
{
    std::vector<std::pair<CapabilityDescriptor, std::string>> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries.reserve(descriptors_.size());
        for (const auto& entry : descriptors_)
        {
            const auto invalid = invalidations_.find(entry.first);
            entries.emplace_back(
                entry.second,
                invalid == invalidations_.end()
                    ? std::string{}
                    : invalid->second
            );
        }
    }
    std::vector<CapabilitySnapshot> output;
    output.reserve(entries.size());
    for (const auto& entry : entries)
    {
        output.push_back(Evaluate(entry.first, engine, entry.second));
    }
    std::sort(
        output.begin(),
        output.end(),
        [](const CapabilitySnapshot& left, const CapabilitySnapshot& right)
        {
            return left.descriptor.id < right.descriptor.id;
        }
    );
    return output;
}

bool CapabilityRegistry::Contains(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return descriptors_.find(NormalizeCapabilityId(id))
        != descriptors_.end();
}

CapabilitySnapshot CapabilityRegistry::Evaluate(
    const CapabilityDescriptor& descriptor,
    const engine::EngineRegistry* engine,
    std::string_view invalidationReason
) const
{
    CapabilitySnapshot snapshot;
    snapshot.descriptor = descriptor;
    if (!invalidationReason.empty())
    {
        snapshot.availability = CapabilityAvailability::Invalid;
        snapshot.reason.assign(invalidationReason);
        return snapshot;
    }
    const bool requiresEngine = descriptor.version.has_value()
        || !descriptor.requiredSymbols.empty()
        || !descriptor.requiredTypes.empty()
        || !descriptor.requiredFields.empty();
    if (!requiresEngine)
    {
        snapshot.availability = CapabilityAvailability::Available;
        snapshot.reason = "registered";
        return snapshot;
    }
    if (!engine || !engine->IsActive() || !engine->ActiveVersion())
    {
        snapshot.availability = CapabilityAvailability::Unsupported;
        snapshot.reason = "engine_profile_unavailable";
        return snapshot;
    }
    if (descriptor.version
        && engine->ActiveVersion()->id != *descriptor.version)
    {
        snapshot.availability = CapabilityAvailability::Unsupported;
        snapshot.reason = "engine_version_mismatch";
        return snapshot;
    }
    for (const engine::SymbolId id : descriptor.requiredSymbols)
    {
        if (!engine->FindSymbol(id) || !engine->Resolve(id))
        {
            snapshot.availability = CapabilityAvailability::Unsupported;
            snapshot.reason = "required_symbol_missing";
            return snapshot;
        }
        if (engine->SymbolValidation(id)
            == engine::SymbolValidationState::Invalid)
        {
            snapshot.availability = CapabilityAvailability::Invalid;
            snapshot.reason = "required_symbol_invalid";
            return snapshot;
        }
    }
    for (const engine::TypeId id : descriptor.requiredTypes)
    {
        if (!engine->FindType(id))
        {
            snapshot.availability = CapabilityAvailability::Unsupported;
            snapshot.reason = "required_type_missing";
            return snapshot;
        }
    }
    for (const engine::FieldId id : descriptor.requiredFields)
    {
        if (!engine->FindField(id))
        {
            snapshot.availability = CapabilityAvailability::Unsupported;
            snapshot.reason = "required_field_missing";
            return snapshot;
        }
    }
    snapshot.availability = CapabilityAvailability::Available;
    snapshot.reason = "available";
    return snapshot;
}

CapabilityRegistry& GetCapabilityRegistry()
{
    static CapabilityRegistry registry;
    return registry;
}

std::string NormalizeCapabilityId(std::string_view value)
{
    const auto begin = std::find_if_not(
        value.begin(), value.end(), [](unsigned char character)
        {
            return std::isspace(character) != 0;
        }
    );
    const auto end = std::find_if_not(
        value.rbegin(), value.rend(), [](unsigned char character)
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
        normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    return normalized;
}

#define CAPABILITY_NAME_FUNCTION(functionName, enumType, ...) \
const char* functionName(enumType value) \
{ \
    switch (value) \
    { \
        __VA_ARGS__ \
    } \
    return "unknown"; \
}

CAPABILITY_NAME_FUNCTION(CapabilityKindName, CapabilityKind,
    case CapabilityKind::EngineSymbol: return "engine_symbol";
    case CapabilityKind::EngineType: return "engine_type";
    case CapabilityKind::EngineField: return "engine_field";
    case CapabilityKind::ObjectResolver: return "object_resolver";
    case CapabilityKind::NativeQuery: return "native_query";
    case CapabilityKind::NativeEffect: return "native_effect";
    case CapabilityKind::Hook: return "hook";
    case CapabilityKind::Module: return "module";)

CAPABILITY_NAME_FUNCTION(CapabilityAccessName, CapabilityAccess,
    case CapabilityAccess::Metadata: return "metadata";
    case CapabilityAccess::Read: return "read";
    case CapabilityAccess::Write: return "write";
    case CapabilityAccess::ControlFlow: return "control_flow";)

CAPABILITY_NAME_FUNCTION(CapabilityAvailabilityName, CapabilityAvailability,
    case CapabilityAvailability::Registered: return "registered";
    case CapabilityAvailability::Available: return "available";
    case CapabilityAvailability::Unsupported: return "unsupported";
    case CapabilityAvailability::Invalid: return "invalid";)

CAPABILITY_NAME_FUNCTION(CapabilityRollbackName, CapabilityRollback,
    case CapabilityRollback::NotApplicable: return "not_applicable";
    case CapabilityRollback::None: return "none";
    case CapabilityRollback::Conditional: return "conditional";
    case CapabilityRollback::Guaranteed: return "guaranteed";)

CAPABILITY_NAME_FUNCTION(CapabilityPersistenceName, CapabilityPersistence,
    case CapabilityPersistence::Unknown: return "unknown";
    case CapabilityPersistence::None: return "none";
    case CapabilityPersistence::Session: return "session";
    case CapabilityPersistence::SaveGame: return "save_game";)

CAPABILITY_NAME_FUNCTION(CapabilityMultiplayerName, CapabilityMultiplayer,
    case CapabilityMultiplayer::Unknown: return "unknown";
    case CapabilityMultiplayer::Unsafe: return "unsafe";
    case CapabilityMultiplayer::Deterministic: return "deterministic";)

#undef CAPABILITY_NAME_FUNCTION

}
