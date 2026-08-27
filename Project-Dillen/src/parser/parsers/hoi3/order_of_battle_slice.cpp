#include "order_of_battle_slice.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "country_tag_slice.hpp"
#include "order_of_battle_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

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
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
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
        content::OrderOfBattleDefinition definition;
        definition.virtualPath = content::NormalizeOrderOfBattlePath(
            file.source.VirtualPath()
        );
        definition.id = content::StableOrderOfBattleDefinitionId(
            definition.virtualPath
        );
        definition.origin = MakeOrigin(file, DocumentSpan(*document));
        const content::OrderOfBattleDeclareResult result =
            definitions.OrdersOfBattle().Declare(std::move(definition));
        if (result != content::OrderOfBattleDeclareResult::Added)
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

std::optional<content::CountryDefinitionId> ResolveCountry(
    std::string_view text,
    const SourceSpan& span,
    content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedCountries,
    DiagnosticBag& diagnostics
)
{
    if (text.empty())
    {
        return std::nullopt;
    }
    const std::optional<content::CountryTag> tag =
        content::CountryTag::Parse(text);
    if (!tag)
    {
        diagnostics.Error(
            "hoi3.oob.country_reference_invalid",
            "Order of battle contains an invalid Country Tag",
            span
        );
        return std::nullopt;
    }
    const content::CountryDefinitionId id = tag->StableId();
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

content::OrderOfBattleNode ResolveNode(
    const ParsedFile& file,
    const UnresolvedOrderOfBattleNode& unresolved,
    content::DefinitionRegistry& definitions,
    std::unordered_set<std::uint32_t>& reportedProvinces,
    std::unordered_set<std::uint32_t>& reportedCountries,
    DiagnosticBag& diagnostics
)
{
    content::OrderOfBattleNode node;
    node.kind = unresolved.kind;
    node.name = unresolved.name;
    node.unitTypeName = unresolved.unitTypeName;
    if (!node.unitTypeName.empty())
    {
        const content::UnitTypeDefinition* unitType =
            definitions.UnitTypes().Find(node.unitTypeName);
        if (unitType != nullptr)
        {
            node.unitType = unitType->id;
        }
    }
    const auto resolveProvince = [&](
        const std::optional<std::uint32_t>& source,
        std::optional<content::ProvinceDefinitionId>& destination)
    {
        if (!source)
        {
            return;
        }
        const content::ProvinceDefinition* province =
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
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
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
        std::vector<content::OrderOfBattleNode> roots;
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

        std::vector<content::OrderOfBattleMilitaryAccess> access;
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

        std::vector<content::OrderOfBattleConstruction> constructions;
        constructions.reserve(document->constructions.size());
        for (const UnresolvedOrderOfBattleConstruction& unresolved
            : document->constructions)
        {
            content::OrderOfBattleConstruction construction;
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

        std::vector<content::OrderOfBattleMetadata> metadata;
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

        const content::OrderOfBattleDefinitionId id =
            content::StableOrderOfBattleDefinitionId(
                file.source.VirtualPath()
            );
        const content::OrderOfBattleResolveResult result =
            definitions.OrdersOfBattle().ResolveReferences(
                id,
                std::move(roots),
                std::move(access),
                std::move(constructions),
                std::move(metadata)
            );
        if (result != content::OrderOfBattleResolveResult::Resolved)
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
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
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

    AnalysisPassDescriptor declarePass;
    declarePass.id = kOrderOfBattleDeclarePass;
    declarePass.name = "hoi3_order_of_battle_declare";
    declarePass.phase = AnalysisPhase::Declare;
    declarePass.priority = 200;
    declarePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareOrdersOfBattle(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(declarePass)))
    {
        parsers.Unregister(kOrderOfBattleParser);
        templates.Unregister(kScenarioArmyTemplate);
        templates.Unregister(kScenarioOrderOfBattleTemplate);
        templates.Unregister(kHistoryOrderOfBattleTemplate);
        return false;
    }

    AnalysisPassDescriptor resolvePass;
    resolvePass.id = kOrderOfBattleResolvePass;
    resolvePass.name = "hoi3_order_of_battle_resolve";
    resolvePass.phase = AnalysisPhase::Resolve;
    resolvePass.priority = -1300;
    resolvePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveOrdersOfBattle(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(resolvePass)))
    {
        analyzer.UnregisterPass(kOrderOfBattleDeclarePass);
        parsers.Unregister(kOrderOfBattleParser);
        templates.Unregister(kScenarioArmyTemplate);
        templates.Unregister(kScenarioOrderOfBattleTemplate);
        templates.Unregister(kHistoryOrderOfBattleTemplate);
        return false;
    }
    return true;
}

}
