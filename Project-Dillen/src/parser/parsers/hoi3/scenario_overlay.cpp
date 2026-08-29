#include "scenario_overlay.hpp"

#include <limits>
#include <string>
#include <utility>

namespace dillen::parser::hoi3 {

namespace {

std::filesystem::path FindChildDirectory(
    const std::filesystem::path& parent,
    std::string_view normalizedName
)
{
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(parent, error), end;
        iterator != end && !error;
        iterator.increment(error))
    {
        if (iterator->is_directory(error)
            && dillen::compatibility::hoi3::content::NormalizeScenarioKey(
                iterator->path().filename().u8string()) == normalizedName)
        {
            return iterator->path();
        }
        error.clear();
    }
    return {};
}

SourceLayer MakeMount(
    SourceLayerId id,
    std::string name,
    std::filesystem::path root,
    int priority,
    std::string virtualPrefix,
    std::vector<std::string> includePatterns
)
{
    SourceLayer layer;
    layer.id = id;
    layer.name = std::move(name);
    layer.root = std::move(root);
    layer.priority = priority;
    layer.virtualPrefix = std::move(virtualPrefix);
    layer.includePatterns = std::move(includePatterns);
    return layer;
}

}

bool BuildScenarioOverlayPlan(
    const dillen::compatibility::hoi3::content::ScenarioDefinition& scenario,
    const std::filesystem::path& contentRoot,
    SourceLayerId firstLayerId,
    int priority,
    ScenarioOverlayPlan& output,
    DiagnosticBag& diagnostics
)
{
    output = {};
    if (!scenario.id
        || scenario.key.empty()
        || firstLayerId == 0
        || firstLayerId > std::numeric_limits<SourceLayerId>::max() - 2)
    {
        diagnostics.Error(
            "hoi3.scenario_overlay.request_invalid",
            "scenario overlay request is invalid"
        );
        return false;
    }
    const std::filesystem::path scenariosRoot = contentRoot / "scenarios";
    const std::filesystem::path scenarioDirectory = FindChildDirectory(
        scenariosRoot,
        scenario.key
    );
    if (scenarioDirectory.empty())
    {
        diagnostics.Error(
            "hoi3.scenario_overlay.directory_missing",
            "scenario overlay directory is missing for " + scenario.key
        );
        return false;
    }

    output.directory = scenarioDirectory;
    output.mounts.push_back(MakeMount(
        firstLayerId,
        "scenario:" + scenario.key + ":countries",
        scenarioDirectory,
        priority,
        "history/countries",
        {"???.txt"}
    ));

    SourceLayerId nextId = firstLayerId + 1;
    const std::filesystem::path provinceDirectory = FindChildDirectory(
        scenarioDirectory,
        "provinces"
    );
    if (!provinceDirectory.empty())
    {
        output.mounts.push_back(MakeMount(
            nextId++,
            "scenario:" + scenario.key + ":provinces",
            provinceDirectory,
            priority,
            "history/provinces",
            {"**/*.txt"}
        ));
    }

    output.mounts.push_back(MakeMount(
        nextId,
        "scenario:" + scenario.key + ":oob",
        scenarioDirectory,
        priority,
        "history/units",
        {"*_oob.txt", "*_army.txt"}
    ));
    return true;
}

}
