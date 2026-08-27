#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

#include "analyzer.hpp"
#include "country_history_slice.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "order_of_battle_definition.hpp"
#include "order_of_battle_definition_registry.hpp"
#include "order_of_battle_slice.hpp"
#include "parser_registry.hpp"
#include "province_definition_slice.hpp"
#include "template_registry.hpp"
#include "unit_type_slice.hpp"

namespace
{

bool CopyFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
)
{
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    return !error;
}

bool CopyDirectory(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
)
{
    namespace fs = std::filesystem;
    std::error_code error;
    for (fs::recursive_directory_iterator iterator(source, error), end;
        iterator != end && !error;
        iterator.increment(error))
    {
        const fs::path relative = fs::relative(iterator->path(), source, error);
        if (error)
        {
            return false;
        }
        const fs::path target = destination / relative;
        if (iterator->is_directory())
        {
            fs::create_directories(target, error);
        }
        else if (iterator->is_regular_file())
        {
            fs::create_directories(target.parent_path(), error);
            if (!error)
            {
                fs::copy_file(
                    iterator->path(),
                    target,
                    fs::copy_options::overwrite_existing,
                    error
                );
            }
        }
    }
    return !error;
}

bool EndsWithInsensitive(std::string text, const std::string& suffix)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    return text.size() >= suffix.size()
        && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool CopyScenarioOrdersOfBattle(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
)
{
    namespace fs = std::filesystem;
    std::error_code error;
    for (fs::recursive_directory_iterator iterator(source, error), end;
        iterator != end && !error;
        iterator.increment(error))
    {
        if (!iterator->is_regular_file())
        {
            continue;
        }
        const std::string filename = iterator->path().filename().string();
        if (!EndsWithInsensitive(filename, "_oob.txt")
            && !EndsWithInsensitive(filename, "_army.txt"))
        {
            continue;
        }
        const fs::path relative = fs::relative(iterator->path(), source, error);
        if (error || !CopyFile(iterator->path(), destination / relative))
        {
            return false;
        }
    }
    return !error;
}

bool CopyRepositoryFixture(const std::filesystem::path& root)
{
    const std::filesystem::path repository = std::filesystem::current_path();
    return CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt")
        && CopyFile(
            repository / "map/definition.csv",
            root / "map/definition.csv")
        && CopyDirectory(repository / "units", root / "units")
        && CopyDirectory(
            repository / "history/units",
            root / "history/units")
        && CopyFile(
            repository / "history/countries/CHI - Nationalist China.txt",
            root / "history/countries/CHI - Nationalist China.txt")
        && CopyScenarioOrdersOfBattle(
            repository / "scenarios",
            root / "scenarios");
}

void PrintDiagnostics(
    const dillen::parser::AnalysisWorkspace& workspace,
    const dillen::parser::DiagnosticBag& diagnostics
)
{
    for (const auto& diagnostic : diagnostics.All())
    {
        if (diagnostic.severity != dillen::parser::DiagnosticSeverity::Error
            && diagnostic.severity != dillen::parser::DiagnosticSeverity::Fatal)
        {
            continue;
        }
        std::string_view virtualPath;
        for (const auto& file : workspace.files)
        {
            if (file.source.Id() == diagnostic.span.begin.source)
            {
                virtualPath = file.source.VirtualPath();
                break;
            }
        }
        std::cerr
            << dillen::parser::FormatDiagnostic(diagnostic, virtualPath)
            << '\n';
    }
}

std::size_t CountNodes(const dillen::content::OrderOfBattleNode& node)
{
    std::size_t count = 1;
    for (const auto& child : node.children)
    {
        count += CountNodes(child);
    }
    return count;
}

const dillen::content::OrderOfBattleNode* FindNode(
    const std::vector<dillen::content::OrderOfBattleNode>& nodes,
    dillen::content::OrderOfBattleNodeKind kind,
    const std::string& typeName = {}
)
{
    for (const auto& node : nodes)
    {
        if (node.kind == kind
            && (typeName.empty() || node.unitTypeName == typeName))
        {
            return &node;
        }
        if (const auto* child = FindNode(node.children, kind, typeName))
        {
            return child;
        }
    }
    return nullptr;
}

bool CountryHistoryReferencesOob(
    const dillen::content::CountryHistoryTimeline& timeline,
    dillen::content::OrderOfBattleDefinitionId expected
)
{
    const auto hasReference = [expected](
        const dillen::content::CountryHistoryOperation& operation)
    {
        const auto* id = std::get_if<
            dillen::content::OrderOfBattleDefinitionId>(&operation.value);
        return id != nullptr && *id == expected;
    };
    if (std::any_of(
            timeline.initialOperations.begin(),
            timeline.initialOperations.end(),
            hasReference))
    {
        return true;
    }
    for (const auto& patch : timeline.patches)
    {
        if (std::any_of(
                patch.operations.begin(),
                patch.operations.end(),
                hasReference))
        {
            return true;
        }
    }
    return false;
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_oob_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryFixture(root))
    {
        std::cerr << "OOB fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Analyzer analyzer;
    dillen::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterCountryTagSlice(
            templates, parsers, analyzer, definitions)
        || !dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates, parsers, analyzer, definitions)
        || !dillen::parser::hoi3::RegisterUnitTypeSlice(
            templates, parsers, analyzer, definitions)
        || !dillen::parser::hoi3::RegisterCountryHistorySlice(
            templates, parsers, analyzer, definitions)
        || !dillen::parser::hoi3::RegisterOrderOfBattleSlice(
            templates, parsers, analyzer, definitions))
    {
        std::cerr << "OOB slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 353)
    {
        std::cerr
            << "OOB catalog failed: active="
            << catalog.ActiveClassifiedFileCount()
            << '\n';
        return 3;
    }

    dillen::parser::AnalysisWorkspace workspace;
    if (!analyzer.Analyze(catalog, parsers, workspace, diagnostics))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "OOB analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* china = definitions.OrdersOfBattle().Find(
        "history/units/CHI_1936.txt"
    );
    const auto* belgium = definitions.OrdersOfBattle().Find(
        "history/units/BEL_1936.txt"
    );
    const auto* fallBlau = definitions.OrdersOfBattle().Find(
        "scenarios/fall_blau/GER_OOB.txt"
    );
    if (definitions.OrdersOfBattle().Size() != 304
        || definitions.OrdersOfBattle().ResolvedCount() != 304
        || china == nullptr
        || belgium == nullptr
        || fallBlau == nullptr
        || china->militaryAccess.size() != 2
        || china->roots.empty()
        || china->roots.front().kind
            != dillen::content::OrderOfBattleNodeKind::Theatre
        || !china->roots.front().location
        || china->roots.front().location->value != 5494
        || belgium->constructions.size() != 1)
    {
        std::cerr << "OOB registry content mismatch\n";
        return 5;
    }

    const auto* hq = FindNode(
        china->roots,
        dillen::content::OrderOfBattleNodeKind::Regiment,
        "hq_brigade"
    );
    if (hq == nullptr
        || !hq->unitType
        || FindNode(
            fallBlau->roots,
            dillen::content::OrderOfBattleNodeKind::Division)
            == nullptr)
    {
        std::cerr << "OOB node reference mismatch\n";
        return 6;
    }

    const auto chinaTag = dillen::content::CountryTag::Parse("CHI");
    const auto* timeline = chinaTag
        ? definitions.CountryHistories().Find(chinaTag->StableId())
        : nullptr;
    if (timeline == nullptr
        || !CountryHistoryReferencesOob(*timeline, china->id))
    {
        std::cerr << "Country history OOB binding mismatch\n";
        return 7;
    }

    std::size_t nodeCount = 0;
    std::size_t constructionCount = 0;
    for (const auto& definition : definitions.OrdersOfBattle().All())
    {
        constructionCount += definition.constructions.size();
        for (const auto& rootNode : definition.roots)
        {
            nodeCount += CountNodes(rootNode);
        }
        for (const auto& construction : definition.constructions)
        {
            for (const auto& component : construction.components)
            {
                nodeCount += CountNodes(component);
            }
        }
    }
    if (nodeCount != 67791 || constructionCount != 200)
    {
        std::cerr
            << "OOB aggregate mismatch: nodes=" << nodeCount
            << " constructions=" << constructionCount << '\n';
        return 8;
    }

    std::cout
        << "Order of battle slice: passed (304 files, "
        << nodeCount << " nodes, "
        << constructionCount << " constructions)\n";
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return 0;
}
