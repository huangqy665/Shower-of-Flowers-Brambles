#include "order_of_battle_parser.hpp"

#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "country_tag_definition.hpp"

namespace dillen::parser::hoi3 {

namespace {

std::string LowerAscii(std::string_view text)
{
    std::string lowered(text);
    for (char& character : lowered)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return lowered;
}

bool ExpectAssignment(ParserCursor& cursor, const SourceSpan& span)
{
    RelationOperator relation;
    if (!cursor.ReadRelation(relation))
    {
        return false;
    }
    if (relation != RelationOperator::Assign)
    {
        cursor.Diagnostics().Error(
            "hoi3.oob.expected_assignment",
            "Order of battle fields must use '='",
            span
        );
        return false;
    }
    return true;
}

std::optional<content::OrderOfBattleNodeKind> NodeKind(
    std::string_view key
)
{
    const std::string lowered = LowerAscii(key);
    if (lowered == "theatre")
    {
        return content::OrderOfBattleNodeKind::Theatre;
    }
    if (lowered == "armygroup")
    {
        return content::OrderOfBattleNodeKind::ArmyGroup;
    }
    if (lowered == "army")
    {
        return content::OrderOfBattleNodeKind::Army;
    }
    if (lowered == "corps")
    {
        return content::OrderOfBattleNodeKind::Corps;
    }
    if (lowered == "division")
    {
        return content::OrderOfBattleNodeKind::Division;
    }
    if (lowered == "navy" || lowered == "naval")
    {
        return content::OrderOfBattleNodeKind::Navy;
    }
    if (lowered == "air")
    {
        return content::OrderOfBattleNodeKind::Air;
    }
    if (lowered == "regiment" || lowered == "rregiment")
    {
        return content::OrderOfBattleNodeKind::Regiment;
    }
    if (lowered == "ship")
    {
        return content::OrderOfBattleNodeKind::Ship;
    }
    if (lowered == "wing")
    {
        return content::OrderOfBattleNodeKind::Wing;
    }
    return std::nullopt;
}

bool IsElementKind(content::OrderOfBattleNodeKind kind)
{
    return kind == content::OrderOfBattleNodeKind::Regiment
        || kind == content::OrderOfBattleNodeKind::Ship
        || kind == content::OrderOfBattleNodeKind::Wing;
}

bool IsNodeFieldName(std::string_view key)
{
    const std::string lowered = LowerAscii(key);
    return NodeKind(lowered).has_value()
        || lowered == "name"
        || lowered == "type"
        || lowered == "location"
        || lowered == "base"
        || lowered == "leader"
        || lowered == "expeditionary_owner"
        || lowered == "builder"
        || lowered == "is_reserve"
        || lowered == "pride"
        || lowered == "historical_model"
        || lowered == "historical"
        || lowered == "istorical_model"
        || lowered == "experience"
        || lowered == "xperience"
        || lowered == "strength"
        || lowered == "organisation"
        || lowered == "dig_in";
}

bool ReadString(ParserCursor& cursor, std::string& output)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    output = std::string(token.text);
    return true;
}

bool ReadNonNegativeInt(
    ParserCursor& cursor,
    int& output,
    std::string_view field,
    const SourceSpan& span
)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    std::int64_t value = 0;
    const char* begin = token.text.data();
    const char* end = token.text.data() + token.text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{}
        || result.ptr == begin
        || value < 0
        || value > std::numeric_limits<int>::max())
    {
        cursor.Diagnostics().Error(
            "hoi3.oob.integer_invalid",
            std::string(field) + " must be a non-negative integer",
            span
        );
        return false;
    }
    if (result.ptr != end)
    {
        cursor.Diagnostics().Warning(
            "hoi3.oob.integer_suffix_recovered",
            std::string("ignored a suffix after ") + std::string(field),
            token.span
        );
    }
    output = static_cast<int>(value);
    return true;
}

bool ReadProvince(
    ParserCursor& cursor,
    std::uint32_t& output,
    std::string_view field,
    const SourceSpan& span
)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    std::uint64_t value = 0;
    const char* begin = token.text.data();
    const char* end = token.text.data() + token.text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{}
        || result.ptr == begin
        || value == 0
        || value > std::numeric_limits<std::uint32_t>::max())
    {
        cursor.Diagnostics().Error(
            "hoi3.oob.province_invalid",
            std::string(field) + " must be a positive Province ID",
            span
        );
        return false;
    }
    if (result.ptr != end)
    {
        cursor.Diagnostics().Warning(
            "hoi3.oob.province_suffix_recovered",
            "ignored a non-numeric suffix after Province ID",
            token.span
        );
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

bool ParseNodeBody(
    ParserCursor& cursor,
    UnresolvedOrderOfBattleNode& output
);

bool ParseNode(
    ParserCursor& cursor,
    const Token& key,
    content::OrderOfBattleNodeKind kind,
    UnresolvedOrderOfBattleNode& output
)
{
    output.kind = kind;
    output.span = key.span;
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Order of battle node"))
    {
        return false;
    }
    if (!ParseNodeBody(cursor, output))
    {
        return false;
    }
    if (IsElementKind(kind) && output.unitTypeName.empty())
    {
        cursor.Diagnostics().Warning(
            "hoi3.oob.element_type_missing",
            "kept a Regiment, Ship or Wing node with a missing unit type",
            key.span
        );
    }
    return true;
}

bool ParseNodeBody(
    ParserCursor& cursor,
    UnresolvedOrderOfBattleNode& output
)
{
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Warning(
                "hoi3.oob.node_implicit_close",
                "implicitly closed an Order of battle node at end of file",
                output.span
            );
            return true;
        }
        if (cursor.Peek().kind == TokenKind::Equals)
        {
            const Token orphan = cursor.Consume();
            if (IsScalarToken(cursor.Peek().kind))
            {
                cursor.Consume();
            }
            cursor.Diagnostics().Warning(
                "hoi3.oob.orphan_assignment_ignored",
                "ignored an orphan assignment fragment",
                orphan.span
            );
            continue;
        }
        Token key;
        if (!cursor.ReadKey(key))
        {
            return false;
        }
        if (!IsRelationToken(cursor.Peek().kind))
        {
            cursor.Diagnostics().Warning(
                "hoi3.oob.bare_token_ignored",
                "ignored a bare annotation token in an Order of battle node",
                key.span
            );
            continue;
        }
        if (!ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        const std::string lowered = LowerAscii(key.text);
        std::optional<content::OrderOfBattleNodeKind> childKind =
            NodeKind(lowered);
        if (childKind)
        {
            if (lowered == "rregiment")
            {
                cursor.Diagnostics().Warning(
                    "hoi3.oob.regiment_key_recovered",
                    "recovered misspelled 'rregiment' as 'regiment'",
                    key.span
                );
            }
            UnresolvedOrderOfBattleNode child;
            if (!ParseNode(cursor, key, *childKind, child))
            {
                return false;
            }
            output.children.push_back(std::move(child));
            continue;
        }
        if (lowered == "name")
        {
            if (!ReadString(cursor, output.name))
            {
                return false;
            }
        }
        else if (lowered == "type")
        {
            if (!ReadString(cursor, output.unitTypeName))
            {
                return false;
            }
            output.unitTypeName = content::NormalizeUnitTypeName(
                output.unitTypeName
            );
        }
        else if (lowered == "location" || lowered == "base")
        {
            std::uint32_t province = 0;
            if (!ReadProvince(cursor, province, lowered, key.span))
            {
                return false;
            }
            if (lowered == "location")
            {
                output.location = province;
            }
            else
            {
                output.base = province;
            }
        }
        else if (lowered == "leader")
        {
            if (cursor.Peek().kind == TokenKind::RightBrace
                || (IsScalarToken(cursor.Peek().kind)
                    && IsNodeFieldName(cursor.Peek().text)))
            {
                cursor.Diagnostics().Warning(
                    "hoi3.oob.leader_missing_ignored",
                    "ignored a leader field with no numeric value",
                    key.span
                );
                continue;
            }
            std::int64_t leader = 0;
            if (!cursor.ReadInt64(leader) || leader < 0)
            {
                cursor.Diagnostics().Error(
                    "hoi3.oob.leader_invalid",
                    "leader must be a non-negative integer",
                    key.span
                );
                return false;
            }
            output.leader = leader;
        }
        else if (lowered == "expeditionary_owner")
        {
            if (!ReadString(cursor, output.expeditionaryOwner))
            {
                return false;
            }
        }
        else if (lowered == "builder")
        {
            if (!ReadString(cursor, output.builder))
            {
                return false;
            }
        }
        else if (lowered == "is_reserve" || lowered == "pride")
        {
            bool value = false;
            if (!cursor.ReadBool(value))
            {
                return false;
            }
            if (lowered == "is_reserve")
            {
                output.reserve = value;
            }
            else
            {
                output.pride = value;
            }
        }
        else if (lowered == "historical_model"
            || lowered == "historical"
            || lowered == "istorical_model")
        {
            if (cursor.Peek().kind == TokenKind::RightBrace)
            {
                cursor.Diagnostics().Warning(
                    "hoi3.oob.historical_model_missing_ignored",
                    "kept a unit element with no historical_model value",
                    key.span
                );
                continue;
            }
            int model = 0;
            if (!ReadNonNegativeInt(
                    cursor,
                    model,
                    "historical_model",
                    key.span))
            {
                return false;
            }
            output.historicalModel = model;
            if (lowered != "historical_model")
            {
                cursor.Diagnostics().Warning(
                    "hoi3.oob.historical_model_alias_recovered",
                    "recovered a historical_model compatibility spelling",
                    key.span
                );
            }
        }
        else if (lowered == "experience"
            || lowered == "xperience"
            || lowered == "strength"
            || lowered == "organisation"
            || lowered == "dig_in")
        {
            double value = 0.0;
            if (!cursor.ReadDouble(value))
            {
                return false;
            }
            if (lowered == "experience" || lowered == "xperience")
            {
                output.experience = value;
                if (lowered == "xperience")
                {
                    cursor.Diagnostics().Warning(
                        "hoi3.oob.experience_key_recovered",
                        "recovered misspelled 'xperience' as 'experience'",
                        key.span
                    );
                }
            }
            else if (lowered == "strength")
            {
                output.strength = value;
            }
            else if (lowered == "organisation")
            {
                output.organisation = value;
            }
            else
            {
                output.digIn = value;
            }
        }
        else if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            cursor.Diagnostics().Warning(
                "hoi3.oob.node_block_ignored",
                "ignored an unsupported nested Order of battle block",
                key.span
            );
            if (!cursor.SkipBlock())
            {
                return false;
            }
        }
        else
        {
            Token ignored;
            cursor.Diagnostics().Warning(
                "hoi3.oob.node_scalar_ignored",
                "ignored an unsupported Order of battle node field",
                key.span
            );
            if (!cursor.ReadScalar(ignored))
            {
                return false;
            }
        }
    }
    return true;
}

bool ParseMilitaryAccess(
    ParserCursor& cursor,
    const Token& key,
    UnresolvedOrderOfBattleMilitaryAccess& output
)
{
    output.country = std::string(key.text);
    output.span = key.span;
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for military access relation"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        Token field;
        if (cursor.AtEnd()
            || !cursor.ReadKey(field)
            || !ExpectAssignment(cursor, field.span))
        {
            return false;
        }
        const std::string lowered = LowerAscii(field.text);
        if (lowered == "military_access")
        {
            if (!cursor.ReadBool(output.enabled))
            {
                return false;
            }
        }
        else if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            if (!cursor.SkipBlock())
            {
                return false;
            }
        }
        else
        {
            Token ignored;
            if (!cursor.ReadScalar(ignored))
            {
                return false;
            }
        }
    }
    return true;
}

bool ParseConstruction(
    ParserCursor& cursor,
    const Token& key,
    UnresolvedOrderOfBattleConstruction& output
)
{
    output.span = key.span;
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for military construction"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.oob.construction_unterminated",
                "unexpected end of file in military construction",
                key.span
            );
            return false;
        }
        Token field;
        if (!cursor.ReadKey(field)
            || !ExpectAssignment(cursor, field.span))
        {
            return false;
        }
        const std::string lowered = LowerAscii(field.text);
        if (lowered == "country" || lowered == "builder")
        {
            std::string value;
            if (!ReadString(cursor, value))
            {
                return false;
            }
            if (lowered == "country")
            {
                output.country = std::move(value);
            }
            else
            {
                output.builder = std::move(value);
            }
        }
        else if (lowered == "name")
        {
            if (!ReadString(cursor, output.name))
            {
                return false;
            }
        }
        else if (lowered == "is_reserve")
        {
            bool value = false;
            if (!cursor.ReadBool(value))
            {
                return false;
            }
            output.reserve = value;
        }
        else if (lowered == "cost"
            || lowered == "progress"
            || lowered == "duration"
            || lowered == "manpower")
        {
            double value = 0.0;
            if (!cursor.ReadDouble(value))
            {
                return false;
            }
            if (lowered == "cost")
            {
                output.cost = value;
            }
            else if (lowered == "progress")
            {
                output.progress = value;
            }
            else if (lowered == "duration")
            {
                output.duration = value;
            }
            else
            {
                output.manpower = value;
            }
        }
        else if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            UnresolvedOrderOfBattleNode component;
            component.kind = content::OrderOfBattleNodeKind::Regiment;
            component.unitTypeName = content::NormalizeUnitTypeName(
                field.text
            );
            component.span = field.span;
            if (!cursor.ConsumeIf(TokenKind::LeftBrace)
                || !ParseNodeBody(cursor, component))
            {
                return false;
            }
            output.components.push_back(std::move(component));
        }
        else
        {
            Token ignored;
            cursor.Diagnostics().Warning(
                "hoi3.oob.construction_scalar_ignored",
                "ignored an unsupported military construction field",
                field.span
            );
            if (!cursor.ReadScalar(ignored))
            {
                return false;
            }
        }
    }
    return true;
}

}

bool ParseOrderOfBattle(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    OrderOfBattleDocument document;
    while (!cursor.AtEnd())
    {
        if (cursor.ConsumeIf(TokenKind::RightBrace))
        {
            cursor.Diagnostics().Warning(
                "hoi3.oob.orphan_right_brace_ignored",
                "ignored an orphan root right brace"
            );
            continue;
        }
        if (cursor.Peek().kind == TokenKind::Equals)
        {
            const Token orphan = cursor.Consume();
            if (IsScalarToken(cursor.Peek().kind))
            {
                cursor.Consume();
            }
            cursor.Diagnostics().Warning(
                "hoi3.oob.root_orphan_assignment_ignored",
                "ignored an orphan root assignment fragment",
                orphan.span
            );
            continue;
        }
        Token key;
        if (!cursor.ReadKey(key))
        {
            return false;
        }
        if (!IsRelationToken(cursor.Peek().kind))
        {
            cursor.Diagnostics().Warning(
                "hoi3.oob.root_bare_token_ignored",
                "ignored a bare root annotation token",
                key.span
            );
            continue;
        }
        if (!ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        const std::string lowered = LowerAscii(key.text);
        const std::optional<content::OrderOfBattleNodeKind> kind =
            NodeKind(lowered);
        if (kind)
        {
            UnresolvedOrderOfBattleNode node;
            if (!ParseNode(cursor, key, *kind, node))
            {
                return false;
            }
            document.roots.push_back(std::move(node));
            continue;
        }
        if (lowered == "military_construction")
        {
            UnresolvedOrderOfBattleConstruction construction;
            if (!ParseConstruction(cursor, key, construction))
            {
                return false;
            }
            document.constructions.push_back(std::move(construction));
            continue;
        }
        if (content::CountryTag::Parse(key.text)
            && cursor.Peek().kind == TokenKind::LeftBrace)
        {
            UnresolvedOrderOfBattleMilitaryAccess access;
            if (!ParseMilitaryAccess(cursor, key, access))
            {
                return false;
            }
            document.militaryAccess.push_back(std::move(access));
            continue;
        }
        if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            cursor.Diagnostics().Warning(
                "hoi3.oob.root_block_ignored",
                "ignored an unsupported root Order of battle block",
                key.span
            );
            if (!cursor.SkipBlock())
            {
                return false;
            }
            continue;
        }
        Token value;
        if (!cursor.ReadScalar(value))
        {
            return false;
        }
        document.metadata.push_back({
            lowered,
            std::string(value.text),
            key.span
        });
    }
    artifact.value = std::move(document);
    return true;
}

}
