#include "war_history_parser.hpp"

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
            "hoi3.war_history.expected_assignment",
            "war history fields must use '='",
            span
        );
        return false;
    }
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

bool ParseHistoryDate(
    std::string_view text,
    content::DefinitionDate& output
)
{
    int components[3]{};
    std::size_t component = 0;
    bool hasDigit = false;
    for (char character : text)
    {
        if (character == '.')
        {
            if (!hasDigit || component >= 2)
            {
                return false;
            }
            ++component;
            hasDigit = false;
            continue;
        }
        if (character < '0' || character > '9')
        {
            return false;
        }
        hasDigit = true;
        components[component] = components[component] * 10
            + (character - '0');
    }
    if (!hasDigit || (component != 1 && component != 2))
    {
        return false;
    }
    const int day = component == 1 ? 1 : components[2];
    if (components[0] < 1
        || components[1] < 1
        || components[1] > 12
        || day < 1
        || day > 31)
    {
        return false;
    }
    output = {components[0], components[1], day};
    return true;
}

bool ReadCountry(ParserCursor& cursor, content::CountryTag& output)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    const auto tag = content::CountryTag::Parse(token.text);
    if (!tag)
    {
        cursor.Diagnostics().Error(
            "hoi3.war_history.country_invalid",
            "war history Country must be a three-character Tag",
            token.span
        );
        return false;
    }
    output = *tag;
    return true;
}

std::optional<content::WarParticipantOperationKind> ParticipantKind(
    std::string_view field
)
{
    if (field == "add_attacker")
    {
        return content::WarParticipantOperationKind::AddAttacker;
    }
    if (field == "rem_attacker")
    {
        return content::WarParticipantOperationKind::RemoveAttacker;
    }
    if (field == "add_defender")
    {
        return content::WarParticipantOperationKind::AddDefender;
    }
    if (field == "rem_defender")
    {
        return content::WarParticipantOperationKind::RemoveDefender;
    }
    return std::nullopt;
}

bool ParseWarGoal(
    ParserCursor& cursor,
    const SourceSpan& span,
    UnresolvedWarGoal& output
)
{
    output.span = span;
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for war_goal"))
    {
        return false;
    }
    bool hasCasusBelli = false;
    bool hasActor = false;
    bool hasReceiver = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.war_history.goal_unterminated",
                "unexpected end of file in war_goal",
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
        if (field == "casus_belli")
        {
            Token value;
            if (!cursor.ReadScalar(value))
            {
                return false;
            }
            output.casusBelli = std::string(value.text);
            hasCasusBelli = true;
        }
        else if (field == "actor")
        {
            hasActor = ReadCountry(cursor, output.actor);
            if (!hasActor)
            {
                return false;
            }
        }
        else if (field == "receiver")
        {
            hasReceiver = ReadCountry(cursor, output.receiver);
            if (!hasReceiver)
            {
                return false;
            }
        }
        else
        {
            cursor.Diagnostics().Warning(
                "hoi3.war_history.goal_field_unknown",
                "unknown war_goal field was ignored",
                key.span
            );
            if (!SkipValue(cursor))
            {
                return false;
            }
        }
    }
    if (!hasCasusBelli || !hasActor || !hasReceiver)
    {
        cursor.Diagnostics().Error(
            "hoi3.war_history.goal_required_field_missing",
            "war_goal requires casus_belli, actor and receiver",
            span
        );
        return false;
    }
    return true;
}

bool ParsePatch(
    ParserCursor& cursor,
    content::DefinitionDate date,
    const SourceSpan& span,
    UnresolvedWarHistoryPatch& output
)
{
    output.date = date;
    output.span = span;
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for war date"))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.war_history.patch_unterminated",
                "unexpected end of file in war date block",
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
        if (const auto kind = ParticipantKind(field))
        {
            UnresolvedWarParticipantOperation operation;
            operation.kind = *kind;
            operation.span = key.span;
            if (!ReadCountry(cursor, operation.country))
            {
                return false;
            }
            output.participantOperations.push_back(std::move(operation));
        }
        else if (field == "war_goal")
        {
            UnresolvedWarGoal goal;
            if (!ParseWarGoal(cursor, key.span, goal))
            {
                return false;
            }
            output.warGoals.push_back(std::move(goal));
        }
        else
        {
            cursor.Diagnostics().Warning(
                "hoi3.war_history.patch_field_unknown",
                "unknown war date field was ignored",
                key.span
            );
            if (!SkipValue(cursor))
            {
                return false;
            }
        }
    }
    return true;
}

}

bool ParseWarHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    WarHistoryDocument document;
    bool hasName = false;
    while (!cursor.AtEnd())
    {
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        if (!document.span.IsValid())
        {
            document.span = key.span;
        }

        content::DefinitionDate date;
        if (ParseHistoryDate(key.text, date))
        {
            UnresolvedWarHistoryPatch patch;
            if (!ParsePatch(cursor, date, key.span, patch))
            {
                return false;
            }
            document.patches.push_back(std::move(patch));
            continue;
        }

        const std::string field = LowerAscii(key.text);
        if (field == "name")
        {
            Token value;
            if (!cursor.ReadScalar(value))
            {
                return false;
            }
            document.name = std::string(value.text);
            hasName = true;
        }
        else if (field == "limited_war")
        {
            if (!cursor.ReadBool(document.limitedWar))
            {
                return false;
            }
        }
        else
        {
            cursor.Diagnostics().Warning(
                "hoi3.war_history.root_field_unknown",
                "unknown war history root field was ignored",
                key.span
            );
            if (!SkipValue(cursor))
            {
                return false;
            }
        }
    }
    if (!hasName)
    {
        cursor.Diagnostics().Error(
            "hoi3.war_history.name_missing",
            "war history requires a name",
            document.span
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
