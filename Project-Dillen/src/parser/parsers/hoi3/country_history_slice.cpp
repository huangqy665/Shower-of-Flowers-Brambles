#include "country_history_slice.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "country_history_parser.hpp"
#include "country_tag_slice.hpp"

namespace dillen::parser::hoi3 {

namespace {

std::optional<dillen::compatibility::hoi3::content::CountryTag> CountryTagFromPath(
    std::string_view virtualPath,
    bool& canonical
)
{
    const std::size_t slash = virtualPath.find_last_of('/');
    std::string_view filename = slash == std::string_view::npos
        ? virtualPath
        : virtualPath.substr(slash + 1);
    const std::size_t extension = filename.rfind('.');
    if (extension != std::string_view::npos)
    {
        filename = filename.substr(0, extension);
    }
    if (filename.size() < 3)
    {
        return std::nullopt;
    }
    const std::optional<dillen::compatibility::hoi3::content::CountryTag> tag =
        dillen::compatibility::hoi3::content::CountryTag::Parse(filename.substr(0, 3));
    if (!tag)
    {
        return std::nullopt;
    }
    if (filename.size() > 3
        && filename[3] != ' '
        && filename[3] != '-')
    {
        return std::nullopt;
    }
    canonical = filename.size() >= 6
        && filename.substr(3, 3) == " - ";
    return tag;
}

SourceSpan DocumentSpan(const CountryHistoryDocument& document)
{
    if (!document.initialOperations.empty())
    {
        return document.initialOperations.front().span;
    }
    if (!document.patches.empty())
    {
        return document.patches.front().span;
    }
    return {};
}

dillen::compatibility::hoi3::content::DefinitionOrigin MakeOrigin(
    const ParsedFile& file,
    const SourceSpan& span
)
{
    dillen::compatibility::hoi3::content::DefinitionOrigin origin;
    origin.virtualPath = std::string(file.source.VirtualPath());
    origin.sourceLayer = file.catalog.sourceLayerName;
    origin.line = span.IsValid() ? span.begin.line : 1;
    origin.column = span.IsValid() ? span.begin.column : 1;
    return origin;
}

void WarnMissingCountry(
    dillen::compatibility::hoi3::content::CountryDefinitionId id,
    const dillen::compatibility::hoi3::content::CountryTag& tag,
    const SourceSpan& span,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedCountries,
    DiagnosticBag& diagnostics
)
{
    if (definitions.Countries().Find(id) == nullptr
        && reportedCountries.emplace(id.value).second)
    {
        diagnostics.Warning(
            "hoi3.country_history.country_unresolved",
            "Country Tag '" + tag.ToString()
                + "' is absent from the active common/countries.txt layer",
            span
        );
    }
}

bool ResolveOperation(
    const ParsedFile& file,
    const UnresolvedCountryHistoryOperation& unresolved,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedCountries,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::CountryHistoryOperation& output
)
{
    output.field = unresolved.field;
    output.key = unresolved.key;
    output.origin = MakeOrigin(file, unresolved.span);
    if (unresolved.field == dillen::compatibility::hoi3::content::CountryHistoryField::NamedAssignment)
    {
        const dillen::compatibility::hoi3::content::TechnologyDefinition* technology =
            definitions.Technologies().Find(unresolved.key);
        if (technology != nullptr)
        {
            const auto* integer = std::get_if<std::int64_t>(
                &unresolved.value
            );
            if (integer == nullptr
                || *integer < std::numeric_limits<int>::min()
                || *integer > std::numeric_limits<int>::max())
            {
                diagnostics.Error(
                    "hoi3.country_history.technology_level_invalid",
                    "Country history Technology level must be an integer",
                    unresolved.span
                );
                return false;
            }
            output.field = dillen::compatibility::hoi3::content::CountryHistoryField::TechnologyLevel;
            output.value = dillen::compatibility::hoi3::content::CountryHistoryTechnologyLevel{
                technology->id,
                static_cast<int>(*integer)
            };
            return true;
        }
    }
    if (unresolved.field == dillen::compatibility::hoi3::content::CountryHistoryField::OrderOfBattle
        || unresolved.field
            == dillen::compatibility::hoi3::content::CountryHistoryField::LoadOrderOfBattle)
    {
        const auto* text = std::get_if<std::string>(&unresolved.value);
        if (text != nullptr)
        {
            std::string virtualPath =
                dillen::compatibility::hoi3::content::NormalizeOrderOfBattlePath(*text);
            if (virtualPath.find('/') == std::string::npos)
            {
                virtualPath = "history/units/" + virtualPath;
            }
            const dillen::compatibility::hoi3::content::OrderOfBattleDefinition* orderOfBattle =
                definitions.OrdersOfBattle().Find(virtualPath);
            if (orderOfBattle != nullptr)
            {
                output.value = orderOfBattle->id;
                return true;
            }
        }
    }
    if (unresolved.field == dillen::compatibility::hoi3::content::CountryHistoryField::Capital)
    {
        const auto* integer = std::get_if<std::int64_t>(&unresolved.value);
        if (integer == nullptr
            || definitions.Provinces().Find(
                static_cast<std::uint32_t>(*integer)) == nullptr)
        {
            diagnostics.Error(
                "hoi3.country_history.capital_province_missing",
                "Country history capital references an unknown Province ID",
                unresolved.span
            );
            return false;
        }
        output.value = dillen::compatibility::hoi3::content::ProvinceDefinitionId{
            static_cast<std::uint32_t>(*integer)
        };
        return true;
    }
    if (unresolved.field == dillen::compatibility::hoi3::content::CountryHistoryField::CreateAlliance)
    {
        const auto* text = std::get_if<std::string>(&unresolved.value);
        const auto tag = text == nullptr
            ? std::nullopt
            : dillen::compatibility::hoi3::content::CountryTag::Parse(*text);
        if (!tag)
        {
            diagnostics.Error(
                "hoi3.country_history.country_reference_invalid",
                "Country history contains an invalid Country Tag reference",
                unresolved.span
            );
            return false;
        }
        const dillen::compatibility::hoi3::content::CountryDefinitionId id = tag->StableId();
        WarnMissingCountry(
            id,
            *tag,
            unresolved.span,
            definitions,
            reportedCountries,
            diagnostics
        );
        output.value = id;
        return true;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&unresolved.value))
    {
        output.value = *integer;
        return true;
    }
    if (const auto* decimal = std::get_if<double>(&unresolved.value))
    {
        output.value = *decimal;
        return true;
    }
    if (const auto* boolean = std::get_if<bool>(&unresolved.value))
    {
        output.value = *boolean;
        return true;
    }
    if (const auto* text = std::get_if<std::string>(&unresolved.value))
    {
        output.value = *text;
        return true;
    }
    if (const auto* alignment = std::get_if<dillen::compatibility::hoi3::content::CountryAlignment>(
            &unresolved.value))
    {
        output.value = *alignment;
        return true;
    }
    if (const auto* map = std::get_if<
            dillen::compatibility::hoi3::content::CountryHistoryNamedNumberMap>(&unresolved.value))
    {
        output.value = *map;
        return true;
    }
    diagnostics.Error(
        "hoi3.country_history.value_resolve_invalid",
        "Country history operation contains an invalid value",
        unresolved.span
    );
    return false;
}

bool ResolveCountryHistories(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    std::unordered_set<std::uint32_t> reportedCountries;
    std::vector<const ParsedFile*> historyFiles;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type == kCountryHistoryDocumentType)
        {
            historyFiles.push_back(&file);
        }
    }
    std::stable_sort(
        historyFiles.begin(),
        historyFiles.end(),
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
    for (const ParsedFile* historyFile : historyFiles)
    {
        const ParsedFile& file = *historyFile;
        const CountryHistoryDocument* document =
            file.result.artifact.As<CountryHistoryDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.country_history.artifact_type_mismatch",
                "Country history parser returned an invalid artifact"
            );
            return false;
        }

        bool canonicalPath = false;
        const auto tag = CountryTagFromPath(
            file.source.VirtualPath(),
            canonicalPath
        );
        const SourceSpan documentSpan = DocumentSpan(*document);
        if (!tag)
        {
            diagnostics.Error(
                "hoi3.country_history.filename_tag_missing",
                "Country history filename must begin with a Country Tag",
                documentSpan
            );
            continue;
        }
        if (!canonicalPath)
        {
            diagnostics.Warning(
                "hoi3.country_history.filename_noncanonical",
                "Country history filename does not use '<TAG> - <name>.txt'",
                documentSpan
            );
        }
        const dillen::compatibility::hoi3::content::CountryDefinitionId country = tag->StableId();
        WarnMissingCountry(
            country,
            *tag,
            documentSpan,
            definitions,
            reportedCountries,
            diagnostics
        );

        dillen::compatibility::hoi3::content::CountryHistorySource source;
        source.origin = MakeOrigin(file, documentSpan);
        bool resolved = true;
        source.initialOperations.reserve(document->initialOperations.size());
        for (const UnresolvedCountryHistoryOperation& operation
            : document->initialOperations)
        {
            dillen::compatibility::hoi3::content::CountryHistoryOperation value;
            const bool operationResolved = ResolveOperation(
                file,
                operation,
                definitions,
                reportedCountries,
                diagnostics,
                value
            );
            resolved = operationResolved && resolved;
            if (operationResolved)
            {
                source.initialOperations.push_back(std::move(value));
            }
        }
        source.patches.reserve(document->patches.size());
        for (const UnresolvedCountryHistoryPatch& unresolvedPatch
            : document->patches)
        {
            dillen::compatibility::hoi3::content::CountryHistoryPatch patch;
            patch.date = unresolvedPatch.date;
            patch.origin = MakeOrigin(file, unresolvedPatch.span);
            patch.operations.reserve(unresolvedPatch.operations.size());
            for (const UnresolvedCountryHistoryOperation& operation
                : unresolvedPatch.operations)
            {
                dillen::compatibility::hoi3::content::CountryHistoryOperation value;
                const bool operationResolved = ResolveOperation(
                    file,
                    operation,
                    definitions,
                    reportedCountries,
                    diagnostics,
                    value
                );
                resolved = operationResolved && resolved;
                if (operationResolved)
                {
                    patch.operations.push_back(std::move(value));
                }
            }
            source.patches.push_back(std::move(patch));
        }
        if (!resolved)
        {
            continue;
        }

        const dillen::compatibility::hoi3::content::CountryHistoryAppendResult appendResult =
            definitions.CountryHistories().Append(
                country,
                std::move(source)
            );
        if (appendResult == dillen::compatibility::hoi3::content::CountryHistoryAppendResult::Merged)
        {
            diagnostics.Warning(
                "hoi3.country_history.duplicate_source_merged",
                "multiple active history files for Country "
                    + tag->ToString()
                    + " were merged in deterministic virtual-path order",
                documentSpan
            );
        }
        else if (appendResult != dillen::compatibility::hoi3::content::CountryHistoryAppendResult::Added)
        {
            diagnostics.Error(
                "hoi3.country_history.append_failed",
                "Country history could not be appended to its Timeline",
                documentSpan
            );
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterCountryHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kCountryHistoryTemplate;
    fileTemplate.name = "hoi3_country_history";
    fileTemplate.pattern = "history/countries/**/*.txt";
    fileTemplate.parser = kCountryHistoryParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kCountryHistoryParser;
    parser.name = "hoi3_country_history";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kCountryHistoryDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseCountryHistory;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kCountryHistoryTemplate);
        return false;
    }

    ResolutionPassDescriptor pass;
    pass.id = kCountryHistoryResolvePass;
    pass.name = "hoi3_country_history_resolve";
    pass.phase = ResolutionPhase::Resolve;
    pass.priority = -1700;
    pass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveCountryHistories(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kCountryHistoryParser);
        templates.Unregister(kCountryHistoryTemplate);
        return false;
    }
    return true;
}

}
