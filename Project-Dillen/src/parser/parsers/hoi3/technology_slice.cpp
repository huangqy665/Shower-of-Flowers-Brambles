#include "technology_slice.hpp"

#include <string>
#include <utility>

#include "country_tag_slice.hpp"
#include "technology_parser.hpp"

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

bool DeclareTechnologies(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kTechnologyDocumentType)
        {
            continue;
        }
        const TechnologyDocument* document =
            file.result.artifact.As<TechnologyDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.technology.artifact_type_mismatch",
                "Technology parser returned an invalid artifact"
            );
            return false;
        }
        for (const UnresolvedTechnologyDefinition& unresolved
            : document->definitions)
        {
            content::TechnologyDefinition definition;
            definition.id = content::StableTechnologyDefinitionId(
                unresolved.name
            );
            definition.name = unresolved.name;
            definition.normalizedName = content::NormalizeTechnologyName(
                unresolved.name
            );
            definition.difficulty = unresolved.difficulty;
            definition.startYear = unresolved.startYear;
            definition.firstOffset = unresolved.firstOffset;
            definition.additionalOffset = unresolved.additionalOffset;
            definition.maxLevel = unresolved.maxLevel;
            definition.folder = unresolved.folder;
            definition.onCompletion = unresolved.onCompletion;
            definition.change = unresolved.change;
            definition.allow = unresolved.allow;
            definition.researchBonuses = unresolved.researchBonuses;
            definition.activatedUnits = unresolved.activatedUnits;
            definition.activatedBuildings = unresolved.activatedBuildings;
            definition.scalarEffects = unresolved.scalarEffects;
            definition.effectBlocks = unresolved.effectBlocks;
            definition.origin = MakeOrigin(file, unresolved.span);
            const content::TechnologyDeclareResult result =
                definitions.Technologies().Declare(std::move(definition));
            if (result == content::TechnologyDeclareResult::Added)
            {
                continue;
            }
            const std::string message = result
                    == content::TechnologyDeclareResult::DuplicateName
                ? "duplicate Technology definition '" + unresolved.name + "'"
                : result == content::TechnologyDeclareResult::IdCollision
                    ? "Technology stable ID collision for '"
                        + unresolved.name + "'"
                    : "invalid Technology definition '"
                        + unresolved.name + "'";
            diagnostics.Error(
                "hoi3.technology.declare_failed",
                message,
                unresolved.span
            );
        }
    }
    return !diagnostics.HasErrors();
}

void ResolveRequirement(
    content::TechnologyRequirement& requirement,
    const content::TechnologyDefinitionRegistry& technologies
)
{
    if (requirement.kind == content::TechnologyRequirementKind::Level)
    {
        const content::TechnologyDefinition* definition =
            technologies.Find(requirement.name);
        if (definition != nullptr)
        {
            requirement.technology = definition->id;
        }
        return;
    }
    for (content::TechnologyRequirement& child : requirement.children)
    {
        ResolveRequirement(child, technologies);
    }
}

void ResolveEffectBlocks(
    std::vector<content::TechnologyEffectBlock>& blocks,
    const content::UnitTypeDefinitionRegistry& units,
    bool topLevel
)
{
    for (content::TechnologyEffectBlock& block : blocks)
    {
        if (topLevel)
        {
            const content::UnitTypeDefinition* unit = units.Find(block.name);
            if (unit != nullptr)
            {
                block.unitType = unit->id;
            }
        }
        ResolveEffectBlocks(block.blocks, units, false);
    }
}

bool ResolveTechnologies(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kTechnologyDocumentType)
        {
            continue;
        }
        const TechnologyDocument* document =
            file.result.artifact.As<TechnologyDocument>();
        if (document == nullptr)
        {
            return false;
        }
        for (const UnresolvedTechnologyDefinition& unresolved
            : document->definitions)
        {
            std::optional<content::TechnologyRequirement> allow =
                unresolved.allow;
            if (allow)
            {
                ResolveRequirement(
                    *allow,
                    definitions.Technologies()
                );
            }

            std::vector<content::TechnologyUnitReference> activatedUnits =
                unresolved.activatedUnits;
            for (content::TechnologyUnitReference& reference
                : activatedUnits)
            {
                const content::UnitTypeDefinition* unit =
                    definitions.UnitTypes().Find(reference.name);
                if (unit != nullptr)
                {
                    reference.unitType = unit->id;
                }
            }

            std::vector<content::TechnologyEffectBlock> effectBlocks =
                unresolved.effectBlocks;
            ResolveEffectBlocks(
                effectBlocks,
                definitions.UnitTypes(),
                true
            );
            const content::TechnologyResolveResult result =
                definitions.Technologies().ResolveReferences(
                    content::StableTechnologyDefinitionId(unresolved.name),
                    std::move(allow),
                    std::move(activatedUnits),
                    std::move(effectBlocks)
                );
            if (result != content::TechnologyResolveResult::Resolved)
            {
                diagnostics.Error(
                    "hoi3.technology.resolve_failed",
                    "Technology references could not be resolved",
                    unresolved.span
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterTechnologySlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kTechnologyTemplate;
    fileTemplate.name = "hoi3_technology";
    fileTemplate.pattern = "technologies/**/*.txt";
    fileTemplate.parser = kTechnologyParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kTechnologyParser;
    parser.name = "hoi3_technology";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kTechnologyDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseTechnologies;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kTechnologyTemplate);
        return false;
    }

    AnalysisPassDescriptor declarePass;
    declarePass.id = kTechnologyDeclarePass;
    declarePass.name = "hoi3_technology_declare";
    declarePass.phase = AnalysisPhase::Declare;
    declarePass.priority = 0;
    declarePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareTechnologies(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(declarePass)))
    {
        parsers.Unregister(kTechnologyParser);
        templates.Unregister(kTechnologyTemplate);
        return false;
    }

    AnalysisPassDescriptor resolvePass;
    resolvePass.id = kTechnologyResolvePass;
    resolvePass.name = "hoi3_technology_resolve";
    resolvePass.phase = AnalysisPhase::Resolve;
    resolvePass.priority = -1500;
    resolvePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveTechnologies(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(resolvePass)))
    {
        analyzer.UnregisterPass(kTechnologyDeclarePass);
        parsers.Unregister(kTechnologyParser);
        templates.Unregister(kTechnologyTemplate);
        return false;
    }
    return true;
}

}
