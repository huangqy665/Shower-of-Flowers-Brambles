#pragma once

#include <cstdint>

#include "authoritative_world.hpp"
#include "frozen_runtime_catalog.hpp"
#include "world_transaction.hpp"

namespace dillen::world {

class WorldTransactionExecutor
{
public:
    kernel::WorldTransactionResult Apply(
        AuthoritativeWorld& world,
        const kernel::WorldTransaction& transaction,
        const kernel::FrozenRuntimeCatalog& catalog,
        std::uint64_t currentTick
    ) const;
};

}
