#pragma once

#include <vector>

#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"

namespace dillen::parser::hoi3 {

struct PoliticalCorpusImportOptions
{
    std::vector<SourceLayer> layers;
};

bool ImportPoliticalCorpus(
    const PoliticalCorpusImportOptions& options,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    DiagnosticBag& diagnostics
);

}