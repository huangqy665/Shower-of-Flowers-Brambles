#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parse_result.hpp"
#include "parser_registry.hpp"
#include "source_buffer.hpp"

namespace dillen::parser {

using AnalysisPassId = std::uint64_t;

enum class AnalysisPhase
{
    Declare,
    Resolve,
    Validate
};

struct ParsedFile
{
    CatalogFile catalog;
    SourceBuffer source;
    ParseResult result;
};

struct AnalysisWorkspace
{
    std::vector<ParsedFile> files;

    void Clear();
};

using AnalysisPassFunction = std::function<bool(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics
)>;

struct AnalysisPassDescriptor
{
    AnalysisPassId id = 0;
    std::string name;
    AnalysisPhase phase = AnalysisPhase::Declare;
    int priority = 0;
    AnalysisPassFunction run;
};

class Analyzer
{
public:
    bool RegisterPass(AnalysisPassDescriptor pass);
    bool UnregisterPass(AnalysisPassId id);
    void ClearPasses();
    void Freeze();
    bool IsFrozen() const noexcept;
    bool Analyze(
        const FileCatalog& catalog,
        const ParserRegistry& parsers,
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics
    ) const;
    const std::vector<AnalysisPassDescriptor>& Passes() const noexcept;

private:
    std::vector<AnalysisPassDescriptor> passes_;
    bool frozen_ = false;
};

}
