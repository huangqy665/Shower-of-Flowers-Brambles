#include "launch_slice.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "file_catalog.hpp"
#include "launch_parser.hpp"
#include "country_tag_slice.hpp"

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

std::string ScenarioKeyFromPath(std::string_view path)
{
    const std::size_t slash = path.find_last_of('/');
    const std::size_t begin = slash == std::string_view::npos
        ? 0
        : slash + 1;
    const std::size_t dot = path.find_last_of('.');
    const std::size_t end = dot == std::string_view::npos || dot < begin
        ? path.size()
        : dot;
    return content::NormalizeScenarioKey(path.substr(begin, end - begin));
}

std::vector<content::CountryDefinitionId> ResolveCountries(
    const std::vector<content::CountryTag>& tags,
    const content::DefinitionRegistry& definitions,
    DiagnosticBag& diagnostics,
    const SourceSpan& span
)
{
    std::vector<content::CountryDefinitionId> resolved;
    resolved.reserve(tags.size());
    for (const content::CountryTag& tag : tags)
    {
        const content::CountryDefinitionId id = tag.StableId();
        if (definitions.Countries().Find(id) == nullptr)
        {
            diagnostics.Warning(
                "hoi3.launch.country_unresolved",
                "launch Country Tag is not declared in the active VFS: "
                    + tag.ToString(),
                span
            );
        }
        if (std::find(resolved.begin(), resolved.end(), id) == resolved.end())
        {
            resolved.push_back(id);
        }
    }
    return resolved;
}

bool DeclareLaunchDefinitions(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type == kBookmarkDocumentType)
        {
            const BookmarkDocument* document =
                file.result.artifact.As<BookmarkDocument>();
            if (document == nullptr)
            {
                diagnostics.Error(
                    "hoi3.bookmark.artifact_type_mismatch",
                    "bookmark parser returned an invalid artifact"
                );
                continue;
            }
            for (const ParsedBookmark& parsed : document->bookmarks)
            {
                content::BookmarkDefinition definition;
                definition.key = content::NormalizeBookmarkKey(parsed.name);
                definition.id = content::StableBookmarkDefinitionId(
                    definition.key
                );
                definition.name = parsed.name;
                definition.description = parsed.description;
                definition.icon = parsed.icon;
                definition.date = parsed.date;
                definition.recommendedCountries = ResolveCountries(
                    parsed.countries,
                    definitions,
                    diagnostics,
                    parsed.span
                );
                definition.origin = MakeOrigin(file, parsed.span);
                if (definitions.Launches().Declare(std::move(definition))
                    != content::LaunchDeclareResult::Added)
                {
                    diagnostics.Error(
                        "hoi3.bookmark.declare_failed",
                        "bookmark could not be added to Launch Registry",
                        parsed.span
                    );
                }
            }
        }
        else if (file.result.artifact.type == kScenarioDocumentType)
        {
            const ScenarioDocument* document =
                file.result.artifact.As<ScenarioDocument>();
            if (document == nullptr)
            {
                diagnostics.Error(
                    "hoi3.scenario.artifact_type_mismatch",
                    "scenario parser returned an invalid artifact"
                );
                continue;
            }
            content::ScenarioDefinition definition;
            definition.key = ScenarioKeyFromPath(file.source.VirtualPath());
            definition.id = content::StableScenarioDefinitionId(
                definition.key
            );
            definition.name = document->name;
            definition.description = document->description;
            definition.icon = document->icon;
            definition.startDate = document->startDate;
            definition.endDate = document->endDate;
            definition.selectableCountries = ResolveCountries(
                document->selectableCountries,
                definitions,
                diagnostics,
                document->span
            );
            definition.additionalCountries = ResolveCountries(
                document->additionalCountries,
                definitions,
                diagnostics,
                document->span
            );
            definition.origin = MakeOrigin(file, document->span);
            if (definitions.Launches().Declare(std::move(definition))
                != content::LaunchDeclareResult::Added)
            {
                diagnostics.Error(
                    "hoi3.scenario.declare_failed",
                    "scenario could not be added to Launch Registry",
                    document->span
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterLaunchDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate bookmarkTemplate;
    bookmarkTemplate.id = kBookmarkTemplate;
    bookmarkTemplate.name = "hoi3_bookmarks";
    bookmarkTemplate.pattern = "common/bookmarks.txt";
    bookmarkTemplate.parser = kBookmarkParser;
    bookmarkTemplate.dialect = kHoi3ClausewitzDialect;
    bookmarkTemplate.priority = 1000;
    if (!templates.Register(std::move(bookmarkTemplate)))
    {
        return false;
    }

    FileTemplate scenarioTemplate;
    scenarioTemplate.id = kScenarioTemplate;
    scenarioTemplate.name = "hoi3_scenario";
    scenarioTemplate.pattern = "scenarios/*.txt";
    scenarioTemplate.parser = kScenarioParser;
    scenarioTemplate.dialect = kHoi3ClausewitzDialect;
    scenarioTemplate.priority = 1000;
    if (!templates.Register(std::move(scenarioTemplate)))
    {
        templates.Unregister(kBookmarkTemplate);
        return false;
    }

    ParserDescriptor bookmarkParser;
    bookmarkParser.id = kBookmarkParser;
    bookmarkParser.name = "hoi3_bookmarks";
    bookmarkParser.inputDialect = kHoi3ClausewitzDialect;
    bookmarkParser.outputType = kBookmarkDocumentType;
    bookmarkParser.schemaVersion = 1;
    bookmarkParser.parse = ParseBookmarks;
    if (!parsers.Register(std::move(bookmarkParser)))
    {
        templates.Unregister(kScenarioTemplate);
        templates.Unregister(kBookmarkTemplate);
        return false;
    }

    ParserDescriptor scenarioParser;
    scenarioParser.id = kScenarioParser;
    scenarioParser.name = "hoi3_scenario";
    scenarioParser.inputDialect = kHoi3ClausewitzDialect;
    scenarioParser.outputType = kScenarioDocumentType;
    scenarioParser.schemaVersion = 1;
    scenarioParser.parse = ParseScenario;
    if (!parsers.Register(std::move(scenarioParser)))
    {
        parsers.Unregister(kBookmarkParser);
        templates.Unregister(kScenarioTemplate);
        templates.Unregister(kBookmarkTemplate);
        return false;
    }

    AnalysisPassDescriptor pass;
    pass.id = kLaunchDefinitionDeclarePass;
    pass.name = "hoi3_launch_definition_declare";
    pass.phase = AnalysisPhase::Declare;
    pass.priority = -800;
    pass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareLaunchDefinitions(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kScenarioParser);
        parsers.Unregister(kBookmarkParser);
        templates.Unregister(kScenarioTemplate);
        templates.Unregister(kBookmarkTemplate);
        return false;
    }
    return true;
}

}
