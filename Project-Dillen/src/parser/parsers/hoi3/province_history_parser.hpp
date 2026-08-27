#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "definition_date.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "province_history.hpp"
#include "source_buffer.hpp"

namespace dillen::parser::hoi3 {

using UnresolvedProvinceHistoryValue = std::variant<
    std::string,
    std::int64_t,
    double
>;

struct UnresolvedProvinceHistoryOperation
{
    content::ProvinceHistoryField field =
        content::ProvinceHistoryField::Owner;
    UnresolvedProvinceHistoryValue value = std::string{};
    SourceSpan span;
};

struct UnresolvedProvinceHistoryPatch
{
    content::DefinitionDate date;
    std::vector<UnresolvedProvinceHistoryOperation> operations;
    SourceSpan span;
};

struct ProvinceHistoryDocument
{
    std::vector<UnresolvedProvinceHistoryOperation> initialOperations;
    std::vector<UnresolvedProvinceHistoryPatch> patches;
};

bool ParseProvinceHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
