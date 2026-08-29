#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "order_of_battle_definition.hpp"

namespace dillen::compatibility::hoi3::content {

enum class OrderOfBattleDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicatePath,
    IdCollision,
    Frozen
};

enum class OrderOfBattleResolveResult
{
    Resolved,
    DefinitionMissing,
    AlreadyResolved,
    Frozen
};

class OrderOfBattleDefinitionRegistry
{
public:
    OrderOfBattleDeclareResult Declare(OrderOfBattleDefinition definition);
    OrderOfBattleResolveResult ResolveReferences(
        OrderOfBattleDefinitionId id,
        std::vector<OrderOfBattleNode> roots,
        std::vector<OrderOfBattleMilitaryAccess> militaryAccess,
        std::vector<OrderOfBattleConstruction> constructions,
        std::vector<OrderOfBattleMetadata> metadata
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t ResolvedCount() const noexcept;
    const OrderOfBattleDefinition* Find(
        OrderOfBattleDefinitionId id
    ) const;
    const OrderOfBattleDefinition* Find(std::string_view virtualPath) const;
    const std::vector<OrderOfBattleDefinition>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<OrderOfBattleDefinition> definitions_;
    std::unordered_map<std::uint64_t, std::size_t> indexById_;
    std::unordered_map<std::string, std::size_t> indexByPath_;
    bool frozen_ = false;
};

}
