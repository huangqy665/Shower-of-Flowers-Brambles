#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kernel_runtime.hpp"
#include "runtime_migration.hpp"
#include "runtime_save_codec.hpp"

namespace dillen::persistence {

enum class RuntimePersistenceStatus
{
    Completed,
    RuntimeCatalogNotFrozen,
    IdentityMismatch,
    InvalidWorldState,
    InvalidSequenceState,
    InvalidQueuedCommand,
    MigrationFailed,
    CodecFailed
};

struct RuntimePersistenceReport
{
    RuntimePersistenceStatus status = RuntimePersistenceStatus::Completed;
    std::string message;
    RuntimeMigrationReport migration;
    RuntimeSaveCodecReport codec;

    explicit operator bool() const noexcept;
};

class RuntimePersistenceService
{
public:
    static RuntimeSaveIdentity IdentityFor(
        const kernel::FrozenRuntimeCatalog& catalog
    );

    RuntimePersistenceReport Capture(
        const runtime::KernelRuntime& runtime,
        RuntimeSaveImage& output
    ) const;
    RuntimePersistenceReport Save(
        const runtime::KernelRuntime& runtime,
        std::vector<std::uint8_t>& output
    ) const;
    RuntimePersistenceReport Restore(
        runtime::KernelRuntime& runtime,
        RuntimeSaveImage image,
        const RuntimeMigrationRegistry* migrations = nullptr
    ) const;
    RuntimePersistenceReport Load(
        runtime::KernelRuntime& runtime,
        const std::vector<std::uint8_t>& bytes,
        const RuntimeMigrationRegistry* migrations = nullptr
    ) const;

private:
    static bool BuildCandidate(
        const RuntimeSaveImage& image,
        const kernel::FrozenRuntimeCatalog& catalog,
        world::AuthoritativeWorld& world,
        kernel::WorldCommandQueue& commands,
        std::uint64_t& nextFactSequence,
        std::string& message
    );
};

}
