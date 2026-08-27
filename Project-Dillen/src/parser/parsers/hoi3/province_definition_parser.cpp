#include "province_definition_parser.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace dillen::parser::hoi3 {

namespace {

std::string_view TrimAscii(std::string_view text)
{
    while (!text.empty()
        && (text.front() == ' '
            || text.front() == '\t'
            || text.front() == '\r'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty()
        && (text.back() == ' '
            || text.back() == '\t'
            || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    return text;
}

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

SourceSpan MakeLineSpan(
    const SourceBuffer& source,
    std::size_t offset,
    std::size_t length,
    std::uint32_t line
)
{
    SourceSpan span;
    span.begin = {source.Id(), offset, line, 1};
    span.end = {
        source.Id(),
        offset + length,
        line,
        static_cast<std::uint32_t>(length + 1)
    };
    return span;
}

bool ParseUnsigned(
    std::string_view text,
    std::uint32_t& output
)
{
    text = TrimAscii(text);
    if (text.empty())
    {
        return false;
    }
    std::uint64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc{}
        || result.ptr != end
        || value > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

bool TryDecodeWholeRecordQuote(
    std::string_view line,
    std::string& storage
)
{
    if (line.size() < 2
        || line.front() != '"'
        || line.back() != '"')
    {
        return false;
    }

    for (std::size_t index = 1; index + 1 < line.size(); ++index)
    {
        if (line[index] != '"')
        {
            continue;
        }
        if (index + 2 >= line.size() || line[index + 1] != '"')
        {
            return false;
        }
        ++index;
    }

    storage.clear();
    storage.reserve(line.size() - 2);
    for (std::size_t index = 1; index + 1 < line.size(); ++index)
    {
        if (line[index] == '"'
            && index + 2 < line.size()
            && line[index + 1] == '"')
        {
            storage.push_back('"');
            ++index;
        }
        else
        {
            storage.push_back(line[index]);
        }
    }
    return true;
}

bool SplitSemicolonCsv(
    std::string_view line,
    std::vector<std::string>& fields
)
{
    fields.clear();
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        const char character = line[index];
        if (character == '"')
        {
            if (quoted
                && index + 1 < line.size()
                && line[index + 1] == '"')
            {
                field.push_back('"');
                ++index;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else if (character == ';' && !quoted)
        {
            fields.push_back(std::move(field));
            field.clear();
        }
        else
        {
            field.push_back(character);
        }
    }
    if (quoted)
    {
        return false;
    }
    fields.push_back(std::move(field));
    return true;
}

bool ParseColorComponent(
    std::string_view text,
    std::uint8_t& output
)
{
    std::uint32_t value = 0;
    if (!ParseUnsigned(text, value) || value > 255)
    {
        return false;
    }
    output = static_cast<std::uint8_t>(value);
    return true;
}

}

bool ParseProvinceDefinitionCsv(
    const SourceBuffer& source,
    DiagnosticBag& diagnostics,
    ParseArtifact& artifact
)
{
    ProvinceDefinitionDocument document;
    std::unordered_map<std::uint32_t, std::uint32_t> idLines;
    std::unordered_map<std::uint32_t, std::uint32_t> colorLines;
    const std::string_view bytes = source.Bytes();
    std::size_t offset = 0;
    std::uint32_t lineNumber = 1;
    bool headerRead = false;
    std::string decodedLine;
    std::vector<std::string> fields;

    while (offset < bytes.size())
    {
        const std::size_t newline = bytes.find('\n', offset);
        const std::size_t lineEnd = newline == std::string_view::npos
            ? bytes.size()
            : newline;
        std::string_view line = bytes.substr(offset, lineEnd - offset);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (lineNumber == 1
            && line.size() >= 3
            && static_cast<unsigned char>(line[0]) == 0xEF
            && static_cast<unsigned char>(line[1]) == 0xBB
            && static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line.remove_prefix(3);
        }
        const SourceSpan lineSpan = MakeLineSpan(
            source,
            offset,
            lineEnd - offset,
            lineNumber
        );

        if (!TrimAscii(line).empty())
        {
            if (TryDecodeWholeRecordQuote(line, decodedLine))
            {
                line = decodedLine;
                ++document.compatibilityWrappedRowCount;
            }
            if (!SplitSemicolonCsv(line, fields))
            {
                diagnostics.Error(
                    "hoi3.province.csv_unterminated_field_quote",
                    "unterminated field quote in map/definition.csv",
                    lineSpan
                );
                return false;
            }

            if (!headerRead)
            {
                if (fields.size() < 4
                    || !EqualsAsciiIgnoreCase(
                        TrimAscii(fields[0]),
                        "province")
                    || !EqualsAsciiIgnoreCase(
                        TrimAscii(fields[1]),
                        "red")
                    || !EqualsAsciiIgnoreCase(
                        TrimAscii(fields[2]),
                        "green")
                    || !EqualsAsciiIgnoreCase(
                        TrimAscii(fields[3]),
                        "blue"))
                {
                    diagnostics.Error(
                        "hoi3.province.csv_header_invalid",
                        "map/definition.csv must begin with province;red;green;blue",
                        lineSpan
                    );
                    return false;
                }
                headerRead = true;
            }
            else
            {
                if (fields.size() < 4)
                {
                    diagnostics.Error(
                        "hoi3.province.csv_column_missing",
                        "province row requires at least four columns",
                        lineSpan
                    );
                    return false;
                }

                content::ProvinceColor color;
                if (!ParseColorComponent(fields[1], color.red)
                    || !ParseColorComponent(fields[2], color.green)
                    || !ParseColorComponent(fields[3], color.blue))
                {
                    diagnostics.Error(
                        "hoi3.province.csv_color_invalid",
                        "province RGB values must be integers from 0 to 255",
                        lineSpan
                    );
                    return false;
                }

                const std::string_view idText = TrimAscii(fields[0]);
                if (idText.empty())
                {
                    ++document.paletteRowCount;
                }
                else
                {
                    std::uint32_t id = 0;
                    if (fields.size() < 5
                        || !ParseUnsigned(idText, id)
                        || id == 0)
                    {
                        diagnostics.Error(
                            "hoi3.province.csv_id_invalid",
                            "province ID must be a positive 32-bit integer",
                            lineSpan
                        );
                        return false;
                    }
                    const auto idResult = idLines.emplace(id, lineNumber);
                    if (!idResult.second)
                    {
                        diagnostics.Error(
                            "hoi3.province.csv_id_duplicate",
                            "duplicate province ID; first declared on line "
                                + std::to_string(idResult.first->second),
                            lineSpan
                        );
                        return false;
                    }
                    const std::uint32_t packedRgb = color.PackedRgb();
                    const auto colorResult = colorLines.emplace(
                        packedRgb,
                        lineNumber
                    );
                    if (!colorResult.second)
                    {
                        diagnostics.Error(
                            "hoi3.province.csv_color_duplicate",
                            "duplicate province RGB; first declared on line "
                                + std::to_string(colorResult.first->second),
                            lineSpan
                        );
                        return false;
                    }

                    content::ProvinceDefinition definition;
                    definition.id = {id};
                    definition.color = color;
                    definition.name = fields[4];
                    definition.origin.virtualPath =
                        std::string(source.VirtualPath());
                    definition.origin.line = lineNumber;
                    definition.origin.column = 1;
                    document.definitions.push_back(std::move(definition));
                }
            }
        }

        if (newline == std::string_view::npos)
        {
            break;
        }
        offset = newline + 1;
        ++lineNumber;
    }

    if (!headerRead || document.definitions.empty())
    {
        diagnostics.Error(
            "hoi3.province.csv_empty",
            "map/definition.csv contains no province definitions"
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

}
