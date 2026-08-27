#include "province_history_parser.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "country_tag_definition.hpp"

namespace dillen::parser::hoi3 {

namespace {

enum class FieldValueKind
{
    Country,
    Symbol,
    Integer,
    Decimal
};

struct FieldDescriptor
{
    content::ProvinceHistoryField field;
    FieldValueKind valueKind;
};

struct PendingKey
{
    std::string text;
    SourceSpan span;
};

bool EqualsAsciiIgnoreCase(
    std::string_view first,
    std::string_view second
)
{
    if (first.size() != second.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        char left = first[index];
        char right = second[index];
        if (left >= 'A' && left <= 'Z')
        {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z')
        {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right)
        {
            return false;
        }
    }
    return true;
}

std::optional<FieldDescriptor> DescribeField(std::string_view key)
{
    using content::ProvinceHistoryField;
    if (EqualsAsciiIgnoreCase(key, "owner"))
    {
        return FieldDescriptor{ProvinceHistoryField::Owner,
            FieldValueKind::Country};
    }
    if (EqualsAsciiIgnoreCase(key, "controller"))
    {
        return FieldDescriptor{ProvinceHistoryField::Controller,
            FieldValueKind::Country};
    }
    if (EqualsAsciiIgnoreCase(key, "add_core"))
    {
        return FieldDescriptor{ProvinceHistoryField::AddCore,
            FieldValueKind::Country};
    }
    if (EqualsAsciiIgnoreCase(key, "remove_core"))
    {
        return FieldDescriptor{ProvinceHistoryField::RemoveCore,
            FieldValueKind::Country};
    }
    if (EqualsAsciiIgnoreCase(key, "terrain"))
    {
        return FieldDescriptor{ProvinceHistoryField::Terrain,
            FieldValueKind::Symbol};
    }
    if (EqualsAsciiIgnoreCase(key, "strategic_resource"))
    {
        return FieldDescriptor{ProvinceHistoryField::StrategicResource,
            FieldValueKind::Symbol};
    }
    if (EqualsAsciiIgnoreCase(key, "infra"))
    {
        return FieldDescriptor{ProvinceHistoryField::Infrastructure,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "industry"))
    {
        return FieldDescriptor{ProvinceHistoryField::Industry,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "points"))
    {
        return FieldDescriptor{ProvinceHistoryField::VictoryPoints,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "naval_base"))
    {
        return FieldDescriptor{ProvinceHistoryField::NavalBase,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "air_base"))
    {
        return FieldDescriptor{ProvinceHistoryField::AirBase,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "anti_air"))
    {
        return FieldDescriptor{ProvinceHistoryField::AntiAir,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "land_fort"))
    {
        return FieldDescriptor{ProvinceHistoryField::LandFort,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "coastal_fort"))
    {
        return FieldDescriptor{ProvinceHistoryField::CoastalFort,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "radar_station"))
    {
        return FieldDescriptor{ProvinceHistoryField::RadarStation,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "rocket_test"))
    {
        return FieldDescriptor{ProvinceHistoryField::RocketTest,
            FieldValueKind::Integer};
    }
    if (EqualsAsciiIgnoreCase(key, "manpower"))
    {
        return FieldDescriptor{ProvinceHistoryField::Manpower,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "leadership"))
    {
        return FieldDescriptor{ProvinceHistoryField::Leadership,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "energy"))
    {
        return FieldDescriptor{ProvinceHistoryField::Energy,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "metal"))
    {
        return FieldDescriptor{ProvinceHistoryField::Metal,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "rare_materials"))
    {
        return FieldDescriptor{ProvinceHistoryField::RareMaterials,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "crude_oil"))
    {
        return FieldDescriptor{ProvinceHistoryField::CrudeOil,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "fuel"))
    {
        return FieldDescriptor{ProvinceHistoryField::Fuel,
            FieldValueKind::Decimal};
    }
    if (EqualsAsciiIgnoreCase(key, "supplies"))
    {
        return FieldDescriptor{ProvinceHistoryField::Supplies,
            FieldValueKind::Decimal};
    }
    return std::nullopt;
}

bool ExpectAssignment(
    ParserCursor& cursor,
    const SourceSpan& span
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
            "hoi3.province_history.expected_assignment",
            "Province history fields must use '='",
            span
        );
        return false;
    }
    return true;
}

bool ParseDateToken(
    const Token& token,
    content::DefinitionDate& output
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

bool ParseInteger(
    const Token& token,
    std::int64_t& output
)
{
    const char* begin = token.text.data();
    const char* end = token.text.data() + token.text.size();
    const auto result = std::from_chars(begin, end, output, 10);
    return result.ec == std::errc{}
        && result.ptr == end
        && output >= 0;
}

bool ParseDecimal(
    const Token& token,
    double& output
)
{
    const std::string text(token.text);
    errno = 0;
    char* end = nullptr;
    output = std::strtod(text.c_str(), &end);
    return errno != ERANGE
        && end == text.c_str() + text.size()
        && std::isfinite(output)
        && output >= 0.0;
}

SourceSpan SuffixSpan(
    const SourceSpan& span,
    std::size_t prefixLength
)
{
    SourceSpan suffix = span;
    suffix.begin.offset += prefixLength;
    suffix.begin.column += static_cast<std::uint32_t>(prefixLength);
    return suffix;
}

bool TryRecoverConcatenatedCountryAssignment(
    ParserCursor& cursor,
    const Token& value,
    std::string& country,
    std::optional<PendingKey>& pending
)
{
    if (value.text.size() <= 3
        || cursor.Peek().kind != TokenKind::Equals)
    {
        return false;
    }
    const std::string_view tagText = value.text.substr(0, 3);
    const std::string_view suffix = value.text.substr(3);
    if (!content::CountryTag::Parse(tagText)
        || !DescribeField(suffix))
    {
        return false;
    }
    country = std::string(tagText);
    pending = PendingKey{
        std::string(suffix),
        SuffixSpan(value.span, 3)
    };
    cursor.Diagnostics().Warning(
        "hoi3.province_history.concatenated_assignment_recovered",
        "recovered missing whitespace before Province history field '"
            + std::string(suffix) + "'",
        value.span
    );
    return true;
}

bool ParseOperation(
    ParserCursor& cursor,
    std::optional<PendingKey>& pending,
    std::vector<UnresolvedProvinceHistoryOperation>& output
)
{
    std::string keyText;
    SourceSpan keySpan;
    if (pending)
    {
        keyText = std::move(pending->text);
        keySpan = pending->span;
        pending.reset();
    }
    else
    {
        Token key;
        if (!cursor.ReadKey(key))
        {
            return false;
        }
        if (key.kind != TokenKind::Identifier)
        {
            cursor.Diagnostics().Error(
                "hoi3.province_history.field_invalid",
                "Province history operation must begin with a field name",
                key.span
            );
            return false;
        }
        keyText = std::string(key.text);
        keySpan = key.span;
    }

    const std::optional<FieldDescriptor> descriptor =
        DescribeField(keyText);
    if (!descriptor)
    {
        cursor.Diagnostics().Error(
            "hoi3.province_history.field_unknown",
            "unknown Province history field '" + keyText + "'",
            keySpan
        );
        return false;
    }
    if (!ExpectAssignment(cursor, keySpan))
    {
        return false;
    }

    Token value;
    if (!cursor.ReadScalar(value))
    {
        return false;
    }
    UnresolvedProvinceHistoryOperation operation;
    operation.field = descriptor->field;
    operation.span = keySpan;
    switch (descriptor->valueKind)
    {
    case FieldValueKind::Country:
    {
        std::string country;
        if (!TryRecoverConcatenatedCountryAssignment(
                cursor,
                value,
                country,
                pending))
        {
            const auto parsed = content::CountryTag::Parse(value.text);
            if (!parsed)
            {
                cursor.Diagnostics().Error(
                    "hoi3.province_history.country_invalid",
                    "Province history country must be a three-character Tag",
                    value.span
                );
                return false;
            }
            country = parsed->ToString();
        }
        operation.value = std::move(country);
        break;
    }
    case FieldValueKind::Symbol:
        if (value.kind != TokenKind::Identifier
            && value.kind != TokenKind::String)
        {
            cursor.Diagnostics().Error(
                "hoi3.province_history.symbol_invalid",
                "Province history symbol must be an identifier or string",
                value.span
            );
            return false;
        }
        operation.value = std::string(value.text);
        break;
    case FieldValueKind::Integer:
    {
        std::int64_t integer = 0;
        if (!ParseInteger(value, integer))
        {
            cursor.Diagnostics().Error(
                "hoi3.province_history.integer_invalid",
                "Province history level must be a non-negative integer",
                value.span
            );
            return false;
        }
        operation.value = integer;
        break;
    }
    case FieldValueKind::Decimal:
    {
        double decimal = 0.0;
        if (!ParseDecimal(value, decimal))
        {
            cursor.Diagnostics().Error(
                "hoi3.province_history.decimal_invalid",
                "Province history quantity must be a non-negative number",
                value.span
            );
            return false;
        }
        operation.value = decimal;
        break;
    }
    }
    output.push_back(std::move(operation));
    return true;
}

bool ParsePatchOperations(
    ParserCursor& cursor,
    std::vector<UnresolvedProvinceHistoryOperation>& output
)
{
    std::optional<PendingKey> pending;
    while (!cursor.ConsumeIf(TokenKind::RightBrace))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "hoi3.province_history.patch_unterminated",
                "unexpected end of file in Province history date Patch"
            );
            return false;
        }
        if (!ParseOperation(cursor, pending, output))
        {
            return false;
        }
    }
    return true;
}

}

bool ParseProvinceHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
)
{
    ProvinceHistoryDocument document;
    std::optional<PendingKey> pending;
    while (!cursor.AtEnd())
    {
        if (!pending && cursor.Peek().kind == TokenKind::RightBrace)
        {
            const Token brace = cursor.Consume();
            cursor.Diagnostics().Warning(
                "hoi3.province_history.orphan_right_brace_ignored",
                "ignored an unmatched root-level '}' in Province history",
                brace.span
            );
        }
        else if (!pending && cursor.Peek().kind == TokenKind::Date)
        {
            const Token dateToken = cursor.Consume();
            UnresolvedProvinceHistoryPatch patch;
            if (!ParseDateToken(dateToken, patch.date))
            {
                cursor.Diagnostics().Error(
                    "hoi3.province_history.date_invalid",
                    "invalid Province history Patch date",
                    dateToken.span
                );
                return false;
            }
            patch.span = dateToken.span;
            if (!ExpectAssignment(cursor, dateToken.span)
                || !cursor.Expect(
                    TokenKind::LeftBrace,
                    nullptr,
                    "for Province history date Patch")
                || !ParsePatchOperations(cursor, patch.operations))
            {
                return false;
            }
            document.patches.push_back(std::move(patch));
        }
        else if (!ParseOperation(
            cursor,
            pending,
            document.initialOperations))
        {
            return false;
        }
    }
    artifact.value = std::move(document);
    return true;
}

}
