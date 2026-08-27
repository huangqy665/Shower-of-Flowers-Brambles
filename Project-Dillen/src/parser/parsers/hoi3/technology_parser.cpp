#include "technology_parser.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

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
            "hoi3.technology.expected_assignment",
            "Technology fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ExpectAssignmentOrBlockRecovery(
    ParserCursor& cursor,
    const SourceSpan& span
)
{
    if (cursor.Peek().kind == TokenKind::LeftBrace)
    {
        cursor.Diagnostics().Warning(
            "hoi3.technology.block_assignment_recovered",
            "inferred a missing '=' before a Technology block",
            span
        );
        return true;
    }
    return ExpectAssignment(cursor, span);
}

bool ParseInteger(const Token& token, int& output)
{
    std::int64_t value = 0;
    const char* begin = token.text.data();
    const char* end = token.text.data() + token.text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{}
        || result.ptr != end
        || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max())
    {
        return false;
    }
    output = static_cast<int>(value);
    return true;
}

bool ParseDecimal(const Token& token, double& output)
{
    const std::string text(token.text);
    errno = 0;
    char* end = nullptr;
    output = std::strtod(text.c_str(), &end);
    return errno != ERANGE
        && end == text.c_str() + text.size()
        && std::isfinite(output);
}

bool ParseScalar(
    ParserCursor& cursor,
    content::TechnologyScalarValue& output
)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    if (token.kind == TokenKind::Number)
    {
        int integer = 0;
        if (token.text.find('.') == std::string_view::npos
            && ParseInteger(token, integer))
        {
            output = static_cast<std::int64_t>(integer);
            return true;
        }
        double decimal = 0.0;
        if (ParseDecimal(token, decimal))
        {
            output = decimal;
            return true;
        }
        cursor.Diagnostics().Error(
            "hoi3.technology.scalar_number_invalid",
            "Technology numeric effect is invalid",
            token.span
        );
        return false;
    }
    const std::string lowered = LowerAscii(token.text);
    if (token.kind == TokenKind::Identifier
        && (lowered == "yes" || lowered == "no"))
    {
        output = lowered == "yes";
    }
    else
    {
        output = std::string(token.text);
    }
    return true;
}

bool ReadInteger(
    ParserCursor& cursor,
    int& output,
    std::string_view field
)
{
    Token token;
    if (!cursor.ReadScalar(token) || !ParseInteger(token, output))
    {
        cursor.Diagnostics().Error(
            "hoi3.technology.integer_invalid",
            "Technology field '" + std::string(field)
                + "' must be an integer",
            token.span
        );
        return false;
    }
    return true;
}

bool ReadDecimal(
    ParserCursor& cursor,
    double& output,
    std::string_view field
)
{
    Token token;
    if (!cursor.ReadScalar(token) || !ParseDecimal(token, output))
    {
        cursor.Diagnostics().Error(
            "hoi3.technology.number_invalid",
            "Technology field '" + std::string(field)
                + "' must be numeric",
            token.span
        );
        return false;
    }
    return true;
}

content::TechnologyRequirementKind RequirementKind(
    std::string_view name
)
{
    const std::string lowered = LowerAscii(name);
    if (lowered == "or")
    {
        return content::TechnologyRequirementKind::Any;
    }
    if (lowered == "not")
    {
        return content::TechnologyRequirementKind::Not;
    }
    return content::TechnologyRequirementKind::All;
}

bool ParseRequirementGroup(
    ParserCursor& cursor,
    content::TechnologyRequirementKind kind,
    content::TechnologyRequirement& output,
    std::string name = {}
)
{
    output.kind = kind;
    output.name = std::move(name);
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Technology requirement group"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.allow_unterminated",
                "unexpected end of file in Technology allow block"
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key)
            || key.kind != TokenKind::Identifier
            || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        const std::string name = LowerAscii(key.text);
        if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            content::TechnologyRequirement child;
            const bool logical = name == "and"
                || name == "or"
                || name == "not";
            if (!ParseRequirementGroup(
                    cursor,
                    logical
                        ? RequirementKind(name)
                        : content::TechnologyRequirementKind::Predicate,
                    child,
                    logical ? std::string{} : name))
            {
                return false;
            }
            output.children.push_back(std::move(child));
            continue;
        }
        if (kind == content::TechnologyRequirementKind::Predicate)
        {
            content::TechnologyRequirement child;
            child.kind = content::TechnologyRequirementKind::Predicate;
            child.name = name;
            content::TechnologyScalarValue value;
            if (!ParseScalar(cursor, value))
            {
                return false;
            }
            child.predicateValue = std::move(value);
            output.children.push_back(std::move(child));
        }
        else
        {
            Token levelToken;
            int level = 0;
            if (!cursor.ReadScalar(levelToken)
                || !ParseInteger(levelToken, level))
            {
                cursor.Diagnostics().Error(
                    "hoi3.technology.requirement_level_invalid",
                    "Technology prerequisite level must be an integer",
                    levelToken.span
                );
                return false;
            }
            content::TechnologyRequirement child;
            child.kind = content::TechnologyRequirementKind::Level;
            child.name = name;
            child.level = level;
            output.children.push_back(std::move(child));
        }
    }
    return true;
}

bool ParseResearchBonuses(
    ParserCursor& cursor,
    std::vector<content::TechnologyResearchBonus>& output
)
{
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Technology research bonuses"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.research_bonus_unterminated",
                "unexpected end of file in research_bonus_from"
            );
            return false;
        }
        Token key;
        Token value;
        double weight = 0.0;
        if (!cursor.ReadKey(key)
            || key.kind != TokenKind::Identifier
            || !ExpectAssignment(cursor, key.span)
            || !cursor.ReadScalar(value)
            || !ParseDecimal(value, weight))
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.research_bonus_invalid",
                "Technology research bonus must be a named number",
                key.span
            );
            return false;
        }
        output.push_back({LowerAscii(key.text), weight});
    }
    return true;
}

bool ParseEffectBlock(
    ParserCursor& cursor,
    std::string name,
    std::size_t openingColumn,
    content::TechnologyEffectBlock& output
)
{
    output.name = std::move(name);
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Technology effect block"))
    {
        return false;
    }
    bool hasNestedBlock = false;
    while (true)
    {
        if (cursor.ConsumeIf(TokenKind::RightBrace))
        {
            return true;
        }
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.effect_block_unterminated",
                "unexpected end of file in Technology effect block"
            );
            return false;
        }
        if (hasNestedBlock
            && openingColumn == 1
            && cursor.Peek().span.begin.column == 1)
        {
            cursor.Diagnostics().Warning(
                "hoi3.technology.effect_block_close_recovered",
                "inferred a missing '}' before the next Technology effect block",
                cursor.Peek().span
            );
            return true;
        }

        Token key;
        if (!cursor.ReadKey(key)
            || key.kind != TokenKind::Identifier
            || !ExpectAssignmentOrBlockRecovery(cursor, key.span))
        {
            return false;
        }
        const std::string keyText = LowerAscii(key.text);
        if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            content::TechnologyEffectBlock child;
            if (!ParseEffectBlock(
                    cursor,
                    keyText,
                    key.span.begin.column,
                    child))
            {
                return false;
            }
            output.blocks.push_back(std::move(child));
            hasNestedBlock = true;
        }
        else
        {
            content::TechnologyScalarEffect effect;
            effect.name = keyText;
            if (!ParseScalar(cursor, effect.value))
            {
                return false;
            }
            output.effects.push_back(std::move(effect));
        }
    }
}

bool ReadText(ParserCursor& cursor, std::string& output)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    output = std::string(token.text);
    return true;
}

bool ParseTechnologyBody(
    ParserCursor& cursor,
    UnresolvedTechnologyDefinition& output
)
{
    bool hasDifficulty = false;
    bool hasStartYear = false;
    bool hasFolder = false;
    bool hasOnCompletion = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.definition_unterminated",
                "unexpected end of file in Technology definition",
                output.span
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key) || key.kind != TokenKind::Identifier)
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.field_invalid",
                "Technology field must be an identifier",
                key.span
            );
            return false;
        }
        const std::string keyText = LowerAscii(key.text);
        if (!ExpectAssignmentOrBlockRecovery(cursor, key.span))
        {
            return false;
        }

        if (keyText == "difficulty")
        {
            hasDifficulty = ReadDecimal(
                cursor,
                output.difficulty,
                keyText
            );
            if (!hasDifficulty)
            {
                return false;
            }
        }
        else if (keyText == "start_year")
        {
            hasStartYear = ReadInteger(
                cursor,
                output.startYear,
                keyText
            );
            if (!hasStartYear)
            {
                return false;
            }
        }
        else if (keyText == "first_offset")
        {
            int value = 0;
            if (!ReadInteger(cursor, value, keyText))
            {
                return false;
            }
            output.firstOffset = value;
        }
        else if (keyText == "additional_offset")
        {
            int value = 0;
            if (!ReadInteger(cursor, value, keyText))
            {
                return false;
            }
            output.additionalOffset = value;
        }
        else if (keyText == "max_level")
        {
            int value = 0;
            if (!ReadInteger(cursor, value, keyText))
            {
                return false;
            }
            output.maxLevel = value;
        }
        else if (keyText == "folder")
        {
            hasFolder = ReadText(cursor, output.folder);
            if (!hasFolder)
            {
                return false;
            }
        }
        else if (keyText == "on_completion")
        {
            hasOnCompletion = ReadText(cursor, output.onCompletion);
            if (!hasOnCompletion)
            {
                return false;
            }
        }
        else if (keyText == "change")
        {
            bool value = false;
            if (!cursor.ReadBool(value))
            {
                return false;
            }
            output.change = value;
        }
        else if (keyText == "allow")
        {
            content::TechnologyRequirement requirement;
            if (!ParseRequirementGroup(
                    cursor,
                    content::TechnologyRequirementKind::All,
                    requirement))
            {
                return false;
            }
            output.allow = std::move(requirement);
        }
        else if (keyText == "research_bonus_from")
        {
            if (!ParseResearchBonuses(cursor, output.researchBonuses))
            {
                return false;
            }
        }
        else if (keyText == "activate_unit")
        {
            std::string name;
            if (!ReadText(cursor, name))
            {
                return false;
            }
            output.activatedUnits.push_back({std::move(name), std::nullopt});
        }
        else if (keyText == "activate_building")
        {
            std::string name;
            if (!ReadText(cursor, name))
            {
                return false;
            }
            output.activatedBuildings.push_back(std::move(name));
        }
        else if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            content::TechnologyEffectBlock block;
            if (!ParseEffectBlock(
                    cursor,
                    keyText,
                    key.span.begin.column,
                    block))
            {
                return false;
            }
            output.effectBlocks.push_back(std::move(block));
        }
        else
        {
            content::TechnologyScalarEffect effect;
            effect.name = keyText;
            if (!ParseScalar(cursor, effect.value))
            {
                return false;
            }
            output.scalarEffects.push_back(std::move(effect));
        }
    }

    if (!hasDifficulty || !hasStartYear || !hasFolder || !hasOnCompletion)
    {
        cursor.Diagnostics().Error(
            "hoi3.technology.required_field_missing",
            "Technology requires difficulty, start_year, folder, and on_completion",
            output.span
        );
        return false;
    }
    return true;
}

}

bool ParseTechnologies(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    TechnologyDocument document;
    while (!cursor.AtEnd())
    {
        Token name;
        if (!cursor.ReadKey(name) || name.kind != TokenKind::Identifier)
        {
            cursor.Diagnostics().Error(
                "hoi3.technology.name_invalid",
                "Technology definition must begin with an identifier",
                name.span
            );
            return false;
        }
        if (!ExpectAssignment(cursor, name.span)
            || !cursor.Expect(
                TokenKind::LeftBrace,
                nullptr,
                "for Technology definition"))
        {
            return false;
        }
        UnresolvedTechnologyDefinition definition;
        definition.name = std::string(name.text);
        definition.span = name.span;
        if (!ParseTechnologyBody(cursor, definition))
        {
            return false;
        }
        document.definitions.push_back(std::move(definition));
    }
    if (document.definitions.empty())
    {
        cursor.Diagnostics().Error(
            "hoi3.technology.document_empty",
            "Technology file contains no definitions"
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
