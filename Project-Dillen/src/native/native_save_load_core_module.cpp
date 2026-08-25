#include "native_save_load_core_module.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstring>
#include <limits>
#include <utility>

#include "engine_registry.h"

namespace core
{

std::atomic<NativeSaveLoadCoreModule*>
    NativeSaveLoadCoreModule::active_{nullptr};

namespace
{

bool CopyProtected(
    void* destination,
    const void* source,
    std::size_t size
)
{
    if (!destination || !source || size == 0)
    {
        return false;
    }
    DWORD previousProtection = 0;
    if (!VirtualProtect(
            destination,
            size,
            PAGE_EXECUTE_READWRITE,
            &previousProtection
        ))
    {
        return false;
    }
    std::memcpy(destination, source, size);
    FlushInstructionCache(
        GetCurrentProcess(),
        destination,
        size
    );
    DWORD ignored = 0;
    return VirtualProtect(
        destination,
        size,
        previousProtection,
        &ignored
    ) != FALSE;
}

bool TryRead(
    const void* source,
    void* destination,
    std::size_t size
) noexcept
{
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

}

std::string_view NativeSaveLoadCoreModule::Id() const
{
    return "native_save_load";
}

int NativeSaveLoadCoreModule::Priority() const
{
    return -300;
}

bool NativeSaveLoadCoreModule::Initialize(
    Services& services,
    std::string& error
)
{
    engine_ = services.engine;
    diagnostic_ = services.diagnostic;
    notifyStarted_ = services.notifySaveLoadStarted;
    notifyCompleted_ = services.notifySaveLoaded;

    if (!engine_ || !notifyStarted_ || !notifyCompleted_)
    {
        error = "native_save_load_services_missing";
        return false;
    }
    if (HookDisabled())
    {
        if (diagnostic_)
        {
            diagnostic_(
                "Native SaveLoaded hook disabled by "
                "NEW_CORE_DISABLE_NATIVE_SAVE_LOAD=1"
            );
        }
        error.clear();
        return true;
    }

    HookDefinition hook;
    hook.id = "native_save_load.file_deserialize";
    hook.priority = -1000;
    hook.install = [this](std::string& hookError)
    {
        return Install(hookError);
    };
    hook.uninstall = [this]()
    {
        Uninstall();
    };
    hook.isInstalled = [this]()
    {
        return IsInstalled();
    };
    if (!services.hooks.Register(std::move(hook), error))
    {
        return false;
    }
    error.clear();
    return true;
}

void NativeSaveLoadCoreModule::OnLifecycleEvent(
    const LifecycleEvent&
)
{
}

void NativeSaveLoadCoreModule::Tick(uint64_t)
{
}

void NativeSaveLoadCoreModule::Shutdown()
{
    Uninstall();
    notifyStarted_ = {};
    notifyCompleted_ = {};
    diagnostic_ = {};
    engine_ = nullptr;
}

void __stdcall NativeSaveLoadCoreModule::LoadProxy(
    void* gameState,
    void* source
)
{
    NativeSaveLoadCoreModule* module = active_.load(
        std::memory_order_acquire
    );
    if (!module || !module->original_)
    {
        return;
    }
    const uint64_t sequence = module->sequence_.fetch_add(
        1,
        std::memory_order_acq_rel
    ) + 1;
    const std::string key = "native_save_load:"
        + std::to_string(sequence);
    module->NotifyStarted(key);
    module->original_(gameState, source);
    module->NotifyCompleted(key);
}

bool NativeSaveLoadCoreModule::HookDisabled()
{
    std::array<wchar_t, 16> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"NEW_CORE_DISABLE_NATIVE_SAVE_LOAD",
        value.data(),
        static_cast<DWORD>(value.size())
    );
    return length == 1 && value[0] == L'1';
}

bool NativeSaveLoadCoreModule::Install(std::string& error)
{
    if (installed_.load(std::memory_order_acquire))
    {
        error.clear();
        return true;
    }
    if (!engine_ || !engine_->IsActive())
    {
        error = "native_save_load_engine_registry_inactive";
        return false;
    }
    for (const engine::SymbolId symbol : {
            engine::SymbolId::SaveGameLoadCore,
            engine::SymbolId::SaveFileLoadWrapper,
            engine::SymbolId::SaveFileDeserializeCallSite,
            engine::SymbolId::SaveFileDeserializeReturn
        })
    {
        std::string validationError;
        if (!engine_->ValidateSymbol(symbol, validationError))
        {
            error = "native_save_load_symbol_invalid: "
                + validationError;
            return false;
        }
    }

    callSite_ = engine_->Resolve(
        engine::SymbolId::SaveFileDeserializeCallSite
    );
    original_ = reinterpret_cast<NativeLoadFunction>(
        engine_->Resolve(engine::SymbolId::SaveGameLoadCore)
    );
    if (!callSite_ || !original_)
    {
        error = "native_save_load_candidate_unresolved";
        return false;
    }
    if (!TryRead(
            reinterpret_cast<const void*>(callSite_),
            originalBytes_,
            sizeof(originalBytes_)
        ))
    {
        error = "native_save_load_call_site_unreadable";
        return false;
    }

    const std::intptr_t relative =
        reinterpret_cast<std::intptr_t>(&LoadProxy)
        - static_cast<std::intptr_t>(callSite_ + 5);
    if (relative < std::numeric_limits<int32_t>::min()
        || relative > std::numeric_limits<int32_t>::max())
    {
        error = "native_save_load_proxy_out_of_range";
        return false;
    }
    patchedBytes_[0] = 0xE8;
    const int32_t relative32 = static_cast<int32_t>(relative);
    std::memcpy(
        patchedBytes_ + 1,
        &relative32,
        sizeof(relative32)
    );

    NativeSaveLoadCoreModule* expected = nullptr;
    if (!active_.compare_exchange_strong(
            expected,
            this,
            std::memory_order_acq_rel
        ))
    {
        error = "native_save_load_locator_already_active";
        return false;
    }
    if (!CopyProtected(
            reinterpret_cast<void*>(callSite_),
            patchedBytes_,
            sizeof(patchedBytes_)
        ))
    {
        active_.store(nullptr, std::memory_order_release);
        error = "native_save_load_call_site_patch_failed";
        return false;
    }
    installed_.store(true, std::memory_order_release);
    if (diagnostic_)
    {
        diagnostic_(
            "Native SaveLoaded hook installed at "
            "save_load.file_deserialize_call_site"
        );
    }
    error.clear();
    return true;
}

void NativeSaveLoadCoreModule::Uninstall()
{
    if (!installed_.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    uint8_t current[5]{};
    const bool ownsPatch = TryRead(
            reinterpret_cast<const void*>(callSite_),
            current,
            sizeof(current)
        )
        && std::memcmp(
            current,
            patchedBytes_,
            sizeof(current)
        ) == 0;
    if (ownsPatch)
    {
        CopyProtected(
            reinterpret_cast<void*>(callSite_),
            originalBytes_,
            sizeof(originalBytes_)
        );
    }
    NativeSaveLoadCoreModule* expected = this;
    active_.compare_exchange_strong(
        expected,
        nullptr,
        std::memory_order_acq_rel
    );
    callSite_ = 0;
    original_ = nullptr;
}

bool NativeSaveLoadCoreModule::IsInstalled() const
{
    if (!installed_.load(std::memory_order_acquire)
        || !callSite_)
    {
        return false;
    }
    uint8_t current[5]{};
    return TryRead(
            reinterpret_cast<const void*>(callSite_),
            current,
            sizeof(current)
        )
        && std::memcmp(
            current,
            patchedBytes_,
            sizeof(current)
        ) == 0;
}

void NativeSaveLoadCoreModule::NotifyStarted(
    std::string_view key
) const
{
    if (diagnostic_)
    {
        diagnostic_("Native SaveLoaded hook hit start: "
            + std::string(key));
    }
    notifyStarted_(key, LifecycleEventSource::NativeProbe);
}

void NativeSaveLoadCoreModule::NotifyCompleted(
    std::string_view key
) const
{
    if (diagnostic_)
    {
        diagnostic_("Native SaveLoaded hook hit complete: "
            + std::string(key));
    }
    notifyCompleted_(key, LifecycleEventSource::NativeProbe);
}

}
