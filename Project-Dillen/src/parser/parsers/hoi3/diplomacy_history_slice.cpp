#include "diplomacy_history_slice.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "country_tag_slice.hpp"
#include "diplomacy_history_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

content::DefinitionOrigin MakeOrigin(
    const ParsedFile& file,
    const SourceSpan& span
)
{
    return {
        std::string(file.source.VirtualPath()),
        file.catalog.sourceLayerName,
        span.begin.line,
        span.begin.column
    };
}

bool ResolveDiplomacyHistory(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    std::vector<const ParsedFile*> files;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type == kDiplomacyHistoryDocumentType)
        {
            files.push_back(&file);
        }
    }
    std::stable_sort(
        files.begin(),
        files.end(),
        [](const ParsedFile* first, const ParsedFile* second)
        {
            if (first->catalog.sourcePriority
                != second->catalog.sourcePriority)
            {
                return first->catalog.sourcePriority
                    < second->catalog.sourcePriority;
            }
            if (first->catalog.sourceLayer != second->catalog.sourceLayer)
            {
                return first->catalog.sourceLayer
                    < second->catalog.sourceLayer;
            }
            return first->catalog.virtualPath < second->catalog.virtualPath;
        }
    );

    for (const ParsedFile* file : files)
    {
        const DiplomacyHistoryDocument* document =
            file->result.artifact.As<DiplomacyHistoryDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.diplomacy_history.artifact_type_mismatch",
                "diplomacy history parser returned an invalid artifact"
            );
            continue;
        }
        for (const ParsedDiplomaticRelation& parsed : document->relations)
        {
            const content::CountryDefinitionId first =
                parsed.first.StableId();
            const content::CountryDefinitionId second =
                parsed.second.StableId();
            if (definitions.Countries().Find(first) == nullptr
                || definitions.Countries().Find(second) == nullptr)
            {
                diagnostics.Error(
                    "hoi3.diplomacy_history.country_unresolved",
                    "diplomacy relation references a Country outside the active VFS",
                    parsed.span
                );
                continue;
            }
            content::DiplomaticRelationPeriod period;
            period.startDate = parsed.startDate;
            period.endDate = parsed.endDate;
            period.origin = MakeOrigin(*file, parsed.span);
            const auto result = definitions.DiplomacyHistories().Append(
                {parsed.kind, first, second},
                std::move(period)
            );
            if (result == content::DiplomacyHistoryAppendResult::InvalidPeriod
                || result == content::DiplomacyHistoryAppendResult::Frozen)
            {
                diagnostics.Error(
                    "hoi3.diplomacy_history.append_failed",
                    "diplomacy relation could not be added to the Registry",
                    parsed.span
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterDiplomacyHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kDiplomacyHistoryTemplate;
    fileTemplate.name = "hoi3_diplomacy_history";
    fileTemplate.pattern = "history/diplomacy/*.txt";
    fileTemplate.parser = kDiplomacyHistoryParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kDiplomacyHistoryParser;
    parser.name = "hoi3_diplomacy_history";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kDiplomacyHistoryDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseDiplomacyHistory;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kDiplomacyHistoryTemplate);
        return false;
    }

    AnalysisPassDescriptor pass;
    pass.id = kDiplomacyHistoryResolvePass;
    pass.name = "hoi3_diplomacy_history_resolve";
    pass.phase = AnalysisPhase::Resolve;
    pass.priority = -1600;
    pass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveDiplomacyHistory(
            workspace,
            diagnostics,
            definitions
        );
    };
    if (!analyzer.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kDiplomacyHistoryParser);
        templates.Unregister(kDiplomacyHistoryTemplate);
        return false;
    }
    return true;
}

}
