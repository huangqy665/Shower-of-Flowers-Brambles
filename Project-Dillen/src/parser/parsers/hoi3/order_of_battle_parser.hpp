#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "order_of_battle_definition.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"

namespace dillen::parser::hoi3 {

struct UnresolvedOrderOfBattleNode
{
    content::OrderOfBattleNodeKind kind =
        content::OrderOfBattleNodeKind::Division;
    std::string name;
    std::string unitTypeName;
    std::optional<std::uint32_t> location;
    std::optional<std::uint32_t> base;
    std::optional<std::int64_t> leader;
    std::string expeditionaryOwner;
    std::string builder;
    std::optional<bool> reserve;
    std::optional<bool> pride;
    std::optional<int> historicalModel;
    std::optional<double> experience;
    std::optional<double> strength;
    std::optional<double> organisation;
    std::optional<double> digIn;
    std::vector<UnresolvedOrderOfBattleNode> children;
    SourceSpan span;
};

struct UnresolvedOrderOfBattleMilitaryAccess
{
    std::string country;
    bool enabled = false;
    SourceSpan span;
};

struct UnresolvedOrderOfBattleConstruction
{
    std::string country;
    std::string builder;
    std::string name;
    std::optional<bool> reserve;
    std::optional<double> cost;
    std::optional<double> progress;
    std::optional<double> duration;
    std::optional<double> manpower;
    std::vector<UnresolvedOrderOfBattleNode> components;
    SourceSpan span;
};

struct UnresolvedOrderOfBattleMetadata
{
    std::string key;
    std::string value;
    SourceSpan span;
};

struct OrderOfBattleDocument
{
    std::vector<UnresolvedOrderOfBattleNode> roots;
    std::vector<UnresolvedOrderOfBattleMilitaryAccess> militaryAccess;
    std::vector<UnresolvedOrderOfBattleConstruction> constructions;
    std::vector<UnresolvedOrderOfBattleMetadata> metadata;
};

bool ParseOrderOfBattle(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
