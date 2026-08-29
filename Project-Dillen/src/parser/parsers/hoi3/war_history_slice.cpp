#include "war_history_slice.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "country_tag_slice.hpp"
#include "war_history_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

dillen::compatibility::hoi3::content::DefinitionOrigin MakeOrigin(
    const ParsedFile& file,
    const SourceSpan& span
)
{
    return {
        std::string(file.source.VirtualPath()),
        file.catalog.sourceLayerName,
        span.IsValid() ? span.begin.line : 1,
        span.IsValid() ? span.begin.column : 1
    };
}

bool ResolveCountry(
    dillen::compatibility::hoi3::content::CountryTag tag,
    const SourceSpan& span,
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::CountryDefinitionId& output
)
{
    output = tag.StableId();
    if (definitions.Countries().Find(output) != nullptr)
    {
        return true;
    }
    diagnostics.Error(
        "hoi3.war_history.country_unresolved",
        "war history references Country '" + tag.ToString()
            + "' outside the active VFS",
        span
    );
    return false;
}

bool ResolveWarHistories(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    std::vector<const ParsedFile*> files;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type == kWarHistoryDocumentType)
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
        const WarHistoryDocument* document =
            file->result.artifact.As<WarHistoryDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.war_history.artifact_type_mismatch",
                "war history parser returned an invalid artifact"
            );
            continue;
        }

        dillen::compatibility::hoi3::content::WarHistoryTimeline timeline;
        timeline.virtualPath = dillen::compatibility::hoi3::content::NormalizeWarHistoryPath(
            file->source.VirtualPath()
        );
        timeline.id = dillen::compatibility::hoi3::content::StableWarHistoryDefinitionId(
            timeline.virtualPath
        );
        timeline.name = document->name;
        timeline.limitedWar = document->limitedWar;
        timeline.origin = MakeOrigin(*file, document->span);

        bool resolved = true;
        timeline.patches.reserve(document->patches.size());
        for (const UnresolvedWarHistoryPatch& parsedPatch
            : document->patches)
        {
            dillen::compatibility::hoi3::content::WarHistoryPatch patch;
            patch.date = parsedPatch.date;
            patch.origin = MakeOrigin(*file, parsedPatch.span);
            patch.participantOperations.reserve(
                parsedPatch.participantOperations.size()
            );
            for (const UnresolvedWarParticipantOperation& parsedOperation
                : parsedPatch.participantOperations)
            {
                dillen::compatibility::hoi3::content::WarParticipantOperation operation;
                operation.kind = parsedOperation.kind;
                operation.origin = MakeOrigin(*file, parsedOperation.span);
                resolved = ResolveCountry(
                    parsedOperation.country,
                    parsedOperation.span,
                    definitions,
                    diagnostics,
                    operation.country
                ) && resolved;
                patch.participantOperations.push_back(std::move(operation));
            }
            patch.warGoals.reserve(parsedPatch.warGoals.size());
            for (const UnresolvedWarGoal& parsedGoal : parsedPatch.warGoals)
            {
                dillen::compatibility::hoi3::content::WarGoalDefinition goal;
                goal.casusBelli = parsedGoal.casusBelli;
                goal.origin = MakeOrigin(*file, parsedGoal.span);
                const bool actorResolved = ResolveCountry(
                    parsedGoal.actor,
                    parsedGoal.span,
                    definitions,
                    diagnostics,
                    goal.actor
                );
                const bool receiverResolved = ResolveCountry(
                    parsedGoal.receiver,
                    parsedGoal.span,
                    definitions,
                    diagnostics,
                    goal.receiver
                );
                resolved = actorResolved && receiverResolved && resolved;
                patch.warGoals.push_back(std::move(goal));
            }
            timeline.patches.push_back(std::move(patch));
        }
        if (!resolved)
        {
            continue;
        }
        if (definitions.WarHistories().Declare(std::move(timeline))
            != dillen::compatibility::hoi3::content::WarHistoryDeclareResult::Added)
        {
            diagnostics.Error(
                "hoi3.war_history.declare_failed",
                "war history timeline could not be declared",
                document->span
            );
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterWarHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kWarHistoryTemplate;
    fileTemplate.name = "hoi3_war_history";
    fileTemplate.pattern = "history/wars/*.txt";
    fileTemplate.parser = kWarHistoryParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kWarHistoryParser;
    parser.name = "hoi3_war_history";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kWarHistoryDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseWarHistory;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kWarHistoryTemplate);
        return false;
    }

    ResolutionPassDescriptor pass;
    pass.id = kWarHistoryResolvePass;
    pass.name = "hoi3_war_history_resolve";
    pass.phase = ResolutionPhase::Resolve;
    pass.priority = -1500;
    pass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveWarHistories(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kWarHistoryParser);
        templates.Unregister(kWarHistoryTemplate);
        return false;
    }
    return true;
}

}
