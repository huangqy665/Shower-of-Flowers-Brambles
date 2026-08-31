#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "authoritative_world.hpp"
#include "mechanism_query_snapshot.hpp"

namespace dillen::runtime {

struct WorldQueryStamp
{
    std::uint64_t publication = 0;
    std::uint64_t tick = 0;
    std::uint64_t revision = 0;
};

// Each sub-snapshot holds its store by value. The stores are copy-on-write, so
// publishing is a refcount bump instead of a deep copy plus an index rebuild --
// which made every tick cost O(world) even when the tick changed nothing. The
// snapshot copy is frozen by construction: the Kernel Runtime never writes
// through it, and any write to the authoritative store clones away from the
// payload the snapshot holds.
//
// This is only sound because the stores now maintain every index a snapshot
// exposes, in the order the snapshot used to rebuild them (ascending id, see
// kernel/sorted_id_index.hpp). Rebuilding lazily inside the snapshot is not an
// option: worker threads read published snapshots during algorithm dispatch
// (memo section 3.9), so a lazily-filled index would be a data race.
class EntityQuerySnapshot
{
public:
    using EntityMap = world::EntityRegistry::EntityMap;

    std::size_t Size() const noexcept;
    const world::EntityRecord* Find(kernel::EntityId id) const;
    const std::vector<kernel::EntityId>& FindByDefinition(
        kernel::EntityDefinitionId definition
    ) const;
    const std::vector<kernel::EntityId>& FindByType(
        kernel::EntityTypeId type
    ) const;
    const EntityMap& All() const noexcept;

private:
    friend class WorldQuerySnapshot;

    void Publish(const world::EntityRegistry& entities);

    world::EntityRegistry entities_;
};

class ComponentQuerySnapshot
{
public:
    using ComponentMap = world::ComponentStore::ComponentMap;

    std::size_t Size() const noexcept;
    const world::ComponentRecord* Find(
        kernel::EntityId owner,
        kernel::ComponentTypeId type
    ) const;
    const kernel::MechanismValue* FindField(
        kernel::EntityId owner,
        kernel::ComponentTypeId type,
        kernel::ComponentFieldSlotId field
    ) const;
    const std::vector<kernel::EntityId>& FindOwners(
        kernel::ComponentTypeId type
    ) const;
    const std::vector<kernel::ComponentTypeId>& FindTypes(
        kernel::EntityId owner
    ) const;
    const ComponentMap& All() const noexcept;

private:
    friend class WorldQuerySnapshot;

    void Publish(const world::ComponentStore& components);

    world::ComponentStore components_;
};

class RelationQuerySnapshot
{
public:
    using RelationMap = world::RelationIndex::RelationMap;

    std::size_t Size() const noexcept;
    const world::RelationRecord* Find(kernel::RelationId id) const;
    const std::vector<kernel::RelationId>& FindByType(
        kernel::RelationTypeId type
    ) const;
    const std::vector<kernel::RelationId>& Outgoing(
        kernel::RelationTypeId type,
        kernel::EntityId source
    ) const;
    const std::vector<kernel::RelationId>& Incoming(
        kernel::RelationTypeId type,
        kernel::EntityId target
    ) const;
    const RelationMap& All() const noexcept;

private:
    friend class WorldQuerySnapshot;

    void Publish(const world::RelationIndex& relations);

    world::RelationIndex relations_;
};

class WorldQuerySnapshot
{
public:
    bool IsPublished() const noexcept;
    const WorldQueryStamp& Stamp() const noexcept;
    std::uint64_t Publication() const noexcept;
    std::uint64_t Tick() const noexcept;
    std::uint64_t Revision() const noexcept;
    const EntityQuerySnapshot& Entities() const noexcept;
    const ComponentQuerySnapshot& Components() const noexcept;
    const RelationQuerySnapshot& Relations() const noexcept;
    const kernel::MechanismQuerySnapshot& Mechanisms() const noexcept;

private:
    friend class WorldQueryService;

    void Publish(
        const world::AuthoritativeWorld& world,
        std::uint64_t publication
    );

    WorldQueryStamp stamp_;
    EntityQuerySnapshot entities_;
    ComponentQuerySnapshot components_;
    RelationQuerySnapshot relations_;
    kernel::MechanismQuerySnapshot mechanisms_;
    bool published_ = false;
};

using WorldQuerySnapshotHandle =
    std::shared_ptr<const WorldQuerySnapshot>;

class WorldQueryService
{
public:
    WorldQueryService() = default;
    WorldQueryService(const WorldQueryService&) = delete;
    WorldQueryService& operator=(const WorldQueryService&) = delete;

    WorldQuerySnapshotHandle Publish(
        const world::AuthoritativeWorld& world
    );
    WorldQuerySnapshotHandle Acquire() const noexcept;

private:
    WorldQuerySnapshotHandle current_;
    std::uint64_t nextPublication_ = 1;
};

}
