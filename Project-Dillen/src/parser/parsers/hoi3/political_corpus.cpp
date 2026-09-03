#include "political_corpus.hpp"

#include <utility>

#include "country_definition_slice.hpp"
#include "country_history_slice.hpp"
#include "country_tag_slice.hpp"
#include "parser_registry.hpp"
#include "province_definition_slice.hpp"
#include "province_history_slice.hpp"
#include "resolver.hpp"
#include "template_registry.hpp"

namespace dillen::parser::hoi3 {

bool ImportPoliticalCorpus(
    const PoliticalCorpusImportOptions& options,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    DiagnosticBag& diagnostics
)
{
    if (definitions.IsFrozen())
    {
        diagnostics.Error(
            "hoi3.political_corpus.registry_already_frozen",
            "Political corpus import requires a fresh Definition Registry"
        );
        return false;
    }

    if (options.layers.empty())
    {
        diagnostics.Error(
            "hoi3.political_corpus.layers_empty",
            "Political corpus import requires at least one Source Layer"
        );
        return false;
    }

    definitions.Clear();

    TemplateRegistry templates;
    ParserRegistry parsers;
    Resolver resolver;

    //
    // First political-world slice only.
    //
    // No diplomacy, war, technology, OOB, event or AI parser is registered
    // here. The files may still exist in the corpus; FileCatalog simply leaves
    // them unclassified for this import.
    //
    if (!RegisterCountryTagSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !RegisterProvinceDefinitionSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !RegisterCountryDefinitionSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !RegisterCountryHistorySlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !RegisterProvinceHistorySlice(
            templates,
            parsers,
            resolver,
            definitions))
    {
        diagnostics.Error(
            "hoi3.political_corpus.slice_registration_failed",
            "one or more HOI3 political-source slices could not be registered"
        );
        return false;
    }

    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    FileCatalog catalog;

    for (SourceLayer layer : options.layers)
    {
        if (!catalog.AddLayer(std::move(layer)))
        {
            diagnostics.Error(
                "hoi3.political_corpus.layer_invalid",
                "a political corpus Source Layer is invalid or duplicates "
                "another layer"
            );
            return false;
        }
    }

    if (!catalog.Build(templates, diagnostics))
    {
        return false;
    }

    ParseWorkspace workspace;

    if (!catalog.Parse(
            parsers,
            workspace,
            diagnostics))
    {
        // Name the files that failed.
        //
        // Diagnostics carry a line and column but not a path -- SourceSpan has
        // no file in it, and one shared bag collects every file's output. So a
        // corpus of several hundred files reports "46:16: expected relation
        // operator" and leaves the reader to find which of them it was.
        //
        // ParseResult records the half-open range of bag entries its file
        // produced, which is the association that already exists and was not
        // being used. Reported only on failure, so a clean import stays quiet.
        for (const ParsedFile& file : workspace.files)
        {
            if (file.result.success)
            {
                continue;
            }
            diagnostics.Error(
                "hoi3.political_corpus.file_rejected",
                "parse failed in '" + file.catalog.virtualPath + "' ("
                    + file.catalog.physicalPath.string() + "), which produced "
                    + std::to_string(
                        file.result.diagnosticEnd
                            - file.result.diagnosticBegin)
                    + " diagnostic(s) above"
            );
        }
        return false;
    }

    if (!resolver.Resolve(
            workspace,
            diagnostics))
    {
        return false;
    }

    //
    // Everything below this point consumes semantic HOI3 definitions rather
    // than parser syntax, so freeze the source-side registry here.
    //
    definitions.Freeze();

    return !diagnostics.HasErrors();
}

}