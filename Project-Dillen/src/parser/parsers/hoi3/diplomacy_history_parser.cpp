#include "diplomacy_history_parser.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dillen::parser::hoi3 {

namespace {

std::string LowerAscii(std::string_view value)
{
    std::string lowered(value);
    for (char& character : lowered)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return lowered;
}

std::optional<dillen::compatibility::hoi3::content::DiplomaticRelationKind> RelationKind(
    std::string_view value
)
{
    const std::string lowered = LowerAscii(value);
    if (lowered == "alliance")
    {
        return dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance;
    }
    if (lowered == "guarantee")
    {
        return dillen::compatibility::hoi3::content::DiplomaticRelationKind::Guarantee;
    }
    if (lowered == "vassal")
    {
        return dillen::compatibility::hoi3::content::DiplomaticRelationKind::Vassal;
    }
    return std::nullopt;
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
            "hoi3.diplomacy_history.expected_assignment",
            "diplomacy history fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ReadCountry(
    ParserCursor& cursor,
    dillen::compatibility::hoi3::content::CountryTag& output
)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    const auto tag = dillen::compatibility::hoi3::content::CountryTag::Parse(token.text);
    if (!tag)
    {
        cursor.Diagnostics().Error(
            "hoi3.diplomacy_history.country_invalid",
            "diplomacy history Country must be a three-character Tag",
            token.span
        );
        return false;
    }
    output = *tag;
    return true;
}

bool ReadDate(
    ParserCursor& cursor,
    dillen::compatibility::hoi3::content::DefinitionDate& output
)
{
    ClausewitzDate date;
    if (!cursor.ReadDate(date))
    {
        return false;
    }
    output = {date.year, date.month, date.day};
    return true;
}

bool SkipValue(ParserCursor& cursor)
{
    if (cursor.Peek().kind == TokenKind::LeftBrace)
    {
        return cursor.SkipBlock();
    }
    Token ignored;
    return cursor.ReadScalar(ignored);
}

bool ParseRelationBlock(
    ParserCursor& cursor,
    dillen::compatibility::hoi3::content::DiplomaticRelationKind kind,
    const SourceSpan& span,
    ParsedDiplomaticRelation& output
)
{
    output.kind = kind;
    output.span = span;
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for relation"))
    {
        return false;
    }
    bool hasFirst = false;
    bool hasSecond = false;
    bool hasStartDate = false;
    bool hasEndDate = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.diplomacy_history.unterminated",
                "unexpected end of file in diplomacy relation",
                span
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        const std::string field = LowerAscii(key.text);
        if (field == "first")
        {
            if (hasFirst)
            {
                cursor.Diagnostics().Error(
                    "hoi3.diplomacy_history.first_duplicate",
                    "diplomacy relation may define first only once",
                    key.span
                );
                return false;
            }
            if (!ReadCountry(cursor, output.first))
            {
                return false;
            }
            hasFirst = true;
        }
        else if (field == "second")
        {
            if (hasSecond)
            {
                cursor.Diagnostics().Error(
                    "hoi3.diplomacy_history.second_duplicate",
                    "diplomacy relation may define second only once",
                    key.span
                );
                return false;
            }
            if (!ReadCountry(cursor, output.second))
            {
                return false;
            }
            hasSecond = true;
        }
        else if (field == "start_date")
        {
            if (hasStartDate)
            {
                cursor.Diagnostics().Error(
                    "hoi3.diplomacy_history.start_date_duplicate",
                    "diplomacy relation may define start_date only once",
                    key.span
                );
                return false;
            }
            if (!ReadDate(cursor, output.startDate))
            {
                return false;
            }
            hasStartDate = true;
        }
        else if (field == "end_date")
        {
            if (hasEndDate)
            {
                cursor.Diagnostics().Error(
                    "hoi3.diplomacy_history.end_date_duplicate",
                    "diplomacy relation may define end_date only once",
                    key.span
                );
                return false;
            }
            if (!ReadDate(cursor, output.endDate))
            {
                return false;
            }
            hasEndDate = true;
        }
        else
        {
            cursor.Diagnostics().Warning(
                "hoi3.diplomacy_history.field_unknown",
                "unknown diplomacy relation field was ignored",
                key.span
            );
            if (!SkipValue(cursor))
            {
                return false;
            }
        }
    }
    if (!hasFirst || !hasSecond || !hasStartDate || !hasEndDate)
    {
        cursor.Diagnostics().Error(
            "hoi3.diplomacy_history.required_field_missing",
            "diplomacy relation requires first, second, start_date and end_date",
            span
        );
        return false;
    }
    if (output.first == output.second)
    {
        cursor.Diagnostics().Error(
            "hoi3.diplomacy_history.self_relation",
            "diplomacy relation cannot target the same Country",
            span
        );
        return false;
    }
    if (!(output.startDate < output.endDate))
    {
        cursor.Diagnostics().Error(
            "hoi3.diplomacy_history.date_range_invalid",
            "diplomacy relation start_date must precede end_date",
            span
        );
        return false;
    }
    return true;
}

}

bool ParseDiplomacyHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    DiplomacyHistoryDocument document;
    while (!cursor.AtEnd())
    {
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        const auto kind = RelationKind(key.text);
        if (!kind)
        {
            cursor.Diagnostics().Error(
                "hoi3.diplomacy_history.relation_unknown",
                "history/diplomacy supports alliance, guarantee and vassal",
                key.span
            );
            return false;
        }
        ParsedDiplomaticRelation relation;
        if (!ParseRelationBlock(cursor, *kind, key.span, relation))
        {
            return false;
        }
        document.relations.push_back(std::move(relation));
    }
    artifact.value = std::move(document);
    return true;
}

}
