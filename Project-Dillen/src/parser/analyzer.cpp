#include "analyzer.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <utility>

namespace dillen::parser {

namespace {

bool ReadSource(
    const CatalogFile& file,
    SourceId sourceId,
    SourceBuffer& output,
    DiagnosticBag& diagnostics
)
{
    std::ifstream stream(file.physicalPath, std::ios::binary);
    if (!stream)
    {
        diagnostics.Error(
            "analyzer.source_open_failed",
            "could not open " + file.physicalPath.u8string()
        );
        return false;
    }
    std::string bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
    if (!stream.good() && !stream.eof())
    {
        diagnostics.Error(
            "analyzer.source_read_failed",
            "could not read " + file.physicalPath.u8string()
        );
        return false;
    }
    output = SourceBuffer(
        sourceId,
        file.virtualPath,
        file.physicalPath.u8string(),
        std::move(bytes),
        file.encoding
    );
    return true;
}

int PhaseOrder(AnalysisPhase phase)
{
    switch (phase)
    {
    case AnalysisPhase::Declare: return 0;
    case AnalysisPhase::Resolve: return 1;
    case AnalysisPhase::Validate: return 2;
    }
    return 3;
}

}

void AnalysisWorkspace::Clear()
{
    files.clear();
}

bool Analyzer::RegisterPass(AnalysisPassDescriptor pass)
{
    if (frozen_
        || pass.id == 0
        || pass.name.empty()
        || !pass.run
        || std::any_of(
            passes_.begin(),
            passes_.end(),
            [&pass](const AnalysisPassDescriptor& item)
            {
                return item.id == pass.id;
            }))
    {
        return false;
    }
    passes_.push_back(std::move(pass));
    return true;
}

bool Analyzer::UnregisterPass(AnalysisPassId id)
{
    if (frozen_)
    {
        return false;
    }
    const auto iterator = std::find_if(
        passes_.begin(),
        passes_.end(),
        [id](const AnalysisPassDescriptor& pass)
        {
            return pass.id == id;
        }
    );
    if (iterator == passes_.end())
    {
        return false;
    }
    passes_.erase(iterator);
    return true;
}

void Analyzer::ClearPasses()
{
    if (!frozen_)
    {
        passes_.clear();
    }
}

void Analyzer::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        passes_.begin(),
        passes_.end(),
        [](const AnalysisPassDescriptor& first,
           const AnalysisPassDescriptor& second)
        {
            const int firstPhase = PhaseOrder(first.phase);
            const int secondPhase = PhaseOrder(second.phase);
            if (firstPhase != secondPhase)
            {
                return firstPhase < secondPhase;
            }
            if (first.priority != second.priority)
            {
                return first.priority < second.priority;
            }
            return first.id < second.id;
        }
    );
    frozen_ = true;
}

bool Analyzer::IsFrozen() const noexcept
{
    return frozen_;
}

bool Analyzer::Analyze(
    const FileCatalog& catalog,
    const ParserRegistry& parsers,
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics
) const
{
    workspace.Clear();
    if (!frozen_)
    {
        diagnostics.Fatal(
            "analyzer.not_frozen",
            "analyzer pass registry must be frozen before analysis"
        );
        return false;
    }
    if (!catalog.IsBuilt())
    {
        diagnostics.Fatal(
            "analyzer.catalog_not_built",
            "file catalog must be built before analysis"
        );
        return false;
    }
    if (!parsers.IsFrozen())
    {
        diagnostics.Fatal(
            "analyzer.parsers_not_frozen",
            "parser registry must be frozen before analysis"
        );
        return false;
    }

    workspace.files.reserve(catalog.ActiveClassifiedFileCount());
    SourceId nextSource = 1;
    bool parseSucceeded = true;
    for (const CatalogFile& file : catalog.Files())
    {
        if (file.disposition != CatalogDisposition::Active
            || !file.match)
        {
            continue;
        }
        ParsedFile parsed;
        parsed.catalog = file;
        if (!ReadSource(file, nextSource++, parsed.source, diagnostics))
        {
            parseSucceeded = false;
            continue;
        }
        workspace.files.push_back(std::move(parsed));
        ParsedFile& stored = workspace.files.back();
        stored.result = parsers.Parse(
            stored.source,
            *stored.catalog.match,
            diagnostics
        );
        parseSucceeded = stored.result.success && parseSucceeded;
    }

    if (!parseSucceeded)
    {
        return false;
    }

    for (const AnalysisPassDescriptor& pass : passes_)
    {
        bool succeeded = false;
        try
        {
            succeeded = pass.run(workspace, diagnostics);
        }
        catch (const std::exception& exception)
        {
            diagnostics.Fatal(
                "analyzer.pass_exception",
                pass.name + ": " + exception.what()
            );
        }
        catch (...)
        {
            diagnostics.Fatal(
                "analyzer.pass_exception",
                pass.name + ": unknown exception"
            );
        }
        if (!succeeded)
        {
            if (!diagnostics.HasErrors())
            {
                diagnostics.Error(
                    "analyzer.pass_failed",
                    pass.name + " failed without a diagnostic"
                );
            }
            return false;
        }
    }
    return !diagnostics.HasErrors();
}

const std::vector<AnalysisPassDescriptor>& Analyzer::Passes() const noexcept
{
    return passes_;
}

}
