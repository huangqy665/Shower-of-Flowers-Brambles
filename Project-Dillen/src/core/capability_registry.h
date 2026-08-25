#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine_registry.h"

namespace core
{

enum class CapabilityKind
{
    EngineSymbol,
    EngineType,
    EngineField,
    ObjectResolver,
    NativeQuery,
    NativeEffect,
    Hook,
    Module
};

enum class CapabilityAccess
{
    Metadata,
    Read,
    Write,
    ControlFlow
};

enum class CapabilityAvailability
{
    Registered,
    Available,
    Unsupported,
    Invalid
};

enum class CapabilityRollback
{
    NotApplicable,
    None,
    Conditional,
    Guaranteed
};

enum class CapabilityPersistence
{
    Unknown,
    None,
    Session,
    SaveGame
};

enum class CapabilityMultiplayer
{
    Unknown,
    Unsafe,
    Deterministic
};

struct CapabilityDescriptor
{
    std::string id;
    std::string provider;
    CapabilityKind kind = CapabilityKind::Module;
    CapabilityAccess access = CapabilityAccess::Metadata;
    CapabilityRollback rollback = CapabilityRollback::NotApplicable;
    CapabilityPersistence persistence = CapabilityPersistence::Unknown;
    CapabilityMultiplayer multiplayer = CapabilityMultiplayer::Unknown;
    std::optional<engine::VersionId> version;
    std::vector<engine::SymbolId> requiredSymbols;
    std::vector<engine::TypeId> requiredTypes;
    std::vector<engine::FieldId> requiredFields;
};

struct CapabilitySnapshot
{
    CapabilityDescriptor descriptor;
    CapabilityAvailability availability =
        CapabilityAvailability::Registered;
    std::string reason;

    bool Available() const
    {
        return availability == CapabilityAvailability::Available;
    }
};

class CapabilityRegistry
{
public:
    bool Register(CapabilityDescriptor descriptor, std::string& error);
    bool Unregister(std::string_view id);
    std::size_t UnregisterProvider(std::string_view provider);
    bool Invalidate(std::string_view id, std::string reason);
    void Clear();

    bool SynchronizeEngineProfile(
        const engine::EngineRegistry& engine,
        std::string& error
    );

    std::optional<CapabilitySnapshot> Query(
        std::string_view id,
        const engine::EngineRegistry* engine = nullptr
    ) const;
    std::vector<CapabilitySnapshot> Snapshot(
        const engine::EngineRegistry* engine = nullptr
    ) const;
    bool Contains(std::string_view id) const;

private:
    CapabilitySnapshot Evaluate(
        const CapabilityDescriptor& descriptor,
        const engine::EngineRegistry* engine,
        std::string_view invalidationReason
    ) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, CapabilityDescriptor> descriptors_;
    std::unordered_map<std::string, std::string> invalidations_;
};

CapabilityRegistry& GetCapabilityRegistry();

std::string NormalizeCapabilityId(std::string_view value);
const char* CapabilityKindName(CapabilityKind value);
const char* CapabilityAccessName(CapabilityAccess value);
const char* CapabilityAvailabilityName(CapabilityAvailability value);
const char* CapabilityRollbackName(CapabilityRollback value);
const char* CapabilityPersistenceName(CapabilityPersistence value);
const char* CapabilityMultiplayerName(CapabilityMultiplayer value);

}
