#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "analyzer.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parse_result.hpp"
#include "parser_registry.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace
{

using AssignmentList = std::vector<std::pair<std::string, std::string>>;

bool ParseAssignments(
    dillen::parser::ParserCursor& cursor,
    dillen::parser::ParseArtifact& artifact
)
{
    AssignmentList assignments;
    while (!cursor.AtEnd())
    {
        dillen::parser::Token key;
        dillen::parser::Token value;
        dillen::parser::RelationOperator relation;
        if (!cursor.ReadKey(key)
            || !cursor.ReadRelation(relation)
            || relation != dillen::parser::RelationOperator::Assign
            || !cursor.ReadScalar(value))
        {
            return false;
        }
        assignments.emplace_back(
            std::string(key.text),
            std::string(value.text)
        );
    }
    artifact.value = std::move(assignments);
    return true;
}

bool ParseRawSource(
    const dillen::parser::SourceBuffer& source,
    dillen::parser::DiagnosticBag&,
    dillen::parser::ParseArtifact& artifact
)
{
    artifact.value = std::string(source.Bytes());
    return true;
}

bool WriteText(
    const std::filesystem::path& path,
    const std::string& text
)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    return stream.good();
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_parser_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    const fs::path base = root / "base";
    const fs::path mod = root / "mod";
    if (!WriteText(
            base / "common/countries.txt",
            "CHI = countries/china.txt\n")
        || !WriteText(
            base / "common/obsolete/old.txt",
            "OLD = removed\n")
        || !WriteText(
            mod / "common/countries.txt",
            "JAP = countries/japan.txt\n")
        || !WriteText(
            mod / "common/new.txt",
            "NEW = active\n"))
    {
        std::cerr << "Parser fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    if (!templates.Register({
            2,
            "common_text",
            "common/**/*.txt",
            10,
            1,
            0,
            nullptr
        })
        || !templates.Register({
            1,
            "countries_index",
            "common/countries.txt",
            10,
            1,
            10,
            nullptr
        }))
    {
        std::cerr << "Template registration failed\n";
        return 2;
    }
    templates.Freeze();
    const auto match = templates.Match("common/countries.txt");
    if (!match
        || match->fileTemplate != 1
        || templates.Register({3, "late", "*.txt", 10}))
    {
        std::cerr << "Template matching/freeze failed\n";
        return 3;
    }

    dillen::parser::ParserRegistry parsers;
    dillen::parser::ParserDescriptor parser;
    parser.id = 10;
    parser.name = "assignment_list";
    parser.inputDialect = 1;
    parser.outputType = 20;
    parser.parse = ParseAssignments;
    if (!parsers.Register(std::move(parser)))
    {
        std::cerr << "Parser registration failed\n";
        return 4;
    }
    dillen::parser::ParserDescriptor sourceParser;
    sourceParser.id = 11;
    sourceParser.name = "raw_source";
    sourceParser.inputDialect = 2;
    sourceParser.outputType = 21;
    sourceParser.parseSource = ParseRawSource;
    if (!parsers.Register(std::move(sourceParser)))
    {
        std::cerr << "Raw source Parser registration failed\n";
        return 4;
    }
    dillen::parser::ParserDescriptor ambiguousParser;
    ambiguousParser.id = 12;
    ambiguousParser.name = "ambiguous";
    ambiguousParser.inputDialect = 2;
    ambiguousParser.outputType = 22;
    ambiguousParser.parse = ParseAssignments;
    ambiguousParser.parseSource = ParseRawSource;
    if (parsers.Register(std::move(ambiguousParser)))
    {
        std::cerr << "Ambiguous Parser registration was accepted\n";
        return 4;
    }
    parsers.Freeze();

    dillen::parser::SourceBuffer rawSource(
        100,
        "probe/raw.csv",
        "probe/raw.csv",
        "one;two;three\n"
    );
    dillen::parser::DiagnosticBag rawDiagnostics;
    const dillen::parser::ParseResult rawResult = parsers.Parse(
        rawSource,
        {100, 11, 2, 0, 0},
        rawDiagnostics
    );
    const std::string* rawBytes = rawResult.artifact.As<std::string>();
    if (!rawResult.success
        || rawDiagnostics.HasErrors()
        || rawBytes == nullptr
        || *rawBytes != "one;two;three\n")
    {
        std::cerr << "Raw source Parser invocation failed\n";
        return 4;
    }

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "base", base, 0, {}})
        || !catalog.AddLayer({
            2,
            "mod",
            mod,
            100,
            {"common/obsolete"}
        })
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 2)
    {
        std::cerr << "File catalog probe failed\n";
        return 5;
    }

    bool declareRan = false;
    bool resolveRan = false;
    bool validateRan = false;
    dillen::parser::Analyzer analyzer;
    if (!analyzer.RegisterPass({
            1,
            "declare",
            dillen::parser::AnalysisPhase::Declare,
            0,
            [&declareRan](
                dillen::parser::AnalysisWorkspace& workspace,
                dillen::parser::DiagnosticBag&)
            {
                declareRan = workspace.files.size() == 2;
                return declareRan;
            }
        })
        || !analyzer.RegisterPass({
            2,
            "resolve",
            dillen::parser::AnalysisPhase::Resolve,
            0,
            [&resolveRan, &declareRan](
                dillen::parser::AnalysisWorkspace& workspace,
                dillen::parser::DiagnosticBag&)
            {
                resolveRan = declareRan;
                for (const auto& file : workspace.files)
                {
                    const AssignmentList* assignments =
                        file.result.artifact.As<AssignmentList>();
                    if (assignments == nullptr || assignments->empty())
                    {
                        return false;
                    }
                    if (file.catalog.virtualPath == "common/countries.txt"
                        && assignments->front().first != "JAP")
                    {
                        return false;
                    }
                }
                return resolveRan;
            }
        })
        || !analyzer.RegisterPass({
            3,
            "validate",
            dillen::parser::AnalysisPhase::Validate,
            0,
            [&validateRan, &resolveRan](
                dillen::parser::AnalysisWorkspace&,
                dillen::parser::DiagnosticBag&)
            {
                validateRan = resolveRan;
                return validateRan;
            }
        }))
    {
        std::cerr << "Analysis pass registration failed\n";
        return 6;
    }
    analyzer.Freeze();
    dillen::parser::AnalysisWorkspace workspace;
    const bool analyzed = analyzer.Analyze(
        catalog,
        parsers,
        workspace,
        diagnostics
    );

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!analyzed
        || diagnostics.HasErrors()
        || !declareRan
        || !resolveRan
        || !validateRan)
    {
        for (const auto& diagnostic : diagnostics.All())
        {
            std::cerr << dillen::parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "Parser pipeline probe failed\n";
        return 7;
    }

    std::cout << "Parser pipeline probe: passed\n";
    return 0;
}
