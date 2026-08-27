#include "region_definition_parser.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace dillen::parser::hoi3 {

namespace {

bool ExpectAssignment(
    ParserCursor& cursor,
    const Token& key
)
{
    RelationOperator relation;
    if (!cursor.ReadRelation(relation))
    {
        return false;
    }
    if (relation != RelationOperator::Assign)
    {
        cursor.Diagnostics().Error(
            "hoi3.region.expected_assignment",
            "Region definitions must use '='",
            key.span
        );
        return false;
    }
    return true;
}

bool ParseProvinceId(
    const Token& token,
    std::uint32_t& output
)
{
    std::uint64_t value = 0;
    const char* begin = token.text.data();
    const char* end = token.text.data() + token.text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{}
        || result.ptr != end
        || value == 0
        || value > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

}

bool ParseRegionDefinitions(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    RegionDefinitionDocument document;
    std::unordered_map<std::string, SourceSpan> names;
    while (!cursor.AtEnd())
    {
        Token name;
        if (!cursor.ReadKey(name))
        {
            return false;
        }
        if (name.kind != TokenKind::Identifier)
        {
            cursor.Diagnostics().Error(
                "hoi3.region.name_invalid",
                "Region name must be an identifier",
                name.span
            );
            return false;
        }
        if (!ExpectAssignment(cursor, name)
            || !cursor.Expect(
                TokenKind::LeftBrace,
                nullptr,
                "for Region definition"))
        {
            return false;
        }

        const auto nameResult = names.emplace(
            std::string(name.text),
            name.span
        );
        if (!nameResult.second)
        {
            cursor.Diagnostics().Error(
                "hoi3.region.name_duplicate",
                "duplicate Region name; first declared on line "
                    + std::to_string(
                        nameResult.first->second.begin.line
                    ),
                name.span
            );
            return false;
        }

        RegionDefinitionDeclaration declaration;
        declaration.name = std::string(name.text);
        declaration.nameSpan = name.span;
        while (!cursor.ConsumeIf(TokenKind::RightBrace))
        {
            if (cursor.AtEnd())
            {
                cursor.Diagnostics().Error(
                    "hoi3.region.unterminated",
                    "unexpected end of file in Region definition",
                    name.span
                );
                return false;
            }
            Token member;
            if (!cursor.ReadScalar(member))
            {
                return false;
            }
            if (member.kind == TokenKind::Number)
            {
                RegionProvinceReference reference;
                if (!ParseProvinceId(member, reference.value))
                {
                    cursor.Diagnostics().Error(
                        "hoi3.region.province_id_invalid",
                        "Region Province ID must be a positive 32-bit integer",
                        member.span
                    );
                    return false;
                }
                reference.span = member.span;
                declaration.provinces.push_back(reference);
            }
            else if (member.kind == TokenKind::Identifier)
            {
                declaration.flags.push_back({
                    std::string(member.text),
                    member.span
                });
            }
            else
            {
                cursor.Diagnostics().Error(
                    "hoi3.region.member_invalid",
                    "Region members must be Province IDs or bare flags",
                    member.span
                );
                return false;
            }
        }
        if (declaration.provinces.empty())
        {
            cursor.Diagnostics().Error(
                "hoi3.region.province_list_empty",
                "Region must contain at least one Province ID",
                name.span
            );
            return false;
        }
        document.declarations.push_back(std::move(declaration));
    }

    if (document.declarations.empty())
    {
        cursor.Diagnostics().Error(
            "hoi3.region.document_empty",
            "map/region.txt contains no Region definitions"
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
