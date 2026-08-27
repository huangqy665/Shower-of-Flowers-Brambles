#include "province_definition_slice.hpp"

#include <string>
#include <utility>

#include "province_definition_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

bool DeclareProvinces(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kProvinceDefinitionDocumentType)
        {
            continue;
        }
        ProvinceDefinitionDocument* document =
            file.result.artifact.As<ProvinceDefinitionDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.province.artifact_type_mismatch",
                "province CSV parser returned an invalid artifact"
            );
            return false;
        }

        for (content::ProvinceDefinition& definition
            : document->definitions)
        {
            definition.origin.sourceLayer =
                file.catalog.sourceLayerName;
            const content::ProvinceDeclareResult result =
                definitions.Provinces().Declare(std::move(definition));
            if (result != content::ProvinceDeclareResult::Added)
            {
                diagnostics.Error(
                    "hoi3.province.declare_failed",
                    "province definition could not be added to the Registry"
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterProvinceDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kProvinceDefinitionTemplate;
    fileTemplate.name = "hoi3_province_definition_csv";
    fileTemplate.pattern = "map/definition.csv";
    fileTemplate.parser = kProvinceDefinitionParser;
    fileTemplate.dialect = kHoi3SemicolonCsvDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kProvinceDefinitionParser;
    parser.name = "hoi3_province_definition_csv";
    parser.inputDialect = kHoi3SemicolonCsvDialect;
    parser.outputType = kProvinceDefinitionDocumentType;
    parser.schemaVersion = 1;
    parser.parseSource = ParseProvinceDefinitionCsv;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kProvinceDefinitionTemplate);
        return false;
    }

    AnalysisPassDescriptor pass;
    pass.id = kProvinceDefinitionDeclarePass;
    pass.name = "hoi3_province_definition_declare";
    pass.phase = AnalysisPhase::Declare;
    pass.priority = -2000;
    pass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareProvinces(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kProvinceDefinitionParser);
        templates.Unregister(kProvinceDefinitionTemplate);
        return false;
    }
    return true;
}

}
