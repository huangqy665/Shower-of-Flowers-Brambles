#include "country_tag_parser.hpp"

#include <utility>

namespace dillen::parser::hoi3 {

bool ParseCountryTagIndex(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    CountryTagDocument document;
    while (!cursor.AtEnd())
    {
        Token tagToken;
        if (!cursor.ReadKey(tagToken))
        {
            return false;
        }
        const auto tag = dillen::compatibility::hoi3::content::CountryTag::Parse(tagToken.text);
        if (!tag)
        {
            cursor.Diagnostics().Error(
                "hoi3.country_tag.invalid",
                "country tag must contain exactly three ASCII tag characters",
                tagToken.span
            );
            return false;
        }

        RelationOperator relation;
        if (!cursor.ReadRelation(relation))
        {
            return false;
        }
        if (relation != RelationOperator::Assign)
        {
            cursor.Diagnostics().Error(
                "hoi3.country_tag.expected_assignment",
                "country tag entries must use '='",
                tagToken.span
            );
            return false;
        }

        Token pathToken;
        if (!cursor.ReadScalar(pathToken))
        {
            return false;
        }
        if (pathToken.kind != TokenKind::String)
        {
            cursor.Diagnostics().Error(
                "hoi3.country_tag.path_not_quoted",
                "country definition path must be a quoted string",
                pathToken.span
            );
            return false;
        }
        if (pathToken.text.empty())
        {
            cursor.Diagnostics().Error(
                "hoi3.country_tag.path_empty",
                "country definition path cannot be empty",
                pathToken.span
            );
            return false;
        }

        document.declarations.push_back({
            *tag,
            std::string(pathToken.text),
            tagToken.span,
            pathToken.span
        });
    }
    artifact.value = std::move(document);
    return true;
}

}
