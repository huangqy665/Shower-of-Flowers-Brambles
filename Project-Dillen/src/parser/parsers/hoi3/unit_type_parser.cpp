#include "unit_type_parser.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
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
            "hoi3.unit_type.expected_assignment",
            "Unit type fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ParseInteger(const Token& token, std::int64_t& output)
{
    const char* begin = token.text.data();
    const char* end = token.text.data() + token.text.size();
    const auto result = std::from_chars(begin, end, output, 10);
    return result.ec == std::errc{} && result.ptr == end;
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
    dillen::compatibility::hoi3::content::UnitScalarValue& output
)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    if (token.kind == TokenKind::Number)
    {
        std::string numericText(token.text);
        if (cursor.Peek().kind == TokenKind::Equals
            && cursor.Peek().span.begin.line == token.span.begin.line)
        {
            cursor.Consume();
            Token suffix;
            if (!cursor.ReadScalar(suffix)
                || suffix.kind != TokenKind::Number
                || suffix.span.begin.line != token.span.begin.line)
            {
                cursor.Diagnostics().Error(
                    "hoi3.unit_type.numeric_fragment_invalid",
                    "malformed Unit numeric property could not be recovered",
                    token.span
                );
                return false;
            }
            numericText.append(suffix.text);
            cursor.Diagnostics().Warning(
                "hoi3.unit_type.numeric_fragment_recovered",
                "removed an embedded '=' from a Unit numeric property",
                token.span
            );
        }
        Token normalized = token;
        normalized.text = numericText;
        if (normalized.text.find('.') == std::string_view::npos)
        {
            std::int64_t integer = 0;
            if (ParseInteger(normalized, integer))
            {
                output = integer;
                return true;
            }
        }
        double decimal = 0.0;
        if (ParseDecimal(normalized, decimal))
        {
            output = decimal;
            return true;
        }
        cursor.Diagnostics().Error(
            "hoi3.unit_type.scalar_number_invalid",
            "Unit type numeric property is invalid",
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

bool ParseDomain(
    ParserCursor& cursor,
    dillen::compatibility::hoi3::content::UnitDomain& output,
    SourceSpan& valueSpan
)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    valueSpan = token.span;
    const std::string lowered = LowerAscii(token.text);
    if (lowered == "land")
    {
        output = dillen::compatibility::hoi3::content::UnitDomain::Land;
    }
    else if (lowered == "naval")
    {
        output = dillen::compatibility::hoi3::content::UnitDomain::Naval;
    }
    else if (lowered == "air")
    {
        output = dillen::compatibility::hoi3::content::UnitDomain::Air;
    }
    else
    {
        cursor.Diagnostics().Error(
            "hoi3.unit_type.domain_invalid",
            "Unit type domain must be land, naval, or air",
            token.span
        );
        return false;
    }
    return true;
}

bool ParseUsableBy(
    ParserCursor& cursor,
    std::vector<std::string>& output
)
{
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Unit usable_by"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.usable_by_unterminated",
                "unexpected end of file in Unit usable_by"
            );
            return false;
        }
        Token token;
        if (!cursor.ReadScalar(token)
            || !dillen::compatibility::hoi3::content::CountryTag::Parse(token.text))
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.usable_by_tag_invalid",
                "Unit usable_by entries must be Country Tags",
                token.span
            );
            return false;
        }
        output.emplace_back(token.text);
    }
    return true;
}

bool ParseModifierBlock(
    ParserCursor& cursor,
    std::string name,
    dillen::compatibility::hoi3::content::UnitModifierBlock& output
)
{
    output.name = std::move(name);
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Unit modifier block"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.modifier_block_unterminated",
                "unexpected end of file in Unit modifier block"
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
        Token value;
        double decimal = 0.0;
        if (!cursor.ReadScalar(value) || !ParseDecimal(value, decimal))
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.modifier_value_invalid",
                "Unit modifier values must be numeric",
                value.span
            );
            return false;
        }
        output.modifiers.push_back({LowerAscii(key.text), decimal});
    }
    return true;
}

bool ParseUnitBody(
    ParserCursor& cursor,
    UnresolvedUnitTypeDefinition& output
)
{
    bool hasDomain = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.definition_unterminated",
                "unexpected end of file in Unit type definition",
                output.span
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key) || key.kind != TokenKind::Identifier)
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.field_invalid",
                "Unit type field must be an identifier",
                key.span
            );
            return false;
        }
        const std::string keyText = LowerAscii(key.text);
        if (!ExpectAssignment(cursor, key.span))
        {
            return false;
        }

        if (keyText == "type")
        {
            dillen::compatibility::hoi3::content::UnitDomain domain;
            SourceSpan valueSpan;
            if (!ParseDomain(cursor, domain, valueSpan))
            {
                return false;
            }
            if (hasDomain && output.domain != domain)
            {
                cursor.Diagnostics().Error(
                    "hoi3.unit_type.domain_conflict",
                    "Unit type contains conflicting domain declarations",
                    valueSpan
                );
                return false;
            }
            if (hasDomain)
            {
                cursor.Diagnostics().Warning(
                    "hoi3.unit_type.domain_duplicate_ignored",
                    "ignored a duplicate Unit type domain declaration",
                    valueSpan
                );
            }
            output.domain = domain;
            hasDomain = true;
        }
        else if (keyText == "sprite")
        {
            Token value;
            if (!cursor.ReadScalar(value))
            {
                return false;
            }
            output.sprite = std::string(value.text);
        }
        else if (keyText == "active")
        {
            bool active = false;
            if (!cursor.ReadBool(active))
            {
                return false;
            }
            output.active = active;
        }
        else if (keyText == "unit_group")
        {
            Token value;
            if (!cursor.ReadScalar(value))
            {
                return false;
            }
            output.unitGroup = std::string(value.text);
        }
        else if (keyText == "usable_by")
        {
            if (!ParseUsableBy(cursor, output.usableBy))
            {
                return false;
            }
        }
        else if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            dillen::compatibility::hoi3::content::UnitModifierBlock block;
            if (!ParseModifierBlock(cursor, keyText, block))
            {
                return false;
            }
            output.modifierBlocks.push_back(std::move(block));
        }
        else
        {
            dillen::compatibility::hoi3::content::UnitScalarProperty property;
            property.name = keyText;
            if (!ParseScalar(cursor, property.value))
            {
                return false;
            }
            output.scalarProperties.push_back(std::move(property));
        }
    }
    if (!hasDomain)
    {
        cursor.Diagnostics().Error(
            "hoi3.unit_type.domain_missing",
            "Unit type definition requires a type field",
            output.span
        );
        return false;
    }
    return true;
}

}

bool ParseUnitTypes(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    UnitTypeDocument document;
    while (!cursor.AtEnd())
    {
        Token name;
        if (!cursor.ReadKey(name) || name.kind != TokenKind::Identifier)
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_type.name_invalid",
                "Unit type definition must begin with an identifier",
                name.span
            );
            return false;
        }
        if (!ExpectAssignment(cursor, name.span)
            || !cursor.Expect(
                TokenKind::LeftBrace,
                nullptr,
                "for Unit type definition"))
        {
            return false;
        }
        UnresolvedUnitTypeDefinition definition;
        definition.name = std::string(name.text);
        definition.span = name.span;
        if (!ParseUnitBody(cursor, definition))
        {
            return false;
        }
        document.definitions.push_back(std::move(definition));
    }
    if (document.definitions.empty())
    {
        cursor.Diagnostics().Error(
            "hoi3.unit_type.document_empty",
            "Unit type file contains no definitions"
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
