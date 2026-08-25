#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core::engine
{

enum class VersionId
{
    Unknown,
    Hoi3Tfh402D328
};

enum class RegistryState
{
    Empty,
    Active,
    Unsupported,
    Invalid
};

enum class SymbolKind
{
    Global,
    Function,
    HookCallSite,
    PatchSite,
    ReturnAddress
};

enum class CallingConvention
{
    None,
    Cdecl,
    Stdcall,
    Thiscall,
    Custom
};

enum class Confidence
{
    Candidate,
    Confirmed,
    Proven
};

enum class SymbolValidationState
{
    Unchecked,
    Valid,
    Invalid
};

enum class TypeId
{
    GameState,
    CountryDatabase,
    Country,
    Province,
    GoodsPool,
    ResolvedCountry,
    ModifierCache,
    ModifierListNode,
    ModifierRecord,
    Ideology,
    SpyPresence,
    Relation,
    OwnedProvinceNode,
    ProvinceCoreNode,
    BuildingDefinition,
    BuildingRecord,
    TechnologyDefinition,
    TechnologyStatus,
    ResearchNode,
    Unit,
    UnitListNode,
    Leader,
    LeaderRegistryNode,
    Combatant,
    Combat,
    CombatSide,
    WinnerCountryNode,
    EngineRules,
    CountryTagValue,
    CountryEffectObject,
    CountryEffectScope,
    IdeologyValuesEffectObject,
    ThreatEffectObject,
    ProvinceAddEffectObject,
    ProvinceRemoveEffectObject,
    ProvinceEffectScope,
    TargetedProvinceEffectScope,
    ProvinceCountryEffectObject,
    AddCoreEffectObject,
    RemoveCoreEffectObject,
    BuildingLevelCommandObject,
    CountryTechnologyCommandObject,
    TechnologyLevelCommandObject,
    ResearchProgressCommandObject,
    TechnologyInvestmentEffectObject,
    CapitalEffectObject,
    NativeBorrowedString32,
    GlobalFlagEffectObject,
    NativeEventScopeObject,
    NativeDecisionCommandObject,
    NativeEventLookupKey,
    D3d9FrameContext
};

enum class LayoutValueKind
{
    Offset,
    Size,
    Stride,
    Constant
};

enum class FieldAccess
{
    ReadOnly,
    ReadWrite
};

enum class FieldSemantics
{
    Unknown,
    Pointer,
    EmbeddedObject,
    StableTag,
    StableId,
    CountryIndex,
    ProvinceId,
    Index,
    Count,
    Boolean,
    FixedPoint,
    DateValue,
    ByteCount
};

enum class ObjectLifetime
{
    Process,
    Session
};

enum class SymbolId
{
#define HOI3_SYMBOL(id, member, name, rva, kind, call, confidence) id,
#define HOI3_FIELD(id, name, type, value, size, kind, access, semantics, lifetime)
#include "engine_schema_hoi3_tfh_402.inc"
    Count
};

enum class FieldId
{
#define HOI3_SYMBOL(id, member, name, rva, kind, call, confidence)
#define HOI3_FIELD(id, name, type, value, size, kind, access, semantics, lifetime) id,
#include "engine_schema_hoi3_tfh_402.inc"
    Count
};

struct ExecutableIdentity
{
    uint16_t machine = 0;
    uint32_t timestamp = 0;
    uint32_t imageSize = 0;
    uint32_t checksum = 0;
};

struct VersionDescriptor
{
    VersionId id = VersionId::Unknown;
    std::string_view name;
    ExecutableIdentity executable;
};

struct SymbolDescriptor
{
    SymbolId id = SymbolId::Count;
    std::string_view name;
    std::uintptr_t rva = 0;
    SymbolKind kind = SymbolKind::Global;
    CallingConvention callingConvention = CallingConvention::None;
    Confidence confidence = Confidence::Candidate;
    std::vector<uint8_t> signature;
    std::optional<SymbolId> expectedCallTarget;
};

struct FieldDescriptor
{
    FieldId id = FieldId::Count;
    std::string_view name;
    TypeId owner = TypeId::GameState;
    std::uintptr_t value = 0;
    std::size_t size = 0;
    LayoutValueKind kind = LayoutValueKind::Offset;
    FieldAccess access = FieldAccess::ReadOnly;
    FieldSemantics semantics = FieldSemantics::Unknown;
    ObjectLifetime lifetime = ObjectLifetime::Session;
};

struct TypeDescriptor
{
    TypeId id = TypeId::GameState;
    std::string_view name;
    ObjectLifetime lifetime = ObjectLifetime::Session;
    std::size_t size = 0;
};

struct VersionProfile
{
    VersionDescriptor version;
    std::vector<SymbolDescriptor> symbols;
    std::vector<TypeDescriptor> types;
    std::vector<FieldDescriptor> fields;
};

struct ObjectHandle
{
    TypeId type = TypeId::GameState;
    uint64_t stableId = 0;
    std::string stableName;
    std::uintptr_t address = 0;
    uint64_t lifecycleGeneration = 0;
};

class EngineRegistry
{
public:
    EngineRegistry();

    bool InitializeCurrentProcess(std::string& error);
    bool SelectVersion(
        const ExecutableIdentity& identity,
        std::uintptr_t moduleBase,
        std::string& error
    );
    void Invalidate(std::string reason);

    RegistryState State() const;
    bool IsActive() const;
    const VersionProfile* ActiveProfile() const;
    const VersionDescriptor* ActiveVersion() const;
    ExecutableIdentity Identity() const;
    std::uintptr_t ModuleBase() const;

    const SymbolDescriptor* FindSymbol(SymbolId id) const;
    const SymbolDescriptor* FindSymbol(std::string_view name) const;
    std::uintptr_t SymbolRva(SymbolId id) const;
    std::uintptr_t Resolve(SymbolId id) const;
    bool ValidateSymbol(SymbolId id, std::string& error) const;
    void InvalidateSymbol(SymbolId id, std::string reason);
    SymbolValidationState SymbolValidation(SymbolId id) const;

    const TypeDescriptor* FindType(TypeId id) const;
    const TypeDescriptor* FindType(std::string_view name) const;
    const FieldDescriptor* FindField(FieldId id) const;
    const FieldDescriptor* FindField(std::string_view name) const;
    std::uintptr_t FieldValue(FieldId id) const;

    void ObserveLifecycleGeneration(uint64_t generation);
    uint64_t LifecycleGeneration() const;
    ObjectHandle MakeHandle(
        TypeId type,
        uint64_t stableId,
        std::uintptr_t address
    ) const;
    bool IsHandleCurrent(const ObjectHandle& handle) const;

    std::string LastError() const;

private:
    mutable std::mutex mutex_;
    std::atomic<RegistryState> state_{RegistryState::Empty};
    std::atomic<const VersionProfile*> profile_{nullptr};
    ExecutableIdentity identity_{};
    std::atomic<std::uintptr_t> moduleBase_{0};
    std::atomic<uint64_t> lifecycleGeneration_{0};
    mutable std::array<
        std::atomic<SymbolValidationState>,
        static_cast<std::size_t>(SymbolId::Count)
    > symbolValidation_{};
    std::string lastError_;
};

EngineRegistry& GetEngineRegistry();
const VersionProfile& Hoi3Tfh402D328Profile();

class SymbolRef
{
public:
    constexpr explicit SymbolRef(SymbolId id) : id_(id) {}
    operator std::uintptr_t() const noexcept;
    SymbolId Id() const noexcept { return id_; }

private:
    SymbolId id_;
};

class FieldRef
{
public:
    constexpr explicit FieldRef(FieldId id) : id_(id) {}
    operator std::uintptr_t() const noexcept;
    FieldId Id() const noexcept { return id_; }

private:
    FieldId id_;
};

struct SymbolReferences
{
#define HOI3_SYMBOL(id, member, name, rva, kind, call, confidence) SymbolRef member{SymbolId::id};
#define HOI3_FIELD(id, name, type, value, size, kind, access, semantics, lifetime)
#include "engine_schema_hoi3_tfh_402.inc"
};

inline constexpr SymbolReferences Symbols{};

namespace field
{
#define HOI3_SYMBOL(id, member, name, rva, kind, call, confidence)
#define HOI3_FIELD(id, name, type, value, size, kind, access, semantics, lifetime) inline constexpr FieldRef id{FieldId::id};
#include "engine_schema_hoi3_tfh_402.inc"
}

}
