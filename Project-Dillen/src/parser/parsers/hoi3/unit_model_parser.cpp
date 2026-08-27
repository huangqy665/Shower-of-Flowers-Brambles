#include "unit_model_parser.hpp"

#include <charconv>
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
            "hoi3.unit_model.expected_assignment",
            "Unit model fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ParseInteger(std::string_view text, int& output)
{
    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
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

bool ParseModelKey(
    const Token& token,
    std::string& unitTypeName,
    int& modelIndex
)
{
    const std::size_t separator = token.text.rfind('.');
    if (separator == std::string_view::npos
        || separator == 0
        || separator + 1 >= token.text.size()
        || !ParseInteger(token.text.substr(separator + 1), modelIndex)
        || modelIndex < 0)
    {
        return false;
    }
    unitTypeName = std::string(token.text.substr(0, separator));
    return true;
}

bool ParseModelBody(
    ParserCursor& cursor,
    UnresolvedUnitModelDefinition& output
)
{
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_model.definition_unterminated",
                "unexpected end of file in Unit model definition",
                output.span
            );
            return false;
        }
        Token key;
        Token value;
        int level = 0;
        if (!cursor.ReadKey(key)
            || key.kind != TokenKind::Identifier
            || !ExpectAssignment(cursor, key.span)
            || !cursor.ReadScalar(value)
            || !ParseInteger(value.text, level))
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_model.technology_level_invalid",
                "Unit model Technology level must be an integer",
                key.span
            );
            return false;
        }
        output.technologyLevels.push_back({
            LowerAscii(key.text),
            level,
            std::nullopt
        });
    }
    return true;
}

}

bool ParseUnitModels(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    UnitModelDocument document;
    while (!cursor.AtEnd())
    {
        Token key;
        UnresolvedUnitModelDefinition definition;
        if (!cursor.ReadKey(key)
            || key.kind != TokenKind::Identifier
            || !ParseModelKey(
                key,
                definition.unitTypeName,
                definition.modelIndex))
        {
            cursor.Diagnostics().Error(
                "hoi3.unit_model.key_invalid",
                "Unit model key must use '<unit_type>.<model_index>'",
                key.span
            );
            return false;
        }
        if (!ExpectAssignment(cursor, key.span)
            || !cursor.Expect(
                TokenKind::LeftBrace,
                nullptr,
                "for Unit model definition"))
        {
            return false;
        }
        definition.span = key.span;
        if (!ParseModelBody(cursor, definition))
        {
            return false;
        }
        document.definitions.push_back(std::move(definition));
    }
    if (document.definitions.empty())
    {
        cursor.Diagnostics().Error(
            "hoi3.unit_model.document_empty",
            "Unit model file contains no definitions"
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
