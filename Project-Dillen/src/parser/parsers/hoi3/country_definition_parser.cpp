#include "country_definition_parser.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace dillen::parser::hoi3 {

namespace {

bool EqualsIgnoreCase(
    std::string_view first,
    std::string_view second
)
{
    return first.size() == second.size()
        && std::equal(
            first.begin(),
            first.end(),
            second.begin(),
            [](char left, char right)
            {
                return std::tolower(
                    static_cast<unsigned char>(left)
                ) == std::tolower(
                    static_cast<unsigned char>(right)
                );
            }
        );
}

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
            "hoi3.country.expected_assignment",
            "country definition fields must use '='",
            key.span
        );
        return false;
    }
    return true;
}

bool ReadDateValue(
    ParserCursor& cursor,
    content::DefinitionDate& output
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

bool ReadUnsignedId(
    ParserCursor& cursor,
    Token& token,
    std::uint64_t& output
)
{
    if (!cursor.ReadKey(token))
    {
        return false;
    }
    std::string text(token.text);
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(
        text.c_str(),
        &end,
        10
    );
    if (errno == ERANGE
        || end != text.c_str() + text.size())
    {
        cursor.Diagnostics().Error(
            "hoi3.country.minister_id_invalid",
            "minister key must be an unsigned integer ID",
            token.span
        );
        return false;
    }
    output = static_cast<std::uint64_t>(value);
    return true;
}

bool ParseColor(
    ParserCursor& cursor,
    content::CountryColor& output
)
{
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for country color"))
    {
        return false;
    }
    std::int64_t components[3]{};
    for (std::int64_t& component : components)
    {
        if (!cursor.ReadInt64(component))
        {
            return false;
        }
        if (component < 0 || component > 255)
        {
            cursor.Diagnostics().Error(
                "hoi3.country.color_out_of_range",
                "country color components must be between 0 and 255"
            );
            return false;
        }
    }
    if (!cursor.Expect(TokenKind::RightBrace, nullptr, "after country color"))
    {
        return false;
    }
    output = {
        static_cast<std::uint8_t>(components[0]),
        static_cast<std::uint8_t>(components[1]),
        static_cast<std::uint8_t>(components[2])
    };
    return true;
}

bool ReadFreeScalarBlock(
    ParserCursor& cursor,
    std::vector<std::string>& values,
    std::string_view context
)
{
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, context))
    {
        return false;
    }
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.country.unterminated_list",
                "unexpected end of file in country list"
            );
            return false;
        }
        Token value;
        if (!cursor.ReadScalar(value))
        {
            return false;
        }
        values.emplace_back(value.text);
    }
    return true;
}

bool ParseDefaultTemplates(
    ParserCursor& cursor,
    std::vector<content::DivisionTemplateDefinition>& output
)
{
    if (!cursor.Expect(
            TokenKind::LeftBrace,
            nullptr,
            "for default_templates"))
    {
        return false;
    }
    std::unordered_set<std::string> names;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.country.default_templates_unterminated",
                "unexpected end of file in default_templates"
            );
            return false;
        }
        Token name;
        if (!cursor.ReadKey(name)
            || !ExpectAssignment(cursor, name))
        {
            return false;
        }
        if (!names.emplace(name.text).second)
        {
            cursor.Diagnostics().Error(
                "hoi3.country.template_duplicate",
                "duplicate default template name",
                name.span
            );
            return false;
        }
        content::DivisionTemplateDefinition definition;
        definition.name = std::string(name.text);
        if (!ReadFreeScalarBlock(
                cursor,
                definition.brigadeTypes,
                "for default template"))
        {
            return false;
        }
        output.push_back(std::move(definition));
    }
    return true;
}

bool ParseUnitNames(
    ParserCursor& cursor,
    std::vector<content::UnitNamePoolDefinition>& output
)
{
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for unit_names"))
    {
        return false;
    }
    std::unordered_set<std::string> unitTypes;
    std::optional<Token> recoveredUnitType;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.country.unit_names_unterminated",
                "unexpected end of file in unit_names"
            );
            return false;
        }
        Token unitType;
        if (recoveredUnitType)
        {
            unitType = *recoveredUnitType;
            recoveredUnitType.reset();
        }
        else if (!cursor.ReadKey(unitType))
        {
            return false;
        }
        if (!ExpectAssignment(cursor, unitType))
        {
            return false;
        }
        if (!unitTypes.emplace(unitType.text).second)
        {
            cursor.Diagnostics().Error(
                "hoi3.country.unit_name_pool_duplicate",
                "duplicate unit name pool",
                unitType.span
            );
            return false;
        }
        content::UnitNamePoolDefinition pool;
        pool.unitType = std::string(unitType.text);
        if (!cursor.Expect(
                TokenKind::LeftBrace,
                nullptr,
                "for unit name pool"))
        {
            return false;
        }
        while (!cursor.ConsumeIf(TokenKind::RightBrace))
        {
            if (cursor.AtEnd())
            {
                cursor.Diagnostics().Error(
                    "hoi3.country.unterminated_list",
                    "unexpected end of file in country list"
                );
                return false;
            }

            Token value;
            if (!cursor.ReadScalar(value))
            {
                return false;
            }
            if (value.kind == TokenKind::Identifier
                && cursor.Peek().kind == TokenKind::Equals)
            {
                cursor.Diagnostics().Warning(
                    "hoi3.country.unit_name_pool_missing_right_brace",
                    "presumed a missing '}' before unit name pool '"
                        + std::string(value.text) + "'",
                    value.span
                );
                recoveredUnitType = value;
                break;
            }
            pool.names.emplace_back(value.text);
        }
        output.push_back(std::move(pool));
    }
    return true;
}

bool ParseMinister(
    ParserCursor& cursor,
    content::MinisterDefinition& output
)
{
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for minister"))
    {
        return false;
    }
    bool hasName = false;
    bool hasIdeology = false;
    bool hasLoyalty = false;
    bool hasPicture = false;
    bool hasStartDate = false;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.country.minister_unterminated",
                "unexpected end of file in minister definition"
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key)
            || !ExpectAssignment(cursor, key))
        {
            return false;
        }

        if (EqualsIgnoreCase(key.text, "name"))
        {
            Token value;
            if (hasName || !cursor.ReadScalar(value))
            {
                return false;
            }
            output.name = std::string(value.text);
            hasName = true;
        }
        else if (EqualsIgnoreCase(key.text, "ideology"))
        {
            Token value;
            if (hasIdeology || !cursor.ReadScalar(value))
            {
                return false;
            }
            output.ideology = std::string(value.text);
            hasIdeology = true;
        }
        else if (EqualsIgnoreCase(key.text, "loyalty"))
        {
            if (hasLoyalty || !cursor.ReadDouble(output.loyalty))
            {
                return false;
            }
            hasLoyalty = true;
        }
        else if (EqualsIgnoreCase(key.text, "picture"))
        {
            Token value;
            if (hasPicture || !cursor.ReadScalar(value))
            {
                return false;
            }
            output.picture = std::string(value.text);
            hasPicture = true;
        }
        else if (EqualsIgnoreCase(key.text, "start_date"))
        {
            if (hasStartDate || !ReadDateValue(cursor, output.startDate))
            {
                return false;
            }
            hasStartDate = true;
        }
        else if (EqualsIgnoreCase(key.text, "death_date"))
        {
            if (output.deathDate)
            {
                cursor.Diagnostics().Error(
                    "hoi3.country.minister_death_date_duplicate",
                    "duplicate minister death_date",
                    key.span
                );
                return false;
            }
            content::DefinitionDate date;
            if (!ReadDateValue(cursor, date))
            {
                return false;
            }
            output.deathDate = date;
        }
        else
        {
            Token value;
            if (!cursor.ReadScalar(value))
            {
                return false;
            }
            output.positions.push_back({
                std::string(key.text),
                std::string(value.text)
            });
        }
    }

    if (!hasName
        || !hasIdeology
        || !hasLoyalty
        || !hasPicture
        || !hasStartDate)
    {
        cursor.Diagnostics().Error(
            "hoi3.country.minister_required_field_missing",
            "minister requires name, ideology, loyalty, picture and start_date"
        );
        return false;
    }
    return true;
}

bool ParseMinisters(
    ParserCursor& cursor,
    std::vector<content::MinisterDefinition>& output
)
{
    if (!cursor.Expect(TokenKind::LeftBrace, nullptr, "for ministers"))
    {
        return false;
    }
    std::unordered_set<std::uint64_t> ids;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            const Token& end = cursor.Peek();
            cursor.Diagnostics().Warning(
                "hoi3.country.ministers_missing_right_brace",
                "presumed a missing final '}' after "
                    + std::to_string(output.size())
                    + " complete minister definitions",
                end.span
            );
            return true;
        }
        Token idToken;
        content::MinisterDefinition definition;
        if (!ReadUnsignedId(cursor, idToken, definition.id)
            || !ExpectAssignment(cursor, idToken))
        {
            return false;
        }
        if (!ids.emplace(definition.id).second)
        {
            cursor.Diagnostics().Error(
                "hoi3.country.minister_duplicate",
                "duplicate minister ID",
                idToken.span
            );
            return false;
        }
        if (!ParseMinister(cursor, definition))
        {
            return false;
        }
        output.push_back(std::move(definition));
    }
    return true;
}

bool SkipUnknownValue(
    ParserCursor& cursor,
    const Token& key
)
{
    cursor.Diagnostics().Warning(
        "hoi3.country.unknown_field",
        "unknown country definition field: " + std::string(key.text),
        key.span
    );
    if (cursor.Peek().kind == TokenKind::LeftBrace)
    {
        return cursor.SkipBlock();
    }
    Token ignored;
    return cursor.ReadScalar(ignored);
}

}

bool ParseCountryDefinition(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    CountryDefinitionDocument document;
    bool hasColor = false;
    bool hasGraphicalCulture = false;
    bool hasMajor = false;
    bool hasLastElection = false;
    bool hasDuration = false;
    bool hasDefaultTemplates = false;
    bool hasUnitNames = false;
    bool hasMinisters = false;

    while (!cursor.AtEnd())
    {
        Token key;
        if (!cursor.ReadKey(key)
            || !ExpectAssignment(cursor, key))
        {
            return false;
        }

        if (EqualsIgnoreCase(key.text, "color"))
        {
            content::CountryColor color;
            if (hasColor || !ParseColor(cursor, color))
            {
                return false;
            }
            document.definition.color = color;
            hasColor = true;
        }
        else if (EqualsIgnoreCase(key.text, "graphical_culture"))
        {
            Token value;
            if (hasGraphicalCulture || !cursor.ReadScalar(value))
            {
                return false;
            }
            document.definition.graphicalCulture = std::string(value.text);
            hasGraphicalCulture = true;
        }
        else if (EqualsIgnoreCase(key.text, "major"))
        {
            if (hasMajor || !cursor.ReadBool(document.definition.major))
            {
                return false;
            }
            hasMajor = true;
        }
        else if (EqualsIgnoreCase(key.text, "last_election"))
        {
            content::DefinitionDate date;
            if (hasLastElection || !ReadDateValue(cursor, date))
            {
                return false;
            }
            document.definition.lastElection = date;
            hasLastElection = true;
        }
        else if (EqualsIgnoreCase(key.text, "duration"))
        {
            std::int64_t duration = 0;
            if (hasDuration || !cursor.ReadInt64(duration))
            {
                return false;
            }
            if (duration < 0
                || static_cast<std::uint64_t>(duration)
                    > std::numeric_limits<std::uint32_t>::max())
            {
                cursor.Diagnostics().Error(
                    "hoi3.country.duration_out_of_range",
                    "election duration must be a non-negative 32-bit value",
                    key.span
                );
                return false;
            }
            document.definition.electionDurationMonths =
                static_cast<std::uint32_t>(duration);
            hasDuration = true;
        }
        else if (EqualsIgnoreCase(key.text, "default_templates"))
        {
            if (hasDefaultTemplates
                || !ParseDefaultTemplates(
                    cursor,
                    document.definition.defaultTemplates))
            {
                return false;
            }
            hasDefaultTemplates = true;
        }
        else if (EqualsIgnoreCase(key.text, "unit_names"))
        {
            if (hasUnitNames
                || !ParseUnitNames(
                    cursor,
                    document.definition.unitNamePools))
            {
                return false;
            }
            hasUnitNames = true;
        }
        else if (EqualsIgnoreCase(key.text, "ministers"))
        {
            if (hasMinisters
                || !ParseMinisters(
                    cursor,
                    document.definition.ministers))
            {
                return false;
            }
            hasMinisters = true;
        }
        else if (!SkipUnknownValue(cursor, key))
        {
            return false;
        }
    }

    if (!hasColor || !hasGraphicalCulture)
    {
        cursor.Diagnostics().Error(
            "hoi3.country.required_field_missing",
            "country definition requires color and graphical_culture"
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
