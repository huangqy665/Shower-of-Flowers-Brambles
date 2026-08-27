#include "province_history_slice.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "country_tag_slice.hpp"
#include "province_history_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

std::optional<std::uint32_t> ProvinceIdFromPath(
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

    std::size_t digitCount = 0;
    std::uint64_t value = 0;
    while (digitCount < filename.size()
        && filename[digitCount] >= '0'
        && filename[digitCount] <= '9')
    {
        value = value * 10
            + static_cast<std::uint64_t>(filename[digitCount] - '0');
        if (value > std::numeric_limits<std::uint32_t>::max())
        {
            return std::nullopt;
        }
        ++digitCount;
    }
    if (digitCount == 0 || value == 0)
    {
        return std::nullopt;
    }
    if (digitCount < filename.size()
        && filename[digitCount] != ' '
        && filename[digitCount] != '-')
    {
        return std::nullopt;
    }
    canonical = digitCount < filename.size()
        && filename[digitCount] == ' '
        && digitCount + 1 < filename.size()
        && filename[digitCount + 1] == '-';
    return static_cast<std::uint32_t>(value);
}

SourceSpan DocumentSpan(const ProvinceHistoryDocument& document)
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

content::DefinitionOrigin MakeOrigin(
    const ParsedFile& file,
    const SourceSpan& span
)
{
    content::DefinitionOrigin origin;
    origin.virtualPath = std::string(file.source.VirtualPath());
    origin.sourceLayer = file.catalog.sourceLayerName;
    origin.line = span.IsValid() ? span.begin.line : 1;
    origin.column = span.IsValid() ? span.begin.column : 1;
    return origin;
}

bool IsCountryField(content::ProvinceHistoryField field)
{
    using content::ProvinceHistoryField;
    return field == ProvinceHistoryField::Owner
        || field == ProvinceHistoryField::Controller
        || field == ProvinceHistoryField::AddCore
        || field == ProvinceHistoryField::RemoveCore;
}

bool ResolveOperation(
    const ParsedFile& file,
    const UnresolvedProvinceHistoryOperation& unresolved,
    content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedCountryTags,
    DiagnosticBag& diagnostics,
    content::ProvinceHistoryOperation& output
)
{
    output.field = unresolved.field;
    output.origin = MakeOrigin(file, unresolved.span);
    if (IsCountryField(unresolved.field))
    {
        const std::string* text = std::get_if<std::string>(
            &unresolved.value
        );
        const auto tag = text == nullptr
            ? std::nullopt
            : content::CountryTag::Parse(*text);
        if (!tag)
        {
            diagnostics.Error(
                "hoi3.province_history.country_resolve_invalid",
                "Province history contains an invalid Country Tag",
                unresolved.span
            );
            return false;
        }
        const content::CountryDefinitionId id = tag->StableId();
        if (definitions.Countries().Find(id) == nullptr
            && reportedCountryTags.emplace(id.value).second)
        {
            diagnostics.Warning(
                "hoi3.province_history.country_unresolved",
                "Country Tag '" + tag->ToString()
                    + "' is absent from the active common/countries.txt layer",
                unresolved.span
            );
        }
        output.value = id;
        return true;
    }
    if (const auto* integer = std::get_if<std::int64_t>(
        &unresolved.value))
    {
        output.value = *integer;
        return true;
    }
    if (const auto* decimal = std::get_if<double>(&unresolved.value))
    {
        output.value = *decimal;
        return true;
    }
    if (const auto* symbol = std::get_if<std::string>(
        &unresolved.value))
    {
        output.value = *symbol;
        return true;
    }
    diagnostics.Error(
        "hoi3.province_history.value_resolve_invalid",
        "Province history operation contains an invalid value",
        unresolved.span
    );
    return false;
}

bool ResolveProvinceHistories(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    std::unordered_set<std::uint32_t> reportedCountryTags;
    std::vector<const ParsedFile*> historyFiles;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type == kProvinceHistoryDocumentType)
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
        const ProvinceHistoryDocument* document =
            file.result.artifact.As<ProvinceHistoryDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.province_history.artifact_type_mismatch",
                "Province history parser returned an invalid artifact"
            );
            return false;
        }

        bool canonicalPath = false;
        const std::optional<std::uint32_t> provinceId =
            ProvinceIdFromPath(
                file.source.VirtualPath(),
                canonicalPath
            );
        const SourceSpan documentSpan = DocumentSpan(*document);
        if (!provinceId)
        {
            diagnostics.Error(
                "hoi3.province_history.filename_id_missing",
                "Province history filename must begin with a positive Province ID",
                documentSpan
            );
            continue;
        }
        if (!canonicalPath)
        {
            diagnostics.Warning(
                "hoi3.province_history.filename_noncanonical",
                "Province history filename does not use '<id> - <name>.txt'",
                documentSpan
            );
        }
        if (definitions.Provinces().Find(*provinceId) == nullptr)
        {
            diagnostics.Error(
                "hoi3.province_history.province_missing",
                "Province history references unknown Province ID "
                    + std::to_string(*provinceId),
                documentSpan
            );
            continue;
        }

        content::ProvinceHistorySource source;
        source.origin = MakeOrigin(file, documentSpan);
        bool resolved = true;
        source.initialOperations.reserve(
            document->initialOperations.size()
        );
        for (const UnresolvedProvinceHistoryOperation& operation
            : document->initialOperations)
        {
            content::ProvinceHistoryOperation value;
            resolved = ResolveOperation(
                file,
                operation,
                definitions,
                reportedCountryTags,
                diagnostics,
                value
            ) && resolved;
            source.initialOperations.push_back(std::move(value));
        }
        source.patches.reserve(document->patches.size());
        for (const UnresolvedProvinceHistoryPatch& unresolvedPatch
            : document->patches)
        {
            content::ProvinceHistoryPatch patch;
            patch.date = unresolvedPatch.date;
            patch.origin = MakeOrigin(file, unresolvedPatch.span);
            patch.operations.reserve(unresolvedPatch.operations.size());
            for (const UnresolvedProvinceHistoryOperation& operation
                : unresolvedPatch.operations)
            {
                content::ProvinceHistoryOperation value;
                resolved = ResolveOperation(
                    file,
                    operation,
                    definitions,
                    reportedCountryTags,
                    diagnostics,
                    value
                ) && resolved;
                patch.operations.push_back(std::move(value));
            }
            source.patches.push_back(std::move(patch));
        }
        if (!resolved)
        {
            continue;
        }

        const content::ProvinceHistoryAppendResult appendResult =
            definitions.ProvinceHistories().Append(
                {*provinceId},
                std::move(source)
            );
        if (appendResult == content::ProvinceHistoryAppendResult::Merged)
        {
            diagnostics.Warning(
                "hoi3.province_history.duplicate_source_merged",
                "multiple active history files for Province "
                    + std::to_string(*provinceId)
                    + " were merged in deterministic virtual-path order",
                documentSpan
            );
        }
        else if (appendResult
            != content::ProvinceHistoryAppendResult::Added)
        {
            diagnostics.Error(
                "hoi3.province_history.append_failed",
                "Province history could not be appended to its Timeline",
                documentSpan
            );
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterProvinceHistorySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kProvinceHistoryTemplate;
    fileTemplate.name = "hoi3_province_history";
    fileTemplate.pattern = "history/provinces/**/*.txt";
    fileTemplate.parser = kProvinceHistoryParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kProvinceHistoryParser;
    parser.name = "hoi3_province_history";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kProvinceHistoryDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseProvinceHistory;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kProvinceHistoryTemplate);
        return false;
    }

    AnalysisPassDescriptor pass;
    pass.id = kProvinceHistoryResolvePass;
    pass.name = "hoi3_province_history_resolve";
    pass.phase = AnalysisPhase::Resolve;
    pass.priority = -1800;
    pass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveProvinceHistories(
            workspace,
            diagnostics,
            definitions
        );
    };
    if (!analyzer.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kProvinceHistoryParser);
        templates.Unregister(kProvinceHistoryTemplate);
        return false;
    }
    return true;
}

}
