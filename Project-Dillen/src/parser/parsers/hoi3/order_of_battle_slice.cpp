#include "order_of_battle_slice.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "country_tag_slice.hpp"
#include "order_of_battle_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

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

SourceSpan DocumentSpan(const OrderOfBattleDocument& document)
{
    if (!document.roots.empty())
    {
        return document.roots.front().span;
    }
    if (!document.constructions.empty())
    {
        return document.constructions.front().span;
    }
    if (!document.militaryAccess.empty())
    {
        return document.militaryAccess.front().span;
    }
    if (!document.metadata.empty())
    {
        return document.metadata.front().span;
    }
    return {};
}

bool DeclareOrdersOfBattle(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kOrderOfBattleDocumentType)
        {
            continue;
        }
        const OrderOfBattleDocument* document =
            file.result.artifact.As<OrderOfBattleDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.oob.artifact_type_mismatch",
                "Order of battle parser returned an invalid artifact"
            );
            return false;
        }
        dillen::compatibility::hoi3::content::OrderOfBattleDefinition definition;
        definition.virtualPath = dillen::compatibility::hoi3::content::NormalizeOrderOfBattlePath(
            file.source.VirtualPath()
        );
        definition.id = dillen::compatibility::hoi3::content::StableOrderOfBattleDefinitionId(
            definition.virtualPath
        );
        definition.origin = MakeOrigin(file, DocumentSpan(*document));
        const dillen::compatibility::hoi3::content::OrderOfBattleDeclareResult result =
            definitions.OrdersOfBattle().Declare(std::move(definition));
        if (result != dillen::compatibility::hoi3::content::OrderOfBattleDeclareResult::Added)
        {
            diagnostics.Error(
                "hoi3.oob.declare_failed",
                "Order of battle definition could not be declared",
                DocumentSpan(*document)
            );
        }
    }
    return !diagnostics.HasErrors();
}

std::optional<dillen::compatibility::hoi3::content::CountryDefinitionId> ResolveCountry(
    std::string_view text,
    const SourceSpan& span,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedCountries,
    DiagnosticBag& diagnostics
)
{
    if (text.empty())
    {
        return std::nullopt;
    }
    const std::optional<dillen::compatibility::hoi3::content::CountryTag> tag =
        dillen::compatibility::hoi3::content::CountryTag::Parse(text);
    if (!tag)
    {
        diagnostics.Error(
            "hoi3.oob.country_reference_invalid",
            "Order of battle contains an invalid Country Tag",
            span
        );
        return std::nullopt;
    }
    const dillen::compatibility::hoi3::content::CountryDefinitionId id = tag->StableId();
    if (definitions.Countries().Find(id) == nullptr
        && reportedCountries.emplace(id.value).second)
    {
        diagnostics.Warning(
            "hoi3.oob.country_unresolved",
            "Order of battle Country Tag '" + tag->ToString()
                + "' is absent from the active Country Registry",
            span
        );
    }
    return id;
}

dillen::compatibility::hoi3::content::OrderOfBattleNode ResolveNode(
    const ParsedFile& file,
    const UnresolvedOrderOfBattleNode& unresolved,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedProvinces,
    std::unordered_set<std::uint32_t>& reportedCountries,
    DiagnosticBag& diagnostics
)
{
    dillen::compatibility::hoi3::content::OrderOfBattleNode node;
    node.kind = unresolved.kind;
    node.name = unresolved.name;
    node.unitTypeName = unresolved.unitTypeName;
    if (!node.unitTypeName.empty())
    {
        const dillen::compatibility::hoi3::content::UnitTypeDefinition* unitType =
            definitions.UnitTypes().Find(node.unitTypeName);
        if (unitType != nullptr)
        {
            node.unitType = unitType->id;
        }
    }
    const auto resolveProvince = [&](
        const std::optional<std::uint32_t>& source,
        std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId>& destination)
    {
        if (!source)
        {
            return;
        }
        const dillen::compatibility::hoi3::content::ProvinceDefinition* province =
            definitions.Provinces().Find(*source);
        if (province != nullptr)
        {
            destination = province->id;
        }
        else if (reportedProvinces.emplace(*source).second)
        {
            diagnostics.Warning(
                "hoi3.oob.province_unresolved",
                "Order of battle references a Province absent from the active Registry",
                unresolved.span
            );
        }
    };
    resolveProvince(unresolved.location, node.location);
    resolveProvince(unresolved.base, node.base);
    node.leader = unresolved.leader;
    node.expeditionaryOwner = ResolveCountry(
        unresolved.expeditionaryOwner,
        unresolved.span,
        definitions,
        reportedCountries,
        diagnostics
    );
    node.builder = ResolveCountry(
        unresolved.builder,
        unresolved.span,
        definitions,
        reportedCountries,
        diagnostics
    );
    node.reserve = unresolved.reserve;
    node.pride = unresolved.pride;
    node.historicalModel = unresolved.historicalModel;
    node.experience = unresolved.experience;
    node.strength = unresolved.strength;
    node.organisation = unresolved.organisation;
    node.digIn = unresolved.digIn;
    node.origin = MakeOrigin(file, unresolved.span);
    node.children.reserve(unresolved.children.size());
    for (const UnresolvedOrderOfBattleNode& child : unresolved.children)
    {
        node.children.push_back(ResolveNode(
            file,
            child,
            definitions,
            reportedProvinces,
            reportedCountries,
            diagnostics
        ));
    }
    return node;
}

bool ResolveOrdersOfBattle(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    std::unordered_set<std::uint32_t> reportedProvinces;
    std::unordered_set<std::uint32_t> reportedCountries;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kOrderOfBattleDocumentType)
        {
            continue;
        }
        const OrderOfBattleDocument* document =
            file.result.artifact.As<OrderOfBattleDocument>();
        if (document == nullptr)
        {
            return false;
        }
        std::vector<dillen::compatibility::hoi3::content::OrderOfBattleNode> roots;
        roots.reserve(document->roots.size());
        for (const UnresolvedOrderOfBattleNode& unresolved : document->roots)
        {
            roots.push_back(ResolveNode(
                file,
                unresolved,
                definitions,
                reportedProvinces,
                reportedCountries,
                diagnostics
            ));
        }

        std::vector<dillen::compatibility::hoi3::content::OrderOfBattleMilitaryAccess> access;
        access.reserve(document->militaryAccess.size());
        for (const UnresolvedOrderOfBattleMilitaryAccess& unresolved
            : document->militaryAccess)
        {
            const auto country = ResolveCountry(
                unresolved.country,
                unresolved.span,
                definitions,
                reportedCountries,
                diagnostics
            );
            if (country)
            {
                access.push_back({
                    *country,
                    unresolved.enabled,
                    MakeOrigin(file, unresolved.span)
                });
            }
        }

        std::vector<dillen::compatibility::hoi3::content::OrderOfBattleConstruction> constructions;
        constructions.reserve(document->constructions.size());
        for (const UnresolvedOrderOfBattleConstruction& unresolved
            : document->constructions)
        {
            dillen::compatibility::hoi3::content::OrderOfBattleConstruction construction;
            construction.country = ResolveCountry(
                unresolved.country,
                unresolved.span,
                definitions,
                reportedCountries,
                diagnostics
            );
            construction.builder = ResolveCountry(
                unresolved.builder,
                unresolved.span,
                definitions,
                reportedCountries,
                diagnostics
            );
            construction.name = unresolved.name;
            construction.reserve = unresolved.reserve;
            construction.cost = unresolved.cost;
            construction.progress = unresolved.progress;
            construction.duration = unresolved.duration;
            construction.manpower = unresolved.manpower;
            construction.origin = MakeOrigin(file, unresolved.span);
            construction.components.reserve(unresolved.components.size());
            for (const UnresolvedOrderOfBattleNode& component
                : unresolved.components)
            {
                construction.components.push_back(ResolveNode(
                    file,
                    component,
                    definitions,
                    reportedProvinces,
                    reportedCountries,
                    diagnostics
                ));
            }
            constructions.push_back(std::move(construction));
        }

        std::vector<dillen::compatibility::hoi3::content::OrderOfBattleMetadata> metadata;
        metadata.reserve(document->metadata.size());
        for (const UnresolvedOrderOfBattleMetadata& unresolved
            : document->metadata)
        {
            metadata.push_back({
                unresolved.key,
                unresolved.value,
                MakeOrigin(file, unresolved.span)
            });
        }

        const dillen::compatibility::hoi3::content::OrderOfBattleDefinitionId id =
            dillen::compatibility::hoi3::content::StableOrderOfBattleDefinitionId(
                file.source.VirtualPath()
            );
        const dillen::compatibility::hoi3::content::OrderOfBattleResolveResult result =
            definitions.OrdersOfBattle().ResolveReferences(
                id,
                std::move(roots),
                std::move(access),
                std::move(constructions),
                std::move(metadata)
            );
        if (result != dillen::compatibility::hoi3::content::OrderOfBattleResolveResult::Resolved)
        {
            diagnostics.Error(
                "hoi3.oob.resolve_failed",
                "Order of battle references could not be resolved",
                DocumentSpan(*document)
            );
        }
    }
    return !diagnostics.HasErrors();
}

bool RegisterTemplate(
    TemplateRegistry& templates,
    TemplateId id,
    std::string name,
    std::string pattern
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = id;
    fileTemplate.name = std::move(name);
    fileTemplate.pattern = std::move(pattern);
    fileTemplate.parser = kOrderOfBattleParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 2200;
    return templates.Register(std::move(fileTemplate));
}

}

bool RegisterOrderOfBattleSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    if (!RegisterTemplate(
            templates,
            kHistoryOrderOfBattleTemplate,
            "hoi3_history_order_of_battle",
            "history/units/**/*.txt")
        || !RegisterTemplate(
            templates,
            kScenarioOrderOfBattleTemplate,
            "hoi3_scenario_order_of_battle",
            "scenarios/**/*_oob.txt")
        || !RegisterTemplate(
            templates,
            kScenarioArmyTemplate,
            "hoi3_scenario_army_order_of_battle",
            "scenarios/**/*_army.txt"))
    {
        templates.Unregister(kScenarioArmyTemplate);
        templates.Unregister(kScenarioOrderOfBattleTemplate);
        templates.Unregister(kHistoryOrderOfBattleTemplate);
        return false;
    }

    ParserDescriptor parser;
    parser.id = kOrderOfBattleParser;
    parser.name = "hoi3_order_of_battle";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kOrderOfBattleDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseOrderOfBattle;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kScenarioArmyTemplate);
        templates.Unregister(kScenarioOrderOfBattleTemplate);
        templates.Unregister(kHistoryOrderOfBattleTemplate);
        return false;
    }

    ResolutionPassDescriptor declarePass;
    declarePass.id = kOrderOfBattleDeclarePass;
    declarePass.name = "hoi3_order_of_battle_declare";
    declarePass.phase = ResolutionPhase::Declare;
    declarePass.priority = 200;
    declarePass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareOrdersOfBattle(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(declarePass)))
    {
        parsers.Unregister(kOrderOfBattleParser);
        templates.Unregister(kScenarioArmyTemplate);
        templates.Unregister(kScenarioOrderOfBattleTemplate);
        templates.Unregister(kHistoryOrderOfBattleTemplate);
        return false;
    }

    ResolutionPassDescriptor resolvePass;
    resolvePass.id = kOrderOfBattleResolvePass;
    resolvePass.name = "hoi3_order_of_battle_resolve";
    resolvePass.phase = ResolutionPhase::Resolve;
    resolvePass.priority = -1300;
    resolvePass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveOrdersOfBattle(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(resolvePass)))
    {
        resolver.UnregisterPass(kOrderOfBattleDeclarePass);
        parsers.Unregister(kOrderOfBattleParser);
        templates.Unregister(kScenarioArmyTemplate);
        templates.Unregister(kScenarioOrderOfBattleTemplate);
        templates.Unregister(kHistoryOrderOfBattleTemplate);
        return false;
    }
    return true;
}

}
