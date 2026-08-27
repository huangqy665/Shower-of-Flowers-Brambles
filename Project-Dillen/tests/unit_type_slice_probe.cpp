#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

#include "analyzer.hpp"
#include "country_tag_definition.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "template_registry.hpp"
#include "unit_type_definition.hpp"
#include "unit_type_definition_registry.hpp"
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

bool CopyRepositoryFixture(const std::filesystem::path& root)
{
    const std::filesystem::path repository = std::filesystem::current_path();
    return CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt")
        && CopyDirectory(repository / "units", root / "units");
}

bool HasDiagnostic(
    const dillen::parser::DiagnosticBag& diagnostics,
    const std::string& code
)
{
    return std::any_of(
        diagnostics.All().begin(),
        diagnostics.All().end(),
        [&code](const dillen::parser::Diagnostic& diagnostic)
        {
            return diagnostic.code == code;
        }
    );
}

const dillen::content::UnitScalarProperty* FindScalar(
    const dillen::content::UnitTypeDefinition& definition,
    const std::string& name
)
{
    const auto iterator = std::find_if(
        definition.scalarProperties.begin(),
        definition.scalarProperties.end(),
        [&name](const dillen::content::UnitScalarProperty& property)
        {
            return property.name == name;
        }
    );
    return iterator == definition.scalarProperties.end()
        ? nullptr
        : &*iterator;
}

const dillen::content::UnitModifierBlock* FindBlock(
    const dillen::content::UnitTypeDefinition& definition,
    const std::string& name
)
{
    const auto iterator = std::find_if(
        definition.modifierBlocks.begin(),
        definition.modifierBlocks.end(),
        [&name](const dillen::content::UnitModifierBlock& block)
        {
            return block.name == name;
        }
    );
    return iterator == definition.modifierBlocks.end()
        ? nullptr
        : &*iterator;
}

bool BlockContains(
    const dillen::content::UnitModifierBlock* block,
    const std::string& name,
    double expected
)
{
    return block != nullptr && std::any_of(
        block->modifiers.begin(),
        block->modifiers.end(),
        [&name, expected](const dillen::content::UnitNumericModifier& value)
        {
            return value.name == name
                && std::abs(value.value - expected) < 0.0001;
        }
    );
}

bool HasUsableCountry(
    const dillen::content::UnitTypeDefinition& definition,
    const char* text
)
{
    const auto tag = dillen::content::CountryTag::Parse(text);
    return tag && std::find(
        definition.usableBy.begin(),
        definition.usableBy.end(),
        tag->StableId()
    ) != definition.usableBy.end();
}

void PrintDiagnostics(
    const dillen::parser::AnalysisWorkspace& workspace,
    const dillen::parser::DiagnosticBag& diagnostics
)
{
    for (const auto& diagnostic : diagnostics.All())
    {
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

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_unit_type_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryFixture(root))
    {
        std::cerr << "Unit type fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Analyzer analyzer;
    dillen::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterCountryTagSlice(
            templates,
            parsers,
            analyzer,
            definitions)
        || !dillen::parser::hoi3::RegisterUnitTypeSlice(
            templates,
            parsers,
            analyzer,
            definitions))
    {
        std::cerr << "Unit type slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 47)
    {
        std::cerr
            << "Unit type catalog failed: active="
            << catalog.ActiveClassifiedFileCount()
            << '\n';
        return 3;
    }

    dillen::parser::AnalysisWorkspace workspace;
    if (!analyzer.Analyze(catalog, parsers, workspace, diagnostics))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "Unit type analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* infantry = definitions.UnitTypes().Find("infantry_brigade");
    const auto* elite = definitions.UnitTypes().Find("light_bergsjaeger");
    const auto* carrier = definitions.UnitTypes().Find("carrier");
    const auto* biplane = definitions.UnitTypes().Find("biplane");
    const auto* foreign = definitions.UnitTypes().Find("foring_brigade");
    const auto* ss = definitions.UnitTypes().Find("ger_ss");
    const auto* maxStrength = infantry == nullptr
        ? nullptr
        : FindScalar(*infantry, "max_strength");
    const auto* buildCost = infantry == nullptr
        ? nullptr
        : FindScalar(*infantry, "build_cost_ic");
    const auto* carrierCapital = carrier == nullptr
        ? nullptr
        : FindScalar(*carrier, "capital");
    const auto* bomber = biplane == nullptr
        ? nullptr
        : FindScalar(*biplane, "is_bomber");
    const auto* strengthValue = maxStrength == nullptr
        ? nullptr
        : std::get_if<std::int64_t>(&maxStrength->value);
    const auto* costValue = buildCost == nullptr
        ? nullptr
        : std::get_if<double>(&buildCost->value);
    const auto* capitalValue = carrierCapital == nullptr
        ? nullptr
        : std::get_if<bool>(&carrierCapital->value);
    const auto* bomberValue = bomber == nullptr
        ? nullptr
        : std::get_if<bool>(&bomber->value);
    const bool valid = definitions.Countries().Size() == 142
        && definitions.UnitTypes().Size() == 46
        && definitions.UnitTypes().ResolvedCount() == 46
        && infantry != nullptr
        && infantry->domain == dillen::content::UnitDomain::Land
        && infantry->sprite
        && *infantry->sprite == "Infantry"
        && infantry->active
        && !*infantry->active
        && infantry->unitGroup
        && *infantry->unitGroup == "infantry_unit_type"
        && strengthValue != nullptr
        && *strengthValue == 30
        && costValue != nullptr
        && std::abs(*costValue - 2.33) < 0.0001
        && BlockContains(FindBlock(*infantry, "river"), "attack", -0.10)
        && BlockContains(
            FindBlock(*infantry, "river"),
            "movement",
            -0.05)
        && elite != nullptr
        && HasUsableCountry(*elite, "CHC")
        && carrier != nullptr
        && carrier->domain == dillen::content::UnitDomain::Naval
        && capitalValue != nullptr
        && *capitalValue
        && biplane != nullptr
        && biplane->domain == dillen::content::UnitDomain::Air
        && bomberValue != nullptr
        && *bomberValue
        && foreign != nullptr
        && foreign->origin.virtualPath == "units/foring_army.txt"
        && ss != nullptr
        && definitions.UnitTypes().Find("GER_SS") == ss
        && definitions.UnitTypes().Find("interceptor.0") == nullptr
        && HasDiagnostic(
            diagnostics,
            "hoi3.unit_type.domain_duplicate_ignored")
        && HasDiagnostic(
            diagnostics,
            "hoi3.unit_type.numeric_fragment_recovered")
        && definitions.UnitTypes().Declare({})
            == dillen::content::UnitTypeDeclareResult::Frozen
        && definitions.UnitTypes().ResolveUsableBy({}, {})
            == dillen::content::UnitTypeResolveResult::Frozen;

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        std::cerr
            << "countries=" << definitions.Countries().Size()
            << " units=" << definitions.UnitTypes().Size()
            << " resolved=" << definitions.UnitTypes().ResolvedCount()
            << " infantry=" << (infantry != nullptr)
            << " elite=" << (elite != nullptr)
            << " carrier=" << (carrier != nullptr)
            << " biplane=" << (biplane != nullptr)
            << '\n';
        std::cerr << "Unit type Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Unit type slice: passed ("
        << definitions.UnitTypes().Size()
        << " UnitTypeDefinitions, 4 model files excluded)\n";
    return 0;
}
