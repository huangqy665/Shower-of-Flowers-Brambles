#include "launch_parser.hpp"

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
            "hoi3.launch.expected_assignment",
            "launch definition fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ReadText(ParserCursor& cursor, std::string& output)
{
    Token token;
    if (!cursor.ReadScalar(token))
    {
        return false;
    }
    output.assign(token.text);
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

bool ReadCountry(
    ParserCursor& cursor,
    std::vector<dillen::compatibility::hoi3::content::CountryTag>& output
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
            "hoi3.launch.country_invalid",
            "launch Country reference must be a three-character Tag",
            token.span
        );
        return false;
    }
    output.push_back(*tag);
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

bool ParseBookmarkBlock(
    ParserCursor& cursor,
    const SourceSpan& span,
    ParsedBookmark& output
)
{
    output.span = span;
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for bookmark"))
    {
        return false;
    }
    bool hasName = false;
    bool hasDate = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.bookmark.unterminated",
                "unexpected end of file in bookmark",
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
        if (field == "name")
        {
            hasName = ReadText(cursor, output.name);
            if (!hasName)
            {
                return false;
            }
        }
        else if (field == "desc")
        {
            if (!ReadText(cursor, output.description))
            {
                return false;
            }
        }
        else if (field == "icon")
        {
            if (!ReadText(cursor, output.icon))
            {
                return false;
            }
        }
        else if (field == "date")
        {
            hasDate = ReadDate(cursor, output.date);
            if (!hasDate)
            {
                return false;
            }
        }
        else if (field == "country")
        {
            if (!ReadCountry(cursor, output.countries))
            {
                return false;
            }
        }
        else if (!SkipValue(cursor))
        {
            return false;
        }
    }
    if (!hasName || !hasDate)
    {
        cursor.Diagnostics().Error(
            "hoi3.bookmark.required_field_missing",
            "bookmark requires name and date",
            span
        );
        return false;
    }
    return true;
}

}

bool ParseBookmarks(ParserCursor& cursor, ParseArtifact& artifact)
{
    BookmarkDocument document;
    while (!cursor.AtEnd())
    {
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        if (LowerAscii(key.text) != "bookmark")
        {
            cursor.Diagnostics().Error(
                "hoi3.bookmark.root_invalid",
                "common/bookmarks.txt may only contain bookmark blocks",
                key.span
            );
            return false;
        }
        ParsedBookmark bookmark;
        if (!ParseBookmarkBlock(cursor, key.span, bookmark))
        {
            return false;
        }
        document.bookmarks.push_back(std::move(bookmark));
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseScenario(ParserCursor& cursor, ParseArtifact& artifact)
{
    ScenarioDocument document;
    bool hasName = false;
    bool hasStartDate = false;
    bool hasEndDate = false;
    while (!cursor.AtEnd())
    {
        Token key;
        if (!cursor.ReadKey(key) || !ExpectAssignment(cursor, key.span))
        {
            return false;
        }
        if (document.span.begin.source == kInvalidSourceId)
        {
            document.span = key.span;
        }
        const std::string field = LowerAscii(key.text);
        if (field == "name")
        {
            hasName = ReadText(cursor, document.name);
            if (!hasName)
            {
                return false;
            }
        }
        else if (field == "desc")
        {
            if (!ReadText(cursor, document.description))
            {
                return false;
            }
        }
        else if (field == "icon")
        {
            if (!ReadText(cursor, document.icon))
            {
                return false;
            }
        }
        else if (field == "startdate")
        {
            hasStartDate = ReadDate(cursor, document.startDate);
            if (!hasStartDate)
            {
                return false;
            }
        }
        else if (field == "enddate")
        {
            hasEndDate = ReadDate(cursor, document.endDate);
            if (!hasEndDate)
            {
                return false;
            }
        }
        else if (field == "selectable_country")
        {
            if (!ReadCountry(cursor, document.selectableCountries))
            {
                return false;
            }
        }
        else if (field == "country")
        {
            if (!ReadCountry(cursor, document.additionalCountries))
            {
                return false;
            }
        }
        else if (!SkipValue(cursor))
        {
            return false;
        }
    }
    if (!hasName || !hasStartDate || !hasEndDate)
    {
        cursor.Diagnostics().Error(
            "hoi3.scenario.required_field_missing",
            "scenario requires name, startdate and enddate",
            document.span
        );
        return false;
    }
    if (document.endDate < document.startDate)
    {
        cursor.Diagnostics().Error(
            "hoi3.scenario.date_range_invalid",
            "scenario enddate cannot precede startdate",
            document.span
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
