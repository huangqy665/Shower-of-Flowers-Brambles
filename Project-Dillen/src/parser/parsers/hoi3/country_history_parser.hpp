#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "country_history.hpp"
#include "definition_date.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"

namespace dillen::parser::hoi3 {

using UnresolvedCountryHistoryValue = std::variant<
    std::int64_t,
    double,
    bool,
    std::string,
    dillen::compatibility::hoi3::content::CountryAlignment,
    dillen::compatibility::hoi3::content::CountryHistoryNamedNumberMap
>;

struct UnresolvedCountryHistoryOperation
{
    dillen::compatibility::hoi3::content::CountryHistoryField field =
        dillen::compatibility::hoi3::content::CountryHistoryField::NamedAssignment;
    std::string key;
    UnresolvedCountryHistoryValue value = std::int64_t{0};
    SourceSpan span;
};

struct UnresolvedCountryHistoryPatch
{
    dillen::compatibility::hoi3::content::DefinitionDate date;
    std::vector<UnresolvedCountryHistoryOperation> operations;
    SourceSpan span;
};

struct CountryHistoryDocument
{
    std::vector<UnresolvedCountryHistoryOperation> initialOperations;
    std::vector<UnresolvedCountryHistoryPatch> patches;
};

bool ParseCountryHistory(
    ParserCursor& cursor,
    ParseArtifact& artifact
);

}
