#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

#include "resolver.hpp"
#include "country_history.hpp"
#include "country_history_slice.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "province_definition_slice.hpp"
#include "technology_definition.hpp"
#include "technology_definition_registry.hpp"
#include "technology_slice.hpp"
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

bool CopyRepositoryFixture(const std::filesystem::path& root)
{
    const std::filesystem::path repository = std::filesystem::current_path();
    return CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt")
        && CopyDirectory(repository / "units", root / "units")
        && CopyDirectory(
            repository / "technologies",
            root / "technologies")
        && CopyFile(
            repository / "map/definition.csv",
            root / "map/definition.csv")
        && CopyFile(
            repository / "history/countries/CHL - Chile.txt",
            root / "history/countries/CHL - Chile.txt");
}

std::size_t CountDiagnostic(
    const dillen::parser::DiagnosticBag& diagnostics,
    const std::string& code
)
{
    return static_cast<std::size_t>(std::count_if(
        diagnostics.All().begin(),
        diagnostics.All().end(),
        [&code](const dillen::parser::Diagnostic& diagnostic)
        {
            return diagnostic.code == code;
        }
    ));
}

const dillen::compatibility::hoi3::content::TechnologyResearchBonus* FindResearchBonus(
    const dillen::compatibility::hoi3::content::TechnologyDefinition& definition,
    const std::string& name
)
{
    const auto iterator = std::find_if(
        definition.researchBonuses.begin(),
        definition.researchBonuses.end(),
        [&name](const dillen::compatibility::hoi3::content::TechnologyResearchBonus& bonus)
        {
            return bonus.source == name;
        }
    );
    return iterator == definition.researchBonuses.end()
        ? nullptr
        : &*iterator;
}

const dillen::compatibility::hoi3::content::TechnologyEffectBlock* FindEffectBlock(
    const dillen::compatibility::hoi3::content::TechnologyDefinition& definition,
    const std::string& name
)
{
    const auto iterator = std::find_if(
        definition.effectBlocks.begin(),
        definition.effectBlocks.end(),
        [&name](const dillen::compatibility::hoi3::content::TechnologyEffectBlock& block)
        {
            return block.name == name;
        }
    );
    return iterator == definition.effectBlocks.end()
        ? nullptr
        : &*iterator;
}

const dillen::compatibility::hoi3::content::TechnologyScalarEffect* FindEffect(
    const dillen::compatibility::hoi3::content::TechnologyEffectBlock& block,
    const std::string& name
)
{
    const auto iterator = std::find_if(
        block.effects.begin(),
        block.effects.end(),
        [&name](const dillen::compatibility::hoi3::content::TechnologyScalarEffect& effect)
        {
            return effect.name == name;
        }
    );
    return iterator == block.effects.end() ? nullptr : &*iterator;
}

const dillen::compatibility::hoi3::content::TechnologyRequirement* FindRequirement(
    const dillen::compatibility::hoi3::content::TechnologyRequirement& requirement,
    const std::string& name
)
{
    if (requirement.kind
            == dillen::compatibility::hoi3::content::TechnologyRequirementKind::Level
        && requirement.name == name)
    {
        return &requirement;
    }
    for (const auto& child : requirement.children)
    {
        const auto* found = FindRequirement(child, name);
        if (found != nullptr)
        {
            return found;
        }
    }
    return nullptr;
}

bool HasActivatedUnit(
    const dillen::compatibility::hoi3::content::TechnologyDefinition& definition,
    const std::string& name,
    bool resolved
)
{
    return std::any_of(
        definition.activatedUnits.begin(),
        definition.activatedUnits.end(),
        [&name, resolved](
            const dillen::compatibility::hoi3::content::TechnologyUnitReference& reference)
        {
            return reference.name == name
                && reference.unitType.has_value() == resolved;
        }
    );
}

const dillen::compatibility::hoi3::content::CountryHistoryOperation* FindHistoryOperation(
    const std::vector<dillen::compatibility::hoi3::content::CountryHistoryOperation>& operations,
    dillen::compatibility::hoi3::content::CountryHistoryField field,
    const std::string& key
)
{
    const auto iterator = std::find_if(
        operations.begin(),
        operations.end(),
        [field, &key](
            const dillen::compatibility::hoi3::content::CountryHistoryOperation& operation)
        {
            return operation.field == field && operation.key == key;
        }
    );
    return iterator == operations.end() ? nullptr : &*iterator;
}

void PrintDiagnostics(
    const dillen::parser::ParseWorkspace& workspace,
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
        / ("project_dillen_technology_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryFixture(root))
    {
        std::cerr << "Technology fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Resolver resolver;
    dillen::compatibility::hoi3::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterCountryTagSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterUnitTypeSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterTechnologySlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterCountryHistorySlice(
            templates,
            parsers,
            resolver,
            definitions))
    {
        std::cerr << "Technology slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 57)
    {
        std::cerr
            << "Technology catalog failed: active="
            << catalog.ActiveClassifiedFileCount()
            << '\n';
        return 3;
    }

    dillen::parser::ParseWorkspace workspace;
    if (!(catalog.Parse(parsers, workspace, diagnostics) && resolver.Resolve(workspace, diagnostics)))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "Technology analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* cavalry = definitions.Technologies().Find(
        "cavalry_smallarms"
    );
    const auto* activation = definitions.Technologies().Find(
        "infantry_activation"
    );
    const auto* smallArms = definitions.Technologies().Find(
        "smallarms_technology"
    );
    const auto* aeroengine = definitions.Technologies().Find(
        "basic_aeroengine"
    );
    const auto* night = definitions.Technologies().Find("night_goggles");
    const auto* arctic = definitions.Technologies().Find(
        "artic_warfare_equipment"
    );
    const auto* chile = definitions.CountryHistories().Find("CHL");
    const auto* cavalryBonus = cavalry == nullptr
        ? nullptr
        : FindResearchBonus(*cavalry, "mobile_theory");
    const auto* infantryBlock = smallArms == nullptr
        ? nullptr
        : FindEffectBlock(*smallArms, "infantry_brigade");
    const auto* infantryAttack = infantryBlock == nullptr
        ? nullptr
        : FindEffect(*infantryBlock, "soft_attack");
    const auto* infantryAttackValue = infantryAttack == nullptr
        ? nullptr
        : std::get_if<double>(&infantryAttack->value);
    const auto* militiaRequirement = activation == nullptr
            || !activation->allow
        ? nullptr
        : FindRequirement(*activation->allow, "militia_smallarms");
    const auto* singleEngineRequirement = aeroengine == nullptr
            || !aeroengine->allow
        ? nullptr
        : FindRequirement(
            *aeroengine->allow,
            "single_engine_aircraft_design"
        );
    const auto* smallArmsHistory = chile == nullptr
        ? nullptr
        : FindHistoryOperation(
            chile->initialOperations,
            dillen::compatibility::hoi3::content::CountryHistoryField::TechnologyLevel,
            "smallarms_technology"
        );
    const auto* infantryTheoryHistory = chile == nullptr
        ? nullptr
        : FindHistoryOperation(
            chile->initialOperations,
            dillen::compatibility::hoi3::content::CountryHistoryField::NamedAssignment,
            "infantry_theory"
        );
    const auto* smallArmsLevel = smallArmsHistory == nullptr
        ? nullptr
        : std::get_if<dillen::compatibility::hoi3::content::CountryHistoryTechnologyLevel>(
            &smallArmsHistory->value
        );
    const bool valid = definitions.Countries().Size() == 142
        && definitions.UnitTypes().Size() == 46
        && definitions.Technologies().Size() == 249
        && definitions.Technologies().ResolvedCount() == 249
        && definitions.CountryHistories().Size() == 1
        && cavalry != nullptr
        && std::abs(cavalry->difficulty) < 0.0001
        && cavalry->startYear == 1918
        && cavalry->firstOffset && *cavalry->firstOffset == 1934
        && cavalry->additionalOffset && *cavalry->additionalOffset == 2
        && cavalry->maxLevel && *cavalry->maxLevel == 12
        && cavalry->folder == "infantry_folder"
        && cavalry->onCompletion == "mobile_theory"
        && cavalryBonus != nullptr
        && std::abs(cavalryBonus->weight - 0.3) < 0.0001
        && activation != nullptr
        && HasActivatedUnit(*activation, "infantry_brigade", true)
        && militiaRequirement != nullptr
        && militiaRequirement->level == 1
        && militiaRequirement->technology.has_value()
        && smallArms != nullptr
        && infantryBlock != nullptr
        && infantryBlock->unitType.has_value()
        && infantryAttackValue != nullptr
        && std::abs(*infantryAttackValue - 0.6) < 0.0001
        && aeroengine != nullptr
        && aeroengine->allow
        && std::any_of(
            aeroengine->allow->children.begin(),
            aeroengine->allow->children.end(),
            [](const dillen::compatibility::hoi3::content::TechnologyRequirement& requirement)
            {
                return requirement.kind
                    == dillen::compatibility::hoi3::content::TechnologyRequirementKind::Any;
            })
        && singleEngineRequirement != nullptr
        && singleEngineRequirement->technology.has_value()
        && chile != nullptr
        && smallArmsLevel != nullptr
        && smallArmsLevel->level == 1
        && smallArmsLevel->technology
            == dillen::compatibility::hoi3::content::StableTechnologyDefinitionId(
                "smallarms_technology")
        && infantryTheoryHistory != nullptr
        && night != nullptr
        && FindEffectBlock(*night, "for_the_occasion_brigade") != nullptr
        && FindEffectBlock(*night, "ger_ss") != nullptr
        && arctic != nullptr
        && FindEffectBlock(*arctic, "field_battalion") != nullptr
        && FindEffectBlock(*arctic, "ger_ss") != nullptr
        && CountDiagnostic(
            diagnostics,
            "hoi3.technology.effect_block_close_recovered") == 2
        && CountDiagnostic(
            diagnostics,
            "hoi3.technology.block_assignment_recovered") == 1
        && definitions.Technologies().Find("NIGHT_GOGGLES") == night
        && definitions.Technologies().Declare({})
            == dillen::compatibility::hoi3::content::TechnologyDeclareResult::Frozen
        && definitions.Technologies().ResolveReferences({}, {}, {}, {})
            == dillen::compatibility::hoi3::content::TechnologyResolveResult::Frozen;

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr
            << "countries=" << definitions.Countries().Size()
            << " units=" << definitions.UnitTypes().Size()
            << " technologies=" << definitions.Technologies().Size()
            << " resolved=" << definitions.Technologies().ResolvedCount()
            << " histories=" << definitions.CountryHistories().Size()
            << " recovery=" << CountDiagnostic(
                diagnostics,
                "hoi3.technology.effect_block_close_recovered")
            << '\n';
        std::cerr << "Technology Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Technology slice: passed ("
        << definitions.Technologies().Size()
        << " TechnologyDefinitions from 8 files)\n";
    return 0;
}
