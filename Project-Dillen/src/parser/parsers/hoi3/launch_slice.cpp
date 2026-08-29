#include "launch_slice.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "file_catalog.hpp"
#include "launch_parser.hpp"
#include "country_tag_slice.hpp"

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
    return dillen::compatibility::hoi3::content::NormalizeScenarioKey(path.substr(begin, end - begin));
}

std::vector<dillen::compatibility::hoi3::content::CountryDefinitionId> ResolveCountries(
    const std::vector<dillen::compatibility::hoi3::content::CountryTag>& tags,
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    DiagnosticBag& diagnostics,
    const SourceSpan& span
)
{
    std::vector<dillen::compatibility::hoi3::content::CountryDefinitionId> resolved;
    resolved.reserve(tags.size());
    for (const dillen::compatibility::hoi3::content::CountryTag& tag : tags)
    {
        const dillen::compatibility::hoi3::content::CountryDefinitionId id = tag.StableId();
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
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
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
                dillen::compatibility::hoi3::content::BookmarkDefinition definition;
                definition.key = dillen::compatibility::hoi3::content::NormalizeBookmarkKey(parsed.name);
                definition.id = dillen::compatibility::hoi3::content::StableBookmarkDefinitionId(
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
                    != dillen::compatibility::hoi3::content::LaunchDeclareResult::Added)
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
            dillen::compatibility::hoi3::content::ScenarioDefinition definition;
            definition.key = ScenarioKeyFromPath(file.source.VirtualPath());
            definition.id = dillen::compatibility::hoi3::content::StableScenarioDefinitionId(
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
                != dillen::compatibility::hoi3::content::LaunchDeclareResult::Added)
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
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
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

    ResolutionPassDescriptor pass;
    pass.id = kLaunchDefinitionDeclarePass;
    pass.name = "hoi3_launch_definition_declare";
    pass.phase = ResolutionPhase::Declare;
    pass.priority = -800;
    pass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareLaunchDefinitions(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(pass)))
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
