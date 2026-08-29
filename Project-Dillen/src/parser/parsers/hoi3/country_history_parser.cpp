#include "country_history_parser.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dillen::parser::hoi3 {

namespace {

enum class FieldValueKind
{
    Capital,
    Country,
    Alignment,
    NumberMap,
    Integer,
    Decimal,
    Symbol,
    Scalar
};

enum class PatchParseResult
{
    Complete,
    ImplicitClose,
    Failure
};

struct FieldDescriptor
{
    dillen::compatibility::hoi3::content::CountryHistoryField field;
    FieldValueKind valueKind;
};

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

bool IsMinisterField(std::string_view key)
{
    return key == "head_of_state"
        || key == "head_of_government"
        || key == "foreign_minister"
        || key == "armament_minister"
        || key == "minister_of_security"
        || key == "minister_of_intelligence"
        || key == "chief_of_staff"
        || key == "chief_of_army"
        || key == "chief_of_navy"
        || key == "chief_of_air";
}

FieldDescriptor DescribeField(std::string_view key)
{
    using dillen::compatibility::hoi3::content::CountryHistoryField;
    if (key == "capital")
    {
        return {CountryHistoryField::Capital, FieldValueKind::Capital};
    }
    if (key == "government")
    {
        return {CountryHistoryField::Government, FieldValueKind::Symbol};
    }
    if (key == "ideology")
    {
        return {CountryHistoryField::Ideology, FieldValueKind::Symbol};
    }
    if (IsMinisterField(key))
    {
        return {CountryHistoryField::Minister, FieldValueKind::Integer};
    }
    if (key == "alignment")
    {
        return {CountryHistoryField::Alignment, FieldValueKind::Alignment};
    }
    if (key == "neutrality")
    {
        return {CountryHistoryField::Neutrality, FieldValueKind::Decimal};
    }
    if (key == "national_unity")
    {
        return {CountryHistoryField::NationalUnity, FieldValueKind::Decimal};
    }
    if (key == "oob")
    {
        return {CountryHistoryField::OrderOfBattle, FieldValueKind::Symbol};
    }
    if (key == "load_oob")
    {
        return {CountryHistoryField::LoadOrderOfBattle,
            FieldValueKind::Symbol};
    }
    if (key == "officers_ratio")
    {
        return {CountryHistoryField::OfficersRatio,
            FieldValueKind::Decimal};
    }
    if (key == "popularity")
    {
        return {CountryHistoryField::Popularity,
            FieldValueKind::NumberMap};
    }
    if (key == "organization")
    {
        return {CountryHistoryField::Organization,
            FieldValueKind::NumberMap};
    }
    if (key == "set_country_flag")
    {
        return {CountryHistoryField::SetCountryFlag,
            FieldValueKind::Symbol};
    }
    if (key == "set_global_flag")
    {
        return {CountryHistoryField::SetGlobalFlag,
            FieldValueKind::Symbol};
    }
    if (key == "join_faction")
    {
        return {CountryHistoryField::JoinFaction, FieldValueKind::Symbol};
    }
    if (key == "leave_faction")
    {
        return {CountryHistoryField::LeaveFaction, FieldValueKind::Symbol};
    }
    if (key == "create_alliance")
    {
        return {CountryHistoryField::CreateAlliance,
            FieldValueKind::Country};
    }
    if (key == "decision")
    {
        return {CountryHistoryField::Decision, FieldValueKind::Symbol};
    }
    if (key == "threat")
    {
        return {CountryHistoryField::Threat, FieldValueKind::Decimal};
    }
    if (key == "set_manpower")
    {
        return {CountryHistoryField::SetManpower, FieldValueKind::Decimal};
    }
    return {CountryHistoryField::NamedAssignment, FieldValueKind::Scalar};
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
            "hoi3.country_history.expected_assignment",
            "Country history fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ParseDateToken(
    const Token& token,
    dillen::compatibility::hoi3::content::DefinitionDate& output
)
{
    int components[3]{};
    std::size_t component = 0;
    for (const char character : token.text)
    {
        if (character == '.')
        {
            ++component;
            continue;
        }
        if (component >= 3 || character < '0' || character > '9')
        {
            return false;
        }
        components[component] = components[component] * 10
            + (character - '0');
    }
    if (component != 2
        || components[0] < 1
        || components[1] < 1
        || components[1] > 12
        || components[2] < 1
        || components[2] > 31)
    {
        return false;
    }
    output = {components[0], components[1], components[2]};
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

bool ParseAlignment(
    ParserCursor& cursor,
    dillen::compatibility::hoi3::content::CountryAlignment& output
)
{
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Country alignment"))
    {
        return false;
    }
    bool hasX = false;
    bool hasY = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.alignment_unterminated",
                "unexpected end of file in Country alignment"
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        Token value;
        if (!cursor.ReadScalar(value))
        {
            return false;
        }
        double decimal = 0.0;
        if (!ParseDecimal(value, decimal))
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.alignment_number_invalid",
                "Country alignment coordinates must be numeric",
                value.span
            );
            return false;
        }
        const std::string lowered = LowerAscii(key.text);
        if (lowered == "x" && !hasX)
        {
            output.x = decimal;
            hasX = true;
        }
        else if (lowered == "y" && !hasY)
        {
            output.y = decimal;
            hasY = true;
        }
        else
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.alignment_field_invalid",
                "Country alignment requires one x and one y coordinate",
                key.span
            );
            return false;
        }
    }
    if (!hasX || !hasY)
    {
        cursor.Diagnostics().Error(
            "hoi3.country_history.alignment_incomplete",
            "Country alignment requires both x and y coordinates"
        );
        return false;
    }
    return true;
}

bool ParseNumberMap(
    ParserCursor& cursor,
    dillen::compatibility::hoi3::content::CountryHistoryNamedNumberMap& output
)
{
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for Country history number map"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.number_map_unterminated",
                "unexpected end of file in Country history number map"
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        Token value;
        if (!cursor.ReadScalar(value))
        {
            return false;
        }
        double decimal = 0.0;
        if (!ParseDecimal(value, decimal))
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.number_map_value_invalid",
                "Country history map values must be numeric",
                value.span
            );
            return false;
        }
        output.values.push_back({LowerAscii(key.text), decimal});
    }
    return true;
}

bool ParseAutomaticScalar(
    ParserCursor& cursor,
    UnresolvedCountryHistoryValue& output
)
{
    Token value;
    if (!cursor.ReadScalar(value))
    {
        return false;
    }
    if (value.kind == TokenKind::Number)
    {
        if (value.text.find('.') == std::string_view::npos)
        {
            std::int64_t integer = 0;
            if (ParseInteger(value, integer))
            {
                output = integer;
                return true;
            }
        }
        double decimal = 0.0;
        if (ParseDecimal(value, decimal))
        {
            output = decimal;
            return true;
        }
        cursor.Diagnostics().Error(
            "hoi3.country_history.number_invalid",
            "Country history numeric value is invalid",
            value.span
        );
        return false;
    }
    const std::string lowered = LowerAscii(value.text);
    if (value.kind == TokenKind::Identifier
        && (lowered == "yes" || lowered == "no"))
    {
        output = lowered == "yes";
    }
    else
    {
        output = std::string(value.text);
    }
    return true;
}

bool ParseOperation(
    ParserCursor& cursor,
    std::vector<UnresolvedCountryHistoryOperation>& output
)
{
    Token key;
    if (!cursor.ReadKey(key) || key.kind != TokenKind::Identifier)
    {
        cursor.Diagnostics().Error(
            "hoi3.country_history.field_invalid",
            "Country history operation must begin with a field name",
            key.span
        );
        return false;
    }
    const std::string keyText = LowerAscii(key.text);
    const FieldDescriptor descriptor = DescribeField(keyText);
    if (!ExpectAssignment(cursor, key.span))
    {
        return false;
    }

    UnresolvedCountryHistoryOperation operation;
    operation.field = descriptor.field;
    operation.key = keyText;
    operation.span = key.span;
    switch (descriptor.valueKind)
    {
    case FieldValueKind::Capital:
    {
        Token value;
        std::int64_t province = 0;
        if (!cursor.ReadScalar(value)
            || !ParseInteger(value, province)
            || province < 1
            || static_cast<std::uint64_t>(province)
                > std::numeric_limits<std::uint32_t>::max())
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.capital_invalid",
                "Country capital must be a positive Province ID",
                value.span
            );
            return false;
        }
        operation.value = province;
        break;
    }
    case FieldValueKind::Country:
    {
        Token value;
        if (!cursor.ReadScalar(value)
            || !dillen::compatibility::hoi3::content::CountryTag::Parse(value.text))
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.country_invalid",
                "Country history reference must be a three-character Tag",
                value.span
            );
            return false;
        }
        operation.value = std::string(value.text);
        break;
    }
    case FieldValueKind::Alignment:
    {
        dillen::compatibility::hoi3::content::CountryAlignment alignment;
        if (!ParseAlignment(cursor, alignment))
        {
            return false;
        }
        operation.value = alignment;
        break;
    }
    case FieldValueKind::NumberMap:
    {
        dillen::compatibility::hoi3::content::CountryHistoryNamedNumberMap map;
        if (!ParseNumberMap(cursor, map))
        {
            return false;
        }
        operation.value = std::move(map);
        break;
    }
    case FieldValueKind::Integer:
    {
        Token value;
        std::int64_t integer = 0;
        if (!cursor.ReadScalar(value)
            || !ParseInteger(value, integer)
            || integer < 0)
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.integer_invalid",
                "Country history identifier must be a non-negative integer",
                value.span
            );
            return false;
        }
        operation.value = integer;
        break;
    }
    case FieldValueKind::Decimal:
    {
        Token value;
        double decimal = 0.0;
        if (!cursor.ReadScalar(value)
            || !ParseDecimal(value, decimal)
            || decimal < 0.0)
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.decimal_invalid",
                "Country history quantity must be a non-negative number",
                value.span
            );
            return false;
        }
        operation.value = decimal;
        break;
    }
    case FieldValueKind::Symbol:
    {
        Token value;
        if (!cursor.ReadScalar(value))
        {
            return false;
        }
        std::string symbol(value.text);
        if ((operation.field == dillen::compatibility::hoi3::content::CountryHistoryField::SetCountryFlag
                || operation.field
                    == dillen::compatibility::hoi3::content::CountryHistoryField::SetGlobalFlag)
            && cursor.Peek().kind == TokenKind::Identifier
            && !cursor.Peek().text.empty()
            && cursor.Peek().text.front() == '_'
            && cursor.Peek().span.begin.line == value.span.begin.line)
        {
            const Token suffix = cursor.Consume();
            symbol.append(suffix.text);
            cursor.Diagnostics().Warning(
                "hoi3.country_history.flag_whitespace_recovered",
                "removed whitespace embedded in a Country history Flag",
                value.span
            );
        }
        operation.value = std::move(symbol);
        break;
    }
    case FieldValueKind::Scalar:
        if (cursor.Peek().kind == TokenKind::LeftBrace)
        {
            cursor.Diagnostics().Error(
                "hoi3.country_history.named_block_unknown",
                "unknown Country history block '" + keyText + "'",
                key.span
            );
            return false;
        }
        if (!ParseAutomaticScalar(cursor, operation.value))
        {
            return false;
        }
        break;
    }
    output.push_back(std::move(operation));
    return true;
}

PatchParseResult ParsePatchOperations(
    ParserCursor& cursor,
    std::vector<UnresolvedCountryHistoryOperation>& output
)
{
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Warning(
                "hoi3.country_history.patch_eof_implicit_close",
                "closed the final Country history Patch at end of file",
                cursor.Peek().span
            );
            return PatchParseResult::ImplicitClose;
        }
        if (cursor.Peek().kind == TokenKind::Date)
        {
            cursor.Diagnostics().Warning(
                "hoi3.country_history.patch_missing_right_brace",
                "closed a Country history Patch before the next date",
                cursor.Peek().span
            );
            return PatchParseResult::ImplicitClose;
        }
        if (!ParseOperation(cursor, output))
        {
            return PatchParseResult::Failure;
        }
    }
    return PatchParseResult::Complete;
}

}

bool ParseCountryHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    CountryHistoryDocument document;
    while (!cursor.AtEnd())
    {
        if (cursor.Peek().kind == TokenKind::RightBrace)
        {
            const Token brace = cursor.Consume();
            cursor.Diagnostics().Warning(
                "hoi3.country_history.orphan_right_brace_ignored",
                "ignored an unmatched root-level '}' in Country history",
                brace.span
            );
            continue;
        }
        if (cursor.Peek().kind == TokenKind::Date)
        {
            const Token dateToken = cursor.Consume();
            UnresolvedCountryHistoryPatch patch;
            if (!ParseDateToken(dateToken, patch.date))
            {
                cursor.Diagnostics().Error(
                    "hoi3.country_history.date_invalid",
                    "invalid Country history Patch date",
                    dateToken.span
                );
                return false;
            }
            patch.span = dateToken.span;
            if (!ExpectAssignment(cursor, dateToken.span)
                || !cursor.Expect(
                    TokenKind::LeftBrace,
                    nullptr,
                    "for Country history date Patch"))
            {
                return false;
            }
            const PatchParseResult result = ParsePatchOperations(
                cursor,
                patch.operations
            );
            if (result == PatchParseResult::Failure)
            {
                return false;
            }
            document.patches.push_back(std::move(patch));
            continue;
        }
        if (!ParseOperation(cursor, document.initialOperations))
        {
            return false;
        }
    }
    artifact.value = std::move(document);
    return true;
}

}
