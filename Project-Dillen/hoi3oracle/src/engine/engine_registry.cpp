#include "engine_registry.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>

namespace core::engine
{
namespace
{

bool TryCopyMemory(
    const void* source,
    void* destination,
    std::size_t size
) noexcept
{
    if (!source || !destination)
    {
        return false;
    }
#if defined(_MSC_VER)
    __try
    {
        std::memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    std::memcpy(destination, source, size);
    return true;
#endif
}

bool InspectExecutable(
    const uint8_t* base,
    ExecutableIdentity& identity
) noexcept
{
    identity = {};
    IMAGE_DOS_HEADER dos{};
    if (!TryCopyMemory(base, &dos, sizeof(dos))
        || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew <= 0)
    {
        return false;
    }

    IMAGE_NT_HEADERS32 nt{};
    if (!TryCopyMemory(base + dos.e_lfanew, &nt, sizeof(nt))
        || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        return false;
    }
    identity.machine = nt.FileHeader.Machine;
    identity.timestamp = nt.FileHeader.TimeDateStamp;
    identity.imageSize = nt.OptionalHeader.SizeOfImage;
    identity.checksum = nt.OptionalHeader.CheckSum;
    return true;
}

bool Matches(
    const ExecutableIdentity& actual,
    const ExecutableIdentity& expected
) noexcept
{
    return actual.machine == expected.machine
        && actual.timestamp == expected.timestamp
        && actual.imageSize == expected.imageSize
        && (expected.checksum == 0
            || actual.checksum == expected.checksum);
}

template <typename Container, typename Predicate>
const typename Container::value_type* FindDescriptor(
    const Container& values,
    Predicate predicate
)
{
    const auto iterator = std::find_if(
        values.begin(),
        values.end(),
        predicate
    );
    return iterator == values.end() ? nullptr : &*iterator;
}

}

EngineRegistry::EngineRegistry()
{
    for (auto& state : symbolValidation_)
    {
        state.store(
            SymbolValidationState::Unchecked,
            std::memory_order_relaxed
        );
    }
}

bool EngineRegistry::InitializeCurrentProcess(std::string& error)
{
    const auto base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr)
    );
    ExecutableIdentity identity{};
    if (!base
        || !InspectExecutable(
            reinterpret_cast<const uint8_t*>(base),
            identity
        ))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.store(RegistryState::Invalid, std::memory_order_release);
        profile_.store(nullptr, std::memory_order_release);
        identity_ = {};
        moduleBase_.store(0, std::memory_order_release);
        lastError_ = "engine_executable_inspection_failed";
        error = lastError_;
        return false;
    }
    return SelectVersion(identity, base, error);
}

bool EngineRegistry::SelectVersion(
    const ExecutableIdentity& identity,
    std::uintptr_t moduleBase,
    std::string& error
)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const VersionProfile* active = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            == RegistryState::Active
        && active
        && moduleBase_.load(std::memory_order_acquire) == moduleBase
        && Matches(identity, active->version.executable))
    {
        error.clear();
        return true;
    }

    identity_ = identity;
    const VersionProfile* candidates[] = {
        &Hoi3Tfh402D328Profile()
    };
    const VersionProfile* selected = nullptr;
    for (const VersionProfile* candidate : candidates)
    {
        if (candidate
            && Matches(identity, candidate->version.executable))
        {
            selected = candidate;
            break;
        }
    }
    if (!selected)
    {
        state_.store(RegistryState::Unsupported, std::memory_order_release);
        profile_.store(nullptr, std::memory_order_release);
        moduleBase_.store(0, std::memory_order_release);
        lastError_ = "engine_unsupported_executable";
        error = lastError_;
        return false;
    }

    moduleBase_.store(moduleBase, std::memory_order_release);
    profile_.store(selected, std::memory_order_release);
    for (auto& validation : symbolValidation_)
    {
        validation.store(
            SymbolValidationState::Unchecked,
            std::memory_order_release
        );
    }
    state_.store(RegistryState::Active, std::memory_order_release);
    lastError_.clear();
    error.clear();
    return true;
}

void EngineRegistry::Invalidate(std::string reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.store(RegistryState::Invalid, std::memory_order_release);
    profile_.store(nullptr, std::memory_order_release);
    moduleBase_.store(0, std::memory_order_release);
    lifecycleGeneration_.fetch_add(1, std::memory_order_acq_rel);
    for (auto& validation : symbolValidation_)
    {
        validation.store(
            SymbolValidationState::Invalid,
            std::memory_order_release
        );
    }
    lastError_ = std::move(reason);
}

RegistryState EngineRegistry::State() const
{
    return state_.load(std::memory_order_acquire);
}

bool EngineRegistry::IsActive() const
{
    return State() == RegistryState::Active;
}

const VersionProfile* EngineRegistry::ActiveProfile() const
{
    return state_.load(std::memory_order_acquire)
            == RegistryState::Active
        ? profile_.load(std::memory_order_acquire)
        : nullptr;
}

const VersionDescriptor* EngineRegistry::ActiveVersion() const
{
    const VersionProfile* profile = ActiveProfile();
    return profile ? &profile->version : nullptr;
}

ExecutableIdentity EngineRegistry::Identity() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return identity_;
}

std::uintptr_t EngineRegistry::ModuleBase() const
{
    return state_.load(std::memory_order_acquire)
            == RegistryState::Active
        ? moduleBase_.load(std::memory_order_acquire)
        : 0;
}

const SymbolDescriptor* EngineRegistry::FindSymbol(SymbolId id) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile)
    {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(id);
    return index < profile->symbols.size()
            && profile->symbols[index].id == id
        ? &profile->symbols[index]
        : nullptr;
}

const SymbolDescriptor* EngineRegistry::FindSymbol(
    std::string_view name
) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile)
    {
        return nullptr;
    }
    return FindDescriptor(
        profile->symbols,
        [name](const SymbolDescriptor& value)
        {
            return value.name == name;
        }
    );
}

std::uintptr_t EngineRegistry::SymbolRva(SymbolId id) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    const std::size_t index = static_cast<std::size_t>(id);
    return state_.load(std::memory_order_acquire)
                == RegistryState::Active
            && profile
            && index < profile->symbols.size()
            && profile->symbols[index].id == id
            && symbolValidation_[index].load(
                std::memory_order_acquire
            ) != SymbolValidationState::Invalid
        ? profile->symbols[index].rva
        : 0;
}

std::uintptr_t EngineRegistry::Resolve(SymbolId id) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    const std::uintptr_t moduleBase = moduleBase_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile
        || !moduleBase)
    {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(id);
    const SymbolDescriptor* symbol = index < profile->symbols.size()
            && profile->symbols[index].id == id
            && symbolValidation_[index].load(
                std::memory_order_acquire
            ) != SymbolValidationState::Invalid
        ? &profile->symbols[index]
        : nullptr;
    return symbol ? moduleBase + symbol->rva : 0;
}

bool EngineRegistry::ValidateSymbol(
    SymbolId id,
    std::string& error
) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    const std::uintptr_t moduleBase = moduleBase_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile
        || !moduleBase)
    {
        error = "engine_registry_inactive";
        return false;
    }
    const SymbolDescriptor* symbol = FindDescriptor(
        profile->symbols,
        [id](const SymbolDescriptor& value)
        {
            return value.id == id;
        }
    );
    const std::size_t symbolIndex = static_cast<std::size_t>(id);
    const auto fail = [this, symbolIndex, &error](const char* message)
    {
        if (symbolIndex < symbolValidation_.size())
        {
            symbolValidation_[symbolIndex].store(
                SymbolValidationState::Invalid,
                std::memory_order_release
            );
        }
        error = message;
        return false;
    };
    if (!symbol || symbol->rva >= identity_.imageSize)
    {
        return fail("engine_symbol_unavailable");
    }
    if (!symbol->signature.empty())
    {
        if (symbol->signature.size() > identity_.imageSize - symbol->rva)
        {
            return fail("engine_symbol_signature_out_of_range");
        }
        std::vector<uint8_t> actual(symbol->signature.size());
        if (!TryCopyMemory(
                reinterpret_cast<const void*>(moduleBase + symbol->rva),
                actual.data(),
                actual.size()
            )
            || actual != symbol->signature)
        {
            return fail("engine_symbol_signature_mismatch");
        }
    }
    if (symbol->expectedCallTarget)
    {
        const SymbolDescriptor* target = FindDescriptor(
            profile->symbols,
            [symbol](const SymbolDescriptor& value)
            {
                return value.id == *symbol->expectedCallTarget;
            }
        );
        uint8_t instruction[5]{};
        if (!target
            || symbol->rva > identity_.imageSize - sizeof(instruction)
            || !TryCopyMemory(
                reinterpret_cast<const void*>(moduleBase + symbol->rva),
                instruction,
                sizeof(instruction)
            )
            || instruction[0] != 0xE8)
        {
            return fail("engine_call_site_invalid");
        }
        int32_t relative = 0;
        std::memcpy(&relative, instruction + 1, sizeof(relative));
        const int64_t actualTarget = static_cast<int64_t>(symbol->rva)
            + static_cast<int64_t>(sizeof(instruction))
            + static_cast<int64_t>(relative);
        if (actualTarget < 0
            || static_cast<std::uintptr_t>(actualTarget) != target->rva)
        {
            return fail("engine_call_target_mismatch");
        }
    }
    symbolValidation_[symbolIndex].store(
        SymbolValidationState::Valid,
        std::memory_order_release
    );
    error.clear();
    return true;
}

void EngineRegistry::InvalidateSymbol(SymbolId id, std::string reason)
{
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= symbolValidation_.size())
    {
        return;
    }
    symbolValidation_[index].store(
        SymbolValidationState::Invalid,
        std::memory_order_release
    );
    std::lock_guard<std::mutex> lock(mutex_);
    lastError_ = std::move(reason);
}

SymbolValidationState EngineRegistry::SymbolValidation(SymbolId id) const
{
    const std::size_t index = static_cast<std::size_t>(id);
    return index < symbolValidation_.size()
        ? symbolValidation_[index].load(std::memory_order_acquire)
        : SymbolValidationState::Invalid;
}

const TypeDescriptor* EngineRegistry::FindType(TypeId id) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile)
    {
        return nullptr;
    }
    return FindDescriptor(
        profile->types,
        [id](const TypeDescriptor& value)
        {
            return value.id == id;
        }
    );
}

const TypeDescriptor* EngineRegistry::FindType(std::string_view name) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile)
    {
        return nullptr;
    }
    return FindDescriptor(
        profile->types,
        [name](const TypeDescriptor& value)
        {
            return value.name == name;
        }
    );
}

const FieldDescriptor* EngineRegistry::FindField(FieldId id) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile)
    {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(id);
    return index < profile->fields.size()
            && profile->fields[index].id == id
        ? &profile->fields[index]
        : nullptr;
}

const FieldDescriptor* EngineRegistry::FindField(
    std::string_view name
) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    if (state_.load(std::memory_order_acquire)
            != RegistryState::Active
        || !profile)
    {
        return nullptr;
    }
    return FindDescriptor(
        profile->fields,
        [name](const FieldDescriptor& value)
        {
            return value.name == name;
        }
    );
}

std::uintptr_t EngineRegistry::FieldValue(FieldId id) const
{
    const VersionProfile* profile = profile_.load(
        std::memory_order_acquire
    );
    const std::size_t index = static_cast<std::size_t>(id);
    return state_.load(std::memory_order_acquire)
                == RegistryState::Active
            && profile
            && index < profile->fields.size()
            && profile->fields[index].id == id
        ? profile->fields[index].value
        : 0;
}

void EngineRegistry::ObserveLifecycleGeneration(uint64_t generation)
{
    lifecycleGeneration_.store(generation, std::memory_order_release);
}

uint64_t EngineRegistry::LifecycleGeneration() const
{
    return lifecycleGeneration_.load(std::memory_order_acquire);
}

ObjectHandle EngineRegistry::MakeHandle(
    TypeId type,
    uint64_t stableId,
    std::uintptr_t address
) const
{
    return {
        type,
        stableId,
        {},
        address,
        lifecycleGeneration_.load(std::memory_order_acquire)
    };
}

bool EngineRegistry::IsHandleCurrent(const ObjectHandle& handle) const
{
    return state_.load(std::memory_order_acquire)
            == RegistryState::Active
        && handle.address != 0
        && handle.lifecycleGeneration
            == lifecycleGeneration_.load(std::memory_order_acquire);
}

std::string EngineRegistry::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

EngineRegistry& GetEngineRegistry()
{
    static EngineRegistry registry;
    return registry;
}

SymbolRef::operator std::uintptr_t() const noexcept
{
    return GetEngineRegistry().SymbolRva(id_);
}

FieldRef::operator std::uintptr_t() const noexcept
{
    return GetEngineRegistry().FieldValue(id_);
}

}
