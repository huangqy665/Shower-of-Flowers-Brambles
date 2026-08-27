#pragma once

#include <variant>
#include <vector>

#include "mechanism_command.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_instance_store.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_transaction.hpp"

namespace dillen::kernel {

using WorldCommandPayload = std::variant<MechanismCommand>;

struct WorldCommand
{
    WorldCommandPayload payload;

    static WorldCommand Mechanism(MechanismCommand command);
};

struct WorldTransaction
{
    std::vector<WorldCommand> commands;

    static WorldTransaction FromMechanismCommands(
        std::vector<MechanismCommand> commands
    );
};

enum class WorldTransactionStatus
{
    Committed,
    MechanismRejected,
    TickRegression
};

struct WorldTransactionResult
{
    WorldTransactionStatus status = WorldTransactionStatus::Committed;
    MechanismTransactionResult mechanism;

    explicit operator bool() const noexcept;
};

WorldTransactionResult ApplyWorldTransaction(
    MechanismInstanceStore& mechanisms,
    const WorldTransaction& transaction,
    const MechanismDefinitionRegistry& definitions,
    const MechanismSchemaRegistry& schemas,
    std::uint64_t currentTick
);

}
