#include "world_transaction.hpp"

#include <utility>

namespace dillen::kernel {

WorldCommand WorldCommand::Mechanism(MechanismCommand command)
{
    return {std::move(command)};
}

WorldTransaction WorldTransaction::FromMechanismCommands(
    std::vector<MechanismCommand> commands
)
{
    WorldTransaction transaction;
    transaction.commands.reserve(commands.size());
    for (MechanismCommand& command : commands)
    {
        transaction.commands.push_back(
            WorldCommand::Mechanism(std::move(command))
        );
    }
    return transaction;
}

WorldTransactionResult::operator bool() const noexcept
{
    return status == WorldTransactionStatus::Committed;
}

WorldTransactionResult ApplyWorldTransaction(
    MechanismInstanceStore& mechanisms,
    const WorldTransaction& transaction,
    const MechanismDefinitionRegistry& definitions,
    const MechanismSchemaRegistry& schemas,
    std::uint64_t currentTick
)
{
    std::vector<MechanismCommand> commands;
    commands.reserve(transaction.commands.size());
    for (const WorldCommand& command : transaction.commands)
    {
        commands.push_back(std::get<MechanismCommand>(command.payload));
    }
    MechanismTransactionResult result = mechanisms.ApplyTransaction(
        commands,
        definitions,
        schemas,
        currentTick
    );
    return {
        result
            ? WorldTransactionStatus::Committed
            : WorldTransactionStatus::MechanismRejected,
        std::move(result)
    };
}

}
