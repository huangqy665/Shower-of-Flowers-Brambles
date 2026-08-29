#pragma once

#include <filesystem>
#include <vector>

#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "launch_definition.hpp"

namespace dillen::parser::hoi3 {

struct ScenarioOverlayPlan
{
    std::filesystem::path directory;
    std::vector<SourceLayer> mounts;
};

bool BuildScenarioOverlayPlan(
    const dillen::compatibility::hoi3::content::ScenarioDefinition& scenario,
    const std::filesystem::path& contentRoot,
    SourceLayerId firstLayerId,
    int priority,
    ScenarioOverlayPlan& output,
    DiagnosticBag& diagnostics
);

}
