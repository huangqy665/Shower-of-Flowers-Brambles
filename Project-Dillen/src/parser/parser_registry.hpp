#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"
#include "template.hpp"

namespace dillen::parser {

using ParseFunction = std::function<bool(
    ParserCursor& cursor,
    ParseArtifact& artifact
)>;

using SourceParseFunction = std::function<bool(
    const SourceBuffer& source,
    DiagnosticBag& diagnostics,
    ParseArtifact& artifact
)>;

struct ParserDescriptor
{
    ParserId id = 0;
    std::string name;
    DialectId inputDialect = 0;
    DefinitionTypeId outputType = 0;
    std::uint32_t schemaVersion = 1;
    bool requiresEndOfFile = true;
    ParseFunction parse;
    SourceParseFunction parseSource;
};

class ParserRegistry
{
public:
    bool Register(ParserDescriptor descriptor);
    bool Unregister(ParserId id);
    void Clear();
    void Freeze() noexcept;
    bool IsFrozen() const noexcept;
    const ParserDescriptor* Find(ParserId id) const;
    bool Contains(ParserId id) const;
    ParseResult Parse(
        const SourceBuffer& source,
        const TemplateMatch& fileTemplate,
        DiagnosticBag& diagnostics
    ) const;
    const std::vector<ParserDescriptor>& All() const noexcept;

private:
    std::vector<ParserDescriptor> parsers_;
    bool frozen_ = false;
};

}
