#pragma once

#include <cstdint>

#include "algorithm_inbox.hpp"
#include "deterministic_rng.hpp"
#include "mechanism_instance_store.hpp"
#include "component_store.hpp"
#include "entity_registry.hpp"
#include "relation_index.hpp"

namespace dillen::runtime {
class KernelRuntime;
}

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::world {

class WorldTransactionExecutor;

class AuthoritativeWorld
{
public:
    AuthoritativeWorld() = default;
    explicit AuthoritativeWorld(
        EntityRegistry entities,
        ComponentStore components,
        RelationIndex relations,
        kernel::MechanismInstanceStore mechanisms,
        kernel::AlgorithmInbox algorithmInbox = {},
        kernel::DeterministicRngRegistry rngStreams = {},
        std::uint64_t tick = 0,
        std::uint64_t revision = 0
    );

    const EntityRegistry& Entities() const noexcept;
    const ComponentStore& Components() const noexcept;
    const RelationIndex& Relations() const noexcept;

    const kernel::MechanismInstanceStore& Mechanisms() const noexcept;
    const kernel::AlgorithmInbox& AlgorithmEvents() const noexcept;
    const kernel::DeterministicRngRegistry& RngStreams() const noexcept;
    std::uint64_t Tick() const noexcept;
    std::uint64_t Revision() const noexcept;

private:
    friend class runtime::KernelRuntime;
    friend class persistence::RuntimePersistenceService;
    friend class WorldTransactionExecutor;

    EntityRegistry entities_;
    ComponentStore components_;
    RelationIndex relations_;
    kernel::MechanismInstanceStore mechanisms_;
    kernel::AlgorithmInbox algorithmInbox_;
    kernel::DeterministicRngRegistry rngStreams_;
    std::uint64_t tick_ = 0;
    std::uint64_t revision_ = 0;
};

}
