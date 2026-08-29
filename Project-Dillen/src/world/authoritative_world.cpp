#include "authoritative_world.hpp"

#include <utility>

namespace dillen::world {

AuthoritativeWorld::AuthoritativeWorld(
    EntityRegistry entities,
    ComponentStore components,
    RelationIndex relations,
    kernel::MechanismInstanceStore mechanisms,
    kernel::AlgorithmInbox algorithmInbox,
    kernel::DeterministicRngRegistry rngStreams,
    std::uint64_t tick,
    std::uint64_t revision
)
    : entities_(std::move(entities)),
      components_(std::move(components)),
      relations_(std::move(relations)),
      mechanisms_(std::move(mechanisms)),
      algorithmInbox_(std::move(algorithmInbox)),
      rngStreams_(std::move(rngStreams)),
      tick_(tick),
      revision_(revision)
{
}

const EntityRegistry& AuthoritativeWorld::Entities() const noexcept
{
    return entities_;
}

const ComponentStore& AuthoritativeWorld::Components() const noexcept
{
    return components_;
}

const RelationIndex& AuthoritativeWorld::Relations() const noexcept
{
    return relations_;
}

const kernel::MechanismInstanceStore&
AuthoritativeWorld::Mechanisms() const noexcept
{
    return mechanisms_;
}

const kernel::AlgorithmInbox&
AuthoritativeWorld::AlgorithmEvents() const noexcept
{
    return algorithmInbox_;
}

const kernel::DeterministicRngRegistry&
AuthoritativeWorld::RngStreams() const noexcept
{
    return rngStreams_;
}

std::uint64_t AuthoritativeWorld::Tick() const noexcept
{
    return tick_;
}

std::uint64_t AuthoritativeWorld::Revision() const noexcept
{
    return revision_;
}

}
